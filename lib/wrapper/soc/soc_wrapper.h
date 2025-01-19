#pragma once
#include "common.h"
#include "common/soc_queue.h"

namespace nicc {

/**
 * \brief Wrapper for executing SoC functions, created and initialized by ComponentBlock_SoC
 */
class SoCWrapper {
/**
 * ----------------------Parameters used in SoCWrapper----------------------
 */ 
/**
 * ----------------------Public Structures----------------------
 */ 
 public:
    enum soc_wrapper_type_t {
        kSoC_Dispatcher = 0x01,     /// The thread will communicate with other component blocks
        kSoC_Worker = 0x02          /// The thread will execute the app function
    };
    /**
     * \brief   local SoCWrapper context for executing SoC functions, including
     *                - function state ptr
     *                - event handler ptr
     *                - queue pair ptrs for component block communication
     */
    struct SoCWrapperContext{
        /* ========== metadata for dispatcher ========== */
        RDMA_SoC_QP *prior_qp;    /// QP for communicating with the prior component block
        RDMA_SoC_QP *next_qp;     /// QP for communicating with the next component block
        /// e.g. pkt handler ptr
        /// e.g. match-action table ptr
        /* ========== metadata for worker ========== */
        /// e.g. function state ptr
        /// e.g. event handler ptr
        /// lock-free queue
    };
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    /**
     * \brief Constructor, ComponentBlock_SoC calls this when allocating a thread for SoCWrapper.
     *        For dispatcher, 
     * \param type  type of the SoCWrapper
     * \param context context of the SoCWrapper, registered by the ComponentBlock_SoC
     */
    SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context);
    ~SoCWrapper(){}

/**
 * ----------------------Internel Methonds----------------------
 */ 
 private:
    /**
     * \brief Initialize the dispatcher, including comm_lib
     */
    nicc_retval_t __init_dispatcher();
    /**
     * \brief Initialize the worker, registering 
     */
    nicc_retval_t __init_worker();
    /**
     * \brief Run the SoCWrapper datapath in the sequence of worker tx, dispatcher tx, worker rx,
     *        and dispatcher rx.
     */
    nicc_retval_t __run();

    /* ========================SoC Datapath ========================*/
    
};


} // namespace nicc