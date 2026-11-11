import io
import struct as S
from pathlib import Path

from PIL import Image


def require(ok, message):
    if not ok:
        raise ValueError(message)


def texture(source,output,blend):
    with Image.open(source) as original:image=original.convert('RGBA')
    w,h=image.size
    require(w>0 and h>0 and not w&(w-1) and not h&(h-1),'Texture dimensions must be powers of two')
    require(max(w,h)<=4096,'Texture exceeds 4096 pixels')
    if blend=='opaque':image.putalpha(255)
    offsets=[0]*16;sizes=[0]*16;payload=[];offset=1172;i=0
    while True:
        padded=image
        if min(image.size)<4:
            padded=Image.new('RGBA',(max(4,image.width),max(4,image.height)))
            for y in range(padded.height):
                for x in range(padded.width):padded.putpixel((x,y),image.getpixel((min(x,image.width-1),min(y,image.height-1))))
        stream=io.BytesIO();padded.save(stream,format='DDS',pixel_format='DXT1' if blend=='opaque' else 'DXT5')
        raw=stream.getvalue()[128:];offsets[i]=offset;sizes[i]=len(raw);payload.append(raw);offset+=len(raw);i+=1
        if image.size==(1,1):break
        image=image.resize((max(1,image.width//2),max(1,image.height//2)),Image.Resampling.LANCZOS)
    header=S.pack('<4sI4BII16I16I',b'BLP2',1,2,0 if blend=='opaque' else 8,0 if blend=='opaque' else 7,1,w,h,*offsets,*sizes)
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);require(not output.exists(),'Texture output already exists')
    output.write_bytes(header+bytes(1024)+b''.join(payload))
    with Image.open(output) as decoded:
        decoded.load();require(decoded.size==(w,h),'BLP decode failed')
    return dict(width=w,height=h,mips=i,encoding='BC1' if blend=='opaque' else 'BC3',alpha_depth=0 if blend=='opaque' else 8)
