#include <stddef.h>
#include <stdint.h>
#include "chos/net_services.h"

#define DHCP_FIXED_BYTES 236u
#define DHCP_MIN_MESSAGE_BYTES 300u
#define DHCP_COOKIE_BYTES 4u
#define DHCP_OPTIONS_OFFSET (DHCP_FIXED_BYTES + DHCP_COOKIE_BYTES)
#define DHCP_MAGIC_COOKIE 0x63825363u
#define DNS_HEADER_BYTES 12u
#define DNS_MAX_NAME_BYTES 255u
#define DNS_MAX_POINTER_DEPTH 16u
#define DNS_TYPE_A 1u
#define DNS_CLASS_IN 1u

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

size_t chos_net_build_dhcpv4_discover(void *output, size_t capacity,
                                      uint32_t transaction_id,
                                      const uint8_t mac_address[6]) {
    uint8_t *out = (uint8_t *)output;
    size_t i, pos = DHCP_OPTIONS_OFFSET;
    static const uint8_t request_list[] = {1u, 3u, 6u, 51u};
    if (out == NULL || mac_address == NULL || capacity < DHCP_MIN_MESSAGE_BYTES) return 0u;
    for (i = 0u; i < DHCP_MIN_MESSAGE_BYTES; ++i) out[i] = 0u;
    out[0] = 1u; /* BOOTREQUEST */
    out[1] = 1u; /* Ethernet */
    out[2] = 6u;
    write_be32(&out[4], transaction_id);
    write_be16(&out[10], 0x8000u); /* Broadcast until a server is known. */
    for (i = 0u; i < 6u; ++i) out[28u + i] = mac_address[i];
    write_be32(&out[DHCP_FIXED_BYTES], DHCP_MAGIC_COOKIE);
    out[pos++] = 53u; out[pos++] = 1u; out[pos++] = 1u; /* DHCPDISCOVER */
    out[pos++] = 61u; out[pos++] = 7u; out[pos++] = 1u; /* Ethernet client ID */
    for (i = 0u; i < 6u; ++i) out[pos++] = mac_address[i];
    out[pos++] = 55u; out[pos++] = (uint8_t)sizeof(request_list);
    for (i = 0u; i < sizeof(request_list); ++i) out[pos++] = request_list[i];
    /* Pad BOOTP/DHCP to the interoperable 300-byte minimum. Keep END last. */
    while (pos < DHCP_MIN_MESSAGE_BYTES - 1u) out[pos++] = 0u;
    out[pos++] = 255u;
    return pos;
}

size_t chos_net_build_dhcpv4_request(void *output, size_t capacity,
                                     uint32_t transaction_id,
                                     const uint8_t mac_address[6],
                                     uint32_t offered_address,
                                     uint32_t server_address) {
    uint8_t *out = (uint8_t *)output;
    size_t i, pos = DHCP_OPTIONS_OFFSET;
    static const uint8_t request_list[] = {1u, 3u, 6u, 51u};
    if (out == NULL || mac_address == NULL || offered_address == 0u || server_address == 0u ||
        capacity < DHCP_MIN_MESSAGE_BYTES) return 0u;
    for (i = 0u; i < DHCP_MIN_MESSAGE_BYTES; ++i) out[i] = 0u;
    out[0] = 1u; /* BOOTREQUEST */
    out[1] = 1u; /* Ethernet */
    out[2] = 6u;
    write_be32(&out[4], transaction_id);
    write_be16(&out[10], 0x8000u);
    for (i = 0u; i < 6u; ++i) out[28u + i] = mac_address[i];
    write_be32(&out[DHCP_FIXED_BYTES], DHCP_MAGIC_COOKIE);
    out[pos++] = 53u; out[pos++] = 1u; out[pos++] = 3u; /* DHCPREQUEST */
    out[pos++] = 61u; out[pos++] = 7u; out[pos++] = 1u;
    for (i = 0u; i < 6u; ++i) out[pos++] = mac_address[i];
    out[pos++] = 50u; out[pos++] = 4u; write_be32(&out[pos], offered_address); pos += 4u;
    out[pos++] = 54u; out[pos++] = 4u; write_be32(&out[pos], server_address); pos += 4u;
    out[pos++] = 55u; out[pos++] = (uint8_t)sizeof(request_list);
    for (i = 0u; i < sizeof(request_list); ++i) out[pos++] = request_list[i];
    /* Pad BOOTP/DHCP to the interoperable 300-byte minimum. Keep END last. */
    while (pos < DHCP_MIN_MESSAGE_BYTES - 1u) out[pos++] = 0u;
    out[pos++] = 255u;
    return pos;
}

