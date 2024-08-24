#pragma once

#include <iostream>

#include <libflexio/flexio.h>
#include <infiniband/mlx5dv.h>
#include <infiniband/mlx5_api.h>

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"
#include "utils/ibv_device.h"

namespace nicc {


/* Logarithm ring size */
#define DPA_LOG_SQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_RQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_CQ_RING_DEPTH       7       // 2^7 entries
#define DPA_LOG_WQ_DATA_ENTRY_BSIZE 11      // WQ buffer logarithmic size
#define DPA_LOG_WQE_BSIZE           6       // size of wqe inside SQ/RQ of DPA (sizeof(struct mlx5_wqe_data_seg))

/* Queues index mask, represents the index of the last CQE/WQE in the queue */
#define DPA_CQ_IDX_MASK ((1 << DPA_LOG_CQ_RING_DEPTH) - 1)
#define DPA_RQ_IDX_MASK ((1 << DPA_LOG_RQ_RING_DEPTH) - 1)
#define DPA_SQ_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH + LOG_SQE_NUM_SEGS)) - 1)
#define DPA_DATA_IDX_MASK ((1 << (DPA_LOG_SQ_RING_DEPTH)) - 1)


// metadata of a dpa cq
struct dpa_cq {
    uint32_t cq_num;
    uint32_t log_cq_depth;
    flexio_uintptr_t cq_ring_daddr;
    flexio_uintptr_t cq_dbr_daddr;
} __attribute__((__packed__, aligned(8)));


// metadata of a dpa wq
struct dpa_wq {
    uint32_t wq_num;
    uint32_t wqd_mkey_id;
    flexio_uintptr_t wq_ring_daddr;
    flexio_uintptr_t wq_dbr_daddr;
    flexio_uintptr_t wqd_daddr;
} __attribute__((__packed__, aligned(8)));


/* Transport data from HOST application to DEV application */
struct dpa_data_queues {
    struct dpa_cq rq_cq_data;   // device RQ's CQ
    struct dpa_wq rq_data;	    // device RQ
    struct dpa_cq sq_cq_data;   // device SQ's CQ
    struct dpa_wq sq_data;	    // device SQ
} __attribute__((__packed__, aligned(8)));


/*!
*  \brief  state of DPA
*/
typedef struct ComponentState_DPA {
    // basic state
    ComponentBaseState_t base_state;
    // state of DPA
    uint8_t mock_state;
} ComponentState_DPA_t;

/*!
*  \brief  descriptor of DPA
*/
typedef struct ComponentDesp_DPA {
    /* ========== Common fields ========== */
    // basic desriptor
    ComponentBaseDesp_t base_desp;

    // IB device name
    const char *device_name;

    /* ========== ComponentBlock_DPA fields ========== */
    // core id
    //! \todo support EU group
    uint8_t core_id;
} ComponentDesp_DPA_t;

/*!
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncState_DPA {
    /* ========== on-host metadata ========== */
    ComponentFuncBaseState_t base_state;

    // IB Verbs resources
    struct ibv_context		    *ibv_ctx;		    // IB device context
    struct ibv_pd			    *pd;			    // Protection domain
    struct mlx5dv_devx_uar      *uar;			    // user access region

    // flexio resources
    struct flexio_process       *flexio_process;	// FlexIO process
    struct flexio_uar		    *flexio_uar;	    // FlexIO UAR
    struct flexio_event_handler	*event_handler;		// Event handler on device
    struct dpa_cq		        rq_cq_transf;       //! \note   contains some device pointers
    struct dpa_cq		        sq_cq_transf;       //! \note   contains some device pointers
    struct flexio_mkey          *rqd_mkey;
    struct dpa_wq		        rq_transf;          //! \note   contains some device pointers
    struct flexio_mkey          *sqd_mkey;
    struct dpa_wq		        sq_transf;          //! \note   contains some device pointers
    struct dpa_data_queues	    *dev_queues;

    // flexio driver handlers
    struct flexio_cq            *flexio_rq_cq_ptr;	// FlexIO RQ CQ
    struct flexio_cq            *flexio_sq_cq_ptr;	// FlexIO SQ CQ
    struct flexio_rq            *flexio_rq_ptr;		// FlexIO RQ
    struct flexio_sq            *flexio_sq_ptr;		// FlexIO SQ

    /* ========== on-device metadata ========== */
    flexio_uintptr_t		    d_dev_queues;		// mirror of dev_queues on device
} ComponentFuncState_DPA_t;


class ComponentBlock_DPA : public ComponentBlock {
 public:
    ComponentBlock_DPA() {
        NICC_CHECK_POINTER(this->_desp = reinterpret_cast<ComponentBaseDesp_t*> (new ComponentDesp_DPA_t));
        NICC_CHECK_POINTER(this->_state = reinterpret_cast<ComponentBaseState_t*> (new ComponentState_DPA_t));
    }
    ~ComponentBlock_DPA(){};

    /*!
     *  \brief  typeid of handlers register into DPA
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Event };

    /*!
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this component
     *  \return NICC_SUCCESS for successful registeration
     */
    nicc_retval_t register_app_function(AppFunction *app_func) override;

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    nicc_retval_t unregister_app_function() override;

    friend class Component_DPA;

