#include "browser_core.h"

static int ascii_lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static int is_space(unsigned char c);
static int prefix_ci(const char *s, const char *prefix) {
    size_t i = 0u;
    while (prefix[i] != '\0') { if (s[i] == '\0' || ascii_lower((unsigned char)s[i]) != ascii_lower((unsigned char)prefix[i])) return 0; ++i; }
    return 1;
}
static void copy_bounded(char *out, size_t cap, const char *src, size_t n) {
    size_t i;
    if (cap == 0u) return;
    for (i = 0u; i < n && i + 1u < cap; ++i) out[i] = src[i];
    out[i] = '\0';
}
int browser_parse_ipv4(const char *text, uint32_t *out) {
    uint32_t parts[4] = {0u,0u,0u,0u};
    size_t i = 0u, part = 0u, digits = 0u;
    if (text == NULL || out == NULL) return 0;
    while (text[i] != '\0') {
        unsigned char c = (unsigned char)text[i];
        if (c >= '0' && c <= '9') {
            parts[part] = parts[part] * 10u + (uint32_t)(c - '0');
            if (parts[part] > 255u) return 0;
            ++digits;
        } else if (c == '.' && part < 3u && digits != 0u) {
            ++part; digits = 0u;
        } else return 0;
        ++i;
    }
    if (part != 3u || digits == 0u) return 0;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}
