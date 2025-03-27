/**
 * @file nicc_channel.h
 * @brief General definitions for all communcation channel types.
 */

#pragma once
#include "common.h"
#include "datapath/component_block.h"

namespace nicc {
/**
 * ----------------------General definations----------------------
 */ 
class Channel {
/**
 * ----------------------Parameters in channel level----------------------
 */ 
public:
    /**
     * \brief  typeid of the established channel
     */
    enum channel_typeid_t : uint8_t { 
        UNKONW_TYPE = 0, 
        RDMA,
        SHM,
        ETHERNET  // only for communication between DPA/SoC and NIC
    };
    /**
     * \brief mode of the established channel
     */
    enum channel_mode_t : uint8_t {
        UNKONW_MODE = 0,
        PAKT_UNORDERED,
        PAKT_ORDERED,
        STREAM
    };
    /*!
    *  \brief  mask for channel state
    */
    using channel_state_t = uint16_t;
    static constexpr channel_state_t kChannel_State_Uninit = 0x0000;
    static constexpr channel_state_t kChannel_State_Prior_Connected = 0x0001;
    static constexpr channel_state_t kChannel_State_Next_Connected = 0x0010;
    static constexpr channel_state_t kChannel_State_Both_Connected = 0x0011;

/**
 * ----------------------Channel internal structures----------------------
 */ 
/**
 * ----------------------Channel methods----------------------
 */ 
public:
    Channel() {};
    virtual ~Channel() {};

    // /**
    //  * \brief register this channel into frontend component block
    //  * \note this function should be called by datapath pipeline, after all component blocks are allocated & initialized
    //  * \note the register dependences are decided by the application DAG
    //  */
    // virtual nicc_retval_t allocate_channel() {
    //     return NICC_ERROR_NOT_IMPLEMENTED;
    // }

    // /**
    //  * \brief unregister this channel from frontend component block
    //  */
    // virtual nicc_retval_t deallocate_channel() {
    //     return NICC_ERROR_NOT_IMPLEMENTED;
    // }

/**
 * ----------------------Internel Parameters----------------------
 */ 
protected:
    channel_typeid_t _typeid;
    channel_mode_t _mode;
    channel_state_t _state = kChannel_State_Uninit;
};
} 

