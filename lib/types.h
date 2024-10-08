#pragma once

#include <iostream>
#include <limits>
#include <cmath>

#include "common.h"

namespace nicc {

#define __NICC_SPARSE_BITWISE __attribute__((bitwise))
#define __NICC_SPARSE_FORCE __attribute__((force))


/* ========== BASIC TYPES ========== */
typedef uint8_t nicc_uint8_t;
typedef uint16_t nicc_uint16_t;
typedef uint32_t nicc_uint32_t;
typedef uint64_t nicc_uint64_t;
#define NICC_UINT8_MAX  ((__NICC_SPARSE_FORCE nicc_uint8_t) 0xff)
#define NICC_UINT16_MAX ((__NICC_SPARSE_FORCE nicc_uint16_t) 0xffff)
#define NICC_UINT32_MAX ((__NICC_SPARSE_FORCE nicc_uint32_t) 0xffffffff)
#define NICC_UINT64_MAX ((__NICC_SPARSE_FORCE nicc_uint64_t) 0xffffffffffffffffULL)

typedef uint16_t __NICC_SPARSE_BITWISE nicc_be16;
typedef uint32_t __NICC_SPARSE_BITWISE nicc_be32;
typedef uint64_t __NICC_SPARSE_BITWISE nicc_be64;
#define NICC_BE16_MAX ((__NICC_SPARSE_FORCE nicc_be16) 0xffff)
#define NICC_BE32_MAX ((__NICC_SPARSE_FORCE nicc_be32) 0xffffffff)
#define NICC_BE64_MAX ((__NICC_SPARSE_FORCE nicc_be64) 0xffffffffffffffffULL)


/* ========== MATH OPERATIONS ========== */

#define NICC_LOG2(val)  (std::log2((double)(val)))

#define NICC_SET_BIT(var, bit, fullmask)                            \
    static_assert(                                                  \
        fullmask == std::numeric_limits<decltype(var)>::max(),      \
        "bitval failed, incorrect fullmask"                         \
    );                                                              \
    var |= (static_cast<decltype(var)>((1UL << bit)) & fullmask);

#define NICC_SET_MASK(var, mask, fullmask)                      \
    static_assert(                                              \
        mask <= std::numeric_limits<decltype(var)>::max(),      \
        "NICC_SET_MASK failed, mask too large"                  \
    );                                                          \
    static_assert(                                              \
        fullmask == std::numeric_limits<decltype(var)>::max(),  \
        "NICC_SET_MASK failed, fullmask error"                  \
    );                                                          \
    var |= (static_cast<decltype(var)>(mask) & fullmask);


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

    bool operator==(const eth_addr& other){
        // TODO: use SIMD to accelerate this process
        return (
            this->be16[0] == other.be16[0]
            && this->be16[1] == other.be16[1]
            && this->be16[2] == other.be16[2]
        );
    }
};


/**
 *  \brief  ethernet header
 */
typedef struct eth_header {
    struct eth_addr dl_dst; //  Ethernet destination address
    struct eth_addr dl_src; //  Ethernet source address
    nicc_be16 dl_type;      //  Ethernet frame type.

    bool operator==(const eth_header& other){
        return (
            this->dl_dst == other.dl_dst
            && this->dl_src == other.dl_src
            && this->dl_type == other.dl_type
        );
    }
} eth_header_t;


/**
 *  \brief  infiniband address
 */
struct ib_addr {
    union {
        uint8_t ia[20];
        nicc_be16 be16[10];
    };

    bool operator==(const eth_addr& other){
        // TODO: use SIMD to accelerate this process
        return (
            this->be16[0] == other.be16[0]
            && this->be16[1] == other.be16[1]
            && this->be16[2] == other.be16[2]
            && this->be16[3] == other.be16[3]
            && this->be16[4] == other.be16[4]
            && this->be16[5] == other.be16[5]
            && this->be16[6] == other.be16[6]
            && this->be16[7] == other.be16[7]
            && this->be16[8] == other.be16[8]
            && this->be16[9] == other.be16[9]
        );
    }
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
