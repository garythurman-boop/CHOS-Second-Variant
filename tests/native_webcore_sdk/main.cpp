#include <algorithm>
#include <array>
#include <random>
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { printf("[FAIL] line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static int addresses()
{
    static_assert(sizeof(in_addr) == 4 && sizeof(in6_addr) == 16 && alignof(in6_addr) == 4);
    struct Example { int family; const char* input; const char* output; };
    const Example cases[] = {
        {AF_INET, "0.0.0.0", "0.0.0.0"},
        {AF_INET, "255.255.255.255", "255.255.255.255"},
        {AF_INET6, "0:0:0:0:0:0:0:0", "::"},
        {AF_INET6, "0:0:0:0:0:0:0:1", "::1"},
        {AF_INET6, "2001:0DB8:0:0:1:0:0:1", "2001:db8::1:0:0:1"},
        {AF_INET6, "::ffff:192.0.2.1", "::ffff:192.0.2.1"},
    };
    for (const auto& test : cases) {
        uint8_t bytes[16] = {}, again[16] = {};
        char buffer[INET6_ADDRSTRLEN];
        CHECK(inet_pton(test.family, test.input, bytes) == 1);
        CHECK(inet_ntop(test.family, bytes, buffer, sizeof(buffer)) == buffer);
        CHECK(strcmp(buffer, test.output) == 0);
        CHECK(inet_pton(test.family, buffer, again) == 1);
        CHECK(memcmp(bytes, again, test.family == AF_INET ? 4 : 16) == 0);
        size_t length = strlen(test.output);
        memset(buffer, 0x5a, sizeof(buffer));
        CHECK(!inet_ntop(test.family, bytes, buffer, length) && errno == ENOSPC);
        CHECK(buffer[length] == 0x5a);
        CHECK(inet_ntop(test.family, bytes, buffer, length + 1) == buffer);
    }
    in_addr loopback;
    CHECK(inet_pton(AF_INET, "127.0.0.1", &loopback) == 1);
    CHECK(loopback.s_addr == htonl(INADDR_LOOPBACK));
    CHECK(ntohl(loopback.s_addr) == INADDR_LOOPBACK);
    uint16_t port = htons(0xabcd);
    uint8_t expected[] = {0xab, 0xcd};
    CHECK(memcmp(&port, expected, 2) == 0 && ntohs(port) == 0xabcd);
    in6_addr address;
    for (auto text : {"001.2.3.4", "256.0.0.1", "1.2", "1.2.3.4."})
        CHECK(inet_pton(AF_INET, text, &address) == 0);
    for (auto text : {"", ":", "1::2::3", "12345::", "::ffff:300.1.1.1", "1:2:3:4:5:6:7:8:9"})
        CHECK(inet_pton(AF_INET6, text, &address) == 0);
    char buffer[INET6_ADDRSTRLEN];
    CHECK(inet_pton(99, "::", &address) == -1 && errno == EAFNOSUPPORT);
    CHECK(!inet_ntop(99, &address, buffer, sizeof(buffer)) && errno == EAFNOSUPPORT);
    return 0;
}

static int random_engine()
{
    std::default_random_engine engine;
    uint64_t reference = 1;
    for (unsigned i = 0; i < 1000; ++i) {
        reference = reference * UINT64_C(48271) % UINT64_C(2147483647);
        CHECK(engine() == reference);
    }
    std::array<int, 8> values{0,1,2,3,4,5,6,7};
    auto original = values;
    auto replay = values;
    auto second = engine;
    std::ranges::shuffle(values, engine);
    std::ranges::shuffle(replay, second);
    CHECK(values != original && values == replay);
    std::ranges::sort(values);
    CHECK(values == original);
    return 0;
}

int main()
{
    CHECK(!addresses());
    CHECK(!random_engine());
    puts("[ OK ] native WebCore IP address conversions and UA shuffle passed");
    return 0;
}
