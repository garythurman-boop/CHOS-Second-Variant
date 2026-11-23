/* CHOS certificate-inspection adapter, 2026-11-17. Real MbedTLS X509 parsing,
 * not an independent ASN.1 implementation and not trust validation. */
#include "CertificateBackend.h"
#include <mbedtls/x509_crt.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/platform_util.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct chos_certificate { mbedtls_x509_crt crt; int64_t from, until; };

struct tm *mbedtls_platform_gmtime_r(const time_t *t, struct tm *out)
{
    return gmtime_r(t, out);
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now))
        abort(); /* This MbedTLS callback has no failure return. */
    return (mbedtls_ms_time_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int64_t epoch(const mbedtls_x509_time *t)
{
    struct tm value = { 0 };
    value.tm_year = t->year - 1900;
    value.tm_mon = t->mon - 1;
    value.tm_mday = t->day;
    value.tm_hour = t->hour;
    value.tm_min = t->min;
    value.tm_sec = t->sec;
    return (int64_t)timegm(&value);
}

chos_certificate *chos_certificate_parse(const uint8_t *der, size_t bytes)
{
    if (!der || !bytes || bytes > CHOS_CERTIFICATE_MAX_DER_BYTES)
        return NULL;
    chos_certificate *value = calloc(1, sizeof(*value));
    if (!value)
        return NULL;
    mbedtls_x509_crt_init(&value->crt);
    if (mbedtls_x509_crt_parse_der(&value->crt, der, bytes) || value->crt.raw.len != bytes) {
        chos_certificate_destroy(value);
        return NULL;
    }
    value->from = epoch(&value->crt.valid_from);
    value->until = epoch(&value->crt.valid_to);
    return value;
}

void chos_certificate_destroy(chos_certificate *value)
{
    if (!value)
        return;
    mbedtls_x509_crt_free(&value->crt);
    mbedtls_platform_zeroize(value, sizeof(*value));
    free(value);
}

const unsigned char *chos_certificate_subject(const chos_certificate *value, size_t *bytes, int *tag)
{
    if (!value || !bytes || !tag)
        return NULL;
    const mbedtls_x509_name *selected = NULL;
    for (const mbedtls_x509_name *n = &value->crt.subject; n; n = n->next) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
            selected = n;
            break;
        }
        if (!selected && MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORGANIZATION, &n->oid) == 0)
            selected = n;
    }
    if (!selected)
        selected = &value->crt.subject;
    *bytes = selected->val.len;
    *tag = selected->val.tag;
    return selected->val.p;
}

int64_t chos_certificate_valid_from(const chos_certificate *value) { return value->from; }
int64_t chos_certificate_valid_until(const chos_certificate *value) { return value->until; }
int chos_certificate_signature_is_sha1(const chos_certificate *value)
{
    return value->crt.MBEDTLS_PRIVATE(sig_md) == MBEDTLS_MD_SHA1;
}

static int san_type(const mbedtls_x509_sequence *item)
{
    int type = item->buf.tag & 31;
    if ((item->buf.tag & 0xe0) != MBEDTLS_ASN1_CONTEXT_SPECIFIC)
        return 0;
    if (type == MBEDTLS_X509_SAN_DNS_NAME)
        return 2;
    if (type == MBEDTLS_X509_SAN_IP_ADDRESS && (item->buf.len == 4 || item->buf.len == 16))
        return 7;
    return 0;
}

size_t chos_certificate_san_count(const chos_certificate *value)
{
    size_t count = 0;
    for (const mbedtls_x509_sequence *n = &value->crt.subject_alt_names; n; n = n->next)
        if (n->buf.p && san_type(n))
            ++count;
    return count;
}

const unsigned char *chos_certificate_san(const chos_certificate *value, size_t index, size_t *bytes, int *type)
{
    if (!value || !bytes || !type)
        return NULL;
    for (const mbedtls_x509_sequence *n = &value->crt.subject_alt_names; n; n = n->next) {
        int kind = san_type(n);
        if (!n->buf.p || !kind)
            continue;
        if (index--) continue;
        *bytes = n->buf.len;
        *type = kind;
        return n->buf.p;
    }
    return NULL;
}
