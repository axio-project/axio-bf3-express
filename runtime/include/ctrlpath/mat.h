# pragma once

#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cstring>

#include "common.h"
#include "log.h"
#include "types.h"
#include "common/crc32.h"


namespace nicc
{


/*!
 *  \brief  flow match fields supported in NICC
 */
enum flow_match_field_t : uint8_t {
    NICC_FMF_UNKNOWN = 0,
    
    /* NICC platform fields */
    NICC_FMF_RT__RETVAL,    // return value of the core

    /* Physical fields */

    /* L2 fields */
    NICC_FMF_ETH__SMAC,     // source MAC address
    NICC_FMF_ETH__DMAC,     // destination MAC address
    NICC_FMF_ETH__TYPE,     // payload type

    // vlan
    NICC_FMF_VLAN__ID,      // vlan index

    /* L3 fields */
    NICC_FMF_IPV4__SIP,     // source ip address
    NICC_FMF_IPV4__DIP,     // destination ip address
    NICC_FMF_IPV4__TYPE,    // payload type

    /* L4 fields */
    NICC_FMF_UDP__SPORT,    // source port
    NICC_FMF_UDP__DPORT,    // destination port

    NICC_FMF_NUM,           // total number of supporting fields
};
using flow_match_field_embedding_t = uint32_t;


/*!
 *  \brief  flow action type supported in NICC
 */
enum flow_action_type_t : uint8_t {
    NICC_ACT_UNKNOWN = 0,

