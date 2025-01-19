#pragma once

#include <libflexio/flexio.h>

#include "mlx5/mlx5dv.h"
#include "mlx5/mlx5_api.h"

#include "common.h"
#include "log.h"
#include "datapath/channel.h"
#include "common/dpa_queue.h"

namespace nicc {
class Channel_DPA : public Channel {
 public:
    Channel_DPA(channel_typeid_t channel_type, channel_mode_t channel_mode) : Channel() {
        this->_typeid = channel_type;
        this->_mode = channel_mode;
    }
    ~Channel_DPA() {};
    
    /**
     * \brief   allocate resources for dpa channel, including CQ, SQ, RQ
     * \return  NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t allocate_channel( struct ibv_pd *pd, 
                                    struct mlx5dv_devx_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct flexio_event_handler	*event_handler,
                                    struct ibv_context *ibv_ctx);

    /**
     * \brief   deallocate resources for dpa channel, including CQ, SQ, RQ
     * \return  NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t deallocate_channel(struct flexio_process *flexio_process);

/**
 * ----------------------Public parameters----------------------
 */ 
 public:
    /// \brief on-device metadata
    flexio_uintptr_t		    d_dev_queues;		// mirror of dev_queues on device

/**
 * ----------------------Internel method----------------------
 */ 
 private:
/*!
     *  \brief  (de)allocate SQ/RQ and corresponding CQ for DPA process
     *  \note   these functions are called within __allocate_device_resources
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_sq_cq( struct ibv_pd *pd, 
                                    struct mlx5dv_devx_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct ibv_context *ibv_ctx);
    nicc_retval_t __allocate_rq_cq( struct ibv_pd *pd, 
                                    struct mlx5dv_devx_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct flexio_event_handler	*event_handler,
                                    struct ibv_context *ibv_ctx);
    nicc_retval_t __deallocate_sq_cq(struct flexio_process *flexio_process);
    nicc_retval_t __deallocate_rq_cq(struct flexio_process *flexio_process);

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

/**
 * ----------------------Internel parameters----------------------
 */
 private:
    /// \brief  SQ
    struct dpa_wq		        _sq_transf;          //! \note   contains some device pointers
    struct dpa_cq		        _sq_cq_transf;       //! \note   contains some device pointers
    struct flexio_mkey          *_sqd_mkey;
    /// \brief  RQ
    struct dpa_wq		        _rq_transf;          //! \note   contains some device pointers
    struct dpa_cq		        _rq_cq_transf;       //! \note   contains some device pointers
    struct flexio_mkey          *_rqd_mkey;

    struct dpa_data_queues	    *_dev_queues;

    /// \brief flexio driver handlers
    struct flexio_cq            *_flexio_rq_cq_ptr;	// FlexIO RQ CQ
    struct flexio_cq            *_flexio_sq_cq_ptr;	// FlexIO SQ CQ
    struct flexio_rq            *_flexio_rq_ptr;		// FlexIO RQ
    struct flexio_sq            *_flexio_sq_ptr;		// FlexIO SQ 
};

}