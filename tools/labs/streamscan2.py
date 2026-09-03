import sys
sys.path.insert(0,'tools')
from smktool.rom import Rom
from smktool.compress import decompress
from smktool import assets
from PIL import Image
im=Image.open('assets/rips/items-on-the-road.png').convert('RGB'); px=im.load(); bg=px[0,0]
def mask(x,y,w,h):
    m=[[1 if px[x+i,y+j]!=bg else 0 for i in range(w)] for j in range(h)]
    e=[[1 if (m[j][i] and all(0<=j+dy<h and 0<=i+dx<w and m[j+dy][i+dx] for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))) else 0 for i in range(w)] for j in range(h)]
    return e
targets=[("green shell f1",mask(7,6,16,16)),("green shell f2",mask(26,6,16,16)),("banana 16",mask(7,28,16,15)),("blue dome",mask(7,71,16,16)),("mushroom",mask(6,122,16,16)),("egg",mask(120,123,14,16)),("fireball",mask(227,124,16,16)),("coin",mask(9,49,10,16))]
r=Rom.load('rom/smk_usa.sfc'); d=bytes(r.data)
def tm_from(buf,t):
    base=t*32; out=[]
    for y in range(8):
        lo0,lo1,hi0,hi1=buf[base+y*2],buf[base+y*2+1],buf[base+16+y*2],buf[base+16+y*2+1]
        out.append([1 if ((lo0|lo1|hi0|hi1)>>(7-x))&1 else 0 for x in range(8)])
    return out
def scan(buf,rowlen,tol):
    nt=len(buf)//32
    if nt<rowlen+2: return []
    TM=[tm_from(buf,t) for t in range(nt)]; hits=[]
    for name,m in targets:
        h=len(m); w=len(m[0])
        for t in range(nt-rowlen-1):
            a,b,c,dd=TM[t],TM[t+1],TM[t+rowlen],TM[t+rowlen+1]
            bm=[a[y]+b[y] for y in range(8)]+[c[y]+dd[y] for y in range(8)]
            if sum(map(sum,bm))<30: continue
            best=None
            for oy in range(0,17-h):
                for ox in range(0,17-w):
                    bad=0
                    for y in range(16):
                        row=bm[y]
                        for x in range(16):
                            s=m[y-oy][x-ox] if 0<=y-oy<h and 0<=x-ox<w else 0
                            if s!=row[x]: bad+=1
                        if bad>tol: break
                    if bad<=tol and (best is None or bad<best): best=bad
            if best is not None: hits.append((name,t,best))
    return hits
print("== pointer tables", flush=True)
for name in assets.REGISTRY:
    tb=assets.table(r,name)
    for i,e in enumerate(tb.entries()):
        try: data=bytes(tb.read(i))
        except Exception: continue
        hits=scan(data,16,6)
        if hits: print(f"{name}[{i}] len {len(data)}: "+' '.join(f"{n} t${t:03X} bad={b}" for n,t,b in hits[:12]), flush=True)
print("== shared blob $C1:0000", flush=True)
blob=decompress(d,0x10000)[0]
print("coin check:", [h for h in scan(bytes(blob),16,6) if h[0]=='coin'][:3], flush=True)
print("== brute force", flush=True)
seen=set()
for off in range(0,len(d)-64,32):
    try: out=decompress(d,off,max_out=0x8000)[0]
    except Exception: continue
    if len(out)<1024: continue
    key=bytes(out[:48])
    if key in seen: continue
    seen.add(key)
    hits=scan(bytes(out),16,6)
    if hits: print(f"stream PC {off:06X} len {len(out)}: "+' '.join(f"{n} t${t:03X} bad={b}" for n,t,b in hits[:12]), flush=True)
print("done", flush=True)