 private:
    /*!
     *  \brief  setup mlnx_device for this DPA block
     *  \note   this function is called within register_app_function
     *  \param  func_state  state of the function which intend to init new rdma revice
     *  \return NICC_SUCCESS on success;
     *          NICC_ERROR otherwise
     */
    nicc_retval_t __setup_ibv_device(ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  register event handler on DPA block
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  app_handler     the handler to be registered
     *  \param  func_state      state of the function on this DPA block
     *  \return NICC_SUCCESS for successful registering
     */
    nicc_retval_t __register_event_handler(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state);
    
    /*!
     *  \brief  unregister event handler on DPA block
     *  \note   this function is called within unregister_app_function
     *  \param  func_state  state of the function on this DPA block
     *  \return NICC_SUCCESS for successful unregistering
     */
    nicc_retval_t __unregister_event_handler(ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  (de)allocate on-device resource for handlers running on DPA
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  func_state      state of the function on this DPA block
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_device_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state);
    
    /*!
     *  \brief  deallocate on-device resource for handlers running on DPA
     *  \note   this function is called within unregister_app_function
     *  \param  func_state  state of the function on this DPA block
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t __deallocate_device_resources(ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  init on-device resource for handlers running on DPA
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  app_handler     the init handler to be registered
     *  \param  func_state      state of the function on this DPA block
     *  \return NICC_SUCCESS for successful initialization
     */
    nicc_retval_t __init_device_resources(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  (de)allocate SQ/RQ and corresponding CQ for DPA process
     *  \note   these functions are called within __allocate_device_resources
     *  \param  func_state  state of the function on this DPA block
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_sq_cq(ComponentFuncState_DPA_t *func_state);
    nicc_retval_t __allocate_rq_cq(ComponentFuncState_DPA_t *func_state);
    nicc_retval_t __deallocate_sq_cq(ComponentFuncState_DPA_t *func_state);
    nicc_retval_t __deallocate_rq_cq(ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  allocate memory resource for SQ/RQ
     *  \note   this function is called within __allocate_sq_cq / __allocate_rq_cq
     *  \param  process         flexIO process
     *  \param  log_depth       log2 of the SQ/RQ depth
     *  \param  wqe_size        size of wqe on the ring
     *  \param  sq_transf       created SQ/RQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_wq_memory(
        struct flexio_process *process, int log_depth, uint64_t wqe_size, struct dpa_wq *wq_transf
    );

    /*!
     *  \brief  deallocate memory resource for SQ/RQ
     *  \note   this function is called within __deallocate_sq_cq / __deallocate_rq_cq
     *  \param  process         flexIO process
     *  \param  sq_transf       created SQ/RQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __deallocate_wq_memory(struct flexio_process *process, struct dpa_wq *wq_transf);

    /*!
     *  \brief  allocate memory resource for SQ/CQ
     *  \note   this function is called within __allocate_sq_cq/__allocate_rq_cq
     *  \param  process   flexIO process
     *  \param  log_depth log2 of the SQ/CQ depth
     *  \param  app_cq    CQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_cq_memory(struct flexio_process *process, int log_depth, struct dpa_cq *app_cq);

    /*!
     *  \brief  deallocate memory resource for SQ/CQ
     *  \note   this function is called within __deallocate_sq_cq / __deallocate_rq_cq
     *  \param  process   flexIO process
     *  \param  app_cq    CQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __deallocate_cq_memory(struct flexio_process *process, struct dpa_cq *app_cq);
    
    /*!
     *  \brief  initialize WQEs on RQ ring (after allocation)
     *  \note   this function is called within __allocate_rq_cq
     *  \param  process         flexIO process
     *  \param  rq_ring_daddr   base address of the allocated RQ ring
     *  \param  log_depth       log2 of the RQ ring depth
     *  \param  data_daddr      base address of the RQ data buffers
     *  \param  wqd_mkey_id     mkey id of the RQ's data buffers
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __init_rq_ring_wqes(
        struct flexio_process *process, flexio_uintptr_t rq_ring_daddr, 
        int log_depth, flexio_uintptr_t data_daddr, uint32_t wqd_mkey_id
    );

    /*!
     *  \brief  allocates doorbell record and return its address on the device's memory
     *  \note   this function is called within __allocate_cq_memory, __allocate_sq_memory and __allocate_rq_cq
     *  \param  process   flexIO process
     *  \param  dbr_daddr doorbell record address on the device's memory
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_dbr(struct flexio_process *process, flexio_uintptr_t *dbr_daddr);

    /*!
     *  \brief  deallocates doorbell record and return its address on the device's memory
     *  \note   this function is called within __deallocate_cq_memory, __deallocate_sq_memory and __deallocate_rq_cq
     *  \param  process   flexIO process
     *  \param  dbr_daddr doorbell record address on the device's memory
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __deallocate_dbr(struct flexio_process *process, flexio_uintptr_t dbr_daddr);

    /*!
     *  \brief  create mkey for the given memory region
     *  \param  process     flexIO process
     *  \param  pd          protection domain
     *  \param  daddr       device address of the memory region
     *  \param  log_bsize   log2 of the memory region size
     *  \param  access      access flags
     *  \param  mkey        mkey
     *  \return NICC_SUCCESS on success
     */
    nicc_retval_t __create_dpa_mkey(
        struct flexio_process *process, struct ibv_pd *pd, flexio_uintptr_t daddr, int log_bsize, int access, struct flexio_mkey **mkey
    );
};

} // namespace nicc
