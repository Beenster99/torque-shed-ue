#!/usr/bin/env python3
"""Road rules checker, stage 2 of the road rulebook plan.

Reads what world_ride measured (WORLD_RIDE_RULES=<file>: SimTest/rules_dump.cpp) and judges it against
Data/RideUK/road_rules.json. Prints one line per breach kind and writes a JSON summary.

    road_rules_check.py <dump.jsonl> [more.jsonl ...] [--out summary.json] [--pics dir]

    road_rules_check.py --same <ride1.jsonl> <ride2.jsonl> [...] [--out same.json] [--pics dir]

--same: rule A3, route independence: the junctions two or more rides both pass, each drawn the same whatever the
route (paint and tarmac compared cell by cell within 25 m of the middle).

--pics: for each rule's worst examples, the drawn junction from above (north up) beside the real map of the same
place (OpenStreetMap tiles, kept in <dir>/tiles).

Every rule here measures what is drawn (the paint and tarmac from above), not what the code meant to draw.
Coordinates: metres east (x) and south (z) of the junction's middle.
"""
import json, math, os, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
RULES = json.load(open(os.path.join(HERE, '..', 'Data', 'RideUK', 'road_rules.json')))
RULE = {r['id']: r for r in RULES['rules']}

MAJOR_CLASSES = ('motorway', 'trunk', 'primary')
QUIET = ('residential', 'unclassified', 'living_street', 'service', 'road')
NO_MARKING = ('service', 'track', 'path', 'footway', 'cycleway', 'bridleway', 'private')
CLASS_RANK = {'motorway': 0, 'motorway_link': 0, 'trunk': 1, 'trunk_link': 1, 'primary': 2, 'primary_link': 2, 'secondary': 3, 'secondary_link': 3,
              'tertiary': 4, 'tertiary_link': 4, 'unclassified': 5, 'residential': 5, 'living_street': 6, 'service': 7, 'track': 8}


