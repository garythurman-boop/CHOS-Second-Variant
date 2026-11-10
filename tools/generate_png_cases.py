#!/usr/bin/env python3
"""Independent PyPNG golden hashes for complete original PNGSuite and bounds cases."""
import pathlib,sys,json,hashlib,argparse,struct,zlib
sys.dont_write_bytecode=True
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
m=json.loads((root/'docs/dependencies/native-png.json').read_text());base=root/m['host_reference']['root']
for name,want in m['host_reference']['files'].items():assert hashlib.sha256((base/name).read_bytes()).hexdigest()==want
sys.path.insert(0,str(base));import png
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
files=sorted((root/'third_party/libpng/contrib/pngsuite').glob('*.png'));assert len(files)==51
files.append(root/'third_party/libpng/pngtest.png')
def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data))
# More than one MiB decoded: rows remain separate bounded allocations on CHOS.
w,h=1024,768
rgba=bytes(c for y in range(h) for x in range(w) for c in [x&255,y&255,(x+y)&255,(x^y)&255])
scan=b''.join(b'\0'+rgba[y*w*4:(y+1)*w*4] for y in range(h))
large=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scan,9))+chunk(b'IEND',b'')
path=out/'large.png';path.write_bytes(large);files.append(path)
records=[];declarations=[];initializers=[];assembly=['.section .rodata.png_fixtures,"a"']
for i,path in enumerate(files):
 data=path.read_bytes();reader=png.Reader(bytes=data);reader.preamble()
 # The target preserves encoded samples, without undoing the encoder's sBIT
 # scaling. PyPNG's convenience API otherwise rescales significant bits a
 # second time. Select the same explicit sample-space policy on the oracle;
 # original files, input CRCs and the independent decoder remain unchanged.
 significant_bits=list(reader.sbit) if reader.sbit else None;reader.sbit=None
 width,height,rows,info=reader.asRGBA8();pixels=b''.join(bytes(row) for row in rows)
 assert info['bitdepth']==8 and info['planes']==4 and len(pixels)==width*height*4
 value=14695981039346656037
 for byte in pixels:value=((value^byte)*1099511628211)&((1<<64)-1)
 name='png_fixture_'+str(i);declarations.append('extern const unsigned char '+name+'[];')
 assembly+=['.balign 8','.global '+name,name+':','.incbin '+json.dumps(str(path))]
 initializers.append('{'+','.join([json.dumps(path.name),name,str(len(data)),str(width),str(height),'UINT64_C('+str(value)+')'])+'}')
 records.append({'name':path.name,'encoded_sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'width':width,'height':height,'RGBA8_sha256':hashlib.sha256(pixels).hexdigest(),'RGBA8_FNV64':hex(value),'bitdepth':data[24],'color_type':data[25],'interlace':data[28],'sBIT':significant_bits})
(out/'png-fixtures.S').write_text('\n'.join(assembly)+'\n')
(out/'png-cases.h').write_text('\n'.join(declarations)+'\nstruct PNGCase {const char *name;const unsigned char *bytes;size_t length;unsigned width,height;uint64_t hash;};\nstatic const struct PNGCase png_cases[]={\n'+',\n'.join(initializers)+'\n};\n')
(out/'cases.json').write_text(json.dumps({'host_reference':'PyPNG0.20220715.0,independent from target libpng','normalization':'Straight encoded sample-space RGBA8,16-bit samples scaled to8;no sBIT rescaling,ICC/gamma transform or bKGD composition','original_pngsuite_cases':51,'cases':records},indent=2)+'\n')
print('Generated',len(records),'independent golden pixel cases')
