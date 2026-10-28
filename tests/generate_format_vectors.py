#!/usr/bin/env python3
"""Exact binary64/binary128 values formatted with independent Python Decimal."""
import argparse,pathlib,decimal,json,random,re
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');o=p.parse_args()
decimal.getcontext().prec=20000;decimal.getcontext().rounding=decimal.ROUND_HALF_EVEN
D=decimal.Decimal

def exact(bits,width):
 n=52 if width==64 else 112;eb=11 if width==64 else 15;bias=(1<<(eb-1))-1
 sign=bits>>(n+eb);exp=(bits>>n)&((1<<eb)-1);frac=bits&((1<<n)-1)
 value=D(frac+(1<<n if exp else 0))*D(2)**((exp-bias if exp else 1-bias)-n)
 return -value if sign and value else D('-0') if sign else value

def exponent(text):
 if 'e' not in text:return text
 mantissa,power=text.split('e');return mantissa+'e'+('+' if int(power)>=0 else '-')+str(abs(int(power))).zfill(2)

def general(value,p):
 scientific=format(value,f'.{p-1}e');mant,power=scientific.split('e');exp=int(power)
 if exp < -4 or exp >= p:return exponent(mant.rstrip('0').rstrip('.')+'e'+power)
 result=format(value,f'.{max(0,p-1-exp)}f')
 return result.rstrip('0').rstrip('.') if '.' in result else result

rows=[];rng=random.Random(0x464f524d4154)
for width in [64,128]:
 n=52 if width==64 else 112;bias=1023 if width==64 else 16383;maxexp=2046 if width==64 else 32766
 values=[0,1,2,(1<<n)-1,1<<n,bias<<n,(bias-1)<<n,(maxexp<<n)|((1<<n)-1),(bias<<n)|(1<<(n-2))]
 values += [((rng.randrange(1,maxexp+1)<<n)|rng.getrandbits(n)) for _ in range(12)]
 values += [v|(1<<(width-1)) for v in values[:9]]
 for bits in values:
  d=exact(bits,width)
  for kind,p in [('e',40),('g',36)]:
   result=exponent(format(d,f'.{p}e')) if kind=='e' else general(d,p)
   if not d:result=('-' if d.is_signed() else '')+('0.'+'0'*p+'e+00' if kind=='e' else '0')
   fmt='%.'+str(p)+('L' if width==128 else '')+kind
   rows.append((bits&((1<<64)-1),bits>>64,width,fmt,result))
s='/* Independent exact Decimal oracle,precision20000,round-half-even. */\n'
s+='struct format_vector {uint64_t lo,hi;unsigned width;const char *format,*expected;};\nstatic const format_vector format_vectors[]={\n'
s+=''.join('{UINT64_C(0x%x),UINT64_C(0x%x),%u,%s,%s},\n'%(lo,hi,w,json.dumps(f),json.dumps(v)) for lo,hi,w,f,v in rows)+'};\n'
target=pathlib.Path(__file__).with_name('format_vectors.h')
if o.check:assert target.read_text()==s,'formatting reference corpus changed'
else:target.write_text(s)
print(len(rows),'independent exact binary64/binary128 Decimal formatting vectors')
