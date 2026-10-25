#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/net_services.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static size_t make_dns_response(uint8_t *packet, size_t capacity) {
    size_t p = 0u;
    static const uint8_t name[] = {7u, 'e','x','a','m','p','l','e', 3u, 'c','o','m', 0u};
    if (capacity < 45u) return 0u;
    memset(packet, 0, capacity);
    put16(&packet[0], 0xbeefu); put16(&packet[2], 0x8180u);
    put16(&packet[4], 1u); put16(&packet[6], 1u);
    p = 12u;
    memcpy(&packet[p], name, sizeof(name)); p += sizeof(name);
    put16(&packet[p], 1u); put16(&packet[p + 2u], 1u); p += 4u;
    packet[p++] = 0xc0u; packet[p++] = 0x0cu; /* Name points to QNAME. */
    put16(&packet[p], 1u); put16(&packet[p + 2u], 1u); put32(&packet[p + 4u], 300u);
    put16(&packet[p + 8u], 4u); p += 10u;
    packet[p++] = 93u; packet[p++] = 184u; packet[p++] = 216u; packet[p++] = 34u;
    return p;
}

static void test_aaaa(void){
 uint8_t p[512],out[16],expected[16]={0x20,1,0x0d,0xb8};expected[15]=7;uint32_t ttl=0;
 size_t q=chos_net_build_dns_aaaa_query(p,sizeof(p),0x2468,"example.com");assert(q==29&&p[q-3]==28);
 put16(p+2,0x8180);put16(p+6,1);size_t n=q;
 p[n++]=0xc0;p[n++]=12;put16(p+n,28);put16(p+n+2,1);put32(p+n+4,300);put16(p+n+8,16);n+=10;
 memcpy(p+n,expected,16);n+=16;
 assert(chos_net_parse_dns_aaaa_response(p,n,0x2468,"ExAmPlE.Com.",out,&ttl)==1 && !memcmp(out,expected,16)&&ttl==300);
 for(size_t i=0;i<n;i++)assert(!chos_net_parse_dns_aaaa_response(p,i,0x2468,"example.com",out,&ttl));
 assert(!chos_net_parse_dns_aaaa_response(p,n,0x2469,"example.com",out,&ttl));
 assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"unrelated.com",out,&ttl));
 p[q+1]=13;assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl));p[q+1]=12;
 p[n-16]=0xff;assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl));p[n-16]=0x20;
 p[2]|=2;assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl));p[2]&=(uint8_t)~2u;
 /* Complete CNAME chain, compressed name, minimum TTL and malformed targets. */
 q=chos_net_build_dns_aaaa_query(p,sizeof(p),0x2468,"example.com");put16(p+2,0x8180);put16(p+6,2);n=q;
 p[n++]=0xc0;p[n++]=12;put16(p+n,5);put16(p+n+2,1);put32(p+n+4,100);put16(p+n+8,8);n+=10;
 size_t cname=n;p[n++]=5;memcpy(p+n,"alias",5);n+=5;p[n++]=0xc0;p[n++]=12;
 p[n++]=0xc0;p[n++]=(uint8_t)cname;put16(p+n,28);put16(p+n+2,1);put32(p+n+4,300);put16(p+n+8,16);n+=10;memcpy(p+n,expected,16);n+=16;
 assert(chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl)==1&&!memcmp(out,expected,16)&&ttl==100);
 p[cname+7]=(uint8_t)cname;assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl));p[cname+7]=12;
 p[cname-2]=0; /* RDLENGTH low byte originally8 is at cname-1; preserve first byte. */
 p[cname-1]=7;assert(!chos_net_parse_dns_aaaa_response(p,n,0x2468,"example.com",out,&ttl));
 q=chos_net_build_dns_aaaa_query(p,sizeof(p),0x2468,"example.com");put16(p+2,0x8183);
 assert(chos_net_parse_dns_aaaa_response(p,q,0x2468,"example.com",out,&ttl)==-3);
 p[13]='z';assert(!chos_net_parse_dns_aaaa_response(p,q,0x2468,"example.com",out,&ttl));
 puts("DNS AAAA: exact question/owner, bounded compressed CNAME chain, minimum TTL, RCODE, invalid/truncated records passed");
}

