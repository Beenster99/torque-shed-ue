"""The bike dirt (Tools/ue_bike_dirt.py DIRT_HLSL, without the wash mask and scan) as GLSL for
page.html, the cloud preview of where the muck lies. Run after changing DIRT_HLSL:
    python Tools/bike_dirt_preview/make_glsl.py
"""
import os
import re
import sys

P = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__)) + '/'
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ue_bike_dirt.py')).read()
h=src.split('DIRT_HLSL = """')[1].split('"""')[0]
st=h.split('struct FBikeDirt\n{')[1].split('\n};')[0]
st=st.replace('    float3 Dims; float Grid; float2 Atlas;\n','')
a=st.index('    float4 Tap('); b=st.index('    float Hash(')
st=st[:a]+st[b:]
body='// Where this pixel is on the bike'+h.split('// Where this pixel is on the bike')[1]
a=body.index('// Washed off by the jet'); b=body.index('float s = lvl')
body=body[:a]+'float washed = 0.0;\n'+body[b:]
a=body.index('// Just washed:')
body=body[:a]+'if (!(alpha >= 0.0 && alpha <= 1.0)) alpha = 0.0;\noutLook = vec4(col, alpha); outRough = rough;\n'
def g(t):
    t=t.replace('float2','vec2').replace('float3','vec3').replace('float4','vec4')
    t=re.sub(r'\blerp\(','mix(',t); t=re.sub(r'\bfrac\(','fract(',t); t=re.sub(r'\batan2\(','atan(',t)
    t=re.sub(r'(?<![A-Za-z0-9_])F\.','',t)
    t=t.replace('pow(saturate(col), 2.2)','pow(saturate(col), vec3(2.2))')
    return t
glsl='float saturate(float x){return clamp(x,0.0,1.0);}\nvec3 saturate(vec3 x){return clamp(x,0.0,1.0);}\n'+g(st)+'\nvoid dirt(vec3 P, vec3 N, out vec4 outLook, out float outRough){\n'+g(body)+'}\n'
open(P+'dirt.glsl','w').write(glsl)