class Picture:
    """The drawn surface from above: 0 nothing, 1 road surface, 2 white paint, 3 yellow paint."""

    def __init__(self, rec):
        self.cell = rec['cell']
        self.n = rec['n']
        self.half = self.n * self.cell / 2
        self.k = bytearray(self.n * self.n)
        for z, x, ln, kind in rec['runs']:
            base = z * self.n + x
            self.k[base:base + ln] = bytes([kind]) * ln

    def at(self, x, z):
        cx = int((x + self.half) / self.cell)
        cz = int((z + self.half) / self.cell)
        if cx < 0 or cz < 0 or cx >= self.n or cz >= self.n:
            return -1
        return self.k[cz * self.n + cx]

    def cells(self, kinds):
        """Every cell of these kinds, as (x, z) middles."""
        out = []
        n, c, h = self.n, self.cell, self.half
        for i, v in enumerate(self.k):
            if v in kinds:
                out.append(((i % n + 0.5) * c - h, (i // n + 0.5) * c - h))
        return out


def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def add(a, b): return (a[0] + b[0], a[1] + b[1])
def mul(a, s): return (a[0] * s, a[1] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1]
def norm(a):
    l = math.hypot(*a)
    return (a[0] / l, a[1] / l) if l > 1e-9 else (1.0, 0.0)
# (left of a heading, seen from above with north up: x east, z south)
def left(d): return (d[1], -d[0])


class Line:
    """A road's laid middle line with its half width, from the junction outward."""

    def __init__(self, pts):
        self.p = [(q[0], q[1]) for q in pts]
        self.h = [q[2] for q in pts]
        self.s = [0.0]
        for i in range(1, len(self.p)):
            self.s.append(self.s[-1] + math.hypot(*sub(self.p[i], self.p[i - 1])))

    def length(self): return self.s[-1]

    def at(self, s):
        """Point, heading and half width s m along."""
        p, s_ = self.p, self.s
        if len(p) < 2:
            return p[0], (1.0, 0.0), self.h[0]
        s = max(0.0, min(s, s_[-1]))
        i = 0
        while i + 2 < len(p) and s_[i + 1] < s:
            i += 1
        seg = s_[i + 1] - s_[i]
        t = (s - s_[i]) / seg if seg > 1e-9 else 0.0
        d = norm(sub(p[i + 1], p[i]))
        return add(p[i], mul(sub(p[i + 1], p[i]), t)), d, self.h[i] + (self.h[i + 1] - self.h[i]) * t

    def nearest(self, q):
        """Distance from q, the half width there, how far along, and the side (+ left as the line runs)."""
        best = (1e18, 0.0, 0.0, 0.0)
        for i in range(len(self.p) - 1):
            a, b = self.p[i], self.p[i + 1]
            ab = sub(b, a)
            l2 = dot(ab, ab)
            if l2 < 1e-12:
                continue
            t = max(0.0, min(1.0, dot(sub(q, a), ab) / l2))
            f = add(a, mul(ab, t))
            d = math.hypot(*sub(q, f))
            if d < best[0]:
                side = dot(sub(q, f), left(norm(ab)))
                best = (d, self.h[i] + (self.h[i + 1] - self.h[i]) * t, self.s[i] + math.sqrt(l2) * t, side)
        return best


def arm_lines(rec):
    """Each arm's laid line from the junction out, or None where it isn't laid."""
    out = []
    for arm in rec['arms']:
        away = norm((arm['ax'], arm['az']))
        best, best_score = None, -1e9
        for road in rec['roads']:
            if road['w'] != arm['w'] or arm['w'] < 0:
                continue
            pts = road['pts']
            if len(pts) < 2:
                continue
            # (the point nearest the junction, then the run of points heading the arm's way from it)
            i0 = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0], pts[i][1]))
            if math.hypot(pts[i0][0], pts[i0][1]) > 25.0:
                continue
            for step in (1, -1):
                run = []
                i = i0
                while 0 <= i < len(pts):
                    run.append(pts[i])
                    i += step
                if len(run) < 2:
                    continue
                far = run[min(len(run) - 1, max(1, len(run) // 2))]
                score = dot(norm((far[0] - run[0][0], far[1] - run[0][1])), away)
                if score > best_score:
                    best_score, best = score, run
        # (a road laid only from a whole junction's mouth out: carried straight back towards the junction's middle, so
        # its give-way line on the through road's edge is looked for where it is)
        if best is not None and len(best) >= 2:
            d = norm((best[1][0] - best[0][0], best[1][1] - best[0][1]))
            t = -dot((best[0][0], best[0][1]), d)
            if -25.0 < t < -1.0:
                best = [[best[0][0] + d[0] * t, best[0][1] + d[1] * t] + list(best[0][2:])] + best
        out.append(Line(best) if best is not None and best_score > 0.5 else None)
    return out


def band(pic, minor, major, v0, v1, e0, e1, kinds=(2,), step=0.1):
    """Paint in a band of the minor arm: across it v0..v1 (+ its approach lane's side, the left coming in), and e0..e1 m
    out from the major road's edge (+ outside the major road). Returns (area painted m2, area looked at m2, the e of
    each painted sample)."""
    painted, looked, es = 0.0, 0.0, []
    a = step * step
    s = 0.0
    while s < min(minor.length(), 40.0):
        p, d, h = minor.at(s)
        n = left(mul(d, -1.0))   # (the approach lane is on the left coming in)
        dm, hm, _, _ = major.nearest(p)
        e = dm - hm
        if e > e1 + 0.5:
            break
        if e >= e0 - 0.5:
            v = v0
            while v <= v1:
                q = add(p, mul(n, v))
                dq, hq, _, _ = major.nearest(q)
                eq = dq - hq
                if e0 <= eq <= e1:
                    looked += a
                    if pic.at(*q) in kinds:
                        painted += a
                        es.append(eq)
                v += step
        s += step
    return painted, looked, es


def straighten(ln, major):
    """A minor arm's line where it crosses the major road's edge: its own heading there carried straight in, not the
    map's line curving into the junction's node (where the lanes and the give-way line are judged)."""
    s = 0.0
    while s < ln.length():
        p, _, _ = ln.at(s)
        dm, hm, _, _ = major.nearest(p)
        if dm - hm >= 1.5:
            break
        s += 0.2
    if s <= 0.0 or s >= ln.length() - 1.0:
        return ln
    p, _, h = ln.at(s)
    q, _, _ = ln.at(min(ln.length(), s + 3.0))
    d = norm(sub(q, p))
    keep = [i for i in range(len(ln.p)) if ln.s[i] > s]
    start = add(p, mul(d, -(s + 3.0)))
    return Line([[start[0], start[1], h], [p[0], p[1], h]] + [[ln.p[i][0], ln.p[i][1], ln.h[i]] for i in keep])


def through_line(lines, arms):
    """The through road as one line from one through arm's far end to the other's."""
    th = [i for i, a in enumerate(arms) if a['through'] and lines[i] is not None]
    if len(th) < 2:
        return None, th
    # (each arm from 6 m out, joined straight across the middle: where the ride turns, its own laid line curves from one
    # arm into another inside the junction, and would read as the through road cutting the corner)
    def out_from(ln, r):
        keep = [(p[0], p[1], h) for p, h in zip(ln.p, ln.h) if math.hypot(*p) >= r]
        return keep if len(keep) >= 2 else [(p[0], p[1], h) for p, h in zip(ln.p, ln.h)]
    a, b = out_from(lines[th[0]], 6.0), out_from(lines[th[1]], 6.0)
    return Line(list(reversed(a)) + b), th


def rank(cls): return CLASS_RANK.get(cls, 6)


def mph_band(table, mph):
    for row in table:
        if mph <= row[0]:
            return row
    return table[-1]


def runs_along(pic, line, s0, s1, v, kinds=(2,), step=0.1, slack=0.3):
    """Painted runs along a line at offset v (+ left), paint anywhere within slack of it across counting (the laid
    middle line and the painted one may sit a little apart): list of (start, end, painted)."""
    out = []
    s = s0
    cur, start = None, s0
    while s <= s1:
        p, d, h = line.at(s)
        on = False
        u = -slack
        while u <= slack + 1e-9 and not on:
            on = pic.at(*add(p, mul(left(d), v + u))) in kinds
            u += 0.05
        if cur is None:
            cur, start = on, s
        elif on != cur:
            out.append((start, s, cur))
            cur, start = on, s
        s += step
    if cur is not None:
        out.append((start, s1, cur))
    return out


def runs_following(pic, line, s0, s1, kinds=(2, 3), step=0.1, reach=1.5, hold=0.4):
    """Painted runs along a middle line, following the paint where it sits a little to one side (a whole junction's
    piece carries its line across on its own curve, and the map's line, joined straight across the middle, cuts the
    corner): the first paint within reach across sets where the line is, and from there each step looks within hold
    of where it was last seen. List of (start, end, painted)."""
    out = []
    u0 = None
    s = s0
    while s <= s1 and u0 is None:
        p, d, h = line.at(s)
        best = None
        u = -reach
        while u <= reach + 1e-9:
            if pic.at(*add(p, mul(left(d), u))) in kinds and (best is None or abs(u) < abs(best)):
                best = u
            u += 0.05
        u0 = best
        s += step
    if u0 is None:
        return [(s0, s1, False)]
    cur, start = None, s0
    s = s0
    while s <= s1:
        p, d, h = line.at(s)
        on, found = False, []
        u = u0 - hold
        while u <= u0 + hold + 1e-9:
            if pic.at(*add(p, mul(left(d), u))) in kinds:
                found.append(u)
            u += 0.05
        if found:
            on = True
            u0 = 0.5 * u0 + 0.5 * (sum(found) / len(found))
        if cur is None:
            cur, start = on, s
        elif on != cur:
            out.append((start, s, cur))
            cur, start = on, s
        s += step
    if cur is not None:
        out.append((start, s1, cur))
    return out


def marks_of(runs):
    """Whole marks and gaps (not cut by the ends): lengths. Breaks under 0.6 m in a mark are mended first (a 10 cm line slanting across the picture's 10 cm cells comes out dotted), and specks
    under 0.5 m dropped, so a mark cut by a junction's piece reads as one."""
    merged = []
    for r in runs:
        if merged and (not r[2] and r[1] - r[0] < 0.6 or r[2] == merged[-1][2]):
            merged[-1] = (merged[-1][0], r[1], merged[-1][2])
        else:
            merged.append(r)
    clean = []
    for r in merged:
        if r[2] and r[1] - r[0] < 0.5:
            r = (r[0], r[1], False)
        if clean and clean[-1][2] == r[2]:
            clean[-1] = (clean[-1][0], r[1], r[2])
        else:
            clean.append(r)
    inner = clean[1:-1]
    return [r[1] - r[0] for r in inner if r[2]], [r[1] - r[0] for r in inner if not r[2]]


def pattern(marks, gaps):
    """A pattern in words: '4.0 m marks, 4.0 to 8.0 m gaps'."""
    def span(v):
        v = sorted(v)
        lo, hi = v[0], v[-1]
        return f"{lo:.1f}" if hi - lo < 0.6 else f"{lo:.1f} to {hi:.1f}"
    return f"{span(marks)} m marks, {span(gaps)} m gaps"


def pattern_ok(marks, gaps, mark, gap, slack=0.4):
    return all(abs(m - mark) <= slack for m in marks) and all(abs(g - gap) <= slack for g in gaps)


def who_sees(where):
    """Which of the rider's views a breach is in: the road they ride in on, the road they ride along past it, or
    off the ride's road (seen to the side)."""
    if where.get('ride_arm'):
        return 'ridden'
    if 'along' in where or where.get('ring'):
        return 'ridden'
    if where.get('ride') in ('along the major road', 'from or onto a minor road'):
        return 'passed'
    return 'off'


class Report:
    def __init__(self):
        self.checked = defaultdict(int)
        self.breach = defaultdict(list)
        self.notes = defaultdict(int)
        self.seen_checked = defaultdict(int)
        self.seen_breach = defaultdict(int)


    def check(self, rule, ok, where, worth=1.0, say=''):
        self.checked[rule] += 1
        seen = who_sees(where)
        self.seen_checked[(rule, seen)] += 1
        if not ok:
            self.seen_breach[(rule, seen)] += 1
            self.breach[rule].append({'worth': worth, 'say': say, **where})
        return ok


def judge_junction(rec, rep, area, others=()):
    arms = rec['arms']
    lines = arm_lines(rec)
    pic = Picture(rec)
    where = {'area': area, 'id': rec['id'], 'lat': rec['lat'], 'lon': rec['lon'], 'kind': rec['kind'], 'ride': 'off'}
    ride_arms = [i for i, a in enumerate(arms) if a['w'] == 0]
    faults_before = sum(len(v) for v in rep.breach.values())
    signals = any(m['kind'] == 'traffic_signals' for m in rec['mapped'])
    if rec['lights'] or signals or rec['kind'].lower() in ('ring', 'mini', 'dualgap', 'slip') or rec['unpaved'] or rec['dual']:
        rep.notes['junctions skipped (lights, roundabouts, duals, unpaved)'] += 1
        return None
    major, th = through_line(lines, arms)
    if major is None:
        rep.notes['junctions with no laid through road'] += 1
        return None
    if ride_arms:
        where['ride'] = 'along the major road' if all(arms[i]['through'] for i in ride_arms) else 'from or onto a minor road'
    major_cls = min((arms[i]['cls'] for i in th), key=rank)
    minors = [i for i in range(len(arms)) if i not in th and lines[i] is not None]
    if not minors:
        return None
    major_road = next((r for r in rec['roads'] if r['w'] == arms[th[0]]['w']), None)
    major_mph = major_road['mph'] if major_road else 0
    gw = RULE['B1']['give_way_line']
    heavy_rows = 2 * gw['width']
    # A5: one priority road: the through arms carry no line across them.
    for i in th:
        ln = lines[i]
        if ln is None or any(m['kind'] in ('crossing', 'traffic_signals') or m['kind'].startswith('crossing') for m in rec['mapped']):
            continue
        rows, s = 0, 3.0
        while s < min(ln.length(), 25.0):
            p, d, h = ln.at(s)
            v, tot, hit = 0.3, 0, 0
            while v < h - 0.45:
                for sg in (1, -1):
                    tot += 1
                    hit += pic.at(*add(p, mul(left(d), sg * v))) == 2
                v += 0.1
            # (a row painted across most of the road: a line across it, not along it)
            if tot and hit / tot > 0.45:
                rows += 1
            s += 0.1
        rep.check('A5', rows < 3, where, rows * 0.1, f"a line painted across the through road ({arms[i]['cls']}) {rows * 0.1:.1f} m deep")
    for i in minors:
        arm, ln = arms[i], straighten(lines[i], major)
        minor_road = next((r for r in rec['roads'] if r['w'] == arm['w']), None)
        if minor_road is None or minor_road.get('unpaved'):
            continue
        cls = arm['cls']
        if cls in NO_MARKING:
            continue
        # (whose junction: a quiet street off a quiet street may be left unmarked, B7)
        quiet = cls in QUIET and major_cls in QUIET
        half = ln.at(3.0)[2]
        one_way = not (arm['in'] and arm['out'])
        if not arm['in']:
            continue   # (no traffic comes out of it: nothing to give way)
        w = dict(where, arm=cls, onto=major_cls, ride_arm=arm['w'] == 0)
        # B1 / B2: the give-way line across the approach lane, its nearer edge on the major road's edge.
        p_in, look_in, es_in = band(pic, ln, major, 0.15, half - 0.15, -0.15, 1.5)
        expect = (half - 0.3) * heavy_rows * gw['dash'] / (gw['dash'] + gw['gap'])
        has_line = look_in > 0 and p_in >= 0.5 * expect
        if not quiet:
            rep.check('B1', has_line, w, 1.0, f"no give-way line where {cls} meets {major_cls} ({p_in:.2f} of {expect:.2f} m2 painted)")
        elif has_line:
            rep.notes['quiet junctions marked anyway (allowed)'] += 1
        if has_line:
            es_in.sort()
            near_edge = es_in[max(0, int(len(es_in) * 0.05))]
            rep.check('B2', abs(near_edge) <= 0.3, w, abs(near_edge), f"give-way line {near_edge:+.2f} m from the major road's edge (should be 0)")
            # B3 / B4: the other half.
            p_out, look_out, _ = band(pic, ln, major, -(half - 0.15), -0.15, -0.15, 1.5)
            whole_needed = one_way or half < RULE['B4']['min_lane_line']
            heavy = look_out > 0 and p_out >= 0.5 * expect
            thin = look_out > 0 and p_out >= 0.12 * expect
            if whole_needed:
                rep.check('B4', heavy, w, 1.0, f"lane {half:.2f} m: the give-way line should cross the whole road")
            else:
                rep.check('B3', thin and not heavy, w, 1.0,
                          "give-way line across both lanes (should be the approach lane only)" if heavy else "no 1009A broken edge line on the exit half")
            # B5: the triangle in the approach lane, its leading edge 2.1 to 2.75 m from the line.
            tri = RULE['B5']['triangle']
            line_far = near_edge + heavy_rows + gw['between_lines']
            p_t, _, es_t = band(pic, ln, major, max(0.2, half * 0.5 - 0.9), half * 0.5 + 0.9, line_far + 0.5, line_far + 15.0 + tri['length'])
            if p_t < 0.15:
                rep.check('B5', False, w, 1.0, "no give-way triangle")
            else:
                gap = min(es_t) - line_far
                rep.check('B5', tri['from_line_min'] - 0.3 <= gap <= tri['from_line_max'] + 0.3, w, abs(gap - 2.4),
                          f"triangle {gap:.1f} m behind the line (should be {tri['from_line_min']} to {tri['from_line_max']})")
        # B6: a GIVE WAY sign where the major road is an A road or trunk, or at crossroads; only the ride's road has them
        # judged (the signs are placed by the ride).
        if has_line and arm['w'] == 0 and (major_cls in MAJOR_CLASSES or rec['kind'].lower() == 'cross'):
            ok, say = False, 'no GIVE WAY sign'
            for sg in rec['signs']:
                if sg['kind'] not in ('give_way', 'stop'):
                    continue
                d, hm, s_al, side = ln.nearest((sg['x'], sg['z']))
                dm, hmj, _, _ = major.nearest((sg['x'], sg['z']))
                back = dm - hmj
                p, dd, h = ln.at(s_al)
                lft = dot(sub((sg['x'], sg['z']), p), left(mul(dd, -1.0))) > 0
                faces = dot((sg['fx'], sg['fz']), dd) > 0.5
                if back > RULE['B6']['sign_back_max'] + 2.0 or back < -0.5:
                    continue
                size = mph_band(RULE['B6']['size_by_mph'], minor_road['mph'] or 30)[1]
                if not lft:
                    ok, say = False, 'GIVE WAY sign on the wrong side'
                elif not faces:
                    ok, say = False, 'GIVE WAY sign facing away from the traffic'
                elif abs(sg['big'] - size) > 0.05 and sg['kind'] == 'give_way':
                    ok, say = False, f"GIVE WAY sign {sg['big']:.2f} m (should be {size:.2f} at {minor_road['mph']} mph)"
                else:
                    ok, say = True, ''
                    break
            rep.check('B6', ok, w, 1.0, say)
        # C3: the major road's edge line not carried across the mouth.
        if major_road is not None and major.length() > 0:
            mside = 0.0
            p3, _, _ = ln.at(min(ln.length(), 6.0))
            _, _, s_m, side = major.nearest(p3)
            if abs(side) > 0:
                mside = 1.0 if side > 0 else -1.0
                hit, tot = 0, 0
                s = s_m - (half - 1.0)
                while s <= s_m + (half - 1.0):
                    p, d, hmj = major.at(s)
                    tot += 1
                    hit += any(pic.at(*add(p, mul(left(d), mside * (hmj - 0.3 + u)))) == 2 for u in (-0.15, -0.1, -0.05, 0.0, 0.05, 0.1, 0.15))
                    s += 0.1
                if tot and hmj >= 2.95:
                    rep.check('C3', hit / tot < 0.25, w, hit / tot, f"the major road's edge line runs across the side road's mouth ({hit / tot:.0%})")
        # C4: the side road's kerbside lines stop at its corner: none inside the major road.
        p_k, look_k, _ = band(pic, ln, major, -(half - 0.1), half - 0.1, -50.0, -0.6, kinds=(2, 3))
        p_c, _, _ = band(pic, ln, major, -0.6, 0.6, -50.0, -0.6, kinds=(2, 3))
        # (what lies inside the major road along the side road's own edges, less its centre and the major's own lines)
        p_e1, _, _ = band(pic, ln, major, half - 0.5, half - 0.1, -50.0, -0.6, kinds=(2, 3))
        p_e2, _, _ = band(pic, ln, major, -(half - 0.1), -(half - 0.5), -50.0, -0.6, kinds=(2, 3))
        stray = p_e1 + p_e2
        rep.check('C4', stray < 0.3, w, stray, f"the side road's edge or yellow lines run {stray:.1f} m2 into the major road")
    # C2 / C6: the major road's centre line runs on through the junction as a warning line.
    if major is not None and major_road is not None:
        mid = major.nearest((0.0, 0.0))[2]
        runs = runs_following(pic, major, max(0.0, mid - 25.0), min(major.length(), mid + 25.0))
        two_way = all(arms[i]['in'] and arms[i]['out'] for i in th)
        hmaj = major.at(mid)[2]
        if two_way and hmaj * 2 >= RULE['D2']['min_width'] and arms[th[0]]['cls'] not in QUIET:
            near = runs_along(pic, major, max(0.0, mid - 8.0), min(major.length(), mid + 8.0), 0.0)
            cover = sum(r[1] - r[0] for r in near if r[2]) / max(1e-9, sum(r[1] - r[0] for r in near))
            rep.check('C2', cover > 0.25, dict(where, onto=major_cls), 1.0, f"no centre line across the junction ({cover:.0%} painted)")
            # (the pattern starts afresh at a junction, where two stretches of the through road meet, as a painter starts it:
            # at this one's middle and at any other junction on the line within reach (a stagger's partner), each piece
            # judged on its own, the marks and gaps cut there not)
            cuts = [mid]
            for ox, oz in others:
                q = (ox - rec['x'], oz - rec['z'])
                if math.hypot(*q) < 30.0:
                    d, _, s_o, _ = major.nearest(q)
                    if d < 4.0 and abs(s_o - mid) < 25.0 and abs(s_o - mid) > 1.0:
                        cuts.append(s_o)
            cuts = sorted(cuts)
            marks, gaps = [], []
            for lo, hi in zip([-1e18] + cuts, cuts + [1e18]):
                piece = [(max(a, lo), min(b, hi), p) for a, b, p in runs if b > lo and a < hi]
                m_, g_ = marks_of(piece)
                marks += m_
                gaps += g_
            if marks and gaps:
                row = next(r for r in RULE['C6']['warning_line'] if (major_mph or 30) <= r['max_mph'])
                m, g = sorted(marks)[len(marks) // 2], sorted(gaps)[len(gaps) // 2]
                rep.check('C6', pattern_ok(marks, gaps, row['mark'], row['gap'], 0.5), dict(where, onto=major_cls), abs(m - row['mark']) + abs(g - row['gap']),
                          f"warning line {pattern(marks, gaps)} (should be {row['mark']:g} m marks, {row['gap']:g} m gaps at {major_mph or 30} mph)")
    return sum(len(v) for v in rep.breach.values()) == faults_before


def judge_stretch(rec, rep, area):
    pic = Picture(rec)
    road = None
    for r in rec['roads']:
        if r['w'] == 0 and len(r['pts']) >= 4:
            road = r
            break
    if road is None:
        return
    pts = road['pts']
    i0 = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0], pts[i][1]))
    line = Line(pts)
    mid = line.s[i0]
    s0, s1 = max(0.0, mid - 25.0), min(line.length(), mid + 25.0)
    p, d, half = line.at(mid)
    lanes, one_way, built = pts[i0][4], pts[i0][5], pts[i0][7]
    where = {'area': area, 'along': rec['along'], 'lat': rec['lat'], 'lon': rec['lon'], 'cls': road['cls'], 'mph': road['mph'], 'width': round(2 * half, 2)}
    if road.get('unpaved') or s1 - s0 < 20.0:
        return
    runs = runs_along(pic, line, s0, s1, 0.0)
    covered = sum(r[1] - r[0] for r in runs if r[2]) / max(1e-9, s1 - s0)
    has_centre = covered > 0.15
    if one_way:
        return
    # D2: no centre line under 5.5 m.
    if 2 * half < RULE['D2']['min_width']:
        rep.check('D2', not has_centre, where, 1.0, f"centre line on a road {2 * half:.1f} m wide (none under {RULE['D2']['min_width']} m)")
        return
    # D1: the centre line's marks and gaps by the limit.
    if has_centre:
        marks, gaps = marks_of(runs)
        if marks and gaps:
            row = next(r for r in RULE['D1']['centre_line'] if (road['mph'] or 30) <= r['max_mph'])
            m, g = sorted(marks)[len(marks) // 2], sorted(gaps)[len(gaps) // 2]
            rep.check('D1', pattern_ok(marks, gaps, row['mark'], row['gap']), where, abs(m - row['mark']) + abs(g - row['gap']),
                      f"centre line {pattern(marks, gaps)} (should be {row['mark']:g} m marks, {row['gap']:g} m gaps at {road['mph'] or 30} mph)")
    else:
        rep.notes['two-way stretches 5.5 m or wider with no centre line'] += 1
    # D6: an edge line's outer side about 0.225 m in from the road's edge, 0.1 m wide.
    for sg in (1.0, -1.0):
        found = []
        s = s0 + 2.0
        while s < s1 - 2.0:
            p, d, h = line.at(s)
            n = mul(left(d), sg)
            # (out from the middle: where the surface ends, and the paint nearest it)
            edge, lo, hi = None, None, None
            v = h - 1.0
            while v < h + 1.0:
                k = pic.at(*add(p, mul(n, v)))
                if k == 2:
                    lo = v if lo is None else lo
                    hi = v
                if k in (1, 2, 3):
                    edge = v
                v += 0.05
            if lo is not None and edge is not None and hi > h - 0.8:
                found.append((edge + 0.05 - hi - 0.025, hi - lo + 0.05))
            s += 1.0
        if len(found) >= 10:
            insets = sorted(f[0] for f in found)
            widths = sorted(f[1] for f in found)
            inset, width = insets[len(insets) // 2], widths[len(widths) // 2]
            want = RULE['D6']['inset']
            rep.check('D6', abs(inset - want) <= 0.1 and abs(width - RULE['D6']['width']['ordinary']) <= 0.04, dict(where, side='left' if sg > 0 else 'right'), abs(inset - want),
                      f"edge line {inset:.2f} m in from the edge, {width:.2f} m wide (should be {want} in, {RULE['D6']['width']['ordinary']} wide)")


def judge_ring(rec, rep, area):
    pts = rec['ride']
    # (the turn the ride makes round the middle: x east, z south, so clockwise from above is + in x, z)
    turn = 0.0
    for a, b in zip(pts, pts[1:]):
        aa, ab = math.atan2(a[1], a[0]), math.atan2(b[1], b[0])
        d = ab - aa
        while d > math.pi: d -= 2 * math.pi
        while d < -math.pi: d += 2 * math.pi
        turn += d
    where = {'area': area, 'id': rec['id'], 'lat': rec['lat'], 'lon': rec['lon'], 'ring': 1}
    if abs(turn) > 0.5:
        rep.check('E1', turn > 0, where, 1.0, f"ridden anticlockwise ({math.degrees(turn):.0f} degrees)")


COLOURS = {0: (70, 92, 52), 1: (52, 52, 56), 2: (240, 240, 228), 3: (230, 170, 20), 4: (110, 104, 86)}


def tile_map(lat, lon, metres, size, cache):
    """The real map round a place, north up, metres across, size pixels (OpenStreetMap, zoom 19)."""
    import subprocess
    from PIL import Image
    z = 19
    n = 2 ** z
    def px(la, lo):
        x = (lo + 180) / 360 * n * 256
        y = (1 - math.log(math.tan(math.radians(la)) + 1 / math.cos(math.radians(la))) / math.pi) / 2 * n * 256
        return x, y
    mpp = 156543.03392 * math.cos(math.radians(lat)) / n
    span = metres / mpp
    cx, cy = px(lat, lon)
    x0, y0 = cx - span / 2, cy - span / 2
    w = int(span) + 1
    img = Image.new('RGB', (w, w), (200, 200, 200))
    os.makedirs(cache, exist_ok=True)
    for tx in range(int(x0 // 256), int((x0 + span) // 256) + 1):
        for ty in range(int(y0 // 256), int((y0 + span) // 256) + 1):
            f = os.path.join(cache, f'z{z}_{tx}_{ty}.png')
            if not os.path.exists(f):
                subprocess.run(['curl', '-s', '-A', 'TorqueShedSimTest/1.0', '-o', f, f'https://tile.openstreetmap.org/{z}/{tx}/{ty}.png'])
            try:
                img.paste(Image.open(f).convert('RGB'), (int(tx * 256 - x0), int(ty * 256 - y0)))
            except Exception:
                pass
    return img.resize((size, size))


def draw_example(rec, ex, path, cache):
    from PIL import Image, ImageDraw
    pic = Picture(rec)
    n = pic.n
    img = Image.new('RGB', (n, n))
    px = img.load()
    for i, v in enumerate(pic.k):
        px[i % n, i // n] = COLOURS.get(v, (255, 0, 255))
    d = ImageDraw.Draw(img)
    def to(x, z): return ((x + pic.half) / pic.cell, (z + pic.half) / pic.cell)
    if 'arms' in rec:
        for arm, ln in zip(rec['arms'], arm_lines(rec)):
            if ln is None:
                continue
            d.line([to(*p) for p in ln.p], fill=(220, 40, 40) if arm['through'] else (40, 120, 255), width=2)
    for sg in rec.get('signs', []):
        x, z = to(sg['x'], sg['z'])
        d.ellipse([x - 5, z - 5, x + 5, z + 5], outline=(255, 60, 200), width=3)
    size = 600
    side = tile_map(rec['lat'], rec['lon'], 2 * pic.half, size, cache)
    both = Image.new('RGB', (2 * size + 10, size + 60), (255, 255, 255))
    both.paste(img.resize((size, size)), (0, 0))
    both.paste(side, (size + 10, 0))
    t = ImageDraw.Draw(both)
    t.text((8, size + 6), f"{ex['rule']}: {ex['say']}", fill=(0, 0, 0))
    t.text((8, size + 24), f"{ex['area']} {rec['lat']:.5f}, {rec['lon']:.5f}  {rec.get('kind', 'stretch')}  ride: {ex.get('ride', 'on')}  (60 m across, north up; red the through road, blue the others; pink rings are signs)", fill=(0, 0, 0))
    both.save(path)


def same_diff(pa, pb, off, within=25.0):
    """A3's measure: m2 of paint, and of tarmac, one picture has and the other does not, within `within` m of the middle.
    (A cell counts only where neither picture has its kind within a cell of it: the same line drawn a few cm over,
    as each ride's own points put it, is the same line.)"""
    cell2 = pa.cell * pa.cell
    n, c = pa.n, pa.cell
    paint = lambda k: k in (2, 3)
    tar = lambda k: k in (1, 2, 3)

    def near(pic, x, z, test, dx=0.0, dz=0.0):
        for ox in (-c, 0.0, c):
            for oz in (-c, 0.0, c):
                if test(pic.at(x + ox - dx, z + oz - dz)):
                    return True
        return False

    dpaint = dtar = 0.0
    for i in range(n * n):
        x, z = (i % n + 0.5) * c - pa.half, (i // n + 0.5) * c - pa.half
        if x * x + z * z > within * within:
            continue
        ka, kb = pa.k[i], pb.at(x - off[0], z - off[1])
        if paint(ka) != paint(kb):
            if not (near(pb, x, z, paint, off[0], off[1]) if paint(ka) else near(pa, x, z, paint)):
                dpaint += cell2
        elif tar(ka) != tar(kb):
            if not (near(pb, x, z, tar, off[0], off[1]) if tar(ka) else near(pa, x, z, tar)):
                dtar += cell2
    return dpaint, dtar


def same_check(paths, out, pics):
    """A3: every junction more than one ride passes, drawn the same by each."""
    seen = defaultdict(list)   # (lat, lon rounded) -> [(ride, rec)]
    ends = []                  # every ride's start and finish (lat, lon)
    for path in paths:
        ride = os.path.splitext(os.path.basename(path))[0]
        for line in open(path):
            rec = json.loads(line)
            if rec['t'] == 'ends':
                ends += [(rec['a']['lat'], rec['a']['lon']), (rec['b']['lat'], rec['b']['lon'])]
            if rec['t'] != 'jn':
                continue
            seen[(round(rec['lat'] * 20000), round(rec['lon'] * 12000))].append((ride, rec))

    def near_end(rec):
        # (a ride starts and finishes differently, its run-out past the last pin, so a junction within 100 m of
        # any ride's end is not a fair test of A3)
        return any(math.hypot((rec['lon'] - lo) * 111320.0 * math.cos(math.radians(la)), (rec['lat'] - la) * 110574.0) < 100.0
                   for la, lo in ends)
    rep = Report()
    shown = []
    for key, recs in sorted(seen.items()):
        rides = {}
        for ride, rec in recs:
            rides.setdefault(ride, rec)
        if len(rides) < 2 or near_end(next(iter(rides.values()))):
            continue
        names = sorted(rides)
        a = rides[names[0]]
        pa = Picture(a)
        for nb in names[1:]:
            b = rides[nb]
            pb = Picture(b)
            # (the same place to a few cm: both pictures centred on the junction's middle as each ride found it)
            off = ((b['lon'] - a['lon']) * 111320.0 * math.cos(math.radians(a['lat'])), -(b['lat'] - a['lat']) * 110574.0)
            if math.hypot(*off) > 0.5:
                continue
            dpaint, dtar = same_diff(pa, pb, off)
            sa = sorted((s['kind'], round(s['x']), round(s['z'])) for s in a['signs'] if s['kind'] in ('give_way', 'stop'))
            sb = sorted((s['kind'], round(s['x']), round(s['z'])) for s in b['signs'] if s['kind'] in ('give_way', 'stop'))
            where = {'area': names[0] + ' / ' + nb, 'id': a['id'], 'lat': a['lat'], 'lon': a['lon'], 'kind': a['kind'], 'ride': 'passed'}
            ok = dpaint < 0.5 and dtar < 2.0 and sa == sb
            say = f"drawn differently by the two rides: {dpaint:.1f} m2 of paint, {dtar:.1f} m2 of tarmac" + ("" if sa == sb else ", junction signs differ")
            if rep.check('A3', ok, where, dpaint + dtar, say) is False:
                shown.append((dpaint + dtar, a, b, where, say))
    r = {'checked': rep.checked['A3'], 'breaches': len(rep.breach['A3']), 'examples': sorted(rep.breach['A3'], key=lambda x: -x['worth'])[:8]}
    print(f"A3: {r['breaches']} of {r['checked']} junctions passed by more than one ride are drawn differently: {RULE['A3']['text']}")
    if pics:
        os.makedirs(pics, exist_ok=True)
        for k, (_, a, b, where, say) in enumerate(sorted(shown, key=lambda t: -t[0])[:4]):
            from PIL import Image
            ex = dict(where, rule='A3', say=say)
            draw_example(a, dict(ex, say=say + ' (first ride)'), os.path.join(pics, f'A3_{k + 1}a.png'), os.path.join(pics, 'tiles'))
            draw_example(b, dict(ex, say=say + ' (second ride)'), os.path.join(pics, f'A3_{k + 1}b.png'), os.path.join(pics, 'tiles'))
    if out:
        json.dump(r, open(out, 'w'), indent=1)


def main(argv):
    dumps, out, pics = [], None, None
    if argv and argv[0] == '--same':
        rest = argv[1:]
        o = rest[rest.index('--out') + 1] if '--out' in rest else None
        pc = rest[rest.index('--pics') + 1] if '--pics' in rest else None
        same_check([a for k, a in enumerate(rest) if a not in ('--out', '--pics') and (k == 0 or rest[k - 1] not in ('--out', '--pics'))], o, pc)
        return
    i = 0
    while i < len(argv):
        if argv[i] == '--out':
            out = argv[i + 1]; i += 2
        elif argv[i] == '--pics':
            pics = argv[i + 1]; i += 2
        else:
            dumps.append(argv[i]); i += 1
    rep = Report()
    per_area = {}
    meta = {}
    for path in dumps:
        area = os.path.splitext(os.path.basename(path))[0]
        good = total = 0
        others = []   # (every junction's middle in the area, for C6's cuts)
        for line in open(path):
            rec = json.loads(line)
            if rec['t'] == 'jn':
                others.append((rec['x'], rec['z']))
        for line in open(path):
            rec = json.loads(line)
            t = rec['t']
            if t == 'meta':
                meta[area] = rec
            elif t == 'jn':
                ok = judge_junction(rec, rep, area, others)
                if ok is not None:
                    total += 1
                    good += ok
            elif t == 'stretch':
                judge_stretch(rec, rep, area)
            elif t == 'ring':
                judge_ring(rec, rep, area)
        per_area[area] = {'junctions': total, 'clean': good}
    summary = {'rules': {}, 'areas': per_area, 'notes': dict(rep.notes), 'meta': meta}
    for rid in sorted(set(rep.checked) | set(rep.breach)):
        # (the rider's own road first: what they meet head on, then what they pass)
        b = sorted(rep.breach[rid], key=lambda x: ({'ridden': 0, 'passed': 1}.get(who_sees(x), 2), -x['worth']))
        summary['rules'][rid] = {'text': RULE[rid]['text'], 'checked': rep.checked[rid], 'breaches': len(b), 'examples': b[:8],
                                 'kinds': {}}
        kinds = defaultdict(int)
        for x in b:
            kinds[x['say'].split(' (')[0] if '(' in x['say'] else x['say']] += 1
        summary['rules'][rid]['kinds'] = dict(sorted(kinds.items(), key=lambda kv: -kv[1])[:6])
        summary['rules'][rid]['seen'] = {sn: [rep.seen_breach[(rid, sn)], rep.seen_checked[(rid, sn)]] for sn in ('ridden', 'passed', 'off') if rep.seen_checked[(rid, sn)]}
    for rid, r in summary['rules'].items():
        print(f"{rid}: {r['breaches']} of {r['checked']} break it: {r['text']}  " + ', '.join(f"{k} {v[0]}/{v[1]}" for k, v in r['seen'].items()))
        for k, v in r['kinds'].items():
            print(f"    {v} x {k}")
    for a, r in per_area.items():
        print(f"{a}: {r['clean']} of {r['junctions']} junctions clean")
    for k, v in rep.notes.items():
        print(f"note: {v} {k}")
    if pics:
        os.makedirs(pics, exist_ok=True)
        want = defaultdict(list)
        for rid, r in summary['rules'].items():
            for k, ex in enumerate(r['examples'][:3]):
                ex['rule'] = rid
                ex['pic'] = f"{rid}_{k + 1}.png"
                want[ex['area']].append(ex)
        for path in dumps:
            area = os.path.splitext(os.path.basename(path))[0]
            if not want[area]:
                continue
            for line in open(path):
                rec = json.loads(line)
                for ex in want[area]:
                    if (rec['t'] == 'jn' and ex.get('id') == rec['id'] and 'along' not in ex and 'ring' not in ex) or (rec['t'] == 'stretch' and ex.get('along') == rec['along']):
                        draw_example(rec, ex, os.path.join(pics, ex['pic']), os.path.join(pics, 'tiles'))
    if out:
        json.dump(summary, open(out, 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
