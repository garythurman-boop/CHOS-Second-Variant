#include <stddef.h>
#include <stdint.h>
#ifdef CHOS_KERNEL_BROWSER
#include "../../kernel/core/browser_kernel_api.h"
#else
#include "chos/user.h"
#endif
#include "browser_core.h"

#define BROWSER_WINDOW_W 760u
#define BROWSER_WINDOW_H 520u
#define BROWSER_CLIENT_H (BROWSER_WINDOW_H - 40u)
#define BROWSER_FOOTER_LINE_Y (BROWSER_CLIENT_H - 28u)
#define BROWSER_FOOTER_TEXT_Y (BROWSER_CLIENT_H - 20u)
#define BROWSER_URL_CAP BROWSER_URL_INPUT_MAX
#define BROWSER_HISTORY 8u
#define BROWSER_RESPONSE_CAP 4096u
#define BROWSER_PAGE_CAP 4096u
#define BROWSER_VISIBLE_LINES 19u
#define BROWSER_MAX_REDIRECTS 5u
#define BROWSER_CLOSE_TIMEOUT_POLLS 300u
#define BROWSER_LINE_WIDTH 82u
#define BROWSER_STATUS_CAP 112u
#define BROWSER_BOOKMARK_PATH "/DOCUMENTS/CHOS-BOOKMARKS.TXT"
#define BROWSER_BOOKMARK_CAP 2048u
#define TCP_STATE_ESTABLISHED 2u
#define TCP_STATE_CLOSE_WAIT 5u
#define TCP_STATE_FAILED 8u
#define DNS_STATE_PENDING 1u
#define DNS_STATE_RESOLVED 2u
#define DNS_STATE_FAILED 3u

typedef enum { BROWSER_IDLE=0, BROWSER_DNS, BROWSER_CONNECTING, BROWSER_SENDING,
               BROWSER_RECEIVING, BROWSER_CLOSING } browser_phase_t;

static const char app_title[] = "CHOS Web Browser";
static chos_gui_message_t message;
static uint32_t window_id;
static char address[BROWSER_URL_CAP];
static char current_url[BROWSER_URL_CAP];
static char history[BROWSER_HISTORY][BROWSER_URL_CAP];
static size_t history_count, history_index;
static int history_valid;
static int address_focused = 1;
static browser_url_t parsed_url;
static browser_phase_t phase = BROWSER_IDLE;
static int socket_handle = -1;
static uint32_t total_polls;
static size_t closing_polls;
static char status_line[BROWSER_STATUS_CAP] = "Enter an http:// URL or hostname and press Enter.";
static uint8_t response[BROWSER_RESPONSE_CAP];
static size_t response_length;
static char page[BROWSER_PAGE_CAP];
static size_t page_length, scroll_line;
static int page_ready;
static int page_truncated;
static char deferred_url[BROWSER_URL_CAP];
static int deferred_navigate;
static int deferred_add_history;
static int navigation_is_redirect;
static size_t redirects_followed;
static size_t response_expected_total;
static int response_has_expected_total;
static char dns_expected_host[BROWSER_HOST_MAX];
static uint8_t tcp_read_buffer[CHOS_TCP_API_MAX_TRANSFER];
static uint8_t chunked_body[BROWSER_RESPONSE_CAP];
static char bookmark_buffer[BROWSER_BOOKMARK_CAP];
static char bookmark_entries[BROWSER_HISTORY][BROWSER_URL_CAP];
static size_t bookmark_count;
static int page_is_bookmarks;
static browser_link_t page_links[BROWSER_LINK_MAX];
static size_t page_link_count;
static int link_focus_active;
static size_t focused_link_index;

