#!/usr/bin/env python3
"""Create memory WebP fixtures with the verified original host encoder."""
import argparse,hashlib,json,pathlib,struct,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--encoder',type=pathlib.Path,required=True)
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=True)
encoded=subprocess.check_output([str(o.encoder.resolve()),str(out)],text=True).strip()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def u24(value):return value.to_bytes(3,'little')
def chunk(name,data):return name+struct.pack('<I',len(data))+data+b'\0'*(len(data)%2)
def payload(data):
    assert data[:4]==b'RIFF' and data[8:12]==b'WEBP'
    items=[];position=12
    while position<len(data):
        name=data[position:position+4];size=struct.unpack_from('<I',data,position+4)[0]
        end=position+8+size;assert end<=len(data)
        if name in [b'ALPH',b'VP8 ',b'VP8L']:items.append(data[position:end+(size%2)])
        position=end+(size%2)
    assert position==len(data) and items
    return b''.join(items)
frames=[(out/'lossless.webp').read_bytes(),(out/'frame2.webp').read_bytes()]
body=chunk(b'VP8X',bytes([0x12,0,0,0])+u24(18)+u24(12))+chunk(b'ANIM',struct.pack('<IH',0,3))
for image,duration in zip(frames,[80,120]):
    header=u24(0)+u24(0)+u24(18)+u24(12)+u24(duration)+bytes([2])
    body+=chunk(b'ANMF',header+payload(image))
animated=b'RIFF'+struct.pack('<I',len(body)+4)+b'WEBP'+body
(out/'animation.webp').write_bytes(animated)
vectors=[]
header=['/* Generated from verified original WebP encoder; see cases.json. */','#ifndef CHOS_WEBP_CASES_H','#define CHOS_WEBP_CASES_H']
def array(name,data):
    header.append('static const unsigned char '+name+'[] = {')
    header.extend('  '+','.join(str(v) for v in data[i:i+24])+',' for i in range(0,len(data),24))
    header.append('};')
for name,kind in [('lossless','independent integer RGBA formula'),('lossy','original host decoder RGBA'),('frame2','independent integer RGBA formula')]:
    image=(out/(name+'.webp')).read_bytes();pixels=(out/(name+'.rgba')).read_bytes()
    assert len(pixels)==19*13*4
    array(name+'_webp',image);array(name+'_rgba',pixels)
    vectors.append({'name':name,'width':19,'height':13,'expected_pixels':kind,'webp_sha256':hashlib.sha256(image).hexdigest(),'rgba_sha256':hashlib.sha256(pixels).hexdigest()})
array('animation_webp',animated)
header.extend(['struct WebPCase { const char *name; const unsigned char *data; size_t length; const unsigned char *rgba; unsigned width,height; };','static const struct WebPCase webp_cases[] = {'])
for name in ['lossless','lossy','frame2']:
    header.append(' {"'+name+'",'+name+'_webp,sizeof('+name+'_webp),'+name+'_rgba,19,13},')
header.extend(['};','#endif'])
(out/'webp-cases.h').write_text('\n'.join(header)+'\n')
report={'encoder':encoded,'encoder_executable_sha256':sha(o.encoder.resolve()),'vectors':vectors,'animation':{'frames':2,'timestamps_ms':[80,200],'loop_count':3,'sha256':hashlib.sha256(animated).hexdigest()},'generated_files_sha256':{q.name:sha(q) for q in sorted(out.iterdir()) if q.is_file() and q.name!='cases.json'}}
(out/'cases.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'vectors':len(vectors),'animation_frames':2,'encoder':encoded}))
