#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../userland/examples/browser_core.h"

static void test_url(void) {
    browser_url_t u;
    assert(browser_parse_url("example.com", &u) == BROWSER_URL_OK);
    assert(strcmp(u.host, "example.com") == 0);
    assert(strcmp(u.path, "/") == 0 && u.port == 80 && !u.secure && !u.is_ipv4_literal);
    {
        char normalized[256];
        assert(browser_normalize_url("example.com", normalized, sizeof(normalized)) == BROWSER_URL_OK);
        assert(strcmp(normalized, "http://example.com/") == 0);
        assert(browser_normalize_url("HTTP://Example.COM:8080/a/../b?q=1#frag", normalized, sizeof(normalized)) == BROWSER_URL_OK);
        assert(strcmp(normalized, "http://example.com:8080/b?q=1") == 0);
    }
    assert(browser_parse_url("HTTP://Example.COM:8080/path?q=1#frag", &u) == BROWSER_URL_OK);
    assert(strcmp(u.host, "example.com") == 0 && strcmp(u.path, "/path?q=1") == 0 && u.port == 8080u);
    assert(browser_parse_url("http://192.0.2.44:8000?x=y", &u) == BROWSER_URL_OK);
    assert(u.is_ipv4_literal && u.ipv4 == 0xc000022cu && strcmp(u.path, "/?x=y") == 0);
    assert(browser_parse_url("https://example.com/", &u) == BROWSER_URL_OK);
    assert(u.secure && u.port == 443u && strcmp(u.host, "example.com") == 0);
    assert(!browser_url_transport_available(&u, 0));
    assert(browser_url_transport_available(&u, 1));
    {
        char normalized[256];
        assert(browser_normalize_url("https://EXAMPLE.com/a/../secure", normalized, sizeof(normalized)) == BROWSER_URL_OK);
        assert(strcmp(normalized, "https://example.com/secure") == 0);
        assert(browser_parse_url("https://example.com:8443/", &u) == BROWSER_URL_OK);
        assert(u.secure && u.port == 8443u);
    }
    assert(browser_parse_url("ftp://example.com/", &u) == BROWSER_URL_SCHEME_UNSUPPORTED);
    assert(browser_parse_url("http://user@example.com/", &u) == BROWSER_URL_INVALID);
    assert(browser_parse_url("http://example.com:0/", &u) == BROWSER_URL_INVALID);
    assert(browser_parse_url("http://-bad.example/", &u) == BROWSER_URL_INVALID);
    assert(browser_parse_url("http://example..com/", &u) == BROWSER_URL_INVALID);
    assert(!browser_parse_ipv4("300.0.0.1", &(uint32_t){0}));
}
static void test_http_and_html(void) {
    static const uint8_t response[] =
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 96\r\nConnection: close\r\n\r\n"
        "<!doctype html><html><head><title>Test</title><script>ignore_me()</script></head>"
        "<body><h1>Example &amp; Domain</h1><p>Hello <b>CHOS</b></p></body></html>";
    int status, chunked, is_html;
    size_t body_offset, body_length, text_length;
    char text[512];
    assert(browser_parse_http_response(response, sizeof(response)-1u, &status,
        &body_offset, &body_length, &chunked, &is_html) == 1);
    assert(status == 200 && !chunked && is_html);
    text_length = browser_html_to_text(response + body_offset, body_length, text, sizeof(text));
    assert(text_length > 0u);
    assert(strstr(text, "Example & Domain") != NULL);
    assert(strstr(text, "Hello CHOS") != NULL);
    assert(strstr(text, "ignore_me") == NULL);
    assert(strstr(text, "Test") != NULL);
    assert(browser_body_looks_like_html((const uint8_t *)"  <html><body>x", 15u));
    assert(!browser_body_looks_like_html((const uint8_t *)"plain < not markup", 18u));
    {
        static const uint8_t malformed_entity[] = {'x',' ','&','a','m','p'};
        char bounded[16];
        size_t bounded_len = browser_html_to_text(malformed_entity, sizeof(malformed_entity), bounded, sizeof(bounded));
        assert(bounded_len == sizeof(malformed_entity));
        assert(memcmp(bounded, "x &amp", sizeof(malformed_entity)) == 0);
    }
}
static void test_chunked(void) {
    static const uint8_t chunks[] = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    static const uint8_t incomplete[] = "5\r\nabc";
    static const uint8_t bad[] = "Z\r\nnope\r\n0\r\n\r\n";
    uint8_t out[64]; size_t n;
    assert(browser_decode_chunked(chunks, sizeof(chunks)-1u, out, sizeof(out), &n) == 1);
    assert(n == 9u && memcmp(out, "Wikipedia", 9u) == 0);
    assert(browser_decode_chunked(incomplete, sizeof(incomplete)-1u, out, sizeof(out), &n) == 0);
    assert(browser_decode_chunked(bad, sizeof(bad)-1u, out, sizeof(out), &n) == -1);
}

