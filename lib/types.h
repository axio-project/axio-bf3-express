#pragma once

#include <iostream>

#include "common.h"

namespace nicc {

#define NICC_BITWISE __attribute__((bitwise))
#define NICC_FORCE __attribute__((force))


/* ========== BASIC TYPES ========== */

typedef uint16_t NICC_BITWISE nicc_be16;
typedef uint32_t NICC_BITWISE nicc_be32;
typedef uint64_t NICC_BITWISE nicc_be64;

#define NICC_BE16_MAX ((NICC_FORCE nicc_be16) 0xffff)
#define NICC_BE32_MAX ((NICC_FORCE nicc_be32) 0xffffffff)
#define NICC_BE64_MAX ((NICC_FORCE nicc_be64) 0xffffffffffffffffULL)


/* ========== NICC PLATFORM TYPES ========== */
using nicc_core_retval_t = uint8_t;


/* ========== PYSICAL PORT TYPES ========== */
using ofp_port_t = uint32_t;
using odp_port_t = uint32_t;
using ofp11_port_t = uint32_t;


/**
 *  \brief  index of a physical port
 */
union physical_port {
    odp_port_t odp_port;
    ofp_port_t ofp_port;
};


/* ========== ETHERNET TYPES ========== */
/**
 *  \brief  ethernet address
 */
struct eth_addr {
    union {
        uint8_t ea[6];
        nicc_be16 be16[3];
    };
};


/**
 *  \brief  ethernet header
 */
typedef struct eth_header {
    struct eth_addr dl_dst; //  Ethernet destination address
    struct eth_addr dl_src; //  Ethernet source address
    nicc_be16 dl_type;      //  Ethernet frame type.
} eth_header_t;


/**
 *  \brief  infiniband address
 */
struct ib_addr {
    union {
        uint8_t ia[20];
        nicc_be16 be16[10];
    };
};


#define ETH_ADDR_C(A,B,C,D,E,F) \
    { { { 0x##A, 0x##B, 0x##C, 0x##D, 0x##E, 0x##F } } }


/* ========== IPv4 TYPES ========== */


/* ========== IPv6 TYPES ========== */
/**
 *  \brief  ipv6 address
 */
struct in6_addr {
    union {
        uint8_t u_s6_addr[16];
    } u;
};

} // namespce nicc
