import re, base64, io, shutil, os
from PIL import Image
s=open('out/definition_proof.html').read()
head=s[:s.index('<main>')]
R='out/renders/corvano_naked_def_'
items=[
('q','Whole bike, front quarter','front_quarter','Radiator now sits below the frame rail and clear of it. The engine top end is a little shorter and 12 mm further back to make the room.'),
('shot_rearq','Rear quarter, exhaust side','rear_quarter','Open silencer outlets, hugger on the swingarm, heel plates.'),
('shot_rearq_low','Rear quarter, low','rear_quarter_low',''),
('side','Side view','side','Square-on, at the same scale as the drawings.'),
('shot_engine','Engine, right side','engine_right','Cooling fins now squared, wider towards the head. Radiator moved, heel plate behind the peg, hugger carried by the swingarm, pillion hanger rebuilt.'),
('shot_weld','Frame welds','welds','Fillet weld beads where every tube meets another, painted over with the frame. End-to-end joints now get a plain ring on the joint itself.'),
('shot_weld2','Frame welds, seat rail','welds_seat_rail','The seat rails now stay outside the side panels and tuck in under the seat.'),
('shot_heel','Heel plate','heel_plate','Brushed aluminium, drilled, bolted to the footrest hanger and cut away round the swingarm pivot nut.'),
('shot_hugger','Rear hugger','hugger','Half the length, on two moulded legs bolted to the top of the swingarm. The old guard under the tail is gone.'),
('shot_guard','Front mudguard','front_mudguard','From your markup: a shroud on the front of each fork leg, bolted to the fork foot, sweeping forward into a plain slim shell that now sits close to the tyre. Nothing behind the leg.'),
('shot_guard_front','Front mudguard from the front','front_mudguard_front',''),
('shot_junction','Pillion hanger and tail','pillion_hanger','Where the clipping was: cast hanger bolted to the rail at two points, tail tucked behind the side panel.'),
('shot_grip','Hand grip','grip','From your grip picture: diamond knurl all round, moulded slots with grey inserts, thin diagonal grooves, big flange, end collar.'),
('shot_bars','Bars and levers','bars','Levers cut from plate to your drawing: wide drilled root, red span adjuster, dog-leg, long curved finger section, ring end.'),
('shot_service','Service view, tank lifted off','service','The clutch lever from above, plus the service parts as before.'),
('shot_outlet','Silencer outlets','outlets','Dished end caps with an open tail pipe, dark inside.'),
('shot_engine_l','Engine, left side','engine_left',''),
]
figs=[]
os.makedirs('/mnt/project-files/bike_models_v2/renders/definition/',exist_ok=True)
for tag,title,slug,cap in items:
    im=Image.open(R+tag+'.png').convert('RGB')
    shutil.copy(R+tag+'.png','/mnt/project-files/bike_models_v2/renders/definition/corvano_naked_r4_'+slug+'.png')
    if im.width>1400: im=im.resize((1400,int(im.height*1400/im.width)))
    b=io.BytesIO(); im.save(b,'JPEG',quality=82)
    figs.append(f'<figure><img alt="{title}" src="data:image/jpeg;base64,{base64.b64encode(b.getvalue()).decode()}"><figcaption><strong>{title}</strong> {cap}</figcaption></figure>')
body='''<main>
<div class="k">Bike Models V2 · Definition pass</div>
<h1>Corvano naked, proof 4</h1>
<p class="lede">Every point from your notes of 15 November afternoon, plus the frame welds, the shorter hugger and the reworked front mudguard. Rendered straight from the model, nothing retouched.</p>
<div class="grid">'''+'\n'.join(figs)+'</div>\n</main>'
open('out/definition_proof.html','w').write(head+body)
shutil.copy('out/definition_proof.html','/mnt/project-files/bike_models_v2/definition_proof.html')
print(len(head+body))