int chos_net_parse_dhcpv4_reply(const void *payload, size_t length,
                                uint32_t expected_xid,
                                chos_dhcpv4_message_t *message) {
    const uint8_t *bytes = (const uint8_t *)payload;
    size_t pos;
    int found_message_type = 0, found_end = 0;
    chos_dhcpv4_message_t parsed;
    if (bytes == NULL || message == NULL || length < DHCP_OPTIONS_OFFSET + 1u) return 0;
    if (bytes[0] != 2u || bytes[1] != 1u || bytes[2] != 6u ||
        read_be32(&bytes[4]) != expected_xid || read_be32(&bytes[DHCP_FIXED_BYTES]) != DHCP_MAGIC_COOKIE)
        return 0;
    parsed.transaction_id = read_be32(&bytes[4]);
    parsed.offered_address = read_be32(&bytes[16]);
    parsed.server_address = 0u;
    parsed.subnet_mask = 0u;
    parsed.router_address = 0u;
    parsed.dns_address = 0u;
    parsed.lease_seconds = 0u;
    parsed.message_type = 0u;
    parsed.has_server_address = 0u;
    parsed.has_subnet_mask = 0u;
    parsed.has_router = 0u;
    parsed.has_dns = 0u;
    parsed.has_lease = 0u;
    pos = DHCP_OPTIONS_OFFSET;
    while (pos < length) {
        uint8_t code = bytes[pos++];
        uint8_t option_length;
        const uint8_t *value;
        if (code == 0u) continue; /* Pad. */
        if (code == 255u) { found_end = 1; break; }
        if (pos >= length) return 0;
        option_length = bytes[pos++];
        if ((size_t)option_length > length - pos) return 0;
        value = &bytes[pos];
        switch (code) {
            case 53u:
                if (option_length != 1u || found_message_type) return 0;
                parsed.message_type = value[0];
                if (parsed.message_type == 0u || parsed.message_type > 8u) return 0;
                found_message_type = 1;
                break;
            case 1u:
                if (option_length != 4u) return 0;
                parsed.subnet_mask = read_be32(value); parsed.has_subnet_mask = 1u;
                break;
            case 3u:
                if (option_length < 4u || (option_length & 3u) != 0u) return 0;
                parsed.router_address = read_be32(value); parsed.has_router = 1u;
                break;
            case 6u:
                if (option_length < 4u || (option_length & 3u) != 0u) return 0;
                parsed.dns_address = read_be32(value); parsed.has_dns = 1u;
                break;
            case 51u:
                if (option_length != 4u) return 0;
                parsed.lease_seconds = read_be32(value); parsed.has_lease = 1u;
                break;
            case 54u:
                if (option_length != 4u) return 0;
                parsed.server_address = read_be32(value); parsed.has_server_address = 1u;
                break;
            default:
                break; /* Unknown options are intentionally skipped. */
        }
        pos += option_length;
    }
    if (!found_message_type || !found_end) return 0;
    *message = parsed;
    return 1;
}

static int dns_name_read(const uint8_t *packet, size_t length, size_t *offset,
                         char *output, size_t capacity) {
    size_t cursor, output_length = 0u, resume_offset = 0u, steps = 0u, pointer_count = 0u;
    int jumped = 0;
    if (packet == NULL || offset == NULL || output == NULL || capacity == 0u || *offset >= length) return 0;
    cursor = *offset;
    output[0] = '\0';
    for (;;) {
        uint8_t label_length;
        if (cursor >= length || ++steps > length + DNS_MAX_POINTER_DEPTH) return 0;
        label_length = packet[cursor];
        if ((label_length & 0xc0u) == 0xc0u) {
            size_t pointer;
            if (cursor + 1u >= length || ++pointer_count > DNS_MAX_POINTER_DEPTH) return 0;
            pointer = ((size_t)(label_length & 0x3fu) << 8) | packet[cursor + 1u];
            if (pointer >= length || pointer >= cursor) return 0; /* Reject forward/cyclic pointers. */
            if (!jumped) resume_offset = cursor + 2u;
            jumped = 1;
            cursor = pointer;
            continue;
        }
        if ((label_length & 0xc0u) != 0u || label_length > 63u) return 0;
        ++cursor;
        if (label_length == 0u) {
            if (!jumped) resume_offset = cursor;
            *offset = resume_offset;
            output[output_length] = '\0';
            return 1;
        }
        if ((size_t)label_length > length - cursor ||
            output_length + (output_length ? 1u : 0u) + (size_t)label_length >= capacity ||
            output_length + (output_length ? 1u : 0u) + (size_t)label_length > DNS_MAX_NAME_BYTES) return 0;
        if (output_length != 0u) output[output_length++] = '.';
        {
            size_t i;
            for (i = 0u; i < (size_t)label_length; ++i) {
                uint8_t ch = packet[cursor++];
                if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 'a');
                output[output_length++] = (char)ch;
            }
        }
    }
}

