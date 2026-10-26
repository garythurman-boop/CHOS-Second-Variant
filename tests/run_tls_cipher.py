#!/usr/bin/env python3
"""Restrict only the local OpenSSL fixture's TLS1.3 ciphersuite, then run QEMU."""
import argparse,os,pathlib,subprocess,sys,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--suite',choices=['TLS_AES_128_GCM_SHA256','TLS_AES_256_GCM_SHA384'],required=True)
p.add_argument('guest_args',nargs=argparse.REMAINDER)
a=p.parse_args();args=a.guest_args
if args and args[0]=='--':args=args[1:]
with tempfile.TemporaryDirectory(prefix='chos-tls13-cipher-') as d:
 config=pathlib.Path(d)/'openssl.cnf'
 config.write_text('openssl_conf = openssl_init\n[openssl_init]\nssl_conf = ssl_sect\n[ssl_sect]\nsystem_default = chos_tls_test\n[chos_tls_test]\nCiphersuites = '+a.suite+'\n')
 env=os.environ.copy();env['OPENSSL_CONF']=str(config)
 subprocess.run([sys.executable,str(pathlib.Path(__file__).with_name('qemu_tls.py')),'--tls-version','1.3','--expect-cipher',a.suite]+args,check=True,env=env)
