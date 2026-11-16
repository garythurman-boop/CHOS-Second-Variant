#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xmlIO.h>
#include <libxml/xmlsave.h>
#include <libxml/xmlerror.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#define CHECK(x) do{if(!(x)){printf("[FAIL] native XML line %u\n",__LINE__);fflush(stdout);return 1;}}while(0)
static const char document[]="<?xml version='1.0' encoding='UTF-8'?><root xmlns='urn:chos'><item id='7'>caf\xc3\xa9 &amp; \xe2\x82\xac</item><item id='8'>&#x3bb;</item></root>";
static void quiet(void *context,const xmlError *error){(void)context;(void)error;}
static xmlParserErrors deny(void *context,const char *url,const char *id,xmlResourceType type,xmlParserInputFlags flags,xmlParserInput **out){
    unsigned *calls=context;(void)url;(void)id;(void)type;(void)flags;++*calls;*out=NULL;return XML_IO_EACCES;
}
static xmlParserCtxt *context(unsigned *calls){
    xmlParserCtxt *c=xmlNewParserCtxt();if(!c)return NULL;
    xmlCtxtSetErrorHandler(c,quiet,NULL);xmlCtxtSetResourceLoader(c,deny,calls);xmlCtxtSetMaxAmplification(c,5);return c;
}
static int inspect(xmlDoc *doc){
    CHECK(doc);xmlNode *root=xmlDocGetRootElement(doc);CHECK(root&&xmlStrEqual(root->name,BAD_CAST "root"));
    CHECK(root->ns&&xmlStrEqual(root->ns->href,BAD_CAST "urn:chos"));
    xmlNode *item=root->children;CHECK(item&&item->type==XML_ELEMENT_NODE);
    xmlChar *id=xmlGetProp(item,BAD_CAST "id");CHECK(id&&xmlStrEqual(id,BAD_CAST "7"));xmlFree(id);
    xmlChar *value=xmlNodeGetContent(item);CHECK(value&&xmlStrEqual(value,BAD_CAST "caf\xc3\xa9 & \xe2\x82\xac"));xmlFree(value);
    xmlXPathContext *xp=xmlXPathNewContext(doc);CHECK(xp);xmlXPathObject *result=xmlXPathEvalExpression(BAD_CAST "count(/*/*)",xp);
    CHECK(result&&result->type==XPATH_NUMBER&&result->floatval==2);xmlXPathFreeObject(result);xmlXPathFreeContext(xp);
    xmlChar *serialized=NULL;int size=0;xmlDocDumpMemoryEnc(doc,&serialized,&size,"UTF-8");CHECK(serialized&&size>0&&strstr((char*)serialized,"caf\xc3\xa9"));xmlFree(serialized);return 0;
}
static int ordinary(void){
    unsigned calls=0;xmlParserCtxt *c=context(&calls);CHECK(c);
    xmlDoc *doc=xmlCtxtReadMemory(c,document,sizeof(document)-1,"memory:test",NULL,XML_PARSE_NONET|XML_PARSE_NO_XXE);
    CHECK(!inspect(doc)&&!calls);xmlFreeDoc(doc);xmlFreeParserCtxt(c);return 0;
}
static int push(void){
    const unsigned fragments[]={1,7,32};
    for(unsigned f=0;f<3;++f){
        unsigned calls=0;xmlParserCtxt *c=xmlCreatePushParserCtxt(NULL,NULL,NULL,0,"memory:push");CHECK(c);
        xmlCtxtSetErrorHandler(c,quiet,NULL);xmlCtxtSetResourceLoader(c,deny,&calls);xmlCtxtSetMaxAmplification(c,5);
        CHECK(xmlCtxtUseOptions(c,XML_PARSE_NONET|XML_PARSE_NO_XXE)==0);
        for(size_t offset=0;offset<sizeof(document)-1;){size_t n=sizeof(document)-1-offset;if(n>fragments[f])n=fragments[f];CHECK(!xmlParseChunk(c,document+offset,(int)n,0));offset+=n;}
        CHECK(!xmlParseChunk(c,NULL,0,1));xmlDoc *doc=c->myDoc;CHECK(!inspect(doc)&&!calls);c->myDoc=NULL;xmlFreeDoc(doc);xmlFreeParserCtxt(c);
    }
    return 0;
}
typedef struct {unsigned starts,ends,namespaces,attributes;size_t used;char text[64];} SAX;
static void start_element(void *opaque,const xmlChar *name,const xmlChar *prefix,const xmlChar *uri,int namespaces,const xmlChar **pairs,int attributes,int defaults,const xmlChar **values){
    SAX *s=opaque;(void)name;(void)prefix;(void)uri;(void)pairs;(void)defaults;(void)values;
    ++s->starts;s->namespaces+=(unsigned)namespaces;s->attributes+=(unsigned)attributes;
}
static void end_element(void *opaque,const xmlChar *name,const xmlChar *prefix,const xmlChar *uri){SAX *s=opaque;(void)name;(void)prefix;(void)uri;++s->ends;}
static void characters(void *opaque,const xmlChar *text,int length){SAX *s=opaque;if(length<0||(size_t)length>sizeof(s->text)-1-s->used)abort();memcpy(s->text+s->used,text,(size_t)length);s->used+=(size_t)length;s->text[s->used]=0;}
static int sax_events(void){
    SAX state={0};xmlSAXHandler sax={0};sax.initialized=XML_SAX2_MAGIC;sax.startElementNs=start_element;sax.endElementNs=end_element;sax.characters=characters;
    xmlParserCtxt *c=xmlCreatePushParserCtxt(&sax,&state,NULL,0,"memory:sax");CHECK(c);unsigned calls=0;
    xmlCtxtSetErrorHandler(c,quiet,NULL);xmlCtxtSetResourceLoader(c,deny,&calls);CHECK(!xmlCtxtUseOptions(c,XML_PARSE_NONET|XML_PARSE_NO_XXE));
    for(size_t i=0;i<sizeof(document)-1;++i)CHECK(!xmlParseChunk(c,document+i,1,0));
    CHECK(!xmlParseChunk(c,NULL,0,1));CHECK(state.starts==3&&state.ends==3&&state.namespaces==1&&state.attributes==2&&!calls);
    CHECK(!strcmp(state.text,"caf\xc3\xa9 & \xe2\x82\xac\xce\xbb"));CHECK(!c->myDoc);xmlFreeParserCtxt(c);return 0;
}
typedef struct {const char *data;size_t length,offset,fragment;unsigned closes;int fail;} IO;
static int read_input(void *opaque,char *buffer,int size){
    IO *io=opaque;if(io->fail&&io->offset>20)return -1;
    size_t n=io->length-io->offset;if(n>(size_t)size)n=(size_t)size;if(n>io->fragment)n=io->fragment;
    memcpy(buffer,io->data+io->offset,n);io->offset+=n;return (int)n;
}
static int close_input(void *opaque){IO *io=opaque;++io->closes;return 0;}
static int custom_io(void){
    for(unsigned failure=0;failure<2;++failure){
        unsigned calls=0;xmlParserCtxt *c=context(&calls);CHECK(c);IO io={document,sizeof(document)-1,0,7,0,(int)failure};
        xmlDoc *doc=xmlCtxtReadIO(c,read_input,close_input,&io,"memory:io",NULL,XML_PARSE_NONET|XML_PARSE_NO_XXE);
        CHECK(failure?!doc:!inspect(doc));if(doc)xmlFreeDoc(doc);xmlFreeParserCtxt(c);CHECK(io.closes==1&&!calls);
    }
    return 0;
}
static int failures(void){
    const char malformed[]="<root><item></root>";
    const char external[]="<!DOCTYPE root SYSTEM 'file:///SECRET'><root/>";
    const char remote[]="<!DOCTYPE root SYSTEM 'https://example.invalid/external.dtd'><root/>";
    for(unsigned i=0;i<3;++i){
        unsigned calls=0;xmlParserCtxt *c=context(&calls);CHECK(c);const char *text=i==0?malformed:i==1?external:remote;
        xmlDoc *doc=xmlCtxtReadMemory(c,text,(int)strlen(text),"memory:denied",NULL,XML_PARSE_NONET|XML_PARSE_DTDLOAD);
        CHECK(i==0?!doc:calls==1);if(doc)xmlFreeDoc(doc);xmlFreeParserCtxt(c);
    }
#ifdef __CHOS__
    /* Unsupported descriptor APIs fail, without calling invented POSIX open/dup. */
    CHECK(!xmlParserInputBufferCreateFd(1,XML_CHAR_ENCODING_NONE));
    CHECK(!xmlOutputBufferCreateFd(1,NULL));
#endif
    xmlResetLastError();return 0;
}
static int limits(void){
    const char invalid_utf8[]="<root>\xc0\xaf</root>";
    const char expanded[]="<!DOCTYPE root [<!ENTITY a '1234567890'><!ENTITY b '&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;'><!ENTITY c '&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;'><!ENTITY d '&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;'><!ENTITY e '&d;&d;&d;&d;&d;&d;&d;&d;&d;&d;'><!ENTITY f '&e;&e;&e;&e;&e;&e;&e;&e;&e;&e;'><!ENTITY g '&f;&f;&f;&f;&f;&f;&f;&f;&f;&f;'>]><root>&g;</root>";
    for(unsigned i=0;i<2;++i){unsigned calls=0;xmlParserCtxt *c=context(&calls);CHECK(c);
        const char *text=i?expanded:invalid_utf8;
        xmlDoc *doc=xmlCtxtReadMemory(c,text,(int)strlen(text),"memory:limits",NULL,XML_PARSE_NONET|XML_PARSE_NO_XXE|XML_PARSE_NOENT);
        CHECK(!doc&&!calls);CHECK(xmlCtxtGetLastError(c));xmlFreeParserCtxt(c);}
    xmlResetLastError();return 0;
}
static void *worker(void *argument){(void)argument;for(unsigned i=0;i<8;++i)if(ordinary()||push()||sax_events()||custom_io()||failures())return(void*)1;return NULL;}
int main(void){
    xmlInitParser();CHECK(xmlHasFeature(XML_WITH_THREAD)&&xmlHasFeature(XML_WITH_PUSH)&&xmlHasFeature(XML_WITH_XPATH));
    CHECK(!ordinary()&&!push()&&!sax_events()&&!custom_io()&&!failures()&&!limits());
    puts("[ OK ] actual XML tree,XPath,UTF8,incremental input and denied external resources passed");
    pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,NULL));
    for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(threads[i],&result)&&!result);}CHECK(!pthread_attr_destroy(&attr));
    xmlResetLastError();xmlCleanupParser();
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats));printf("[XML HEAP] allocations=%lu mapped=%lu\n",(unsigned long)stats.allocations,(unsigned long)stats.mapped_bytes);
#endif
    puts("[ OK ] actual XML native threads and parser cleanup passed");return 0;
}
