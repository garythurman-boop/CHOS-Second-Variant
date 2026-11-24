/* CHOS WebCore key adapter, 2026-11-20.
 * DER parsing/writing, big integers and key checks are maintained MbedTLS code.
 * This adapter owns objects and enforces the selected input/type boundary. */
#include "KeyBackend.h"
#include <stdlib.h>
#include <string.h>
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/ecp.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>

#define INPUT_LIMIT 65536u
#define WRITE_LIMIT 16384u
struct chos_webcore_key {
    mbedtls_pk_context context;
    chos_webcore_key_info info;
};

static int sequence(unsigned char **p,const unsigned char *end,const unsigned char **sequence_end)
{
    size_t length;
    int rc=mbedtls_asn1_get_tag(p,end,&length,MBEDTLS_ASN1_CONSTRUCTED|MBEDTLS_ASN1_SEQUENCE);
    if(rc)return rc;
    *sequence_end=*p+length;
    return 0;
}

/* Validate only the format envelope with the maintained ASN.1 reader. The
 * maintained PK parser validates the algorithm, parameters and actual key. */
static int envelope(const uint8_t *data,size_t bytes,int private_key)
{
    if(!data||!bytes||bytes>INPUT_LIMIT)return CHOS_KEY_BAD_INPUT;
    unsigned char *p=(unsigned char *)data;const unsigned char *end=data+bytes,*outer,*algorithm;
    int rc=sequence(&p,end,&outer);if(rc||outer!=end)return CHOS_KEY_BAD_INPUT;
    if(private_key){int version=-1;rc=mbedtls_asn1_get_int(&p,outer,&version);if(rc||version)return CHOS_KEY_BAD_INPUT;}
    rc=sequence(&p,outer,&algorithm);if(rc)return CHOS_KEY_BAD_INPUT;
    p=(unsigned char *)algorithm;
    size_t length;rc=mbedtls_asn1_get_tag(&p,outer,&length,private_key?MBEDTLS_ASN1_OCTET_STRING:MBEDTLS_ASN1_BIT_STRING);
    if(rc||!length||p+length!=outer)return CHOS_KEY_BAD_INPUT;
    if(!private_key&&(length<2||*p))return CHOS_KEY_BAD_INPUT;
    return 0;
}

static int curve_bits(mbedtls_ecp_group_id group)
{
    switch(group){case MBEDTLS_ECP_DP_SECP256R1:return CHOS_KEY_P256;
    case MBEDTLS_ECP_DP_SECP384R1:return CHOS_KEY_P384;
    case MBEDTLS_ECP_DP_SECP521R1:return CHOS_KEY_P521;
    default:return 0;}
}

static int fail_rng(void *unused,unsigned char *out,size_t bytes)
{
    (void)unused;(void)out;(void)bytes;return CHOS_KEY_NO_ENTROPY;
}

static chos_webcore_key *import_der(unsigned family,unsigned curve,const uint8_t *data,size_t bytes,
    int private_key,chos_webcore_key_rng rng,void *rng_context,int *error)
{
    int rc=CHOS_KEY_BAD_INPUT;
    if(error)*error=rc;
    if((family!=CHOS_KEY_RSA&&family!=CHOS_KEY_EC)||
       (family==CHOS_KEY_RSA&&curve)||
       (family==CHOS_KEY_EC&&curve!=256&&curve!=384&&curve!=521)||envelope(data,bytes,private_key))return NULL;
    chos_webcore_key *key=calloc(1,sizeof(*key));if(!key){if(error)*error=CHOS_KEY_NO_MEMORY;return NULL;}
    mbedtls_pk_init(&key->context);
    if(private_key)rc=mbedtls_pk_parse_key(&key->context,data,bytes,NULL,0,rng?rng:fail_rng,rng_context);
    else rc=mbedtls_pk_parse_public_key(&key->context,data,bytes);
    if(rc)goto failure;
    mbedtls_pk_type_t type=mbedtls_pk_get_type(&key->context);
    if((family==CHOS_KEY_RSA&&type!=MBEDTLS_PK_RSA)||(family==CHOS_KEY_EC&&type!=MBEDTLS_PK_ECKEY)){
        rc=CHOS_KEY_BAD_INPUT;goto failure;
    }
    key->info=(chos_webcore_key_info){family,curve,(unsigned)mbedtls_pk_get_bitlen(&key->context),(unsigned)private_key};
    if(family==CHOS_KEY_RSA){
        if(key->info.bits<1024||key->info.bits>8192){rc=CHOS_KEY_UNSUPPORTED;goto failure;}
        rc=private_key?mbedtls_rsa_check_privkey(mbedtls_pk_rsa(key->context)):mbedtls_rsa_check_pubkey(mbedtls_pk_rsa(key->context));
    }else{
        if((unsigned)curve_bits(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key->context)))!=curve){rc=CHOS_KEY_BAD_INPUT;goto failure;}
        if(private_key)rc=mbedtls_pk_check_pair(&key->context,&key->context,rng?rng:fail_rng,rng_context);
    }
    if(rc)goto failure;
    if(error)*error=0;
    return key;
