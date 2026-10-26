#include "chos/user.h"
#include "chos_tls.h"
#include <stdlib.h>
#include <string.h>
static void say(const char *s){chos_write(s,strlen(s));}
static void number(unsigned n){char s[11];s[10]='\n';for(int i=9;i>=0;i--){s[i]=(char)('0'+n%10);n/=10;}chos_write(s,sizeof(s));}
static chos_tls_client_t *create(unsigned port,const char *host,const void *ca,size_t bytes,int *why,int retry){
 for(unsigned n=0;;n++){
  chos_tls_client_t *c=chos_tls_create_ex(0x0a000202,(uint16_t)port,host,ca,bytes,why);
  if(c||!retry||*why!=CHOS_TLS_INIT_ENTROPY||n==99)return c;
  /* Nonblocking provider may have a real refill in flight. Never replace it. */
  chos_sleep_ticks(1);
 }
}
static int fail(unsigned line){say("[FAIL] native TLS line ");number(line);return 1;}
#define CHECK(x) do {if(!(x))return fail(__LINE__);}while(0)
int main(int argc,char **argv){
 CHECK(argc==5);unsigned port=0,expected=0;
 for(const char *p=argv[1];*p;p++){CHECK(*p>='0'&&*p<='9');port=port*10+(unsigned)(*p-'0');CHECK(port<=65535);}
 for(const char *p=argv[3];*p;p++){CHECK(*p>='0'&&*p<='9');expected=expected*10+(unsigned)(*p-'0');}
 int fd=(int)chos_open(argv[4]);CHECK(fd>=0);
 unsigned char ca[8192];size_t used=0;
 for(;;){size_t chunk=sizeof(ca)-used;if(chunk>CHOS_SYS_MAX_READ)chunk=CHOS_SYS_MAX_READ;long n=chos_read(fd,ca+used,chunk);CHECK(n>=0);if(!n)break;used+=(size_t)n;CHECK(used<sizeof(ca));}
 CHECK(!chos_close(fd));
 if(expected==995){
  chos_tls_client_t *clients[CHOS_TCP_MAX_SOCKETS];
  for(unsigned i=0;i<CHOS_TCP_MAX_SOCKETS;i++){
   int why=0;clients[i]=create(port,argv[2],ca,used,&why,1);CHECK(clients[i]);
  }
  int rejected=0;
  chos_tls_client_t *extra=create(port,argv[2],ca,used,&rejected,1);
  CHECK(!extra&&rejected==CHOS_TLS_INIT_TRANSPORT);
  for(unsigned i=0;i<CHOS_TCP_MAX_SOCKETS;i++)chos_tls_destroy(clients[i]);
  say("[ OK ] native TLS socket exhaustion and cancellation cleanup passed\n");return 0;
 }
 int no_hostname=0;
 CHECK(!chos_tls_create_ex(0x0a000202,(uint16_t)port,0,ca,used,&no_hostname)&&no_hostname==CHOS_TLS_INIT_ARGUMENT);
 int init_error=0;
 chos_tls_client_t *c=create(port,argv[2],ca,used,&init_error,expected!=992);
 if(expected>=991&&expected<=993){CHECK(!c);CHECK(init_error==(expected==991?CHOS_TLS_INIT_CLOCK:expected==992?CHOS_TLS_INIT_ENTROPY:CHOS_TLS_INIT_CA));say("[ OK ] native TLS unavailable prerequisite rejected before connection\n");return 0;}
 CHECK(c);
 int rc;
 for(unsigned n=0;;n++){CHECK(n<2500);rc=chos_tls_step(c);if(rc!=CHOS_TLS_AGAIN)break;chos_sleep_ticks(1);}
 if(expected&&expected!=997){
  say("TLS verify flags: ");number(chos_tls_verify_flags(c));
  CHECK(rc==CHOS_TLS_ERROR&&chos_tls_state(c)==CHOS_TLS_FAILED);
  if(expected==998)CHECK(chos_tls_error(c)!=0);
  else if(expected==996)CHECK(chos_tls_error(c)==-0x6800);
  else CHECK((chos_tls_verify_flags(c)&expected)==expected);
  chos_tls_destroy(c);say("[ OK ] native TLS rejected invalid peer\n");return 0;
 }
 if(rc){say("TLS error magnitude: ");number((unsigned)-chos_tls_error(c));say("TLS flags: ");number(chos_tls_verify_flags(c));}
 CHECK(rc==0&&chos_tls_state(c)==CHOS_TLS_READY&&chos_tls_verify_flags(c)==0);
 static const char request[]="GET /fixture HTTP/1.1\r\nHost: fixture.chos.test\r\nConnection: close\r\n\r\n";
 size_t sent=0;
 for(unsigned n=0;sent<sizeof(request)-1;n++){
  CHECK(n<2500);rc=chos_tls_write(c,request+sent,sizeof(request)-1-sent);
  if(rc==CHOS_TLS_AGAIN){chos_sleep_ticks(1);continue;}CHECK(rc>0);sent+=(unsigned)rc;
 }
 char body[4096];used=0;
 for(unsigned n=0;;n++){
  CHECK(n<2500&&used<sizeof(body)-1);rc=chos_tls_read(c,body+used,sizeof(body)-1-used);
  if(rc==CHOS_TLS_AGAIN){chos_sleep_ticks(1);continue;}
  if(rc==0)break;
  if(rc<0){say("TLS read error magnitude: ");number((unsigned)-chos_tls_error(c));}
  if(expected==997&&rc<0){CHECK(chos_tls_state(c)==CHOS_TLS_FAILED);chos_tls_destroy(c);say("[ OK ] native TLS unauthenticated truncation rejected\n");return 0;}
  CHECK(rc>0);used+=(size_t)rc;
 }
 body[used]=0;CHECK(strstr(body,"chos-native-tls-fixture-ok"));
 CHECK(chos_tls_state(c)==CHOS_TLS_CLOSED);chos_tls_destroy(c);
 say("[ OK ] native TLS verified HTTPS body and authenticated close\n");return 0;
}
