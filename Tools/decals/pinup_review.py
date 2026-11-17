"""Stage 6 review only (not on a shelf yet): classic pin-up silhouettes, not explicit.
    python3 Tools/decals/pinup_review.py OUT.png
Joins the art shelf only if beenster passes the review sheet."""
import sys, math; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import numpy as np
from PIL import Image
import dk
from dk import *

def limb(pts, w0, w1, wm=None):
    return taper(spline(pts, 14), w0, w1, wm if wm else (w0+w1)/2, ends="round")

def standing():
    # profile, facing right: back arched, one knee forward, a hand behind her head
    pts = [(250,22),(270,28),(279,44),(282,58),(291,69),(284,75),(287,81),(282,87),(284,92),(274,98),(270,110),
           (276,126),(292,140),(306,158),(301,176),(284,186),(276,204),(280,226),(290,250),(296,270),
           (300,300),(298,340),(296,392),(294,432),(292,456),(314,470),(318,478),(294,481),(283,472),(281,489),(274,489),
           (276,462),(273,432),(280,394),(276,350),(254,322),(232,292),(222,264),(228,236),(244,212),(246,190),(240,162),(242,136),
           (249,120),(244,108),(232,102),(222,84),(222,58),(232,36)]
    body = poly(spline(pts, 8, closed=True))
    knee = union(limb([(286,272),(318,312),(338,352)], 36, 22, 30), limb([(338,352),(326,400),(318,440)], 22, 11, 17),
                 poly([(312,436),(326,436),(346,462),(340,468),(318,458)]))
    arm = union(limb([(246,134),(214,98),(194,60)], 17, 13), limb([(194,60),(208,40),(234,40)], 13, 11))
    bun = circle(236, 28, 15)
    curls = union(circle(226, 100, 10), circle(236, 106, 9))
    return union(body, knee, arm, bun, curls)

def seated():
    # sat side-on, knees drawn up, leaning back on her hands
    pts = [(200,92),(220,98),(229,114),(232,128),(241,139),(234,145),(237,151),(232,157),(234,162),(224,168),(222,180),
           (230,196),(248,210),(262,228),(256,246),(240,254),(236,272),(242,292),(254,306),
           (330,250),(352,236),(366,248),(352,272),(372,350),(392,356),(396,364),(366,366),(356,356),(338,290),
           (290,330),(232,338),(196,326),(186,300),(194,270),(196,240),(190,214),(192,196),
           (198,186),(194,176),(182,170),(172,152),(172,126),(182,104)]
    body = poly(spline(pts, 8, closed=True))
    arm = union(limb([(196,206),(176,262),(160,318)], 17, 12), limb([(160,318),(150,332)], 12, 10))
    bun = circle(186, 96, 15)
    curls = union(circle(176, 170, 10), circle(186, 176, 9))
    return union(body, arm, bun, curls)

ims=[]
for name, k in (("standing", standing()), ("seated", seated())):
    d = Decal("pinup_"+name,"art","Pin-up",[],512,512, fill=k, line=outline(k, 7), colours=2).fitted(14)
    rgba=dk.compose(d.masks(1.0),(0.05,0.05,0.05),(0.05,0.05,0.05),bg=(0.93,0.91,0.86))
    im=Image.fromarray((np.clip(rgba,0,1)*255+0.5).astype(np.uint8),"RGBA").convert("RGB")
    im.thumbnail((500,500)); ims.append(im)
s=Image.new("RGB",(1040,540),(60,60,60))
for i,im in enumerate(ims): s.paste(im,(20+i*510,20))
s.save(sys.argv[1] if len(sys.argv) > 1 else 'pinup_review.png')