static int valid_hostname_char(unsigned char c) {
    c = (unsigned char)ascii_lower(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}
int browser_parse_url(const char *input, browser_url_t *out) {
    const char *authority, *authority_end, *path_start, *fragment;
    size_t input_len = 0u, host_len, path_len, i;
    uint32_t ipv4 = 0u;
    uint32_t port = 80u;
    int secure = 0;

    if (input == NULL || out == NULL) return BROWSER_URL_INVALID;
    while (input[input_len] != '\0' && input_len < BROWSER_URL_INPUT_MAX) ++input_len;
    if (input_len == 0u || input_len == BROWSER_URL_INPUT_MAX) return BROWSER_URL_INVALID;
    for (i = 0u; i < sizeof(*out); ++i) ((uint8_t *)out)[i] = 0u;
    out->port = 80u;
    if (prefix_ci(input, "https://")) { authority = input + 8u; port = 443u; secure = 1; }
    else if (prefix_ci(input, "http://")) authority = input + 7u;
    else {
        const char *colon = input;
        while (*colon != '\0' && *colon != ':' && *colon != '/' && *colon != '?' && *colon != '#') ++colon;
        if (*colon == ':' && colon[1] == '/' && colon[2] == '/') return BROWSER_URL_SCHEME_UNSUPPORTED;
        authority = input;
    }
    authority_end = authority;
    while (*authority_end != '\0' && *authority_end != '/' && *authority_end != '?' && *authority_end != '#') ++authority_end;
    if (authority_end == authority) return BROWSER_URL_INVALID;
    for (i = 0u; authority + i < authority_end; ++i) if (authority[i] == '@' || authority[i] == '[' || authority[i] == ']') return BROWSER_URL_INVALID;
    {
        const char *colon = authority;
        while (colon < authority_end && *colon != ':') ++colon;
        host_len = (size_t)(colon - authority);
        if (host_len == 0u || host_len >= sizeof(out->host)) return BROWSER_URL_INVALID;
        for (i = 0u; i < host_len; ++i) {
            unsigned char c = (unsigned char)authority[i];
            if (!valid_hostname_char(c)) return BROWSER_URL_INVALID;
            out->host[i] = (char)ascii_lower(c);
        }
        out->host[host_len] = '\0';
        if (colon < authority_end) {
            const char *p = colon + 1u;
            if (p == authority_end) return BROWSER_URL_INVALID;

            port = 0u;
            while (p < authority_end) {
                if (*p < '0' || *p > '9') return BROWSER_URL_INVALID;
                port = port * 10u + (uint32_t)(*p - '0');
                if (port > 65535u) return BROWSER_URL_INVALID;
                ++p;
            }
            if (port == 0u) return BROWSER_URL_INVALID;
        }
    }
    if (host_len > 253u || out->host[0] == '.' || out->host[host_len - 1u] == '.') return BROWSER_URL_INVALID;
    /* Enforce DNS label bounds and reject labels beginning/ending with '-'. */
    {
        size_t label = 0u;
        for (i = 0u; i <= host_len; ++i) {
            char c = out->host[i];
            if (c == '.' || c == '\0') {
                if (label == 0u || label > 63u || out->host[i - label] == '-' || out->host[i - 1u] == '-') return BROWSER_URL_INVALID;
                label = 0u;
            } else ++label;
        }
    }
    out->port = (uint16_t)port;
    out->secure = (uint8_t)secure;
    out->is_ipv4_literal = (uint8_t)browser_parse_ipv4(out->host, &ipv4);
    out->ipv4 = ipv4;
    path_start = authority_end;
    if (*path_start == '#' || *path_start == '\0') {
        out->path[0] = '/'; out->path[1] = '\0';
        return BROWSER_URL_OK;
    }
    fragment = path_start;
    while (*fragment != '\0' && *fragment != '#') ++fragment;
    path_len = (size_t)(fragment - path_start);
    if (*path_start == '?') {
        if (path_len + 1u >= sizeof(out->path)) return BROWSER_URL_INVALID;
        out->path[0] = '/';
        copy_bounded(out->path + 1u, sizeof(out->path) - 1u, path_start, path_len);
    } else {
        if (path_len >= sizeof(out->path)) return BROWSER_URL_INVALID;
        copy_bounded(out->path, sizeof(out->path), path_start, path_len);
    }
    return BROWSER_URL_OK;
}
static int find_header_end(const uint8_t *r, size_t n, size_t *offset) {
    size_t i;
    if (r == NULL || offset == NULL) return -1;
    for (i = 0u; i + 3u < n; ++i) if (r[i] == '\r' && r[i+1u] == '\n' && r[i+2u] == '\r' && r[i+3u] == '\n') { *offset = i + 4u; return 1; }
    return 0;
}
static int bytes_ci_equal(const uint8_t *a, size_t n, const char *s) {
    size_t i = 0u;
    while (s[i] != '\0') { if (i >= n || ascii_lower(a[i]) != ascii_lower((unsigned char)s[i])) return 0; ++i; }
    return i == n;
}
int browser_parse_http_response_ex(const uint8_t *r, size_t n, int *status,
                                   size_t *body_offset, size_t *body_length,
                                   int *chunked, int *is_html, char *location,
                                   size_t location_capacity, size_t *content_length,
                                   int *has_content_length) {
    size_t end = 0u, line_end = 0u, pos, header_start;
    int header_result;
    if (r == NULL || status == NULL || body_offset == NULL || body_length == NULL ||
        chunked == NULL || is_html == NULL || content_length == NULL ||
        has_content_length == NULL) return -1;
    if (location_capacity != 0u && location != NULL) location[0] = '\0';
    *content_length = 0u;
    *has_content_length = 0;
    header_result = find_header_end(r, n, &end);
    if (header_result <= 0) return header_result;
    while (line_end + 1u < n && !(r[line_end] == '\r' && r[line_end+1u] == '\n')) ++line_end;
    if (line_end < 12u || !(r[0] == 'H' && r[1] == 'T' && r[2] == 'T' && r[3] == 'P' && r[4] == '/')) return -1;
    pos = 0u;
    while (pos < line_end && r[pos] != ' ') ++pos;
    while (pos < line_end && r[pos] == ' ') ++pos;
    if (pos + 3u > line_end || r[pos] < '0' || r[pos] > '9' || r[pos+1u] < '0' || r[pos+1u] > '9' || r[pos+2u] < '0' || r[pos+2u] > '9') return -1;
    *status = (r[pos]-'0')*100 + (r[pos+1u]-'0')*10 + (r[pos+2u]-'0');
    *chunked = 0; *is_html = 0;
    header_start = line_end + 2u;
    while (header_start + 1u < end - 2u) {
        size_t h_end = header_start;
        while (h_end + 1u < end && !(r[h_end] == '\r' && r[h_end+1u] == '\n')) ++h_end;
        if (h_end == header_start) break;
        if (h_end - header_start >= 18u && bytes_ci_equal(r + header_start, 18u, "transfer-encoding:")) {
            size_t v = header_start + 18u;
            while (v < h_end && (r[v] == ' ' || r[v] == '\t')) ++v;
            if (h_end - v >= 7u && bytes_ci_equal(r + v, 7u, "chunked")) *chunked = 1;
        } else if (h_end - header_start >= 13u && bytes_ci_equal(r + header_start, 13u, "content-type:")) {
            size_t v = header_start + 13u;
            while (v < h_end && (r[v] == ' ' || r[v] == '\t')) ++v;
            if (h_end - v >= 9u && bytes_ci_equal(r + v, 9u, "text/html")) *is_html = 1;
        } else if (h_end - header_start >= 9u && bytes_ci_equal(r + header_start, 9u, "location:")) {
            size_t v = header_start + 9u, stop = h_end, k;
            while (v < stop && (r[v] == ' ' || r[v] == '\t')) ++v;
            while (stop > v && (r[stop-1u] == ' ' || r[stop-1u] == '\t')) --stop;
            if (location != NULL && location_capacity != 0u) {
                size_t copy = stop - v;
                if (copy >= location_capacity) return -1;
                for (k = 0u; k < copy; ++k) location[k] = (char)r[v+k];
                location[copy] = '\0';
            }
        } else if (h_end - header_start >= 15u && bytes_ci_equal(r + header_start, 15u, "content-length:")) {
            size_t v = header_start + 15u, value = 0u, digits = 0u;
            while (v < h_end && (r[v] == ' ' || r[v] == '\t')) ++v;
            for (; v < h_end; ++v) {
                unsigned char c = r[v];
                if (c == ' ' || c == '\t') {
                    while (v < h_end && (r[v] == ' ' || r[v] == '\t')) ++v;
                    if (v != h_end) return -1;
                    break;
                }
                if (c < '0' || c > '9') return -1;
                if (value > (SIZE_MAX - (size_t)(c - '0')) / 10u) return -1;
                value = value * 10u + (size_t)(c - '0');
                ++digits;
            }
            if (digits == 0u) return -1;
            if (*has_content_length && *content_length != value) return -1;
            *has_content_length = 1;
            *content_length = value;
        }
        header_start = h_end + 2u;
    }
    if (*chunked) *has_content_length = 0; /* Transfer-Encoding takes precedence. */
    *body_offset = end;
    *body_length = n - end;
    return 1;
}

int browser_parse_http_response(const uint8_t *r, size_t n, int *status,
                                size_t *body_offset, size_t *body_length,
                                int *chunked, int *is_html) {
    size_t content_length = 0u;
    int has_content_length = 0;
    return browser_parse_http_response_ex(r, n, status, body_offset, body_length,
                                          chunked, is_html, NULL, 0u,
                                          &content_length, &has_content_length);
}

static int append_string_bounded(char *out, size_t cap, size_t *used, const char *in) {
    size_t i = 0u;
    if (out == NULL || used == NULL || in == NULL || cap == 0u) return 0;
    while (in[i] != '\0') {
        if (*used + 1u >= cap) { out[*used] = '\0'; return 0; }
        out[(*used)++] = in[i++];
    }
    out[*used] = '\0';
    return 1;
}

static int normalize_url_path(browser_url_t *url) {
    char normalized[BROWSER_PATH_MAX];
    size_t starts[BROWSER_PATH_MAX / 2u], ends[BROWSER_PATH_MAX / 2u];
    size_t path_len=0u, query_pos, i=0u, count=0u, out=0u;
    int trailing;
    if(url==NULL)return 0;
    while(url->path[path_len]!='\0'&&path_len<sizeof(url->path))++path_len;
    if(path_len==0u||path_len>=sizeof(url->path)||url->path[0]!='/')return 0;
    query_pos=0u;while(query_pos<path_len&&url->path[query_pos]!='?')++query_pos;
    trailing=(query_pos>0u&&url->path[query_pos-1u]=='/');
    if(query_pos>0u&&!trailing){
        size_t last=query_pos;while(last>0u&&url->path[last-1u]!='/')--last;
        trailing=((query_pos-last)==1u&&url->path[last]=='.')||
                 ((query_pos-last)==2u&&url->path[last]=='.'&&url->path[last+1u]=='.');
    }
    i=1u;
    while(i<query_pos){
        size_t start=i,end;
        while(i<query_pos&&url->path[i]!='/')++i;
        end=i;
        if(end==start|| (end-start==1u&&url->path[start]=='.')) { }
        else if(end-start==2u&&url->path[start]=='.'&&url->path[start+1u]=='.') { if(count>0u)--count; }
        else if(count<sizeof(starts)/sizeof(starts[0])) { starts[count]=start; ends[count]=end; ++count; }
        if(i<query_pos&&url->path[i]=='/')++i;
    }
    normalized[out++]='/';
    for(size_t seg=0u;seg<count;++seg){
        size_t n=ends[seg]-starts[seg];
        if(out>1u){if(out+1u>=sizeof(normalized))return 0;normalized[out++]='/';}
        if(out+n>=sizeof(normalized))return 0;
        for(size_t j=0u;j<n;++j)normalized[out++]=url->path[starts[seg]+j];
    }
    if(trailing&&out>1u){if(out+1u>=sizeof(normalized))return 0;normalized[out++]='/';}
    for(i=query_pos;i<path_len;++i){if(out+1u>=sizeof(normalized))return 0;normalized[out++]=url->path[i];}
    normalized[out]='\0';
    for(i=0u;i<=out;++i)url->path[i]=normalized[i];
    return 1;
}

static int compose_http_url(const browser_url_t *u, char *out, size_t cap) {
    size_t used = 0u;
    char portbuf[6]; size_t portn = 0u; uint16_t port;
    if (u == NULL || out == NULL || cap == 0u) return 0;
    out[0] = '\0';
    if (!append_string_bounded(out, cap, &used, u->secure ? "https://" : "http://") ||
        !append_string_bounded(out, cap, &used, u->host)) return 0;
    if (u->port != (u->secure ? 443u : 80u)) {
        port = u->port;
        do { portbuf[portn++] = (char)('0' + (port % 10u)); port = (uint16_t)(port / 10u); } while (port != 0u && portn < sizeof(portbuf));
        if (used + portn + 2u >= cap) return 0;
        out[used++] = ':';
        while (portn > 0u) out[used++] = portbuf[--portn];
        out[used] = '\0';
    }
    return append_string_bounded(out, cap, &used, u->path);
}

int browser_normalize_url(const char *input, char *absolute_url, size_t capacity) {
    browser_url_t parsed;
    int result;
    if (absolute_url == NULL || capacity == 0u) return BROWSER_URL_INVALID;
    absolute_url[0] = '\0';
    result = browser_parse_url(input, &parsed);
    if (result != BROWSER_URL_OK) return result;
    if (!normalize_url_path(&parsed)) return BROWSER_URL_INVALID;
    return compose_http_url(&parsed, absolute_url, capacity) ? BROWSER_URL_OK : BROWSER_URL_INVALID;
}

int browser_url_transport_available(const browser_url_t *url, int tls_backend_ready) {
    if (url == NULL) return 0;
    return !url->secure || tls_backend_ready != 0;
}

int browser_body_looks_like_html(const uint8_t *body, size_t length) {
    size_t i = 0u;
    if (body == NULL || length == 0u) return 0;
    while (i < length && i < 128u && is_space(body[i])) ++i;
    if (i >= length || body[i] != '<') return 0;
    ++i;
    if (i < length && body[i] == '!') {
        ++i;
        if (i + 6u <= length && ascii_lower(body[i]) == 'd' &&
            ascii_lower(body[i+1u]) == 'o' && ascii_lower(body[i+2u]) == 'c' &&
            ascii_lower(body[i+3u]) == 't' && ascii_lower(body[i+4u]) == 'y' &&
            ascii_lower(body[i+5u]) == 'p' && i + 6u < length &&
            ascii_lower(body[i+6u]) == 'e') return 1;
        return 0;
    }
    if (i < length && body[i] == '/') ++i;
    {
        static const char *const tags[] = {"html", "head", "body", "title", "p", "a", "h1", "h2", "h3", "div", "span", "ul", "ol", "li", "table", "section", "main", "article", "br", "hr", "pre"};
        size_t t;
        for (t = 0u; t < sizeof(tags)/sizeof(tags[0]); ++t) {
            size_t j = 0u;
            while (tags[t][j] != '\0' && i + j < length && ascii_lower(body[i+j]) == tags[t][j]) ++j;
            if (tags[t][j] == '\0' && (i+j >= length || is_space(body[i+j]) || body[i+j] == '>' || body[i+j] == '/')) return 1;
        }
    }
    return 0;
}

int browser_resolve_reference(const char *base_url, const char *reference,
                              char *absolute_url, size_t capacity) {
    browser_url_t base, target;
    char candidate[BROWSER_URL_INPUT_MAX];
    char origin[ BROWSER_URL_INPUT_MAX ];
    const char *ref, *hash;
    size_t rlen = 0u, olen = 0u, cut;
    int parsed;
    if (base_url == NULL || reference == NULL || absolute_url == NULL || capacity == 0u) return BROWSER_URL_INVALID;
    while (reference[rlen] != '\0' && rlen < sizeof(candidate)) ++rlen;
    if (rlen == 0u || rlen >= sizeof(candidate)) return BROWSER_URL_INVALID;
    ref = reference;
    while (*ref == ' ' || *ref == '\t' || *ref == '\r' || *ref == '\n') ++ref;
    while (rlen > 0u && (reference[rlen-1u] == ' ' || reference[rlen-1u] == '\t' || reference[rlen-1u] == '\r' || reference[rlen-1u] == '\n')) --rlen;
    rlen = 0u; while (ref[rlen] != '\0' && ref[rlen] != ' ' && ref[rlen] != '\t' && ref[rlen] != '\r' && ref[rlen] != '\n') ++rlen;
    if (rlen == 0u || rlen >= sizeof(candidate)) return BROWSER_URL_INVALID;
    if (prefix_ci(ref, "https://") || prefix_ci(ref, "http://")) {
        parsed = browser_parse_url(ref, &target);
        if (parsed != BROWSER_URL_OK) return parsed;
        if (!normalize_url_path(&target)) return BROWSER_URL_INVALID;
        return compose_http_url(&target, absolute_url, capacity) ? BROWSER_URL_OK : BROWSER_URL_INVALID;
    }
    {
        size_t i=0u;
        while(i<rlen&&ref[i]!='/'&&ref[i]!='?'&&ref[i]!='#'){
            if(ref[i]==':') return BROWSER_URL_SCHEME_UNSUPPORTED;
            ++i;
        }
    }
    parsed = browser_parse_url(base_url, &base);
    if (parsed != BROWSER_URL_OK) return parsed;
    if (ref[0] == '/' && ref[1] == '/') {
        size_t prefix_length;
        candidate[0] = 'h'; candidate[1] = 't'; candidate[2] = 't'; candidate[3] = 'p';
        prefix_length = 4u;
        if (base.secure) candidate[prefix_length++] = 's';
        candidate[prefix_length++] = ':';
        if (prefix_length + rlen >= sizeof(candidate)) return BROWSER_URL_INVALID;
        for (size_t i=0u; i<rlen; ++i) candidate[prefix_length+i]=ref[i];
        candidate[prefix_length+rlen]='\0';
        parsed=browser_parse_url(candidate,&target);
        if(parsed!=BROWSER_URL_OK) return parsed;
        if(!normalize_url_path(&target)) return BROWSER_URL_INVALID;
        return compose_http_url(&target,absolute_url,capacity)?BROWSER_URL_OK:BROWSER_URL_INVALID;
    }
    if (ref[0] == '#') {
        if (!compose_http_url(&base, candidate, sizeof(candidate))) return BROWSER_URL_INVALID;
        {
            size_t k=0u;
            while(candidate[k]!='\0' && k+1u<capacity){absolute_url[k]=candidate[k];++k;}
            absolute_url[k]='\0';
            return candidate[k]=='\0' ? BROWSER_URL_OK : BROWSER_URL_INVALID;
        }
    }
    if (ref[0] == '?') {
        size_t q=0u, query_len=0u; while(base.path[q]!='\0'&&base.path[q]!='?') ++q;
        while(query_len<rlen&&ref[query_len]!='#')++query_len;
        base.path[q]='\0';
        if(q+query_len>=sizeof(base.path)) return BROWSER_URL_INVALID;
        for(size_t k=0u;k<query_len;++k)base.path[q+k]=ref[k];
        base.path[q+query_len]='\0';
        if(!normalize_url_path(&base)) return BROWSER_URL_INVALID;
        return compose_http_url(&base,absolute_url,capacity)?BROWSER_URL_OK:BROWSER_URL_INVALID;
    }
    if (!compose_http_url(&base, origin, sizeof(origin))) return BROWSER_URL_INVALID;
    while(origin[olen]!='\0') ++olen;
    /* Drop the previous path from the origin string. */
    cut=olen;
    while(cut>0u&&origin[cut-1u]!='/') --cut;
    if (ref[0]=='/') {
        /* origin's first slash starts the path; retain only scheme + authority. */
        size_t slash=base.secure ? 8u : 7u;
        while(origin[slash]!='\0'&&origin[slash]!='/') ++slash;
        cut=slash;
        candidate[0]='\0'; olen=0u;
        for(size_t i=0u;i<cut;++i) candidate[olen++]=origin[i];
        candidate[olen]='\0';
    } else {
        candidate[0]='\0'; olen=0u;
        for(size_t i=0u;i<cut;++i) candidate[olen++]=origin[i];
        candidate[olen]='\0';
    }
    hash=ref; while(*hash!='\0'&&*hash!='#') ++hash;
    rlen=(size_t)(hash-ref);
    if (olen+rlen>=sizeof(candidate)) return BROWSER_URL_INVALID;
    for(size_t i=0u;i<rlen;++i) candidate[olen++]=ref[i];
    candidate[olen]='\0';
    parsed=browser_parse_url(candidate,&target);
    if(parsed!=BROWSER_URL_OK) return parsed;
    if(!normalize_url_path(&target)) return BROWSER_URL_INVALID;
    return compose_http_url(&target,absolute_url,capacity)?BROWSER_URL_OK:BROWSER_URL_INVALID;
}

int browser_decode_chunked(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out_len) {
    size_t pos = 0u, written = 0u;
    if (src == NULL || dst == NULL || out_len == NULL) return -1;
    *out_len = 0u;
    for (;;) {
        size_t line = pos, digits = 0u;
        uint32_t chunk = 0u;
        while (pos + 1u < len && !(src[pos] == '\r' && src[pos+1u] == '\n')) ++pos;
        if (pos + 1u >= len) return 0;
        while (line < pos && src[line] != ';') {
            unsigned char c = src[line++];
            uint32_t val;
            if (c >= '0' && c <= '9') val = c - '0';
            else if (ascii_lower(c) >= 'a' && ascii_lower(c) <= 'f') val = (uint32_t)(ascii_lower(c) - 'a' + 10);
            else return -1;
            if (chunk > (UINT32_MAX - val) / 16u) return -1;
            chunk = chunk * 16u + val; ++digits;
        }
        if (digits == 0u) return -1;
        pos += 2u;
        if (chunk == 0u) {
            /* End marker followed by an empty trailer line, or regular trailers. */
            while (pos + 1u < len) {
                size_t t = pos;
                while (pos + 1u < len && !(src[pos] == '\r' && src[pos+1u] == '\n')) ++pos;
                if (pos + 1u >= len) return 0;
                if (pos == t) { pos += 2u; *out_len = written; return 1; }
                pos += 2u;
            }
            return 0;
        }
        if ((size_t)chunk > cap - written) return -1;
        if ((size_t)chunk > len - pos || len - pos - (size_t)chunk < 2u) return 0;
        for (size_t i = 0u; i < (size_t)chunk; ++i) dst[written++] = src[pos++];
        if (src[pos] != '\r' || src[pos+1u] != '\n') return -1;
        pos += 2u;
    }
}
static int is_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'; }
static int tag_name_eq(const uint8_t *tag, size_t n, const char *name) {
    size_t i = 0u, j = 0u;
    while (i < n && (tag[i] == '/' || tag[i] == ' ' || tag[i] == '\t')) ++i;
    while (name[j] != '\0') {
        if (i >= n || ascii_lower(tag[i]) != ascii_lower((unsigned char)name[j])) return 0;
        ++i; ++j;
    }
    return i == n || is_space(tag[i]) || tag[i] == '/' || tag[i] == '=';
}
static int starts_closing_tag(const uint8_t *t, size_t n) {
    size_t i=0u; while(i<n && is_space(t[i])) ++i; return i<n && t[i]=='/';
}
static int block_tag(const uint8_t *t, size_t n) {
    return tag_name_eq(t,n,"br") || tag_name_eq(t,n,"p") || tag_name_eq(t,n,"div") ||
           tag_name_eq(t,n,"li") || tag_name_eq(t,n,"tr") || tag_name_eq(t,n,"h1") ||
           tag_name_eq(t,n,"h2") || tag_name_eq(t,n,"h3") || tag_name_eq(t,n,"h4") ||
           tag_name_eq(t,n,"h5") || tag_name_eq(t,n,"h6") || tag_name_eq(t,n,"hr") ||
           tag_name_eq(t,n,"section") || tag_name_eq(t,n,"article") || tag_name_eq(t,n,"pre") ||
           tag_name_eq(t,n,"ul") || tag_name_eq(t,n,"ol") || tag_name_eq(t,n,"blockquote") ||
           tag_name_eq(t,n,"main") || tag_name_eq(t,n,"header") || tag_name_eq(t,n,"footer") ||
           tag_name_eq(t,n,"nav") || tag_name_eq(t,n,"aside") || tag_name_eq(t,n,"dl") ||
           tag_name_eq(t,n,"dt") || tag_name_eq(t,n,"dd") || tag_name_eq(t,n,"table");
}
static int entity_decode(const uint8_t *s, size_t n, char *out, size_t *used) {
    struct entity { const char *name; char value; };
    static const struct entity entities[] = {
        {"amp",'&'}, {"lt",'<'}, {"gt",'>'}, {"quot",'"'}, {"apos",'\''}, {"nbsp",' '},
        {"copy",'c'}, {"ndash",'-'}, {"mdash",'-'}, {"hellip",'.'}
    };
    size_t i;
    if (n == 0u || s[0] != '&') return 0;
    for (i=0u; i<sizeof(entities)/sizeof(entities[0]); ++i) {
        size_t j=0u; while (entities[i].name[j] != '\0') ++j;
        if (n >= j + 2u && s[j+1u] == ';') {
            size_t k; int match=1;
            for(k=0u;k<j;++k) if(ascii_lower(s[k+1u])!=ascii_lower((unsigned char)entities[i].name[k])) match=0;
            if(match) { *out=entities[i].value; *used=j+2u; return 1; }
        }
    }
    if(n>=4u&&s[1]=='#') {
        size_t pos=2u; unsigned value=0u; int hex=0;
        if(pos<n&&(s[pos]=='x'||s[pos]=='X')){hex=1;++pos;}
        if(pos>=n)return 0;
        for(;pos<n&&s[pos]!=';';++pos){unsigned char c=s[pos];unsigned d;
            if(c>='0'&&c<='9')d=c-'0';
            else if(hex&&ascii_lower(c)>='a'&&ascii_lower(c)<='f')d=ascii_lower(c)-'a'+10u;
            else return 0;
            if(value>(0x10ffffu-d)/(hex?16u:10u))return 0;
            value=value*(hex?16u:10u)+d;
        }
        if(pos<n&&s[pos]==';'&&value>=32u&&value<127u){*out=(char)value;*used=pos+1u;return 1;}
    }
    return 0;
}
static int attr_value(const uint8_t *tag, size_t n, const char *wanted, char *out, size_t cap) {
    size_t i=0u, name_len=0u;
    if(out==NULL||cap==0u)return 0;
    out[0]='\0';
    while(i<n&&is_space(tag[i]))++i;
    if(i<n&&tag[i]=='/')return 0;
    while(i<n&&!is_space(tag[i])&&tag[i]!='/'&&tag[i]!='>')++i;
    while(i<n){
        size_t start, end, value_start, value_end;
        while(i<n&&(is_space(tag[i])||tag[i]=='/'))++i;
        if(i>=n)break;
        start=i; while(i<n&&!is_space(tag[i])&&tag[i]!='='&&tag[i]!='/'&&tag[i]!='>')++i; end=i;
        if(end==start){++i;continue;}
        while(i<n&&is_space(tag[i]))++i;
        if(i>=n||tag[i]!='=') {while(i<n&&!is_space(tag[i]))++i;continue;}
        ++i; while(i<n&&is_space(tag[i]))++i;
        if(i>=n)break;
        if(tag[i]=='\''||tag[i]=='"') {uint8_t q=tag[i++];value_start=i;while(i<n&&tag[i]!=q)++i;value_end=i;if(i<n)++i;}
        else {value_start=i;while(i<n&&!is_space(tag[i])&&tag[i]!='>')++i;value_end=i;}
        name_len=0u; while(wanted[name_len]!='\0')++name_len;
        if(end-start==name_len){size_t k;int match=1;for(k=0u;k<name_len;++k)if(ascii_lower(tag[start+k])!=ascii_lower((unsigned char)wanted[k]))match=0;
            if(match){
            size_t source=value_start, written=0u;
            while(source<value_end&&written+1u<cap){
                if(tag[source]=='&'){
                    char decoded; size_t consumed=0u;
                    if(entity_decode(tag+source,value_end-source,&decoded,&consumed)){
                        out[written++]=decoded; source+=consumed; continue;
                    }
                }
                out[written++]=(char)tag[source++];
            }
            out[written]='\0'; return written!=0u;
        }}
    }
    return 0;
}
static void output_char(char *dst, size_t cap, size_t *out, char c,
                        browser_link_t *links, size_t active_link) {
    if (*out + 1u >= cap) return;
    if (active_link != (size_t)-1 && links != NULL) {
        if (links[active_link].text_start == (size_t)-1) links[active_link].text_start = *out;
        links[active_link].text_end = *out + 1u;
    }
    dst[(*out)++] = c;
    dst[*out] = '\0';
}
static void output_pending(char *dst, size_t cap, size_t *out, int *pending_space, int *pending_newline,
                           browser_link_t *links, size_t active_link) {
    if(*pending_newline&&*out>0u&&dst[*out-1u]!='\n') output_char(dst,cap,out,'\n',links,active_link);
    else if(*pending_space&&*out>0u&&dst[*out-1u]!=' '&&dst[*out-1u]!='\n') output_char(dst,cap,out,' ',links,active_link);
    *pending_space=0;*pending_newline=0;
}
static void append_literal(char *dst,size_t cap,size_t *out,const char *text,browser_link_t *links,size_t active_link){
    size_t i;for(i=0u;text[i]!='\0'&&*out+1u<cap;++i)output_char(dst,cap,out,text[i],links,active_link);
}
size_t browser_html_to_text_ex(const uint8_t *src, size_t len, char *dst, size_t cap,
                               browser_link_t *links, size_t link_capacity, size_t *link_count) {
    size_t i=0u,out=0u,count=0u,active=(size_t)-1;
    int pending_space=0,pending_newline=0,skip_script=0,skip_style=0;
    if(link_count!=NULL)*link_count=0u;
    if(src==NULL||dst==NULL||cap==0u)return 0u;
    dst[0]='\0';
    if(links!=NULL) for(size_t j=0u;j<link_capacity;++j){links[j].text_start=(size_t)-1;links[j].text_end=0u;links[j].url[0]='\0';}
    while(i<len&&out+1u<cap){
        if(src[i]=='<'){
            size_t start=i+1u,end=start;
            while(end<len&&src[end]!='>')++end;
            if(end==len){++i;continue;}
            if(end-start>=3u&&src[start]=='!'&&src[start+1u]=='-'&&src[start+2u]=='-'){
                size_t comment=end+1u;while(comment+2u<len&&!(src[comment]=='-'&&src[comment+1u]=='-'&&src[comment+2u]=='>'))++comment;
                i=(comment+2u<len)?comment+3u:end+1u;continue;
            }
            if(tag_name_eq(src+start,end-start,"script")){skip_script=!starts_closing_tag(src+start,end-start);i=end+1u;continue;}
            if(tag_name_eq(src+start,end-start,"style")){skip_style=!starts_closing_tag(src+start,end-start);i=end+1u;continue;}
            if(skip_script||skip_style){i=end+1u;continue;}
            if(tag_name_eq(src+start,end-start,"a")){
                if(starts_closing_tag(src+start,end-start)){
                    if(active!=(size_t)-1&&links!=NULL){if(links[active].text_start==(size_t)-1){links[active].text_start=out;links[active].text_end=out;}active=(size_t)-1;}
                }else if(links!=NULL&&count<link_capacity){
                    char href[BROWSER_LINK_URL_MAX];
                    if(attr_value(src+start,end-start,"href",href,sizeof(href))){
                        output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);
                        active=count;links[count].text_start=(size_t)-1;links[count].text_end=out;
                        {size_t k=0u;while(k+1u<sizeof(links[count].url)&&href[k]!='\0'){links[count].url[k]=href[k];++k;}links[count].url[k]='\0';}
                        ++count;
                    }
                }
            }
            if(tag_name_eq(src+start,end-start,"img")){
                char alt[96];
                if(attr_value(src+start,end-start,"alt",alt,sizeof(alt))){
                    output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);
                    if(out>0u&&dst[out-1u]!='\n')output_char(dst,cap,&out,'\n',links,active);
                    append_literal(dst,cap,&out,"[Image: ",links,active);append_literal(dst,cap,&out,alt,links,active);append_literal(dst,cap,&out,"]",links,active);
                    pending_newline=1;
                }
                i=end+1u;continue;
            }
            if(block_tag(src+start,end-start))pending_newline=1;
            if(tag_name_eq(src+start,end-start,"li")&&!starts_closing_tag(src+start,end-start)){
                output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);append_literal(dst,cap,&out,"* ",links,active);
            } else if(tag_name_eq(src+start,end-start,"h1")||tag_name_eq(src+start,end-start,"h2")||tag_name_eq(src+start,end-start,"h3")){
                output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);append_literal(dst,cap,&out,"# ",links,active);
            }
            i=end+1u;continue;
        }
        if(skip_script||skip_style){++i;continue;}
        if(src[i]=='&'){
            char decoded;size_t consumed=0u;
            if(entity_decode(src+i,len-i,&decoded,&consumed)){
                if(is_space((unsigned char)decoded)){if(decoded=='\n')pending_newline=1;else pending_space=1;}
                else{output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);output_char(dst,cap,&out,decoded,links,active);}
                i+=consumed;continue;
            }
        }
        if(is_space(src[i])){if(src[i]=='\n'||src[i]=='\r')pending_newline=1;else pending_space=1;++i;continue;}
        output_pending(dst,cap,&out,&pending_space,&pending_newline,links,active);
        output_char(dst,cap,&out,(char)src[i],links,active);++i;
    }
    while(out>0u&&(dst[out-1u]==' '||dst[out-1u]=='\n'))--out;
    dst[out]='\0';
    if(links!=NULL){
        size_t write=0u;
        for(i=0u;i<count;++i){
            if(links[i].url[0]=='\0'||links[i].text_start==(size_t)-1||links[i].text_end<=links[i].text_start)continue;
            if(write!=i){
                size_t k=0u;
                links[write].text_start=links[i].text_start;
                links[write].text_end=links[i].text_end;
                while(k+1u<sizeof(links[write].url)&&links[i].url[k]!='\0'){links[write].url[k]=links[i].url[k];++k;}
                links[write].url[k]='\0';
            }
            ++write;
        }
        count=write;
    }else count=0u;
    if(link_count!=NULL)*link_count=count;
    return out;
}
size_t browser_html_to_text(const uint8_t *src, size_t len, char *dst, size_t cap) {
    return browser_html_to_text_ex(src,len,dst,cap,NULL,0u,NULL);
}
