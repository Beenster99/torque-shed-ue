import json, sys, numpy as np
for town in sys.argv[1:]:
    R = json.load(open(town + ".json")); M = [r for r in R if "med" in r]
    mapped = [r for r in M if r["mapped"]]; fl = [r for r in M if r["floors"] and not r["mapped"]]
    ok = [r for r in M if 2.5 <= r["med"] <= 150]
    low = [r for r in M if r["med"] < 2.5]
    print(f"== {town}: {len(R)} outlines, {len(M)} measured, {len(ok)} usable (2.5-150 m), {len(low)} under 2.5 m")
    print(f"   mapped height {len(mapped)} ({100*len(mapped)/len(R):.0f}%), only floors {len(fl)}, neither {len(R)-len(mapped)-len(fl)}")
    if mapped:
        d = np.array([r["p90"] - r["mapped"] for r in mapped]); dm = np.array([r["med"] - r["mapped"] for r in mapped])
        print(f"   mapped vs LiDAR p90: median diff {np.median(d):+.1f} m, |diff|<2m {np.mean(abs(d)<2)*100:.0f}%, <4m {np.mean(abs(d)<4)*100:.0f}%; vs median {np.median(dm):+.1f}")
    for c in ("house", "terrace", "residential", "detached", "semidetached_house", "garage", "church", "commercial", "retail", "apartments", "industrial", ""):
        h = [r["med"] for r in ok if r["cls"] == c]
        if len(h) >= 5: print(f"   {c or 'building'}: n={len(h)} med {np.median(h):.1f} (p10 {np.percentile(h,10):.1f}, p90 {np.percentile(h,90):.1f}) top p90 {np.median([r['p90'] for r in ok if r['cls']==c]):.1f}")
    tall = sorted(ok, key=lambda r: -r["top"])[:5]
    print("   tallest tops:", [(r["cls"], r["top"], r["mapped"]) for r in tall])