    NICC_ACT_DROP,  // drop packet
    NICC_ACT_FWD    // fwd packet to next component block
};
#define ACT_BYTES sizeof(flow_action_type_t)


/**
 *  \brief  nicc dataplane flow definition
 */
typedef struct flow {
    /* NICC platform fields (64-bits aligned) */
    nicc_core_retval_t core_retval; // return value from user-defined core function
    uint8_t __pad_nicc[7];          //  pad to 64-bit aligned

    /* Physical fields (64-bit aligned) */
    physical_port in_port;
    nicc_be32 __pad_phy;            // pad to 64 bits

    /* L2 fields, Order the same as in the Ethernet header! (64-bit aligned) */
    struct eth_addr dl_dst;         //  Ethernet destination address
    struct eth_addr dl_src;         //  Ethernet source address
    nicc_be16 dl_type;              //  Ethernet frame type.
    uint8_t __pad_l2[2];            //  pad to 64-bit aligned

    /* L3 fields (64-bit aligned) */
    nicc_be32 nw_src;               // IPv4 source address or ARP SPA
    nicc_be32 nw_dst;               // IPv4 destination address or ARP TPA
    nicc_be32 ct_nw_src;            // CT orig tuple IPv4 source address
    nicc_be32 ct_nw_dst;            // CT orig tuple IPv4 destination address
    struct in6_addr ipv6_src;       // IPv6 source address
    struct in6_addr ipv6_dst;       // IPv6 destination address
    struct in6_addr ct_ipv6_src;    // CT orig tuple IPv6 source address
    struct in6_addr ct_ipv6_dst;    // CT orig tuple IPv6 destination address
    nicc_be32 ipv6_label;           // IPv6 flow label
    uint8_t nw_frag;                // FLOW_FRAG_* flags
    uint8_t nw_tos;                 // IP ToS (including DSCP and ECN)
    uint8_t nw_ttl;                 // IP TTL/Hop Limit
    uint8_t nw_proto;               // IP protocol or low 8 bits of ARP opcode

    /* L4 fields (64-bit aligned) */
    struct in6_addr nd_target;      // IPv6 neighbor discovery (ND) target
    struct eth_addr arp_sha;        // ARP/ND source hardware address
    struct eth_addr arp_tha;        // ARP/ND target hardware address
    nicc_be16 tcp_flags;            // TCP flags/ICMPv6 ND options type
    nicc_be16 __pad_l4_1;           // pad to 64 bits
    nicc_be16 tp_src;               // TCP/UDP/SCTP source port/ICMP type
    nicc_be16 tp_dst;               // TCP/UDP/SCTP destination port/ICMP code
    nicc_be16 ct_tp_src;            // CT original tuple source port/ICMP type
    nicc_be16 ct_tp_dst;            // CT original tuple dst port/ICMP code
    nicc_be32 igmp_group_ip4;       // IGMP group IPv4 address/ICMPv6 ND reserved field
    nicc_be32 __pad_l4_2;           // pad to 64 bits

    bool operator==(const flow& other) const {
        return std::memcmp(this, &other, sizeof(flow)) == 0;
    }

    bool operator<(const flow& other) const {
        return this->tp_dst < other.tp_dst;
    }

    bool operator>(const flow& other) const {
        return this->tp_dst > other.tp_dst;
    }
} flow_t;

NICC_STATIC_ASSERT(
    sizeof(flow_t) % sizeof(nicc_uint64_t) == 0, "size of flow_t is misaligned to 64-bits"
);


/**
 *  \brief  flow matching wildcard
 *  \note   a 1-bit in each bit in 'masks' indicates that the corresponding bit of
 *          the flow is significant (must match).  A 0-bit indicates that the
 *          corresponding bit of the flow is wildcarded (need not match)
 */
typedef struct flow_wildcards {
    flow_t masks = { 0 };

    bool operator==(const flow_wildcards& other) const {
        return this->masks == other.masks;
    }
    bool operator<(const flow_wildcards& other) const {
        return this->masks < other.masks;
    }
    bool operator>(const flow_wildcards& other) const {
        return this->masks > other.masks;
    }
} flow_wildcards_t;


/**
 *  \brief  flow classification match
 */
typedef struct flow_match {
    flow_t flow = { 0 };
    flow_wildcards_t wildcards = { 0 };
} flow_match_t;


/**
 *  \brief  action for processing a packet
 */
typedef struct flow_action {
    flow_action_type_t at = NICC_ACT_UNKNOWN;
} flow_action_t;


/*!
 *  \brief  hash value of a flow entry
 */
typedef struct flow_hash {
    uint32_t _value = 0;

    bool operator==(const flow_hash& other){
        return this->_value == other._value;
    }

    flow_hash& operator=(const flow_hash& other){
        if(this == &other){ return *this; }

        this->_value = other._value;
        return *this;
    }

    void cal(flow_match_t& match, flow_action_t& action){
        uint32_t _fmf_hash;
        uint32_t _act_hash;

        _fmf_hash = Utils_CRC32::hash(
            /* buf */ reinterpret_cast<uint8_t*>(&match),
            /* len */ sizeof(flow_match_t),
            /* init */ 0xFFFFFFFF
        );

        _act_hash = Utils_CRC32::hash(
            /* buf */ reinterpret_cast<uint8_t*>(&action.at),
            /* len */ ACT_BYTES,
            /* init */ 0xFFFFFFFF
        );

        this->_value = _fmf_hash | _act_hash;
    }

    flow_hash() : _value(0) {}
    flow_hash(flow_match_t& match, flow_action_t& action){ cal(match, action); }
} flow_hash_t;


/**
 *  \brief  flow entry interfaces
 */
typedef struct flow_entry {
    flow_match_t match;
    flow_action_t action;
    flow_hash_t hash;
} flow_entry_t;


/**
 *  \brief  interface for a matcher, which represent a certain match pattern
 */
class FlowMatcher {
 public:
    FlowMatcher(flow_wildcards_t match_wc){}
    ~FlowMatcher() = default;

    // operators for matcher
    bool operator==(const flow_wildcards_t& match_wc) { return this->_match_wc == match_wc; }
    bool operator==(const FlowMatcher& other) const { return this->_match_wc == other._match_wc; }
    bool operator<(const FlowMatcher& other) const { return this->_match_wc < other._match_wc; }
    bool operator>(const FlowMatcher& other) const { return this->_match_wc > other._match_wc; }
    FlowMatcher& operator=(const FlowMatcher& other){
        if(this == &other){ return *this; }
        this->_match_wc = other._match_wc;
        return *this;
    }

 protected:
    // match pattern of this matcher
    flow_wildcards_t _match_wc;
};


/**
 *  \brief  interface for a match-action table, which contains a list of flow entries
 */
class FlowMAT {
 public:
    FlowMAT(){}
    ~FlowMAT() = default;