static void message_begin(chos_gui_message_t *m, uint16_t type, size_t payload_size) {
    chos_memset(m, 0, sizeof(*m));
    m->header.version = CHOS_GUI_ABI_VERSION;
    m->header.type = type;
    m->header.size_bytes = (uint32_t)(offsetof(chos_gui_message_t, payload) + payload_size);
}
static long submit_message(chos_gui_message_t *m) { return chos_gui_submit(m, m->header.size_bytes); }
static int fill_rect(uint32_t id, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    message_begin(&message, CHOS_GUI_MSG_FILL_RECT, sizeof(message.payload.fill_rect));
    message.payload.fill_rect.window_id=id; message.payload.fill_rect.x=x; message.payload.fill_rect.y=y;
    message.payload.fill_rect.width=w; message.payload.fill_rect.height=h; message.payload.fill_rect.argb=color;
    return submit_message(&message)==0;
}
static int draw_text_font(uint32_t id, int32_t x, int32_t y, const char *text, uint32_t color, uint16_t font_id) {
    size_t n=chos_strlen(text);
    if (n >= CHOS_GUI_MAX_TEXT) n=CHOS_GUI_MAX_TEXT-1u;
    message_begin(&message, CHOS_GUI_MSG_DRAW_TEXT, sizeof(message.payload.draw_text));
    message.payload.draw_text.window_id=id; message.payload.draw_text.x=x; message.payload.draw_text.y=y;
    message.payload.draw_text.argb=color; message.payload.draw_text.font_id=font_id;
    message.payload.draw_text.text_length=(uint16_t)n; chos_memcpy(message.payload.draw_text.text,text,n);
    return submit_message(&message)==0;
}
static int draw_text(uint32_t id, int32_t x, int32_t y, const char *text, uint32_t color) {
    return draw_text_font(id,x,y,text,color,0u);
}
static int set_title(const char *title) {
    size_t n=chos_strlen(title); if (n >= CHOS_GUI_MAX_TEXT) n=CHOS_GUI_MAX_TEXT-1u;
    message_begin(&message, CHOS_GUI_MSG_SET_TITLE, sizeof(message.payload.set_title));
    message.payload.set_title.window_id=window_id; message.payload.set_title.title_length=(uint16_t)n;
    chos_memcpy(message.payload.set_title.title,title,n); return submit_message(&message)==0;
}
static void status_set(const char *text) { chos_strlcpy(status_line,text,sizeof(status_line)); }
static void page_set(const char *text) {
    chos_strlcpy(page,text,sizeof(page)); page_length=chos_strlen(page); scroll_line=0u; page_ready=1; page_is_bookmarks=0; page_link_count=0u; link_focus_active=0;
}
static void append_char(char *dst, size_t cap, size_t *len, char c) {
    if (*len+1u < cap) { dst[*len]=c; ++*len; dst[*len]='\0'; }
}
static int ascii_eq(const char *a, const char *b) { return chos_strcmp(a,b)==0; }
static void abort_current_connection(void) {
    if (socket_handle >= 1) (void)chos_net_tcp_abort(socket_handle);
    socket_handle=-1;
    phase=BROWSER_IDLE;
    closing_polls=0u;
    deferred_navigate=0;
    deferred_add_history=0;
}
static void stop_current_fetch(void) {
    /* A fresh user action must not depend on the previous peer completing FIN. */
    abort_current_connection();
    response_length=0u;
    response_expected_total=0u;
    response_has_expected_total=0;
}
static int start_tcp_connection(uint32_t ip) {
    long h;
    if (socket_handle >= 1) { status_set("A previous socket is still closing; try Go again shortly."); return 0; }
    h=chos_net_tcp_open(0u);
    if (h < 1 || h > CHOS_TCP_MAX_SOCKETS) { status_set("No TCP socket is available."); return 0; }
    socket_handle=(int)h;
    if (chos_net_tcp_connect(socket_handle,ip,parsed_url.port) < 0) {
        status_set("TCP connect could not be started."); (void)chos_net_tcp_close(socket_handle); socket_handle=-1; return 0;
    }
    phase=BROWSER_CONNECTING;
    status_set("Connecting over TCP...");
    return 1;
}
static void history_push(const char *url) {
    size_t i;
    if (history_valid && history_index < history_count && ascii_eq(history[history_index],url)) return;
    if (history_valid && history_index + 1u < history_count) history_count=history_index+1u;
    if (history_count == BROWSER_HISTORY) {
        for (i=1u;i<BROWSER_HISTORY;++i) chos_strlcpy(history[i-1u],history[i],sizeof(history[i-1u]));
        --history_count;
    }
    chos_strlcpy(history[history_count],url,sizeof(history[history_count]));
    history_index=history_count; ++history_count; history_valid=1;
}
static void request_navigation(const char *url, int add_history);
static void navigate_from_address(int add_history) { request_navigation(address,add_history); }
static void request_navigation(const char *url, int add_history) {
    int parsed;
    char normalized[BROWSER_URL_CAP];
    chos_strlcpy(address,url,sizeof(address));
    parsed=browser_parse_url(address,&parsed_url);
    if (parsed == BROWSER_URL_OK) {
        int nr=browser_normalize_url(address,normalized,sizeof(normalized));
        if (nr != BROWSER_URL_OK) parsed=nr;
        else {
            chos_strlcpy(address,normalized,sizeof(address));
            parsed=browser_parse_url(address,&parsed_url);
        }
    }
    /* Replace an in-flight request rather than blocking navigation on a peer FIN. */
    abort_current_connection();
    if (add_history || !navigation_is_redirect) redirects_followed=0u;
    navigation_is_redirect=0;
    if (parsed == BROWSER_URL_OK && !browser_url_transport_available(&parsed_url, 0)) {
        stop_current_fetch();
        status_set("HTTPS blocked: TLS and certificate verification are not integrated. No HTTP downgrade was attempted.");
        page_set("Secure browsing is not available in this build.\n\nThe URL is recognized as HTTPS, but CHOS refuses to open a TCP connection until a TLS provider, certificate-chain and hostname verification, a trusted CA store, a trustworthy clock, and a secure entropy source are available.\nNo HTTP downgrade was attempted.");
        (void)set_title("CHOS Web Browser - HTTPS unavailable");
        return;
    }
    if (parsed == BROWSER_URL_SCHEME_UNSUPPORTED || parsed == BROWSER_URL_INVALID) {
        status_set("Invalid URL or unsupported scheme. Use a valid http:// or https:// address.");
        page_set("Unable to navigate.\n\nSupported scheme: HTTP only.\nURL contains an invalid hostname, port, or path.");
        return;
    }
    /* The previous socket has been released above; start the new fetch immediately. */
    chos_strlcpy(current_url,address,sizeof(current_url));
    if (add_history) history_push(current_url);
    response_length=0u; response_expected_total=0u; response_has_expected_total=0;
    page_length=0u; page[0]='\0'; page_ready=0; page_is_bookmarks=0; scroll_line=0u; page_link_count=0u;
    page_truncated=0; total_polls=0u;
    if (parsed_url.is_ipv4_literal) {
        if (start_tcp_connection(parsed_url.ipv4)) { /* phase is non-blocking */ }
        else { page_set("Could not start a TCP connection to this address."); }
    } else {
        chos_strlcpy(dns_expected_host,parsed_url.host,sizeof(dns_expected_host));
        if (chos_net_dns_lookup(parsed_url.host) < 0) {
            status_set("DNS query could not start; another lookup may be pending or DHCP is offline.");
            page_set("DNS lookup could not start. Check CHOS network status and try again.");
            phase=BROWSER_IDLE;
        } else {
            phase=BROWSER_DNS; status_set("Resolving hostname...");
            page_set("Resolving hostname...");
        }
    }
    (void)set_title("CHOS Web Browser");
}
static void render_received_text(const uint8_t *body, size_t length, int html) {
    size_t i, out=0u;
    page_link_count=0u;
    if (html || browser_body_looks_like_html(body,length)) {
        page_length=browser_html_to_text_ex(body,length,page,sizeof(page),page_links,BROWSER_LINK_MAX,&page_link_count);
        for (i=0u;i<page_link_count;++i) {
            char absolute[BROWSER_URL_CAP];
            int resolved=browser_resolve_reference(current_url,page_links[i].url,absolute,sizeof(absolute));
            if (resolved==BROWSER_URL_OK) chos_strlcpy(page_links[i].url,absolute,sizeof(page_links[i].url));
            else page_links[i].url[0]='\0';
        }
    } else {
        for (i=0u;i<length && out+1u<sizeof(page);++i) {
            unsigned char c=body[i];
            if (c=='\r') continue;
            if (c=='\n' || c=='\t' || (c>=32u && c<127u)) page[out++]=(char)c;
            else if (c>=128u) page[out++]='?';
        }
        page[out]='\0'; page_length=out;
    }
    while (page_length>0u && page[page_length-1u]=='\n') page[--page_length]='\0';
    if (page_length==0u) page_set("The server returned an empty or unsupported body.");
    else { page_ready=1; page_is_bookmarks=0; }
    scroll_line=0u;
}
static int is_redirect_status(int status) {
    return status==301 || status==302 || status==303 || status==307 || status==308;
}
static void finish_http_response(int truncated) {
    int status=0, chunked=0, is_html=0, has_content_length=0;
    size_t offset=0u, length=0u, content_length=0u;
    char location[BROWSER_URL_CAP];
    int result=browser_parse_http_response_ex(response,response_length,&status,&offset,&length,&chunked,&is_html,
                                              location,sizeof(location),&content_length,&has_content_length);
    page_truncated=truncated;
    if (result != 1) {
        status_set("HTTP response was incomplete or malformed.");
        page_set("CHOS received a response that did not contain complete valid HTTP headers.\nTry Reload, or a smaller page.");
    } else if (is_redirect_status(status) && location[0]!='\0') {
        char target[BROWSER_URL_CAP];
        int rr=browser_resolve_reference(current_url,location,target,sizeof(target));
        if (redirects_followed>=BROWSER_MAX_REDIRECTS) {
            status_set("Redirect limit reached.");
            page_set("This page redirected too many times. Check the address and try again.");
            redirects_followed=0u;
        } else if (rr==BROWSER_URL_HTTPS_UNAVAILABLE) {
            status_set("Redirect requires HTTPS; navigation was blocked safely.");
            page_set("The server redirected to HTTPS, but TLS and certificate verification are not available in this build. CHOS did not downgrade the request.");
            redirects_followed=0u;
        } else if (rr!=BROWSER_URL_OK) {
            status_set("Invalid redirect target.");
            page_set("The server returned a redirect with an invalid or unsupported Location header.");
            redirects_followed=0u;
        } else {
            ++redirects_followed;
            /* A redirect replaces the current history entry, not a separate visit. */
            if (history_valid && history_index < history_count)
                chos_strlcpy(history[history_index],target,sizeof(history[history_index]));
            chos_strlcpy(deferred_url,target,sizeof(deferred_url));
            deferred_navigate=1; deferred_add_history=0;
            status_set("Following HTTP redirect...");
            page_set("Redirecting to the server-provided Location...");
            (void)set_title("CHOS Web Browser - Redirecting");
        }
        return;
    } else if (status < 200 || status >= 300) {
        char notice[128]; size_t n=0u;
        const char prefix[]="HTTP response status: ";
        for (size_t i=0u;i<sizeof(prefix)-1u;++i) append_char(notice,sizeof(notice),&n,prefix[i]);
        append_char(notice,sizeof(notice),&n,(char)('0'+(status/100)%10));
        append_char(notice,sizeof(notice),&n,(char)('0'+(status/10)%10));
        append_char(notice,sizeof(notice),&n,(char)('0'+status%10));
        notice[n]='\0'; status_set(notice);
        if(status==404) page_set("Page not found (HTTP 404). Check the URL or return to the previous page.");
        else if(status==403) page_set("Access denied (HTTP 403). The server refused this request.");
        else if(status>=500) page_set("The remote server reported an error. Try again later.");
        else page_set("The server returned a non-success HTTP status code.");
        redirects_followed=0u;
    } else if (chunked) {
        size_t decoded=0u;
        int dr=browser_decode_chunked(response+offset,length,chunked_body,sizeof(chunked_body),&decoded);
        if (dr==1) render_received_text(chunked_body,decoded,is_html);
        else { render_received_text(response+offset,length,is_html); page_truncated=1; }
        status_set(dr==1 ? "HTTP response loaded (chunked transfer decoded)." : "Partial chunked response; transfer did not finish within the buffer.");
        redirects_followed=0u;
    } else {
        render_received_text(response+offset,length,is_html);
        status_set(truncated ? "Loaded partial page (response buffer limit reached)." : "HTTP response loaded.");
        redirects_followed=0u;
    }
    if (page_truncated) {
        size_t n=chos_strlen(page);
        const char tail[]="\n\n[Content truncated by CHOS browser preview limits.]";
        for(size_t i=0u;i<sizeof(tail)-1u && n+1u<sizeof(page);++i) page[n++]=tail[i];
        page[n]='\0'; page_length=n;
    }
    (void)set_title(status>=200 && status<300 ? "CHOS Web Browser - Page loaded" : "CHOS Web Browser - HTTP result");
}
static void check_expected_response_length(void) {
    int status=0,chunked=0,is_html=0,has_length=0;
    size_t off=0u,len=0u,bodylen=0u;
    char loc[BROWSER_URL_CAP];
    if (response_has_expected_total) return;
    if (browser_parse_http_response_ex(response,response_length,&status,&off,&len,&chunked,&is_html,
                                       loc,sizeof(loc),&bodylen,&has_length)!=1) return;
    if (!has_length || chunked || bodylen > sizeof(response)-off) return;
    response_expected_total=off+bodylen;
    response_has_expected_total=1;
}
static void close_connection_after_load(void) {
    if (socket_handle < 1) { phase=BROWSER_IDLE; return; }
    (void)chos_net_tcp_close(socket_handle);
    phase=BROWSER_CLOSING;
    closing_polls=0u;
}
static void poll_network(void) {
    if (phase==BROWSER_IDLE) {
        if (socket_handle>=1) {
            chos_tcp_socket_info_t ti;
            if (chos_net_tcp_status(socket_handle,&ti)==0 &&
                ((uint32_t)ti.state==0u || (uint32_t)ti.state==TCP_STATE_FAILED)) {
                (void)chos_net_tcp_close(socket_handle); socket_handle=-1;
                if (deferred_navigate) { char next[BROWSER_URL_CAP]; chos_strlcpy(next,deferred_url,sizeof(next)); deferred_navigate=0; navigation_is_redirect=(deferred_add_history==0); request_navigation(next,deferred_add_history); }
            }
        }
        return;
    }
    if (++total_polls > 2000u && phase!=BROWSER_CLOSING) {
        status_set("Network operation timed out."); page_set("The network operation timed out. Check DHCP/DNS status and try Reload.");
        close_connection_after_load(); return;
    }
    if (phase==BROWSER_DNS) {
        chos_dns_api_info_t d;
        if (chos_net_dns_status(&d)<0) return;
        if ((uint32_t)d.state==DNS_STATE_RESOLVED && ascii_eq(d.hostname,dns_expected_host)) {
            if (!start_tcp_connection(d.ipv4_address)) { page_set("DNS succeeded, but a TCP connection could not be started."); phase=BROWSER_IDLE; }
        } else if ((uint32_t)d.state==DNS_STATE_FAILED) {
            status_set("DNS lookup failed."); page_set("CHOS could not resolve this hostname. Check DNS configuration and try again."); phase=BROWSER_IDLE;
        }
        return;
    }
    if (phase==BROWSER_CLOSING) {
        chos_tcp_socket_info_t ti;
        if (closing_polls >= BROWSER_CLOSE_TIMEOUT_POLLS) {
            char next[BROWSER_URL_CAP];
            int follow = deferred_navigate;
            int add = deferred_add_history;
            if (follow) chos_strlcpy(next,deferred_url,sizeof(next));
            abort_current_connection();
            status_set("TCP shutdown timed out; the old socket was released.");
            if (follow) {
                navigation_is_redirect=(add==0);
                request_navigation(next,add);
            }
            return;
        }
        if (socket_handle<1 || chos_net_tcp_status(socket_handle,&ti)<0 ||
            (uint32_t)ti.state==0u || (uint32_t)ti.state==TCP_STATE_FAILED) {
            if (socket_handle>=1) (void)chos_net_tcp_close(socket_handle);
            socket_handle=-1; phase=BROWSER_IDLE; closing_polls=0u;
            if (deferred_navigate) { char next[BROWSER_URL_CAP]; chos_strlcpy(next,deferred_url,sizeof(next)); deferred_navigate=0; navigation_is_redirect=(deferred_add_history==0); request_navigation(next,deferred_add_history); }
        } else {
            /* Close can return EAGAIN while a data segment is still pending.
               Retry graceful close after the pending segment is serviced; otherwise
               the browser can remain forever in CLOSING with no new navigation. */
            if ((uint32_t)ti.state==TCP_STATE_ESTABLISHED || (uint32_t)ti.state==TCP_STATE_CLOSE_WAIT)
                (void)chos_net_tcp_close(socket_handle);
            ++closing_polls;
            if (closing_polls==400u) status_set("Waiting for TCP shutdown; the network peer may be slow to acknowledge close.");
        }
        return;
    }
    if (socket_handle<1) { phase=BROWSER_IDLE; status_set("No TCP socket is active."); return; }
    {
        chos_tcp_socket_info_t ti;
        long rc=chos_net_tcp_status(socket_handle,&ti);
        if (rc<0) { status_set("TCP status request failed."); close_connection_after_load(); return; }
        if ((uint32_t)ti.state==TCP_STATE_FAILED) {
            status_set("TCP connection failed or was reset."); page_set("CHOS could not complete the HTTP connection. Try Reload or another HTTP site."); close_connection_after_load(); return;
        }
        if (phase==BROWSER_CONNECTING) {
            if ((uint32_t)ti.state==TCP_STATE_ESTABLISHED || (uint32_t)ti.state==TCP_STATE_CLOSE_WAIT) {
                phase=BROWSER_SENDING; status_set("Connected; sending HTTP request...");
            } else { return; }
        }
        if (phase==BROWSER_SENDING) {
            char req[512]; size_t n=0u;
            const char *parts[8]; size_t i;
            parts[0]="GET "; parts[1]=parsed_url.path; parts[2]=" HTTP/1.0\r\nHost: ";
            parts[3]=parsed_url.host; parts[4]="\r\nConnection: close\r\nUser-Agent: CHOSBrowser/0.22.0\r\nAccept: text/html,text/plain\r\n\r\n";
            for (i=0u;i<4u;++i) for (size_t j=0u;parts[i][j]!='\0';++j) append_char(req,sizeof(req),&n,parts[i][j]);
            if (parsed_url.port != 80u) {
                char digits[5]; size_t digit_count=0u; uint16_t port=parsed_url.port;
                append_char(req,sizeof(req),&n,':');
                do { digits[digit_count++]=(char)('0'+(port%10u)); port=(uint16_t)(port/10u); } while(port!=0u && digit_count<sizeof(digits));
                while(digit_count>0u) append_char(req,sizeof(req),&n,digits[--digit_count]);
            }
            for (size_t j=0u;parts[4][j]!='\0';++j) append_char(req,sizeof(req),&n,parts[4][j]);
            rc=chos_net_tcp_send(socket_handle,req,n);
            if (rc>0) { phase=BROWSER_RECEIVING; status_set("Request sent; waiting for HTTP response..."); }
            else if (rc!=-(long)CHOS_ERRNO_EAGAIN) { status_set("Could not queue HTTP request."); close_connection_after_load(); }
            return;
        }
        if (phase==BROWSER_RECEIVING) {
            int eof=0;
            for (;;) {
                size_t remaining=sizeof(response)-response_length;
                if (remaining==0u) { finish_http_response(1); close_connection_after_load(); return; }
                if (remaining>CHOS_TCP_API_MAX_TRANSFER) remaining=CHOS_TCP_API_MAX_TRANSFER;
                rc=chos_net_tcp_recv(socket_handle,tcp_read_buffer,remaining);
                if (rc>0) {
                    if ((size_t)rc>sizeof(response)-response_length) rc=(long)(sizeof(response)-response_length);
                    chos_memcpy(response+response_length,tcp_read_buffer,(size_t)rc); response_length+=(size_t)rc;
                    check_expected_response_length();
                    if (response_has_expected_total && response_length>=response_expected_total) {
                        response_length=response_expected_total;
                        finish_http_response(0); close_connection_after_load(); return;
                    }
                    if (response_length==sizeof(response)) { finish_http_response(1); close_connection_after_load(); return; }
                    continue;
                }
                if (rc==0) { eof=1; break; }
                if (rc==-(long)CHOS_ERRNO_EAGAIN) break;
                status_set("TCP receive failed."); page_set("The HTTP response could not be read completely."); close_connection_after_load(); return;
            }
            if (eof || (uint32_t)ti.state==TCP_STATE_CLOSE_WAIT) {
                finish_http_response(0); close_connection_after_load();
            }
        }
    }
}
static size_t page_line_count(void) {
    size_t i=0u, lines=0u, width=0u;
    if (page_is_bookmarks) return bookmark_count + 1u;
    if (!page_ready || page_length==0u) return 1u;
    while (i<page_length) {
        char c=page[i++];
        if (c=='\n') { ++lines; width=0u; }
        else if (++width>=BROWSER_LINE_WIDTH) { ++lines; width=0u; }
    }
    if (width!=0u || lines==0u) ++lines;
    return lines;
}
static size_t page_get_line(size_t wanted, char *out, size_t cap) {
    size_t i=0u, line=0u, n=0u;
    if (cap==0u) return 0u;
    out[0]='\0';
    if (!page_ready) return 0u;
    while (i<page_length && line<wanted) {
        char c=page[i++];
        if (c=='\n' || ++n>=BROWSER_LINE_WIDTH) { ++line; n=0u; }
    }
    while (i<page_length && n+1u<cap) {
        char c=page[i++];
        if (c=='\n') break;
        out[n++]=c;
        if (n>=BROWSER_LINE_WIDTH) break;
    }
    out[n]='\0'; return n;
}
static int page_get_line_range(size_t wanted, size_t *start_out, size_t *length_out) {
    size_t i=0u,line=0u,width=0u,start=0u;
    if(start_out==NULL||length_out==NULL||page_is_bookmarks||!page_ready)return 0;
    while(i<page_length&&line<wanted){
        char c=page[i++];
        if(c=='\n'){++line;width=0u;start=i;}
        else if(++width>=BROWSER_LINE_WIDTH){++line;width=0u;start=i;}
    }
    if(line!=wanted)return 0;
    i=start;width=0u;
    while(i<page_length&&page[i]!='\n'&&width<BROWSER_LINE_WIDTH){++i;++width;}
    *start_out=start;*length_out=i-start;return 1;
}
static int page_line_has_link(size_t wanted) {
    size_t start,length,i;
    if(!page_get_line_range(wanted,&start,&length))return 0;
    for(i=0u;i<page_link_count;++i) if(page_links[i].text_end>start&&page_links[i].text_start<start+length&&page_links[i].url[0]!='\0')return 1;
    return 0;
}
static size_t line_for_text_offset(size_t offset) {
    size_t i=0u,line=0u,width=0u;
    while(i<page_length && i<offset) {
        if(page[i++]=='\n') { ++line; width=0u; }
        else if(++width>=BROWSER_LINE_WIDTH) { ++line; width=0u; }
    }
    return line;
}
static void focus_next_link(void) {
    size_t tried, start;
    if(page_is_bookmarks || page_link_count==0u) {
        link_focus_active=0;
        status_set("No clickable links on this page.");
        return;
    }
    start=link_focus_active?(focused_link_index+1u)%page_link_count:0u;
    for(tried=0u;tried<page_link_count;++tried) {
        size_t candidate=(start+tried)%page_link_count;
        if(page_links[candidate].url[0]!='\0') {
            size_t line=line_for_text_offset(page_links[candidate].text_start);
            focused_link_index=candidate;
            link_focus_active=1;
            if(line<scroll_line) scroll_line=line;
            else if(line>=scroll_line+BROWSER_VISIBLE_LINES) scroll_line=line-BROWSER_VISIBLE_LINES+1u;
            status_set("Link focused; press Enter to open.");
            return;
        }
    }
    link_focus_active=0;
    status_set("No valid HTTP links on this page.");
}
static void open_focused_link(void) {
    char target[BROWSER_URL_CAP];
    if(!link_focus_active || focused_link_index>=page_link_count || page_links[focused_link_index].url[0]=='\0') return;
    chos_strlcpy(target,page_links[focused_link_index].url,sizeof(target));
    link_focus_active=0; address_focused=0;
    request_navigation(target,1);
}
static int open_link_at(int32_t x,int32_t y) {
    size_t line,start,length,column,offset,i;
    if(page_is_bookmarks||x<14||y<80||y>=(int32_t)(80u+BROWSER_VISIBLE_LINES*19u))return 0;
    line=scroll_line+(size_t)((y-80)/19);
    if(!page_get_line_range(line,&start,&length))return 0;
    column=(size_t)(x-14)/8u;
    if(column<length) {
        offset=start+column;
        for(i=0u;i<page_link_count;++i){
            if(page_links[i].url[0]!='\0'&&offset>=page_links[i].text_start&&offset<page_links[i].text_end){
                char target[BROWSER_URL_CAP];chos_strlcpy(target,page_links[i].url,sizeof(target));
                address_focused=0; link_focus_active=0; request_navigation(target,1);return 1;
            }
        }
    }
    /* UI text uses proportional glyph advances, while the compact browser
       cannot query those advances through its user ABI. If the exact byte
       hit-test misses, make a line containing an anchor clickable as a
       fallback (first link on that line). */
    for(i=0u;i<page_link_count;++i){
        if(page_links[i].url[0]!='\0'&&page_links[i].text_end>start&&page_links[i].text_start<start+length){
            char target[BROWSER_URL_CAP];chos_strlcpy(target,page_links[i].url,sizeof(target));
            address_focused=0; request_navigation(target,1);return 1;
        }
    }
    return 0;
}

