#!/usr/bin/env python3
# sumk.py dir...: totals of jcheck outputs (*.txt) by kind; old faults, ride, the plan's standard; the worst junctions
import sys, re, glob, collections
rows=[]
for d in sys.argv[1:]:
    for f in sorted(glob.glob(d+'/*.txt')):
        for l in open(f):
            if not l.startswith('jcheck node'): continue
            m=re.search(r'node (\d+) \(([-\d.]+), ([-\d.]+)\) (\d+) roads: fold ([\d.]+) step ([\d.]+) spike ([\d.]+) hole ([\d.]+) land ([\d.]+) grass ([\d.]+)(  BAD)? \| (\S+) over ([\d.]+) ride (\d+)/(\d+) bad off ([\d.]+) jolt ([\d.]+) pull ([\d.]+)',l)
            if not m: continue
            g=m.groups()
            rows.append(dict(place=f.split('/')[-1][:-4],node=int(g[0]),lat=float(g[1]),lon=float(g[2]),roads=int(g[3]),fold=float(g[4]),step=float(g[5]),spike=float(g[6]),hole=float(g[7]),land=float(g[8]),grass=float(g[9]),bad=bool(g[10]),kind=g[11],over=float(g[12]),badturns=int(g[13]),turns=int(g[14]),off=float(g[15]),jolt=float(g[16]),pull=float(g[17]),meets='MEETS' in l))
K=collections.OrderedDict()
for k in ['T','cross','fork','multi','cluster','lights','dual','ring','mini','unpaved']: K[k]=[r for r in rows if r['kind']==k]
def line(name,rs):
    n=len(rs) or 1
    return f"{name:9s} {len(rs):5d}  bad {sum(r['bad'] for r in rs):5d} ({100*sum(r['bad'] for r in rs)/n:3.0f}%)  ride-bad {sum(r['badturns']>0 for r in rs):5d} ({100*sum(r['badturns']>0 for r in rs)/n:3.0f}%)  meet {sum(r['meets'] for r in rs):4d}  step {sum(r['step'] for r in rs):7.0f} m2  spikes {sum(r['spike'] for r in rs):5.0f}  holes {sum(r['hole'] for r in rs):6.0f} m  land {sum(r['land'] for r in rs):6.0f}  grass {sum(r['grass'] for r in rs):6.0f}  doubled {sum(r['over'] for r in rs):7.0f} m2"
for k,rs in K.items():
    if rs: print(line(k,rs))
print(line('ALL',rows))
t=sum(r['turns'] for r in rows); b=sum(r['badturns'] for r in rows); print(f"turns ridden {t}, bad {b}")
if '-w' in sys.argv[0:1] or True:
    w=sorted(rows,key=lambda r:-(r['step']+r['spike']+3*r['hole']+r['land']+r['grass']+2*r['badturns']))
    print('worst:'); [print(f"  {r['place']} node {r['node']} {r['kind']} ({r['lat']:.5f},{r['lon']:.5f}) step {r['step']:.0f} spike {r['spike']:.0f} hole {r['hole']:.1f} land {r['land']:.0f} grass {r['grass']:.0f} ride {r['badturns']}/{r['turns']} jolt {r['jolt']:.2f} pull {r['pull']:.1f}") for r in w[:20]]