static int dns_names_equal(const char *left, const char *right) {
    size_t i = 0u;
    if (left == NULL || right == NULL) return 0;
    while (left[i] != '\0' && right[i] != '\0') {
        unsigned char a = (unsigned char)left[i], b = (unsigned char)right[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
        ++i;
    }
    return left[i] == right[i];
}

size_t chos_net_build_dns_a_query(void *output, size_t capacity, uint16_t query_id,
                                  const char *hostname, int recursion_desired) {
    uint8_t *packet = (uint8_t *)output;
    size_t name_length = 0u, pos = DNS_HEADER_BYTES, label_start = 0u, label_length = 0u;
    size_t i;
    if (packet == NULL || hostname == NULL || hostname[0] == '\0') return 0u;
    while (hostname[name_length] != '\0') {
        unsigned char ch = (unsigned char)hostname[name_length];
        if (ch < 0x21u || ch > 0x7eu) return 0u;
        ++name_length;
        if (name_length > DNS_MAX_NAME_BYTES) return 0u;
    }
    if (hostname[name_length - 1u] == '.') --name_length; /* Accept FQDN notation. */
    if (name_length == 0u || name_length > 253u || capacity < DNS_HEADER_BYTES + 6u)
        return 0u;
    for (i = 0u; i < DNS_HEADER_BYTES; ++i) packet[i] = 0u;
    write_be16(&packet[0], query_id);
    write_be16(&packet[2], recursion_desired ? 0x0100u : 0u);
    write_be16(&packet[4], 1u); /* One question. */
    for (i = 0u; i <= name_length; ++i) {
        unsigned char ch = i == name_length ? (unsigned char)'.' : (unsigned char)hostname[i];
        if (ch == '.') {
            size_t current_label = i - label_start;
            if (current_label == 0u || current_label > 63u || pos + 1u + current_label > capacity)
                return 0u;
            packet[pos++] = (uint8_t)current_label;
            while (label_length < current_label) {
                unsigned char label_ch = (unsigned char)hostname[label_start + label_length];
                if (label_ch >= 'A' && label_ch <= 'Z') label_ch = (unsigned char)(label_ch - 'A' + 'a');
                packet[pos++] = label_ch;
                ++label_length;
            }
            label_start = i + 1u;
            label_length = 0u;
        }
    }
    if (pos + 5u > capacity) return 0u;
    packet[pos++] = 0u;
    write_be16(&packet[pos], DNS_TYPE_A); pos += 2u;
    write_be16(&packet[pos], DNS_CLASS_IN); pos += 2u;
    return pos;
}

int chos_net_parse_dns_a_response(const void *payload, size_t length,
                                  uint16_t expected_id, const char *expected_name,
                                  uint32_t *address_out, uint32_t *ttl_out) {
    const uint8_t *packet = (const uint8_t *)payload;
    uint16_t flags, questions, answers;
    size_t offset = DNS_HEADER_BYTES, i;
    char name[DNS_MAX_NAME_BYTES + 1u];
    if (packet == NULL || expected_name == NULL || address_out == NULL || length < DNS_HEADER_BYTES) return 0;
    if (read_be16(&packet[0]) != expected_id) return 0;
    flags = read_be16(&packet[2]);
    questions = read_be16(&packet[4]);
    answers = read_be16(&packet[6]);
    if ((flags & 0x8000u) == 0u || (flags & 0x7800u) != 0u ||
        (flags & 0x0200u) != 0u || (flags & 0x000fu) != 0u || questions != 1u || answers == 0u) return 0;
    if (!dns_name_read(packet, length, &offset, name, sizeof(name)) ||
        offset > length || length - offset < 4u || !dns_names_equal(name, expected_name)) return 0;
    if (read_be16(&packet[offset]) != DNS_TYPE_A || read_be16(&packet[offset + 2u]) != DNS_CLASS_IN) return 0;
    offset += 4u;
    for (i = 0u; i < (size_t)answers; ++i) {
        uint16_t type, class_code, data_length;
        uint32_t ttl;
        if (!dns_name_read(packet, length, &offset, name, sizeof(name)) || offset > length || length - offset < 10u) return 0;
        type = read_be16(&packet[offset]);
        class_code = read_be16(&packet[offset + 2u]);
        ttl = read_be32(&packet[offset + 4u]);
        data_length = read_be16(&packet[offset + 8u]);
        offset += 10u;
        if ((size_t)data_length > length - offset) return 0;
        if (type == DNS_TYPE_A && class_code == DNS_CLASS_IN && data_length == 4u) {
            *address_out = read_be32(&packet[offset]);
            if (ttl_out != NULL) *ttl_out = ttl;
            return 1;
        }
        offset += data_length;
    }
    return 0;
}