static void draw_button(int x, int y, int w, const char *label, int disabled) {
    (void)fill_rect(window_id,x,y,(uint32_t)w,28u,0xffc0c0c0u);
    (void)fill_rect(window_id,x,y,(uint32_t)w,1u,0xffffffffu);
    (void)fill_rect(window_id,x,y,1u,28u,0xffffffffu);
    (void)fill_rect(window_id,x,y+27,(uint32_t)w,1u,0xff404040u);
    (void)fill_rect(window_id,x+w-1,y,1u,28u,0xff404040u);
    (void)draw_text(window_id,x+5,y+8,label,disabled?0xff808080u:0xff000000u);
}
static void redraw(void) {
    char line[BROWSER_LINE_WIDTH+1u];
    char shown_url[70];
    size_t i, n;
    chos_gui_message_t m;
    message_begin(&m,CHOS_GUI_MSG_CLEAR,sizeof(m.payload.clear));
    m.payload.clear.window_id=window_id; m.payload.clear.background_argb=0xfff0f0f0u;
    if (submit_message(&m)!=0) return;
    (void)fill_rect(window_id,0,0,BROWSER_WINDOW_W,40u,0xffc0c0c0u);
    draw_button(8,6,34,"<",!history_valid||history_index==0u);
    draw_button(46,6,34,">",!history_valid||history_index+1u>=history_count);
    draw_button(84,6,50,"Reload",0);
    (void)fill_rect(window_id,140,6,422u,28u,0xffffffffu);
    (void)fill_rect(window_id,140,6,422u,1u,address_focused?0xff000080u:0xff808080u);
    (void)fill_rect(window_id,140,6,1u,28u,address_focused?0xff000080u:0xff808080u);
    chos_strlcpy(shown_url,address,sizeof(shown_url));
    n=chos_strlen(address);
    if (address_focused && n>30u) chos_strlcpy(shown_url,address+n-30u,sizeof(shown_url));
    else if(n>52u) { shown_url[49]='.';shown_url[50]='.';shown_url[51]='.';shown_url[52]='\0'; }
    (void)draw_text(window_id,146,14,shown_url,0xff000000u);
    message_begin(&m,CHOS_GUI_MSG_SET_CARET,sizeof(m.payload.caret));
    m.payload.caret.window_id=window_id;
    m.payload.caret.x=147; m.payload.caret.y=15;
    m.payload.caret.width=1; m.payload.caret.height=14;
    m.payload.caret.argb=0xff000000u; m.payload.caret.enabled=address_focused!=0;
    m.payload.caret.text_length=(uint32_t)chos_strlen(shown_url);
    chos_memcpy(m.payload.caret.text,shown_url,m.payload.caret.text_length);
    (void)submit_message(&m);
    draw_button(566,6,43,"Go",0); draw_button(613,6,44,"Star",0); draw_button(661,6,91,"Marks",0);
    (void)draw_text(window_id,10,48,status_line,0xff404040u);
    (void)fill_rect(window_id,8,68,BROWSER_WINDOW_W-16u,1u,0xff808080u);
    (void)fill_rect(window_id,8,69,BROWSER_WINDOW_W-16u,BROWSER_FOOTER_LINE_Y-69u,0xffffffffu);
    if (page_is_bookmarks) {
        (void)draw_text(window_id,14,80,"Saved bookmarks - select a row to open",0xff202060u);
        for(i=0u;i+scroll_line<bookmark_count && i+1u<BROWSER_VISIBLE_LINES;++i) {
            (void)fill_rect(window_id,10,98+(int32_t)i*19,BROWSER_WINDOW_W-20u,18u,
                            ((i+scroll_line)&1u)!=0u?0xfff4f4f4u:0xffffffffu);
            if (bookmark_entries[scroll_line+i][0] != '\0')
                (void)draw_text(window_id,14,101+(int32_t)i*19,bookmark_entries[scroll_line+i],0xff101060u);
        }
        if(bookmark_count==0u) (void)draw_text(window_id,14,105,"No saved bookmarks yet. Load a page and press Star.",0xff404040u);
    } else {
        for(i=0u;i<BROWSER_VISIBLE_LINES;++i) {
            size_t line_number=scroll_line+i;
            size_t line_len=page_get_line(line_number,line,sizeof(line));
            if(line_len!=0u) {
                int has_link=page_line_has_link(line_number);
                int focused=0;
                int heading=(line_len>=2u && line[0]=='#' && line[1]==' ');
                int list_item=(line_len>=2u && line[0]=='*' && line[1]==' ');
                const char *display_line=line;
                if(link_focus_active && focused_link_index<page_link_count) {
                    size_t start,length;
                    if(page_get_line_range(line_number,&start,&length) &&
                       page_links[focused_link_index].text_start>=start && page_links[focused_link_index].text_start<start+length) focused=1;
                }
                if(focused) (void)fill_rect(window_id,10,80+(int32_t)i*19,BROWSER_WINDOW_W-20u,18u,0xffe3edffu);
                if(heading) {
                    display_line=line+2; /* Use semantic markers for style, not as visible punctuation. */
                    (void)draw_text_font(window_id,14,80+(int32_t)i*19,display_line,0xff18396bu,1u);
                } else if(list_item) {
                    (void)draw_text(window_id,14,80+(int32_t)i*19,"-",0xff404040u);
                    display_line=line+2;
                    (void)draw_text_font(window_id,26,80+(int32_t)i*19,display_line,0xff303030u,0u);
                } else {
                    (void)draw_text_font(window_id,14,80+(int32_t)i*19,line,has_link?0xff0000b0u:0xff101010u,0u);
                }
                if(has_link) {
                    uint32_t underline_width=(uint32_t)(line_len*7u);
                    if(underline_width>BROWSER_WINDOW_W-30u) underline_width=BROWSER_WINDOW_W-30u;
                    (void)fill_rect(window_id,14,95+(int32_t)i*19,underline_width,1u,0xff0000b0u);
                }
            }
        }
    }
    (void)fill_rect(window_id,8,(int32_t)BROWSER_FOOTER_LINE_Y,BROWSER_WINDOW_W-16u,1u,0xff808080u);
    (void)draw_text(window_id,10,(int32_t)BROWSER_FOOTER_TEXT_Y,
        "HTTP preview | links and redirects supported; images use alt text; HTTPS blocked",0xff505050u);
    if (page_truncated) (void)draw_text(window_id,540,(int32_t)BROWSER_FOOTER_TEXT_Y,"Truncated",0xff800000u);
}
static char key_to_ascii(uint32_t key, uint32_t modifiers) {
    static const char digits[]="1234567890";
    static const char shifted_digits[]="!@#$%^&*()";
    static const char row_q[]="qwertyuiop", row_a[]="asdfghjkl", row_z[]="zxcvbnm";
    int shift=(modifiers&1u)!=0u, caps=(modifiers&4u)!=0u;
    char c=0;
    if(key>=2u&&key<=11u) c=shift?shifted_digits[key-2u]:digits[key-2u];
    else if(key>=16u&&key<=25u) c=row_q[key-16u];
    else if(key>=30u&&key<=38u) c=row_a[key-30u];
    else if(key>=44u&&key<=50u) c=row_z[key-44u];
    else if(key==57u) c=' ';
    else if(key==12u) c=shift?'_':'-'; else if(key==13u) c=shift?'+':'=';
    else if(key==26u) c=shift?'{':'['; else if(key==27u) c=shift?'}':']';
    else if(key==39u) c=shift?':':';'; else if(key==40u) c=shift?'"':'\'';
    else if(key==41u) c=shift?'~':'`'; else if(key==43u||key==86u) c=shift?'|':'\\';
    else if(key==51u) c=shift?'<':','; else if(key==52u) c=shift?'>':'.'; else if(key==53u) c=shift?'?':'/';
    if(c>='a'&&c<='z'&&(shift^caps)) c=(char)(c-('a'-'A'));
    return c;
}
static void show_bookmarks(void) {
    long fd, rc;
    size_t length=0u, pos=0u;
    stop_current_fetch();
    bookmark_count=0u;
    fd=chos_open(BROWSER_BOOKMARK_PATH);
    if (fd<0) {
        page_is_bookmarks=1;
        page_ready=1;
        page_length=0u;
        scroll_line=0u;
        status_set("No saved bookmarks file exists yet.");
        (void)set_title("CHOS Web Browser - Bookmarks");
        return;
    }
    while (length+1u<sizeof(bookmark_buffer)) {
        rc=chos_read((int)fd,bookmark_buffer+length,sizeof(bookmark_buffer)-length-1u);
        if (rc<=0) break;
        length+=(size_t)rc;
    }
    (void)chos_close((int)fd);
    bookmark_buffer[length]='\0';
    while (pos<length) {
        size_t start=pos, n;
        while(pos<length && bookmark_buffer[pos]!='\n' && bookmark_buffer[pos]!='\r') ++pos;
        n=pos-start;
        while(pos<length && (bookmark_buffer[pos]=='\n' || bookmark_buffer[pos]=='\r')) ++pos;
        if (n==0u) continue;
        /* Keep the most recent bounded set, newest at the bottom. */
        if (bookmark_count==BROWSER_HISTORY) {
            size_t j;
            for(j=1u;j<BROWSER_HISTORY;++j) chos_strlcpy(bookmark_entries[j-1u],bookmark_entries[j],sizeof(bookmark_entries[0]));
            --bookmark_count;
        }
        if(n>=sizeof(bookmark_entries[0])) n=sizeof(bookmark_entries[0])-1u;
        chos_memcpy(bookmark_entries[bookmark_count],bookmark_buffer+start,n);
        bookmark_entries[bookmark_count][n]='\0';
        ++bookmark_count;
    }
    page_is_bookmarks=1;
    page_ready=1;
    page_length=0u;
    scroll_line=0u;
    status_set("Saved bookmarks; click a row to navigate.");
    (void)set_title("CHOS Web Browser - Bookmarks");
}

