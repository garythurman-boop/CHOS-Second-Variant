#!/usr/bin/env python3
"""Configure the upstream libc++ template for the explicit CHOS static subset."""
from pathlib import Path
import argparse,re,shutil
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);o=p.parse_args()
values={'_LIBCPP_ABI_VERSION': '1', '_LIBCPP_ABI_NAMESPACE': '__1', '_LIBCPP_HARDENING_MODE_DEFAULT': '_LIBCPP_HARDENING_MODE_FAST'}
flags=['_LIBCPP_HAS_NO_FILESYSTEM', '_LIBCPP_HAS_NO_LOCALIZATION', '_LIBCPP_HAS_NO_RANDOM_DEVICE', '_LIBCPP_HAS_NO_STD_MODULES', '_LIBCPP_HAS_NO_TIME_ZONE_DATABASE', '_LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS', '_LIBCPP_HAS_THREAD_API_EXTERNAL', '_LIBCPP_PSTL_BACKEND_SERIAL']
text=Path('third_party/libcxx/include/__config_site.in').read_text()
def define(m):
 name=m[1]
 if name in values:return '#define '+name+' '+values[name]
 return '#define '+name if name in flags else '/* #undef '+name+' */'
text=re.sub(r'^#cmakedefine (\w+).*$',define,text,flags=re.M)
text=text.replace('@_LIBCPP_ABI_DEFINES@','').replace('@_LIBCPP_EXTRA_SITE_DEFINES@','/* Native pthread_cond_clockwait supports both checked CHOS clocks. */\n#define _LIBCPP_HAS_COND_CLOCKWAIT')
assert '@' not in text and '#cmake' not in text
o.out.mkdir(parents=True,exist_ok=True);(o.out/'__config_site').write_text(text)
shutil.copyfile('third_party/libcxx/vendor/llvm/default_assertion_handler.in',o.out/'__assertion_handler')
