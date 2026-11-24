#!/usr/bin/env python3
# jindex_sheet.py out.png R dump:label ... : for each place, the raw map (left: every point where 3+ roads meet, red)
# beside the tidied junctions (right: one ring per junction, coloured by kind, lines to the points merged into it)
import sys
from PIL import Image, ImageDraw, ImageFont
out=sys.argv[1]; R=float(sys.argv[2]); items=sys.argv[3:]
S=520
COL={'T':(30,120,220),'cross':(20,160,60),'stagger':(120,180,0),'fork':(230,140,0),'slip':(200,90,200),'multi':(200,0,0),
     'ring':(120,60,200),'mini':(0,170,170),'dualgap':(150,100,40),'bend':(150,150,150),'none':(0,0,0)}
try: F=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',13); FB=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',15)
except: F=FB=ImageFont.load_default()
rows=[]
for it in items:
    f,lab=it.split(':',1)
    N=[];E=[];J=[]
    for l in open(f):
        w=l.split()
        if not w: continue
        if w[0]=='N': N.append((float(w[1]),float(w[2]),int(w[3])))
        elif w[0]=='E': E.append((w[1],w[2]=='dual',[(float(w[i]),float(w[i+1])) for i in range(3,len(w)-1,2)]))
        elif w[0]=='J':
            head,*arms=l.split('|'); h=head.split()
            J.append((h[1],float(h[2]),float(h[3]),int(h[5]),[tuple(map(float,a.split()[:2])) for a in arms]))
    s=S/(2*R); P=lambda x,z:(S/2+x*s,S/2+z*s)
    def base():
        im=Image.new('RGB',(S,S),(250,250,247)); d=ImageDraw.Draw(im)
        for c,du,pts in E:
            wd=3 if c in('primary','trunk','secondary','motorway') else 2 if c in ('tertiary','residential','unclassified') else 1
            d.line([P(*p) for p in pts],fill=(70,110,210) if du else (120,120,120),width=wd)
        return im,d
    a,da=base()
    raw=[n for n in N if n[2]>=3]
    for x,z,k in raw: da.ellipse([P(x,z)[0]-3,P(x,z)[1]-3,P(x,z)[0]+3,P(x,z)[1]+3],fill=(220,0,0))
    da.rectangle([0,0,S,22],fill=(255,255,255)); da.text((6,4),f"{lab}: map as drawn, {len(raw)} junction points",fill=(0,0,0),font=FB)
    b,db=base()
    for k,x,z,nn,arms in J:
        c=COL.get(k,(0,0,0)); cx,cy=P(x,z)
        for ax,az in arms: db.line([cx,cy,*P(ax,az)],fill=c,width=2)
        r=7 if nn>1 else 5; db.ellipse([cx-r,cy-r,cx+r,cy+r],outline=c,width=3)
    db.rectangle([0,0,S,22],fill=(255,255,255)); db.text((6,4),f"tidied: {len(J)} junctions, {sum(1 for j in J if j[3]>1)} merged",fill=(0,0,0),font=FB)
    rows.append((a,b))
L=Image.new('RGB',(2*S,40),(255,255,255)); dl=ImageDraw.Draw(L); x=6
for k,c in COL.items(): dl.ellipse([x,14,x+12,26],outline=c,width=3); dl.text((x+16,12),k,fill=(0,0,0),font=F); x+=86
Sh=Image.new('RGB',(2*S+10,len(rows)*(S+10)+40),(255,255,255))
for i,(a,b) in enumerate(rows): Sh.paste(a,(0,i*(S+10))); Sh.paste(b,(S+10,i*(S+10)))
Sh.paste(L,(0,len(rows)*(S+10))); Sh.save(out)