int main(void) {
    test_aaaa();
    const uint8_t mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
    uint8_t discover[300], request[300], reply[320], dns[128], query[128];
    chos_dhcpv4_message_t dhcp;
    uint32_t address = 0u, ttl = 0u;
    size_t discover_length, request_length, reply_length, p, i, dns_length, query_length;

    discover_length = chos_net_build_dhcpv4_discover(discover, sizeof(discover), 0x12345678u, mac);
    assert(discover_length == 300u);
    assert(discover[0] == 1u && discover[1] == 1u && discover[2] == 6u);
    assert(discover[4] == 0x12u && discover[7] == 0x78u);
    assert(discover[10] == 0x80u && discover[11] == 0u);
    assert(memcmp(&discover[28], mac, sizeof(mac)) == 0);
    assert(discover[236] == 0x63u && discover[239] == 0x63u);
    assert(discover[240] == 53u && discover[241] == 1u && discover[242] == 1u);
    assert(discover[discover_length - 1u] == 255u);
    for (i = 258u; i < 299u; ++i) assert(discover[i] == 0u);
    assert(chos_net_build_dhcpv4_discover(discover, 299u, 1u, mac) == 0u);
    assert(chos_net_build_dhcpv4_discover(discover, sizeof(discover), 1u, NULL) == 0u);

    request_length = chos_net_build_dhcpv4_request(request, sizeof(request), 0x12345678u, mac,
                                                   0xc0000264u, 0xc0000202u);
    assert(request_length == 300u);
    assert(request[0] == 1u && request[4] == 0x12u && request[7] == 0x78u);
    assert(request[240] == 53u && request[241] == 1u && request[242] == 3u);
    assert(request[252] == 50u && request[253] == 4u);
    assert(request[254] == 192u && request[255] == 0u && request[256] == 2u && request[257] == 100u);
    assert(request[258] == 54u && request[259] == 4u);
    assert(request[260] == 192u && request[261] == 0u && request[262] == 2u && request[263] == 2u);
    for (i = 270u; i < 299u; ++i) assert(request[i] == 0u);
    assert(request[299] == 255u);
    assert(chos_net_build_dhcpv4_request(request, 299u, 1u, mac, 0xc0000264u, 0xc0000202u) == 0u);
    assert(chos_net_build_dhcpv4_request(request, sizeof(request), 1u, mac, 0u, 1u) == 0u);

    /* A valid bounded OFFER exercises the options relevant to initial IPv4 setup. */
    memcpy(reply, discover, discover_length);
    reply[0] = 2u;
    reply[16] = 192u; reply[17] = 0u; reply[18] = 2u; reply[19] = 100u;
    reply[240] = 53u; reply[241] = 1u; reply[242] = 2u; /* OFFER */
    p = 243u;
    reply[p++] = 1u; reply[p++] = 4u; reply[p++] = 255u; reply[p++] = 255u; reply[p++] = 255u; reply[p++] = 0u;
    reply[p++] = 3u; reply[p++] = 4u; reply[p++] = 192u; reply[p++] = 0u; reply[p++] = 2u; reply[p++] = 1u;
    reply[p++] = 6u; reply[p++] = 4u; reply[p++] = 192u; reply[p++] = 0u; reply[p++] = 2u; reply[p++] = 53u;
    reply[p++] = 51u; reply[p++] = 4u; reply[p++] = 0u; reply[p++] = 0u; reply[p++] = 0x0eu; reply[p++] = 0x10u;
    reply[p++] = 54u; reply[p++] = 4u; reply[p++] = 192u; reply[p++] = 0u; reply[p++] = 2u; reply[p++] = 2u;
    reply[p++] = 255u;
    reply_length = p;
    assert(chos_net_parse_dhcpv4_reply(reply, reply_length, 0x12345678u, &dhcp));
    assert(dhcp.message_type == 2u && dhcp.offered_address == 0xc0000264u);
    assert(dhcp.has_subnet_mask && dhcp.subnet_mask == 0xffffff00u);
    assert(dhcp.has_router && dhcp.router_address == 0xc0000201u);
    assert(dhcp.has_dns && dhcp.dns_address == 0xc0000235u);
    assert(dhcp.has_lease && dhcp.lease_seconds == 3600u);
    assert(dhcp.has_server_address && dhcp.server_address == 0xc0000202u);
    assert(!chos_net_parse_dhcpv4_reply(reply, reply_length, 0x87654321u, &dhcp));
    for (i = 0u; i < reply_length; ++i) assert(!chos_net_parse_dhcpv4_reply(reply, i, 0x12345678u, &dhcp));
    reply[p - 1u] = 53u; /* No end marker; must not accept a truncated options stream. */
    assert(!chos_net_parse_dhcpv4_reply(reply, reply_length, 0x12345678u, &dhcp));

    query_length = chos_net_build_dns_a_query(query, sizeof(query), 0x1234u, "ExAmPlE.Com", 1);
    assert(query_length == 29u);
    assert(query[0] == 0x12u && query[1] == 0x34u && query[2] == 0x01u && query[3] == 0x00u);
    assert(query[4] == 0u && query[5] == 1u);
    assert(query[12] == 7u && memcmp(&query[13], "example", 7u) == 0);
    assert(query[20] == 3u && memcmp(&query[21], "com", 3u) == 0);
    assert(query[24] == 0u && query[25] == 0u && query[26] == 1u && query[27] == 0u && query[28] == 1u);
    assert(chos_net_build_dns_a_query(query, sizeof(query), 2u, "example.com.", 0) == query_length);
    assert(query[2] == 0u && query[3] == 0u);
    assert(chos_net_build_dns_a_query(query, 28u, 1u, "example.com", 1) == 0u);
    assert(chos_net_build_dns_a_query(query, sizeof(query), 1u, "bad..name", 1) == 0u);
    assert(chos_net_build_dns_a_query(query, sizeof(query), 1u, ".example", 1) == 0u);

    dns_length = make_dns_response(dns, sizeof(dns));
    assert(dns_length == 45u);
    assert(chos_net_parse_dns_a_response(dns, dns_length, 0xbeefu, "ExAmPlE.Com", &address, &ttl));
    assert(address == 0x5db8d822u && ttl == 300u);
    assert(!chos_net_parse_dns_a_response(dns, dns_length, 0x1234u, "example.com", &address, &ttl));
    assert(!chos_net_parse_dns_a_response(dns, dns_length, 0xbeefu, "elsewhere.com", &address, &ttl));
    for (i = 0u; i < dns_length; ++i)
        assert(!chos_net_parse_dns_a_response(dns, i, 0xbeefu, "example.com", &address, &ttl));
    dns[29] = 0xc0u; dns[30] = 0xffu; /* Invalid forward compression reference. */
    assert(!chos_net_parse_dns_a_response(dns, dns_length, 0xbeefu, "example.com", &address, &ttl));
    puts("network services: DHCP Discover/Request/OFFER parsing and DNS A-query/response validation passed");
    return 0;
}
