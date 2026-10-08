"""Compile production lightmap HLSL arithmetic and compare source-shader cases.

Only a float3 shim translates HLSL builtins; LIGHTMAP_CODE is read unchanged.
This verifies algebra/UNORM8 transfer, not UE shader or postprocess compilation.
"""
import math
import pathlib
import runpy
import subprocess
import tempfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
HELPER=runpy.run_path(str(ROOT/'tools/bridge_lighting_materials.py'))
SHIM=r'''
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
using std::max;using std::pow;
struct float3 {float r,g,b;float3(float x,float y,float z):r(x),g(y),b(z){}float3(float v):r(v),g(v),b(v){}float3& operator-=(float v){r-=v;g-=v;b-=v;return *this;}float3& operator*=(float3 v){r*=v.r;g*=v.g;b*=v.b;return *this;}};
float max(float a,double b){return std::max(a,float(b));}
float3 operator+(float3 a,float3 b){return {a.r+b.r,a.g+b.g,a.b+b.b};}
float3 operator*(float3 a,float3 b){return {a.r*b.r,a.g*b.g,a.b*b.b};}
float3 operator*(float3 a,float b){return {a.r*b,a.g*b,a.b*b};}
float3 operator/(float3 a,float b){return {a.r/b,a.g/b,a.b/b};}
float saturate(float v){return std::clamp(v,0.f,1.f);}
float3 saturate(float3 v){return {saturate(v.r),saturate(v.g),saturate(v.b)};}
float3 floor(float3 v){return {std::floor(v.r),std::floor(v.g),std::floor(v.b)};}
float3 lerp(float3 a,float3 b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
'''

def reference(sky,block,e):
    """Independent scalar evaluation of 1.21.11 lightmap.fsh's render target."""
    sf,bf,ambient,gamma,night,dark,darken,*colors=e
    sky_color,ambient_color=colors[:3],colors[3:]
    b=block/(4-3*block)*bf;s=sky/(4-3*sky)*sf
    c=[b,b*((b*.6+.4)*.6+.4),b*(b*b*.6+.4)]
    c=[(v*(1-ambient)+a*ambient+t*s)*.96+.03 for v,a,t in zip(c,ambient_color,sky_color)]
    if ambient==0:c=[v*(1-darken*d) for v,d in zip(c,(.3,.4,.4))]
    maximum=max(c)
    if night>0 and maximum<1:c=[v*(1-night)+v/maximum*night for v in c]
    if ambient==0:c=[v-dark for v in c]
    c=[min(1,max(0,v)) for v in c];maximum=max(c)
    scale=(1-(1-maximum)**4)/maximum if maximum else 0
    c=[(v*(1-gamma)+v*scale*gamma)*.96+.03 for v in c]
    return [math.floor(min(1,max(0,v))*255+.5)/255 for v in c]

def main():
    cases=[]
    for e in ((1,1.5,0,.5,0,0,0,1,1,1,1,1,1),
              (.05,1.4,0,0,0,0,0,.7,.8,1,1,1,1),
              (.05,1.6,0,1,0,0,0,.7,.8,1,1,1,1),
              (.3,1.5,0,.5,1,0,0,1,1,1,1,1,1),
              (1,1.5,0,.3,0,.27,.6,1,1,1,1,1,1),
              (0,1.5,.1,.5,0,0,0,1,1,1,1,1,1),
              (.8,1.5,.25,.5,0,0,0,1,1,1,.99,1.12,1)):
        for sky in range(16):
            for block in range(16):cases.append((sky/15,block/15,e))
    shader=HELPER['LIGHTMAP_CODE'].replace('.rgb','')
    source=SHIM+'\nfloat3 lightmap(float3 Light,float SkyFactor,float BlockFactor,float Ambient,float Gamma,float NightVision,float Darkness,float DarkenWorld,float3 SkyColor,float3 AmbientColor){\n'+shader+'\n}\n'
    source+=r'''int main(){float sky,block,sf,bf,a,g,n,d,w,s0,s1,s2,a0,a1,a2;std::cout<<std::setprecision(9);while(std::cin>>sky>>block>>sf>>bf>>a>>g>>n>>d>>w>>s0>>s1>>s2>>a0>>a1>>a2){auto c=lightmap({sky,block,1},sf,bf,a,g,n,d,w,{s0,s1,s2},{a0,a1,a2});std::cout<<c.r<<" "<<c.g<<" "<<c.b<<"\n";}}'''
    with tempfile.TemporaryDirectory(prefix='bridge-render-math-') as tmp:
        path=pathlib.Path(tmp);cpp=path/'check.cpp';exe=path/'check';cpp.write_text(source)
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
        data='\n'.join(' '.join(map(str,(s,b,*e))) for s,b,e in cases)
        rows=subprocess.run([str(exe)],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(rows)==len(cases)
    for row,(s,b,e) in zip(rows,cases):
        actual=list(map(float,row.split()));expected=reference(s,b,e)
        assert all(abs(a-v)<.00001 for a,v in zip(actual,expected)),(s,b,e,actual,expected)
    # Byte-accurate overlay constant/order, and final display-space decode.
    assert '77/255' in (ROOT/'tools/bridge_lighting_materials.py').read_text()
    print(f'Production lightmap shader math: {len(cases)} cases / {len(cases)*3} RGB channels passed')

if __name__=='__main__':main()
