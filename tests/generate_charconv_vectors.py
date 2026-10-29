#!/usr/bin/env python3
"""Independent exact Decimal formatting oracle for native float/double to_chars."""
import argparse,decimal,json,pathlib,random
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');o=p.parse_args()
decimal.getcontext().prec=2000;decimal.getcontext().rounding=decimal.ROUND_HALF_EVEN
D=decimal.Decimal

def exact(bits,width):
 n=23 if width==32 else 52;eb=8 if width==32 else 11;bias=(1<<(eb-1))-1
 sign=bits>>(n+eb);exp=(bits>>n)&((1<<eb)-1);frac=bits&((1<<n)-1)
 value=D(frac+(1<<n if exp else 0))*D(2)**((exp-bias if exp else 1-bias)-n)
 return -value if sign and value else D('-0') if sign else value

def exponent(s):
 mantissa,power=s.split('e');value=int(power)
 return mantissa+'e'+('+' if value>=0 else '-')+str(abs(value)).zfill(2)

def general(value,p):
 if not value:return '-0' if value.is_signed() else '0'
 scientific=format(value,f'.{p-1}e');mant,power=scientific.split('e');exp=int(power)
 if exp < -4 or exp>=p:return exponent(mant.rstrip('0').rstrip('.')+'e'+power)
 result=format(value,f'.{max(0,p-1-exp)}f')
 return result.rstrip('0').rstrip('.') if '.' in result else result

rng=random.Random(0x43484152434f4e56);rows=[]
for width in [32,64]:
 n=23 if width==32 else 52;eb=8 if width==32 else 11;bias=(1<<(eb-1))-1;maxexp=(1<<eb)-2
 values=[0,1,2,(1<<n)-1,1<<n,bias<<n,(bias-1)<<n,(maxexp<<n)|((1<<n)-1),(bias<<n)|(1<<(n-2))]
 values += [rng.randrange(1,maxexp+1)<<n|rng.getrandbits(n) for _ in range(12)]
 values += [v|(1<<(width-1)) for v in values[:9]]
 for bits in values:
  value=exact(bits,width)
  for kind,precision in [('f',0),('f',3),('f',20),('e',0),('e',6),('e',40),('g',1),('g',6),('g',36)]:
   expected=general(value,precision) if kind=='g' else exponent(format(value,f'.{precision}e')) if kind=='e' else format(value,f'.{precision}f')
   # Decimal's zero exponent depends on its source scale; C++ scientific zero is e+00.
   if kind=='e' and not value:expected=('-' if value.is_signed() else '')+'0'+('.'+'0'*precision if precision else '')+'e+00'
   rows.append((bits,width,kind,precision,expected))
s='/* Independent exact Decimal oracle,precision2000,round-half-even. */\n'
s+='struct charconv_vector {uint64_t bits;unsigned width;char kind;int precision;const char *expected;};\nstatic const charconv_vector charconv_vectors[]={\n'
s+=''.join('{UINT64_C(0x%x),%u,\'%s\',%u,%s},\n'%(b,w,k,p,json.dumps(v)) for b,w,k,p,v in rows)+'};\n'
target=pathlib.Path(__file__).with_name('charconv_vectors.h')
if o.check:assert target.read_text()==s,'charconv oracle corpus changed'
else:target.write_text(s)
print(len(rows),'independent binary32/binary64 Decimal to_chars vectors')
