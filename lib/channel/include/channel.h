/**
 * @file nicc_channel.h
 * @brief General definitions for all communcation channel types. 
 */

#pragma once
#include "common.h"

namespace nicc {
/**
 * ----------------------General definations----------------------
 */ 
using channel_typeid_t = uint8_t;
using channel_mode_t = uint8_t;
class Channel {
/**
 * ----------------------Parameters in channel level----------------------
 */ 
public:
    /**
     * \brief  typeid of the established channel
     */
    enum channel_typeid_t : channel_typeid_t { 
        UNKONW = 0, 
        RDMA,
        SHM
    };
    /**
     * \brief mode of the established channel
     */
    enum channel_mode_t : channel_mode_t {
        UNKONW = 0,
        PAKT_UNORDERED,
        PAKT_ORDERED,
        STREAM
    };
/**
 * ----------------------Channel internal structures----------------------
 */ 
/**
 * ----------------------Channel methods----------------------
 */ 
public:
    Channel() {};
    virtual ~Channel() {};

    /**
     * \brief register this channel into frontend component block
     * \note this function should be called by datapath pipeline, after all component blocks are allocated & initialized
     * \note the register dependences are decided by the application DAG
     */
    virtual nicc_retval_t register_channel() {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     * \brief unregister this channel from frontend component block
     */
    virtual nicc_retval_t unregister_channel() {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     * \brief send data through this channel
     */
    virtual nicc_retval_t tx_burst() {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     * \brief receive data from this channel
     */
    virtual nicc_retval_t rx_burst() {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

/**
 * ----------------------Internel Methonds----------------------
 */ 
private:
    /**
     * \brief allocate a queue pair, including sq, rq, sqcq and rqcq
     */
    virtual nicc_retval_t __allocate_queue_pair() {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /**
     * \brief deallocate a queue pair
     */
    virtual nicc_retval_t __deallocate_queue_pair(uint16_t qpid) {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }
/**
 * ----------------------Internel Parameters----------------------
 */ 
private:
    channel_typeid_t _typeid;
    channel_mode_t _mode;
};
} 

