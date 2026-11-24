#include <netinet/in.h>

uint16_t htons(uint16_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(value);
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return value;
#else
#error Unsupported native byte order
#endif
}

uint32_t htonl(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

uint16_t ntohs(uint16_t value) { return htons(value); }
uint32_t ntohl(uint32_t value) { return htonl(value); }
