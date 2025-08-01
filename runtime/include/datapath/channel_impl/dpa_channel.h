#pragma once

#include <libflexio/flexio.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "mlx5/mlx5dv.h"
#include "mlx5/mlx5_api.h"

#include "common.h"
#include "log.h"
#include "datapath/channel.h"
#include "common/dpa_queue.h"

namespace nicc {
class Channel_DPA : public Channel {
 public:
    Channel_DPA(channel_typeid_t channel_type_of_prior, channel_mode_t channel_mode_of_prior, channel_typeid_t channel_type_of_next, channel_mode_t channel_mode_of_next) : Channel() {
        this->_typeid_of_prior = channel_type_of_prior;
        this->_mode_of_prior = channel_mode_of_prior;
        this->_typeid_of_next = channel_type_of_next;
        this->_mode_of_next = channel_mode_of_next;

        this->dev_queues_for_prior = (struct dpa_data_queues *)calloc(1, sizeof(struct dpa_data_queues));
        memset(this->dev_queues_for_prior, 0, sizeof(struct dpa_data_queues));
        this->dev_queues_for_next = (struct dpa_data_queues *)calloc(1, sizeof(struct dpa_data_queues));
        memset(this->dev_queues_for_next, 0, sizeof(struct dpa_data_queues));

        this->qp_for_prior_info = (QPInfo*)malloc(sizeof(QPInfo));
        this->qp_for_next_info = (QPInfo*)malloc(sizeof(QPInfo));
    }
    ~Channel_DPA() {};
    
    struct flexio_queues_handler {
        /// \brief maintain flexio driver handlers and metadata in SoC
        uint8_t mode;
        struct flexio_cq            *flexio_rq_cq_ptr;	// FlexIO RQ CQ
        struct flexio_cq            *flexio_sq_cq_ptr;	// FlexIO SQ CQ
        struct flexio_rq            *flexio_rq_ptr;		// FlexIO RQ, used for Ethernet mode
        struct flexio_sq            *flexio_sq_ptr;		// FlexIO SQ, used for Ethernet mode
        struct flexio_qp            *flexio_qp_ptr;    	// FlexIO QP, used for RDMA mode

        /// \brief  mkey for eth SQ
        struct flexio_mkey          *sqd_mkey;
        /// \brief  mkey for eth RQ
        struct flexio_mkey          *rqd_mkey;
        /// \brief  mkey for rdma QP
        struct flexio_mkey          *qpd_mkey;
        /// \brief  mr for rdma QP
        struct ibv_mr               *qpd_mr;
        /// \brief  start address of the first mbuf
        size_t                      qpd_mbuf_start_addr;
        /// \brief  rdma rq pi, used for rdma rq post receive
        size_t                      rdma_rq_pi = 0;
    };