    /**
     *  \brief  create new macther in this table (wrapper)
     *  \param  flow_wildcards_t    wildcard for creating this matcher
     *  \param  priority            priority to create this matcher
     *  \param  matcher             the created matcher
     *  \return NICC_SUCCESS for successfully creation
     */
    nicc_retval_t create_matcher(flow_wildcards_t wc, int priority, FlowMatcher** matcher);

    /**
     *  \brief  destory macther in this table (wrapper)
     *  \param  matcher matcher to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    nicc_retval_t detory_matcher(FlowMatcher* matcher);

    /**
     *  \brief  load MAT entries (wrapper)
     *  \param  matcher mather to load the entries
     *  \param  entries list of entries to be loaded
     *  \return NICC_SUCCESS for successfully loading
     */
    nicc_retval_t load_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len);

    /**
     *  \brief  unload MAT entries (wrapper)
     *  \param  matcher mather to unload the entries
     *  \param  entries list of entries to be unloaded
     *  \return NICC_SUCCESS for successfully unloading
     */
    nicc_retval_t unload_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len);

 protected:
    /**
     *  \brief  create new macther in this table (detailed implementation)
     *  \param  flow_wildcards_t    wildcard for creating this matcher
     *  \param  priority            priority to create this matcher
     *  \param  matcher             the created matcher
     *  \return NICC_SUCCESS for successfully creation
     */
    virtual nicc_retval_t __create_matcher(flow_wildcards_t wc, int priority, FlowMatcher** matcher) {
        return NICC_ERROR_NOT_IMPLEMENTED;
    };

    /**
     *  \brief  destory macther in this table (detailed implementation)
     *  \param  matcher matcher to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    virtual nicc_retval_t __destory_matcher(FlowMatcher* matcher) {
        return NICC_ERROR_NOT_IMPLEMENTED;
    };

    /**
     *  \brief  load MAT entries (detailed implementation)
     *  \param  matcher mather to load the entries
     *  \param  entries list of entries to be loaded
     *  \return NICC_SUCCESS for successfully loading
     */
    virtual nicc_retval_t __load_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
        return NICC_ERROR_NOT_IMPLEMENTED;
    };

    /**
     *  \brief  unload MAT entries (detailed implementation)
     *  \param  matcher mather to unload the entries
     *  \param  entries list of entries to be unloaded
     *  \return NICC_SUCCESS for successfully unloading
     */
    virtual nicc_retval_t __unload_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
        return NICC_ERROR_NOT_IMPLEMENTED;
    };

    // entry map
    std::map<FlowMatcher, std::vector<flow_entry_t*>*> _entry_map;
};


/**
 *  \brief  inferface for a flow domain, which contains a chain of flow tables
 */
class FlowDomain {
 public:
    FlowDomain(){}
    ~FlowDomain() = default;

    /**
     *  \brief  create new table in this domain (wrapper)
     *  \param  level       level of the table
     *  \param  table       created table
     *  \return NICC_SUCCESS for successfully allocation
     */
    nicc_retval_t create_table(int level, FlowMAT** table);

    /**
     *  \brief  destory table in this domain (wrapper)
     *  \param  table       table to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    nicc_retval_t destory_table(FlowMAT* table);

 protected:
    // chain of tables
    std::multimap<int, FlowMAT*> _table_map;

    /**
     *  \brief  create new table in this domain (detailed implementation)
     *  \param  priority    prority of the table
     *  \param  table       created table
     *  \return NICC_SUCCESS for successfully allocation
     */
    virtual nicc_retval_t __create_table(int priority, FlowMAT** table){
        return NICC_ERROR_NOT_IMPLEMENTED;
    };

    /**
     *  \brief  destory table in this domain (detailed implementation)
     *  \param  level       level of the table
     *  \param  table       table to be destoried
     *  \return NICC_SUCCESS for successfully destory
     */
    virtual nicc_retval_t __detory_table(int level, FlowMAT* table){
        return NICC_ERROR_NOT_IMPLEMENTED;
    };
};


} // namespace nicc
