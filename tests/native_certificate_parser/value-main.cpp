#include "config.h"
#include "CertificateInfo.h"
#include "certificate-cases.h"
#include <wtf/Threading.h>
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <chos/libc.h>
using WebCore::CertificateInfo;
#define CHECK(x) do{if(!(x)){std::printf("[FAIL] CertificateInfo line %u\n",__LINE__);return false;}}while(0)
static CertificateInfo::Certificate bytes(const unsigned char *data,size_t n){return CertificateInfo::makeCertificate({data,n});}
static CertificateInfo::CertificateChain one(const struct CertificateCase& c){CertificateInfo::CertificateChain chain;chain.append(bytes(c.der,c.size));return chain;}
static WTF::String ipv6(const unsigned char *p){char text[80];std::snprintf(text,sizeof(text),"%x:%x:%x:%x:%x:%x:%x:%x",(p[0]<<8)|p[1],(p[2]<<8)|p[3],(p[4]<<8)|p[5],(p[6]<<8)|p[7],(p[8]<<8)|p[9],(p[10]<<8)|p[11],(p[12]<<8)|p[13],(p[14]<<8)|p[15]);return WTF::String::fromLatin1(text);}
static bool positive(const struct CertificateCase& c)
{
    CertificateInfo info(0,one(c));CHECK(info.verificationWasReported()&&info.verificationError()==0&&!info.isEmpty());
    auto summary=info.summary();
    if(!c.accepted){CHECK(!summary&&info.containsNonRootSHA1SignedCertificate());return true;}
    CHECK(summary);
    CHECK(summary->subject==WTF::String::fromUTF8(std::span(c.subject,c.subject_size)));
    CHECK(summary->validFrom.value()==c.from&&summary->validUntil.value()==c.until);
    size_t dns=0,ip=0;
    for(size_t i=0;i<c.san_count;++i){const auto&san=c.sans[i];
        if(san.type==2){CHECK(dns<summary->dnsNames.size()&&summary->dnsNames[dns++]==WTF::String{std::span(san.data,san.size)});}
        else{WTF::String expected;if(san.size==4){char text[32];std::snprintf(text,sizeof(text),"%u.%u.%u.%u",san.data[0],san.data[1],san.data[2],san.data[3]);expected=WTF::String::fromLatin1(text);}else expected=ipv6(san.data);
            CHECK(ip<summary->ipAddresses.size()&&summary->ipAddresses[ip++]==expected);}
    }
    CHECK(summary->dnsNames.size()==dns&&summary->ipAddresses.size()==ip);
    CHECK(info.containsNonRootSHA1SignedCertificate()==bool(c.sha1));
    auto copy=info.isolatedCopy();CHECK(copy==info);
    CHECK(copy.certificateChain()[0].span().data()!=info.certificateChain()[0].span().data());
    CHECK(!std::memcmp(copy.certificateChain()[0].span().data(),c.der,c.size));
    return true;
}
static bool cases()
{
    CertificateInfo empty;CHECK(empty.isEmpty()&&!empty.verificationWasReported()&&empty.verificationError()<0&&!empty.summary());
    CertificateInfo emptyReported(0,{});CHECK(emptyReported.isEmpty()&&!emptyReported.verificationWasReported()&&emptyReported.verificationError()<0);
    CHECK(!empty.containsNonRootSHA1SignedCertificate());
    for(const auto&c:certificate_cases)CHECK(positive(c));
    const auto&sha1=certificate_cases[5];CHECK(sha1.sha1);
    CertificateInfo trusted(0,one(sha1),0);CHECK(!trusted.containsNonRootSHA1SignedCertificate());
    CertificateInfo failed(7,one(sha1),0);CHECK(failed.containsNonRootSHA1SignedCertificate());
    CertificateInfo unreported(-1,one(sha1),0);CHECK(!unreported.verificationWasReported()&&unreported.containsNonRootSHA1SignedCertificate());
    CertificateInfo badAnchor(0,one(sha1),99);CHECK(badAnchor.containsNonRootSHA1SignedCertificate());
    CertificateInfo::CertificateChain invalid;invalid.append(bytes(reinterpret_cast<const unsigned char*>("bad"),3));
    CertificateInfo malformed(0,WTF::move(invalid),0);CHECK(!malformed.summary()&&malformed.containsNonRootSHA1SignedCertificate());
    CertificateInfo::CertificateChain chain;for(unsigned i=0;i<33;++i)chain.append(bytes(sha1.der,sha1.size));
    CertificateInfo tooMany(0,WTF::move(chain));CHECK(tooMany.containsNonRootSHA1SignedCertificate());
    CertificateInfo copied;
    {CertificateInfo original(0,one(certificate_cases[0]));copied=original.isolatedCopy();}
    CHECK(copied.summary()&&copied.summary()->subject==WTF::String::fromLatin1("metadata.chos.test"));
    return true;
}
static void *worker(void*){for(unsigned n=0;n<5;++n)if(!cases())return reinterpret_cast<void*>(1);return nullptr;}
static bool run()
{
    CHECK(cases());pthread_attr_t attr;pthread_t t[3];CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(t+i,&attr,worker,nullptr));
    for(unsigned i=0;i<3;++i){void *result=nullptr;CHECK(!pthread_join(t[i],&result)&&!result);}CHECK(!pthread_attr_destroy(&attr));return true;
}
int main()
{
    WTF::initialize();chos_heap_stats_t warm{};
    for(unsigned round=0;round<3;++round){if(!run())return 1;chos_heap_stats_t now;if(chos_libc_heap_info(&now))return 1;
        std::printf("[CERTIFICATE HEAP] round=%u allocations=%lu mapped=%lu\n",round,(unsigned long)now.allocations,(unsigned long)now.mapped_bytes);
        if(now.allocations||now.mapped_bytes)return 1;
        if(!round)warm=now;else if(now.allocations!=warm.allocations||now.mapped_bytes!=warm.mapped_bytes)return 1;}
    puts("[ OK ] actual CertificateInfo metadata,ownership,reported verification and threads passed");return 0;
}