failure:
    if(error)*error=rc;
    chos_webcore_key_destroy(key);
    return NULL;
}

chos_webcore_key *chos_webcore_key_import_spki(unsigned family,unsigned curve,const uint8_t *data,size_t bytes,int *error)
{
    return import_der(family,curve,data,bytes,0,NULL,NULL,error);
}
chos_webcore_key *chos_webcore_key_import_pkcs8(unsigned family,unsigned curve,const uint8_t *data,size_t bytes,chos_webcore_key_rng rng,void *context,int *error)
{
    return import_der(family,curve,data,bytes,1,rng,context,error);
}
int chos_webcore_key_get_info(const chos_webcore_key *key,chos_webcore_key_info *out)
{
    if(!key||!out)return CHOS_KEY_BAD_INPUT;
    *out=key->info;return 0;
}
void chos_webcore_key_destroy(chos_webcore_key *key)
{
    if(!key)return;
    mbedtls_pk_free(&key->context);
    mbedtls_platform_zeroize(key,sizeof(*key));free(key);
}
void chos_webcore_key_free_bytes(uint8_t *data,size_t bytes)
{
    if(!data)return;
    mbedtls_platform_zeroize(data,bytes);free(data);
}

static int pkcs8_der(const chos_webcore_key *key,unsigned char *buffer,size_t capacity)
{
    unsigned char *inner=calloc(1,WRITE_LIMIT);if(!inner)return CHOS_KEY_NO_MEMORY;
    int rc=mbedtls_pk_write_key_der(&key->context,inner,WRITE_LIMIT);
    if(rc<=0){chos_webcore_key_free_bytes(inner,WRITE_LIMIT);return rc?rc:CHOS_KEY_BAD_INPUT;}
    unsigned char *p=buffer+capacity;int written=mbedtls_asn1_write_octet_string(&p,buffer,inner+WRITE_LIMIT-rc,(size_t)rc);
    chos_webcore_key_free_bytes(inner,WRITE_LIMIT);if(written<0)return written;
    size_t total=(size_t)written;const char *oid;size_t oid_bytes;size_t parameters=0;
    if(key->info.family==CHOS_KEY_RSA){oid=MBEDTLS_OID_PKCS1_RSA;oid_bytes=MBEDTLS_OID_SIZE(MBEDTLS_OID_PKCS1_RSA);}
    else{
        rc=mbedtls_oid_get_oid_by_ec_grp(mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key->context)),&oid,&oid_bytes);if(rc)return rc;
        rc=mbedtls_asn1_write_oid(&p,buffer,oid,oid_bytes);if(rc<0)return rc;parameters=(size_t)rc;
        oid=MBEDTLS_OID_EC_ALG_UNRESTRICTED;oid_bytes=MBEDTLS_OID_SIZE(MBEDTLS_OID_EC_ALG_UNRESTRICTED);
    }
    rc=mbedtls_asn1_write_algorithm_identifier(&p,buffer,oid,oid_bytes,parameters);if(rc<0)return rc;total+=(size_t)rc;
    rc=mbedtls_asn1_write_int(&p,buffer,0);if(rc<0)return rc;total+=(size_t)rc;
    rc=mbedtls_asn1_write_len(&p,buffer,total);if(rc<0)return rc;total+=(size_t)rc;
    rc=mbedtls_asn1_write_tag(&p,buffer,MBEDTLS_ASN1_CONSTRUCTED|MBEDTLS_ASN1_SEQUENCE);if(rc<0)return rc;total+=(size_t)rc;
    return (int)total;
}

static int export_der(const chos_webcore_key *key,int private_key,uint8_t **out,size_t *length)
{
    if(!key||!out||!length||(private_key&&!key->info.is_private))return CHOS_KEY_BAD_INPUT;
    *out=NULL;*length=0;
    unsigned char *buffer=calloc(1,WRITE_LIMIT);if(!buffer)return CHOS_KEY_NO_MEMORY;
    int rc=private_key?pkcs8_der(key,buffer,WRITE_LIMIT):mbedtls_pk_write_pubkey_der(&key->context,buffer,WRITE_LIMIT);
    if(rc<=0){chos_webcore_key_free_bytes(buffer,WRITE_LIMIT);return rc?rc:CHOS_KEY_BAD_INPUT;}
    size_t bytes=(size_t)rc;
    uint8_t *result=malloc(bytes);if(!result){chos_webcore_key_free_bytes(buffer,WRITE_LIMIT);return CHOS_KEY_NO_MEMORY;}
    memcpy(result,buffer+WRITE_LIMIT-bytes,bytes);chos_webcore_key_free_bytes(buffer,WRITE_LIMIT);
    *out=result;*length=bytes;return 0;
}
int chos_webcore_key_export_spki(const chos_webcore_key *key,uint8_t **out,size_t *bytes){return export_der(key,0,out,bytes);}
int chos_webcore_key_export_pkcs8(const chos_webcore_key *key,uint8_t **out,size_t *bytes){return export_der(key,1,out,bytes);}