static void save_bookmark(void) {
    long fd; size_t len=0u, i; long rc;
    if (current_url[0]=='\0') { status_set("Load a page before bookmarking it."); return; }
    fd=chos_open(BROWSER_BOOKMARK_PATH);
    if (fd>=0) {
        while (len+1u<sizeof(bookmark_buffer)) {
            rc=chos_read((int)fd,bookmark_buffer+len,sizeof(bookmark_buffer)-len-1u);
            if (rc<=0) break; len+=(size_t)rc;
        }
        (void)chos_close((int)fd);
    }
    if (len+chos_strlen(current_url)+2u>=sizeof(bookmark_buffer)) { status_set("Bookmark file is full."); return; }
    /* Do not duplicate the most recent bookmark line.  Compare its bounded
       length first so a nearly-full bookmark file cannot make this scan read
       past the end of bookmark_buffer. */
    if (len>0u) {
        size_t start=len, url_len=chos_strlen(current_url), last_len;
        while(start>0u&&bookmark_buffer[start-1u]!='\n') --start;
        last_len=len-start;
        if (last_len>0u && bookmark_buffer[len-1u]=='\n') --last_len;
        if (last_len==url_len && url_len<=len-start &&
            chos_strncmp(bookmark_buffer+start,current_url,url_len)==0) {
            status_set("Already bookmarked."); return;
        }
    }
    for(i=0u;current_url[i]!='\0';++i) bookmark_buffer[len++]=current_url[i];
    bookmark_buffer[len++]='\n'; bookmark_buffer[len]='\0';
    rc=chos_write_file(BROWSER_BOOKMARK_PATH,bookmark_buffer,len);
    status_set(rc<0?"Could not save bookmark in Documents.":"Bookmark saved to Documents/CHOS-BOOKMARKS.TXT.");
}
static void handle_pointer(const chos_gui_message_t *event) {
    int32_t x=event->payload.pointer_event.x, y=event->payload.pointer_event.y;
    uint32_t flags=event->payload.pointer_event.event_flags;
    if ((flags&CHOS_GUI_EVENT_POINTER_SCROLL)!=0u) {
        if(event->payload.pointer_event.delta_y>0 && scroll_line>0u) scroll_line-=scroll_line>2u?2u:scroll_line;
        else if(event->payload.pointer_event.delta_y<0 && scroll_line+BROWSER_VISIBLE_LINES<page_line_count()) scroll_line+=2u;
        return;
    }
    if((flags&CHOS_GUI_EVENT_POINTER_DOWN)==0u) return;
    if(y>=6&&y<35) {
        if(x>=8&&x<42&&history_valid&&history_index>0u) { --history_index; request_navigation(history[history_index],0); address_focused=0; }
        else if(x>=46&&x<80&&history_valid&&history_index+1u<history_count) { ++history_index; request_navigation(history[history_index],0); address_focused=0; }
        else if(x>=84&&x<134&&current_url[0]!='\0') { request_navigation(current_url,0); address_focused=0; }
        else if(x>=140&&x<562) address_focused=1;
        else if(x>=566&&x<609) { address_focused=0; navigate_from_address(1); }
        else if(x>=613&&x<657) { address_focused=0; save_bookmark(); }
        else if(x>=661&&x<752) { address_focused=0; show_bookmarks(); }
    } else address_focused=0;
    if(y>=80&&y<(int32_t)(BROWSER_WINDOW_H-35u)) {
        if (page_is_bookmarks && y >= 98 && x >= 10 && x < (int32_t)BROWSER_WINDOW_W-10) {
            size_t selected = scroll_line + (size_t)((y-98)/19);
            if (selected < bookmark_count) {
                char target[BROWSER_URL_CAP];
                chos_strlcpy(target,bookmark_entries[selected],sizeof(target));
                address_focused=0;
                request_navigation(target,1);
                return;
            }
        } else if (!page_is_bookmarks && open_link_at(x,y)) {
            return;
        }
        if(y<115&&x>=8) address_focused=0;
    }
}
static int create_window(void) {
    message_begin(&message,CHOS_GUI_MSG_CREATE_WINDOW,sizeof(message.payload.create_window));
    message.payload.create_window.x=20; message.payload.create_window.y=10;
    message.payload.create_window.width=BROWSER_WINDOW_W; message.payload.create_window.height=BROWSER_WINDOW_H;
    message.payload.create_window.background_argb=0xfff0f0f0u;
    { long r=submit_message(&message); if(r<100||r>103) return 0; window_id=(uint32_t)r; }
    (void)set_title(app_title);
    message_begin(&message,CHOS_GUI_MSG_SHOW_WINDOW,sizeof(message.payload.window));
    message.payload.window.window_id=window_id;
    return submit_message(&message)==0;
}
static void destroy_window(void) {
    abort_current_connection();
    message_begin(&message,CHOS_GUI_MSG_DESTROY_WINDOW,sizeof(message.payload.window));
    message.payload.window.window_id=window_id; (void)submit_message(&message);
}
static void handle_key(const chos_gui_message_t *event) {
    uint32_t key=event->payload.key_event.keycode;
    uint32_t mod=event->payload.key_event.modifiers;
    char ch;
    if((event->payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)==0u) return;
    if(key==1u) { destroy_window(); chos_exit(0); return; }
    if((mod&2u)!=0u && (key==38u)) { address_focused=1; return; } /* Ctrl+L */
    if(address_focused) {
        size_t n=chos_strlen(address);
        if(key==28u) { navigate_from_address(1); address_focused=0; return; }
        if(key==14u) { if(n>0u) address[n-1u]='\0'; return; }
        if(key==15u) { address_focused=0; return; }
        ch=key_to_ascii(key,mod);
        if(ch>=32&&ch<=126&&n+1u<sizeof(address)) { address[n++]=ch; address[n]='\0'; }
        return;
    }
    if(key==15u) { focus_next_link(); return; } /* Tab cycles focus to the next link. */
    if(key==28u && link_focus_active) { open_focused_link(); return; }
    if(key==103u) { if(scroll_line>0u) --scroll_line; return; }
    if(key==108u) { if(scroll_line+BROWSER_VISIBLE_LINES<page_line_count()) ++scroll_line; return; }
    if(key==104u) { scroll_line=scroll_line>BROWSER_VISIBLE_LINES?scroll_line-BROWSER_VISIBLE_LINES:0u; return; }
    if(key==109u) { if(scroll_line+BROWSER_VISIBLE_LINES<page_line_count()) scroll_line+=BROWSER_VISIBLE_LINES; return; }
    if(key==102u) { scroll_line=0u; return; }
    if(key==107u) { size_t lines=page_line_count(); scroll_line=lines>BROWSER_VISIBLE_LINES?lines-BROWSER_VISIBLE_LINES:0u; return; }
}
#ifdef CHOS_KERNEL_BROWSER
int chos_kernel_browser_start(void) {
    if (kernel_browser_active) return 1;
    if (!create_window()) return 0;
    kernel_browser_active=1;
    page_set("Welcome to CHOS Web Browser.\n\nEnter a hostname or http:// URL in the address bar and press Enter.\n\nThe browser supports HTTP redirects, clickable HTML links, scrolling, basic semantic text layout, and safe image-alt placeholders. JavaScript, full CSS layout, and image decoding are not available. HTTPS remains blocked until TLS and certificate verification are integrated.");
    redraw();
    return 1;
}
void chos_kernel_browser_poll(void) {
    chos_gui_message_t event;
    if (!kernel_browser_active) return;
    /* One event and one bounded network step per desktop tick. */
    int received=chos_gui_poll_event(&event,sizeof(event))>0;
    if (received) {
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST) { destroy_window(); chos_exit(0); return; }
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT) handle_key(&event);
        else if(event.header.type==CHOS_GUI_MSG_POINTER_EVENT) handle_pointer(&event);
    }
    if (!kernel_browser_active) return;
    browser_phase_t old_phase=phase;
    size_t old_length=response_length;
    poll_network();
    if(received || old_phase!=phase || old_length!=response_length)redraw();
}
#else
int main(int argc, char **argv) {
    chos_gui_message_t event;
    (void)argc;(void)argv;
    if(!create_window()) chos_exit(2);
    page_set("Welcome to CHOS Web Browser.\n\nEnter a hostname or http:// URL in the address bar and press Enter.\n\nThe browser supports HTTP redirects, clickable HTML links, scrolling, basic semantic text layout, and safe image-alt placeholders. JavaScript, full CSS layout, and image decoding are not available. HTTPS remains blocked until TLS and certificate verification are integrated.");
    redraw();
    for(;;) {
        long got=chos_gui_poll_event(&event,sizeof(event));
        if(got>0) {
            if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST) { destroy_window(); chos_exit(0); return 0; }
            if(event.header.type==CHOS_GUI_MSG_KEY_EVENT) handle_key(&event);
            else if(event.header.type==CHOS_GUI_MSG_POINTER_EVENT) handle_pointer(&event);
        }
        browser_phase_t old_phase=phase;
        size_t old_length=response_length;
        poll_network();
        if(got>0 || old_phase!=phase || old_length!=response_length)redraw();
        (void)chos_sleep_ticks(1u);
    }
}

#endif
