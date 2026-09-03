import sys
sys.path.insert(0,'tools')
from PIL import Image
im=Image.open('assets/rips/items-on-the-road.png').convert('RGB'); px=im.load(); bg=px[0,0]
def mask(x,y,w,h):
    m=[[1 if px[x+i,y+j]!=bg else 0 for i in range(w)] for j in range(h)]
    return [[1 if (m[j][i] and all(0<=j+dy<h and 0<=i+dx<w and m[j+dy][i+dx] for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))) else 0 for i in range(w)] for j in range(h)]
base=[("green shell f1",mask(7,6,16,16)),("banana 16",mask(7,28,16,15)),("blue dome",mask(7,71,16,16)),("mushroom",mask(6,122,16,16)),("fireball",mask(227,124,16,16))]
targets=base+[(n+" mirrored",[row[::-1] for row in m]) for n,m in base]
d=open('rom/smk_usa.sfc','rb').read()
if len(d)%1024==512: d=d[512:]
nt=len(d)//32
def tm(t):
    b=t*32; out=[]
    for y in range(8):
        lo0,lo1,hi0,hi1=d[b+y*2],d[b+y*2+1],d[b+16+y*2],d[b+16+y*2+1]
        out.append([1 if ((lo0|lo1|hi0|hi1)>>(7-x))&1 else 0 for x in range(8)])
    return out
TM=[tm(t) for t in range(nt)]
FILL=[sum(map(sum,m)) for m in TM]
for rowlen in (2,16):
    for name,m in targets:
        h=len(m); w=len(m[0]); tot=sum(map(sum,m)); res=[]
        for t in range(nt-rowlen-1):
            f=FILL[t]+FILL[t+1]+FILL[t+rowlen]+FILL[t+rowlen+1]
            if abs(f-tot)>8: continue
            a,b,c,dd=TM[t],TM[t+1],TM[t+rowlen],TM[t+rowlen+1]
            bm=[a[y]+b[y] for y in range(8)]+[c[y]+dd[y] for y in range(8)]
            for oy in range(0,17-h):
                for ox in range(0,17-w):
                    bad=0
                    for y in range(16):
                        row=bm[y]
                        for x in range(16):
                            s=m[y-oy][x-ox] if 0<=y-oy<h and 0<=x-ox<w else 0
                            if s!=row[x]: bad+=1
                        if bad>6: break
                    if bad<=6: res.append((bad,t))
        res.sort()
        if res: print(f"rowlen {rowlen} {name}: "+' '.join(f"PC {t*32:06X} bad={b}" for b,t in res[:6]), flush=True)
print("done", flush=True)