    /**
     * \brief   allocate resources for dpa channel, including CQ, SQ, RQ
     * \return  NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t allocate_channel( struct ibv_pd *pd, 
                                    struct flexio_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct flexio_event_handler	*event_handler,
                                    struct ibv_context *ibv_ctx,
                                    const char *dev_name,
                                    uint8_t phy_port);

    /**
     * \brief   deallocate resources for dpa channel, including CQ, SQ, RQ
     * \return  NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t deallocate_channel(struct flexio_process *flexio_process);

    /**
     * @brief Connect QP to a component block or remote/local host
     * @param is_prior [in] whether the connection is for qp_for_prior or qp_for_next
     * @param neighbour_component_block [in] the neighbour component block
     * @param qp_info [in] QP info of the remote/local host
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t connect_qp(bool is_prior, 
                             const ComponentBlock *neighbour_component_block, 
                             const QPInfo *qp_info);

/**
 * ----------------------Util Methods----------------------
 */ 
 public:
    struct flexio_rq *get_flexio_rq_ptr(bool is_prior) {
        return is_prior ? this->_flexio_queues_handler_for_prior.flexio_rq_ptr : 
                          this->_flexio_queues_handler_for_next.flexio_rq_ptr;
    }

/**
 * ----------------------Public parameters----------------------
 */ 
 public:
    struct dpa_data_queues      *dev_queues_for_prior;
    struct dpa_data_queues      *dev_queues_for_next;
    /// \brief on-device metadata, used for init or destroy on-device resources,
    ///        the metadata is the dev addr of dev_queues_for_prior/next content
    ///        a flexio_process_call will call an init function on the device, 
    ///        to read dev_queues_for_prior/next content
    flexio_uintptr_t		    dev_metadata_for_prior;		// mirror of dev_queues_for_prior on device
    flexio_uintptr_t		    dev_metadata_for_next;		// mirror of dev_queues_for_next on device
    /// \brief QP info for prior and next component block, used for connect QP to component block (currently only used for RDMA mode)
    QPInfo *qp_for_prior_info;
    QPInfo *qp_for_next_info;
/**
 * ----------------------Internel method----------------------
 */ 
 private:
    /**
     * @brief Resolve InfiniBand-specific fields in \p resolve
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __roce_resolve_phy_port();

    /*!
     *  \brief  (de)allocate SQ/RQ and corresponding CQ for DPA process
     *  \note   these functions are called within __allocate_device_resources
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_sq_cq( struct ibv_pd *pd, 
                                    struct flexio_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct ibv_context *ibv_ctx,
                                    struct dpa_data_queues *dev_queues,
                                    struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __allocate_rq_cq( struct ibv_pd *pd, 
                                    struct flexio_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct flexio_event_handler	*event_handler,
                                    struct ibv_context *ibv_ctx,
                                    struct dpa_data_queues *dev_queues,
                                    struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __deallocate_sq_cq(struct flexio_process *flexio_process,
                                     struct dpa_data_queues *dev_queues,
                                     struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __deallocate_rq_cq(struct flexio_process *flexio_process,
                                     struct dpa_data_queues *dev_queues,
                                     struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __create_qp(struct ibv_pd *pd, 
                              struct flexio_uar *uar, 
                              struct flexio_process *flexio_process,
                              struct dpa_data_queues *dev_queues,
                              struct flexio_queues_handler *flexio_queues_handler,
                              channel_typeid_t type);
    nicc_retval_t __create_ethernet_qp(struct ibv_pd *pd, 
                                       struct flexio_uar *uar, 
                                       struct flexio_process *flexio_process,
                                       struct dpa_data_queues *dev_queues,
                                       struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __create_rdma_qp(struct ibv_pd *pd, 
                                    struct flexio_uar *uar, 
                                    struct flexio_process *flexio_process,
                                    struct dpa_data_queues *dev_queues,
                                    struct flexio_queues_handler *flexio_queues_handler);
    nicc_retval_t __destroy_qp(struct flexio_process *flexio_process,
                              struct dpa_data_queues *dev_queues,
                              struct flexio_queues_handler *flexio_queues_handler);


    /*!
     *  \brief  allocate memory resource for SQ/RQ
     *  \note   this function is called within __create_qp
     *  \param  process         flexIO process
     *  \param  log_depth       log2 of the SQ/RQ depth
     *  \param  wqe_size        size of wqe on the ring
     *  \param  sq_transf       created SQ/RQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_wq_memory(
        struct flexio_process *process, int log_depth, uint64_t wqe_size, struct dpa_eth_wq *wq_transf
    );

    /*!
     *  \brief  deallocate memory resource for SQ/RQ
     *  \note   this function is called within __destroy_qp
     *  \param  process         flexIO process
     *  \param  sq_transf       created SQ/RQ resource
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __deallocate_wq_memory(struct flexio_process *process, struct dpa_eth_wq *wq_transf);

    /**
     *  \brief  allocate memory resource for RDMA QP
     *  \param  process         flexIO process
     *  \param  qp_transf       created RDMA QP resource
     *  \param  log_sq_depth    log2 of the SQ depth
     *  \param  log_rq_depth    log2 of the RQ depth
     *  \param  wqe_size        size of wqe on the ring
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_qp_memory(struct flexio_process *process, 
                                       struct flexio_queues_handler *flexio_queues_handler,
                                       struct dpa_qp *qp_transf, 
                                       int log_sq_depth, int log_rq_depth, uint64_t wqe_size);

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
     *  \note   this function is called within __create_qp
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
     *  \note   this function is called within __allocate_cq_memory / __allocate_wq_memory
     *  \param  process   flexIO process
     *  \param  dbr_daddr doorbell record address on the device's memory
     *  \return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __allocate_dbr(struct flexio_process *process, flexio_uintptr_t *dbr_daddr);

    /*!
     *  \brief  deallocates doorbell record and return its address on the device's memory
     *  \note   this function is called within __deallocate_cq_memory / __deallocate_wq_memory
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
     *  \brief  set QP info for prior and next
     *  \param  ibv_ctx     ibv context
     *  \return NICC_SUCCESS on success
     */
    void __set_local_qp_info(QPInfo *qp_info, struct dpa_data_queues *dev_queues);

    /**
     *  \brief  connect QP to component block
     *  \param  dev_queues      device queues
     *  \param  neighbour_component_block  neighbour component block
     *  \param  local_qp_info  local QP info
     *  \return NICC_SUCCESS on success
     */
    nicc_retval_t __connect_qp_to_component_block(dpa_data_queues *dev_queues, 
                                                  const ComponentBlock *neighbour_component_block, 
                                                  const QPInfo *local_qp_info);

    /**
     *  \brief  connect QP to host
     *  \param  dev_queues      device queues
     *  \param  qp_info  QP info
     *  \return NICC_SUCCESS on success
     */
    nicc_retval_t __connect_qp_to_host( dpa_data_queues *dev_queues, 
                                        struct flexio_queues_handler *flexio_queues_handler,
                                        const QPInfo *qp_info, 
                                        const QPInfo *local_qp_info);
    /**
     * @brief Fill the RECV queue
     * @param qp_transfer pointer to the qp metadata
     * @return NICC_SUCCESS on success and NICC_ERROR otherwise
     */
    nicc_retval_t __fill_recv_queue(struct dpa_qp *qp_transfer);

/**
 * ----------------------Internel parameters----------------------
 */
 private:
    struct flexio_process *_flexio_process;
    /// Info resolved from \p phy_port, must be filled by constructor.
    class IBResolve : public VerbsResolve {
    public:
      ipaddr_t ipv4_addr;           ///< The port's IPv4 address in host-byte order
      uint16_t port_lid = 0;        ///< Port LID. 0 is invalid.
      union ibv_gid gid;            ///< GID, used only for RoCE
      uint8_t gid_index = 0;        ///< GID index, used only for RoCE
      uint8_t mac_addr[6] = {0};    ///< MAC address of the device port
    } _resolve;

    struct flexio_queues_handler	_flexio_queues_handler_for_prior;
    struct flexio_queues_handler	_flexio_queues_handler_for_next;

    /// Temp parameter for connect QP to host
    uint8_t _remote_mac_addr[6];
    union ibv_gid _remote_gid = {0};
};

}