#ifndef CHOS_BROWSER_CORE_H
#define CHOS_BROWSER_CORE_H
#include <stddef.h>
#include <stdint.h>
#define BROWSER_URL_INPUT_MAX 256u
#define BROWSER_HOST_MAX 128u
#define BROWSER_PATH_MAX 256u
#define BROWSER_LINK_MAX 8u
#define BROWSER_LINK_URL_MAX 256u
#define BROWSER_URL_OK 1
#define BROWSER_URL_INVALID 0
#define BROWSER_URL_HTTPS_UNAVAILABLE -1
#define BROWSER_URL_SCHEME_UNSUPPORTED -2

typedef struct {
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    uint16_t port;
    uint8_t secure;
    uint8_t is_ipv4_literal;
    uint32_t ipv4;
} browser_url_t;

typedef struct {
    size_t text_start;
    size_t text_end;
    char url[BROWSER_LINK_URL_MAX];
} browser_link_t;

int browser_parse_url(const char *input, browser_url_t *out);
/* Canonicalize a user URL/hostname into an absolute HTTP or HTTPS URL. */
int browser_normalize_url(const char *input, char *absolute_url, size_t capacity);
/* HTTPS remains fail-closed until a TLS provider with peer/hostname verification is ready. */
int browser_url_transport_available(const browser_url_t *url, int tls_backend_ready);
/* Conservative HTML sniffing for responses with missing or inaccurate Content-Type. */
int browser_body_looks_like_html(const uint8_t *body, size_t length);
int browser_parse_ipv4(const char *text, uint32_t *out);
/* Parse an HTTP response in a bounded byte buffer. 1=complete headers parsed,
 * 0=headers incomplete, -1=invalid. body_offset/body_length identify available body. */
int browser_parse_http_response(const uint8_t *response, size_t response_length,
                                int *status_code, size_t *body_offset,
                                size_t *body_length, int *is_chunked,
                                int *is_html);
/* Extended response parser: captures Location and bounded Content-Length metadata. */
int browser_parse_http_response_ex(const uint8_t *response, size_t response_length,
                                   int *status_code, size_t *body_offset,
                                   size_t *body_length, int *is_chunked,
                                   int *is_html, char *location,
                                   size_t location_capacity,
                                   size_t *content_length,
                                   int *has_content_length);
/* Resolve an HTTP(S) reference against an absolute HTTP(S) base URL. */
int browser_resolve_reference(const char *base_url, const char *reference,
                              char *absolute_url, size_t capacity);
/* Decode a complete HTTP chunked body: 1=complete, 0=incomplete, -1=malformed. */
int browser_decode_chunked(const uint8_t *source, size_t source_length,
                           uint8_t *destination, size_t destination_capacity,
                           size_t *destination_length);
/* Convert bounded HTML to readable plain text without executing active content. */
size_t browser_html_to_text(const uint8_t *source, size_t source_length,
                            char *destination, size_t destination_capacity);
/* Rich text extraction records bounded anchor spans and emits readable image-alt placeholders. */
size_t browser_html_to_text_ex(const uint8_t *source, size_t source_length,
                               char *destination, size_t destination_capacity,
                               browser_link_t *links, size_t link_capacity,
                               size_t *link_count);
#endif
