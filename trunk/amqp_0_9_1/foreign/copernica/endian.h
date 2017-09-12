#ifndef DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ENDIAN_H
#define DATA_PROCESSORS_SYNAPSE_AMQP_0_9_1_FOREIGN_COPERNICA_ENDIAN_H

#if defined(__GNUC__)

#if defined(_MSC_VER)
#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN
#define DP_ENDIANNESS LITTLE
#elif REG_DWORD == REG_DWORD_BIG_ENDIAN
#define DP_ENDIANNESS BIG
#endif
#else
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define DP_ENDIANNESS LITTLE
#elif __BYTE_ORDER == __ORDER_BIG_ENDIAN__
#define DP_ENDIANNESS BIG
#endif
#endif
 
#if DP_ENDIANNESS == LITTLE
 
#define htobe16(x) __builtin_bswap16(x)
#define htole16(x) (x)
#define be16toh(x) __builtin_bswap16(x)
#define le16toh(x) (x)
#define htobe32(x) __builtin_bswap32(x)
#define htole32(x) (x)
#define be32toh(x) __builtin_bswap32(x)
#define le32toh(x) (x)
#define htobe64(x) __builtin_bswap64(x)
#define htole64(x) (x)
#define be64toh(x) __builtin_bswap64(x)
#define le64toh(x) (x)
 
#elif DP_ENDIANNESS == BIG
 
#define htobe16(x) (x)
#define htole16(x) __builtin_bswap16(x)
#define be16toh(x) (x)
#define le16toh(x) __builtin_bswap16(x)
#define htobe32(x) (x)
#define htole32(x) __builtin_bswap32(x)
#define be32toh(x) (x)
#define le32toh(x) __builtin_bswap32(x)
#define htobe64(x) (x)
#define htole64(x) __builtin_bswap64(x)
#define be64toh(x) (x)
#define le64toh(x) __builtin_bswap64(x)
 
#else
 
#error Unsupported endianness in target architecture.
 
#endif 

#elif defined(_MSC_VER)

#include <Windows.h>

#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN
#define DP_ENDIANNESS LITTLE
#elif REG_DWORD == REG_DWORD_BIG_ENDIAN
#define DP_ENDIANNESS BIG
#endif

#include <winsock2.h>

#if DP_ENDIANNESS == LITTLE

#define htobe16(x) _byteswap_ushort(x)
#define htole16(x) (x)
#define be16toh(x) _byteswap_ushort(x)
#define le16toh(x) (x)
#define htobe32(x) _byteswap_ulong(x)
#define htole32(x) (x)
#define be32toh(x) _byteswap_ulong(x)
#define le32toh(x) (x)
#define htobe64(x) _byteswap_uint64(x)
#define htole64(x) (x)
#define be64toh(x) _byteswap_uint64(x)
#define le64toh(x) (x)

#elif DP_ENDIANNESS == BIG

#define htobe16(x) (x)
#define htole16(x) _byteswap_ushort(x)
#define be16toh(x) (x)
#define le16toh(x) _byteswap_ushort(x)
#define htobe32(x) (x)
#define htole32(x) _byteswap_ulong(x)
#define be32toh(x) (x)
#define le32toh(x) _byteswap_ulong(x)
#define htobe64(x) (x)
#define htole64(x) _byteswap_uint64(x)
#define be64toh(x) (x)
#define le64toh(x) _byteswap_uint64(x)

#else

#error NEED TO BEFINE ENDIANNESSSS

#endif 

#else

//elif defined(__XXX__)

#error TODO...

#define htobe16(x) htons(x)
#define be16toh(x) ntohs(x)
#define htobe32(x) htonl(x)
#define be32toh(x) ntohl(x)
#define htobe64(x) htonll(x)
#define be64toh(x) ntohll(x)

#endif
#endif