static void test_redirects_and_content_length(void) {
    static const uint8_t redirect[] =
        "HTTP/1.1 302 Found\r\nLocation: ../new/page?x=1&amp;y=2\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    static const uint8_t conflicting[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\nabc";
    int status, chunked, is_html, has_length;
    size_t off, body_len, content_length;
    char location[256], url[256];
    assert(browser_parse_http_response_ex(redirect,sizeof(redirect)-1u,&status,&off,&body_len,
        &chunked,&is_html,location,sizeof(location),&content_length,&has_length)==1);
    assert(status==302 && !chunked && has_length && content_length==0u);
    assert(strcmp(location,"../new/page?x=1&amp;y=2")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html",location,url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"http://example.com/a/new/page?x=1&amp;y=2")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","/redirected",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"http://example.com/redirected")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","next.html",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"http://example.com/a/b/next.html")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","?new=1#ignored",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"http://example.com/a/b/index.html?new=1")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","//other.example/path",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"http://other.example/path")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","https://example.com/secure",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"https://example.com/secure")==0);
    assert(browser_resolve_reference("https://example.com/a/b/index.html","//other.example/path",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"https://other.example/path")==0);
    assert(browser_resolve_reference("https://example.com/a/b/index.html","next.html",url,sizeof(url))==BROWSER_URL_OK);
    assert(strcmp(url,"https://example.com/a/b/next.html")==0);
    assert(browser_resolve_reference("http://example.com/a/b/index.html","javascript:alert(1)",url,sizeof(url))==BROWSER_URL_SCHEME_UNSUPPORTED);
    assert(browser_parse_http_response_ex(conflicting,sizeof(conflicting)-1u,&status,&off,&body_len,
        &chunked,&is_html,location,sizeof(location),&content_length,&has_length)==-1);
    {
        const int statuses[] = {301,302,303,307,308};
        char fixture[160];
        size_t i;
        for (i=0u;i<sizeof(statuses)/sizeof(statuses[0]);++i) {
            int used=snprintf(fixture,sizeof(fixture),
                "HTTP/1.1 %d Redirect\r\nLocation: /target\r\nContent-Length: 0\r\n\r\n",statuses[i]);
            assert(used>0 && (size_t)used<sizeof(fixture));
            assert(browser_parse_http_response_ex((const uint8_t *)fixture,(size_t)used,&status,&off,&body_len,
                &chunked,&is_html,location,sizeof(location),&content_length,&has_length)==1);
            assert(status==statuses[i] && strcmp(location,"/target")==0 && has_length && content_length==0u);
        }
    }
}
static void test_html_links_and_images(void) {
    static const uint8_t html[] = "<html><body><h1>Docs</h1><p>Read <a href=\"/guide/start.html\">the guide</a> or <a href=\"/search?q=a&amp;b=2\">search</a>.</p><ul><li>First</li><li>Second</li></ul><img alt=\"Diagram\" src=\"/img.png\"><script>bad()</script></body></html>";
    char text[512]; browser_link_t links[BROWSER_LINK_MAX]; size_t count=0u,n;
    n=browser_html_to_text_ex(html,sizeof(html)-1u,text,sizeof(text),links,BROWSER_LINK_MAX,&count);
    assert(n>0u && count==2u);
    assert(strstr(text,"# Docs")!=NULL);
    assert(strstr(text,"the guide")!=NULL);
    assert(strstr(text,"* First")!=NULL && strstr(text,"* Second")!=NULL);
    assert(strstr(text,"[Image: Diagram]")!=NULL);
    assert(strstr(text,"bad()") == NULL);
    assert(links[0].text_start < links[0].text_end);
    assert(strncmp(text+links[0].text_start,"the guide",9u)==0);
    assert(strcmp(links[0].url,"/guide/start.html")==0);
    assert(strcmp(links[1].url,"/search?q=a&b=2")==0);
    assert(links[1].text_start < links[1].text_end);
}
int main(void) {
    test_url(); test_http_and_html(); test_chunked(); test_redirects_and_content_length(); test_html_links_and_images();
    puts("Browser core: URL validation/resolution, redirect and content-length parsing, chunked decoding, clickable-link extraction, image-alt placeholders and safe HTML text extraction passed");
    return 0;
}
