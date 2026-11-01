#!/usr/bin/env python3
"""Generate a checked native scanf copy; preserve original musl source bytes."""
import argparse,pathlib,json,hashlib

def native_source(data):
 s=data.decode()
 replacements=[
 ('\tchar *s;\n\twchar_t *wcs;','\tchar *s = NULL;\n\twchar_t *wcs = NULL;\n\tint wide_limit = 0;'),
 ("} else if (isdigit(*p) && p[1]=='$') {","} else if (isdigit(*p) && p[1]=='$') {\n\t\t\tif (*p=='0') { errno = EINVAL; goto fmt_fail; }"),
 ("\t\t\twidth = 10*width + *p - '0';","\t\t\tif (width > (INT_MAX-(*p-'0'))/10) {\n\t\t\t\terrno = EOVERFLOW; goto fmt_fail;\n\t\t\t}\n\t\t\twidth = 10*width + *p - '0';"),
 ("\t\t\t\t\tif (!*p) goto fmt_fail;","\t\t\t\t\tif (!*p) goto fmt_fail;\n\t\t\t\t\tif (size == SIZE_l && (*p >= 128 || (*p == '-' && p[1] >= 128))) { errno = ENOTSUP; goto fmt_fail; }"),
 ('\t\t\ti = 0;\n\t\t\tk =', '\t\t\ti = 0;\n\t\t\twide_limit = 0;\n\t\t\tk ='),
 ('\t\t\tif (size == SIZE_l) {\n\t\t\t\tif (alloc)', '\t\t\tif (size == SIZE_l) {\n\t\t\t\tshlim(f, 0); /* wide field widths count characters, not UTF8 bytes */\n\t\t\t\tif (alloc)'),
 ('\t\t\t\twhile (scanset[(c=shgetc(f))+1]) {\n\t\t\t\t\tswitch (mbrtowc', '\t\t\t\twhile ((!width || i < width) && scanset[(c=shgetc(f))+1]) {\n\t\t\t\t\tswitch (mbrtowc'),
 ('\t\t\t\t\tif (wcs) wcs[i++] = wc;', '\t\t\t\t\tif (wcs) wcs[i] = wc;\n\t\t\t\t\ti++;'),
 ('\t\t\t\tif (!mbsinit(&st)) goto input_fail;', '\t\t\t\twide_limit = width && i == width;\n\t\t\t\tif (!mbsinit(&st)) { errno = EILSEQ; goto input_fail; }'),
 ("\t\t\tshunget(f);\n\t\t\tif (!shcnt(f)) goto match_fail;\n\t\t\tif (t == 'c' && shcnt(f) != width) goto match_fail;", "\t\t\tif (!wide_limit) shunget(f);\n\t\t\tif (!shcnt(f)) goto match_fail;\n\t\t\tif (t == 'c' && (size == SIZE_l ? i : shcnt(f)) != width) goto match_fail;")]
 for old,new in replacements:
  assert s.count(old)==1,old;s=s.replace(old,new)
 return s.encode()

def main():
 r=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);o=p.parse_args()
 m=json.loads((r/'docs/dependencies/musl-memoryscan.json').read_text());data=(r/'third_party/musl/src/stdio/vfscanf.c').read_bytes();assert hashlib.sha256(data).hexdigest()==m['files']['src/stdio/vfscanf.c'];post=native_source(data)
 assert hashlib.sha256(post).hexdigest()==m['generated_vfscanf_sha256'],'native scanf transformation changed'
 o.out.parent.mkdir(parents=True,exist_ok=True);o.out.write_bytes(post)
 print('Checked native scanf: cleanup state,width/selector bounds and UTF8 wide-character field accounting')
if __name__=='__main__':main()
