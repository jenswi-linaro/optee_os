#ifndef __NETINIT_IN_H
#define __NETINIT_IN_H

#include <utee_defines.h>

#define htonl(x) TEE_U32_TO_BIG_ENDIAN(x)
#define ntohl(x) TEE_U32_FROM_BIG_ENDIAN(x)
#define htons(x) TEE_U16_TO_BIG_ENDIAN(x)
#define ntohs(x) TEE_U16_FROM_BIG_ENDIAN(x)

#endif /* __NETINIT_IN_H */
