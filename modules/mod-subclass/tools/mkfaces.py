import struct,sys
def lznt1(src):
    out=bytearray(); i=0
    while i+2<=len(src):
        h=struct.unpack('<H',src[i:i+2])[0]; i+=2
        if h==0: break
        size=(h&0xfff)+1; chunk=src[i:i+size]; i+=size
        if not (h&0x8000): out+=chunk; continue
        start=len(out); j=0
        while j<len(chunk):
            flags=chunk[j]; j+=1
            for b in range(8):
                if j>=len(chunk): break
                if not (flags>>b)&1: out.append(chunk[j]); j+=1
                else:
                    tok=struct.unpack('<H',chunk[j:j+2])[0]; j+=2
                    p=len(out)-start-1; sh=12
                    while p>=0x10: p>>=1; sh-=1
                    disp=(tok>>sh)+1; ln=(tok&((1<<sh)-1))+3
                    for k in range(ln): out.append(out[-disp])
    return out
def read(path):
    d=open(path,'rb').read(); o=16; ents=[]
    while o+12<=len(d):
        w,h,sz=struct.unpack('<3I',d[o:o+12]); ents.append((w,h,lznt1(d[o+12:o+12+sz]))); o+=12+sz
    return d[:16],ents
import numpy as np
from scipy import ndimage
FACES=[0,3,6,9,16,17,18,19]
LOWERS=[2,5,8,11]

def background(px):
    rgb=px[:,:,:3].astype(int)
    h,w=rgb.shape[:2]
    corners=[rgb[0,0],rgb[0,w-1],rgb[h-1,0],rgb[h-1,w-1]]
    bg=max(corners,key=lambda c:int(c.sum()))
    near=np.abs(rgb-bg).max(axis=2)<=40
    labels,count=ndimage.label(near)
    border=set(labels[0,:])|set(labels[-1,:])|set(labels[:,0])|set(labels[:,-1])
    sizes=ndimage.sum(np.ones_like(labels),labels,range(1,count+1))
    border|={i+1 for i,size in enumerate(sizes) if size>=300}
    border.discard(0)
    return np.isin(labels,list(border))

def fill(px):
    mask=background(px)
    if mask.all() or not mask.any():
        return px,0
    idx=ndimage.distance_transform_edt(mask,return_distances=False,return_indices=True)
    out=px.copy()
    out[mask]=px[idx[0][mask],idx[1][mask]]
    soft=ndimage.uniform_filter(out[:,:,:3].astype(float),size=(9,9,1))
    out[:,:,:3][mask]=soft[mask].astype(np.uint8)
    return out,int(mask.sum())

def lznt1_store(raw):
    out=bytearray()
    for i in range(0,len(raw),4096):
        chunk=raw[i:i+4096]
        out+=struct.pack('<H',(len(chunk)-1)|0x3000)+chunk
    return bytes(out)

def write(path,head,ents):
    out=bytearray(head)
    for w,h,raw in ents:
        data=lznt1_store(raw)
        out+=struct.pack('<3I',w,h,len(data))+data
    open(path,'wb').write(out)

if __name__=='__main__':
    head,ents=read(sys.argv[1])
    fixed=[]
    for n,(w,h,raw) in enumerate(ents):
        if n in FACES or n in LOWERS:
            px=np.frombuffer(bytes(raw),np.uint8).reshape(h,w,4)
            new,count=fill(px)
            print(n,w,h,'filled',count)
            raw=new.tobytes()
        fixed.append((w,h,bytes(raw)))
    write(sys.argv[2],head,fixed)
