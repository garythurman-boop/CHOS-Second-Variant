#!/usr/bin/env python3
"""Exact integer-square-root and signed-multiply vectors, independent of target code."""
from fractions import Fraction
from pathlib import Path
import argparse,random,math,struct
from generate_quad_vectors import unpack,pack,pair

def rootpack(r,precision=113,expbits=15):
 if not r:return 0
 n,d=r.numerator,r.denominator;power=n.bit_length()-d.bit_length()
 if (n<d<<power) if power>=0 else (n<<-power<d):power-=1
 exponent=power//2;bias=(1<<(expbits-1))-1;frac=precision-1;shift=frac-max(exponent,1-bias)
 if shift>=0:n<<=2*shift
 else:d<<=-2*shift
 q=math.isqrt(n//d);comparison=4*n-d*(2*q+1)**2
 if comparison>0 or comparison==0 and q&1:q+=1
 if q==1<<precision:q>>=1;exponent+=1
 if exponent>bias:return ((1<<expbits)-1)<<frac
 field=0 if q<1<<frac else max(exponent,1-bias)+bias
 return field<<frac | q&((1<<frac)-1)

def generate():
 rng=random.Random(0x43484f53564d4d)
 # Independent binary64 hardware/libm sqrt check validates exponent,subnormal
 # and tie rounding logic before generating any binary128 expected results.
 for _ in range(10000):
  bits=rng.randrange(0x7ff0000000000000);x=struct.unpack('>d',bits.to_bytes(8,'big'))[0]
  assert rootpack(unpack(bits,53,11),53,11)==int.from_bytes(struct.pack('>d',math.sqrt(x)),'big')
 edge=[0,1,2,(1<<112)-1,1<<112,(1<<112)+1,16383<<112,(16383<<112)+1,(32766<<112)|((1<<112)-1)]
 roots=edge+[rng.randint(1,32766)<<112|rng.getrandbits(112) for _ in range(128)]
 text='/* Generated exact integer-isqrt/Python-int oracle; tools/generate_vm_math_vectors.py. */\n#ifndef CHOS_VM_MATH_VECTORS_H\n#define CHOS_VM_MATH_VECTORS_H\n#include "quad_vectors.h"\ntypedef struct {quad_bits_t x,result;} vm_sqrt_vector_t;\nstatic const vm_sqrt_vector_t vm_sqrt_vectors[]={\n'
 for x in roots:text+=' {'+pair(x)+','+pair(rootpack(unpack(x)))+'},\n'
 text+='};\ntypedef struct {quad_bits_t x,y,result;unsigned exact;} vm_hypot_vector_t;\nstatic const vm_hypot_vector_t vm_hypot_vectors[]={\n'
 pairs=[(a,b,False) for a in edge for b in edge]
 for scale in [-16494,-16382,-10000,-8001,-8000,-20,0,20,8000,8001,10000,16380]:
  a=pack(Fraction(3)*Fraction(2)**scale);b=pack(Fraction(4)*Fraction(2)**scale);pairs.extend([(a,b,True),(a|(1<<127),b,True),(b,a,True)])
 for _ in range(128):
  exponent=rng.randint(150,32616);a=exponent<<112|rng.getrandbits(112);b=(exponent-rng.randrange(150))<<112|rng.getrandbits(112);pairs.append((a,b,False))
 for a,b,exact in pairs:
  x,y=unpack(a),unpack(b);text+=' {'+pair(a)+','+pair(b)+','+pair(rootpack(x*x+y*y))+','+str(int(exact))+'},\n'
 text+='};\ntypedef struct {quad_bits_t a,b,result;unsigned overflow;} vm_multiply_vector_t;\nstatic const vm_multiply_vector_t vm_multiply_vectors[]={\n'
 boundary=[-(1<<127),-(1<<127)+1,-(1<<126),-3,-2,-1,0,1,2,3,(1<<126),(1<<127)-1];products=[(a,b) for a in boundary for b in boundary]
 for _ in range(512):
  a=rng.randrange(-(1<<127),1<<127);b=rng.randrange(-(1<<127),1<<127) if rng.getrandbits(1) else rng.randint(-8,8);products.append((a,b))
 for a,b in products:
  product=a*b;overflow=int(product<-(1<<127) or product>=(1<<127));text+=' {'+pair(a&((1<<128)-1))+','+pair(b&((1<<128)-1))+','+pair(product&((1<<128)-1))+','+str(overflow)+'},\n'
 return text+'};\n#endif\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=Path('tests/vm_math_vectors.h'));p.add_argument('--check',action='store_true');o=p.parse_args();text=generate()
 if o.check:assert o.out.read_text()==text
 else:o.out.write_text(text)
 print('Exact isqrt and signed multiply vectors verified' if o.check else 'Generated exact isqrt/multiply vectors')
