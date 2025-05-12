#pragma once

#include <iostream>

#include <libflexio/flexio.h>

#include "mlx5/mlx5dv.h"
#include "mlx5/mlx5_api.h"

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"
#include "datapath/channel_impl/dpa_channel.h"
#include "datapath/block_impl/dpa_mat.h"

namespace nicc {
/**
 * ----------------------General structure----------------------
 */ 

/**
 * \brief  block state of DPA, used for control plane
 */
typedef struct ComponentState_DPA {
    /* ========== Common fields ========== */
    ComponentBaseState_t base_state;

    /* ========== ComponentBlock_DPA fields ========== */
    uint8_t mock_state;
} ComponentState_DPA_t;

/**
 * \brief  descriptor of DPA, used for resource allocation & 
 *         re-allocation
 */
typedef struct ComponentDesp_DPA {
    /* ========== Common fields ========== */
    // basic desriptor
    ComponentBaseDesp_t base_desp;

    /* ========== ComponentBlock_DPA fields ========== */
    // IB device name
    const char *device_name;
    // core id
    //! \todo use core mask instead of core id
    uint8_t core_id;
} ComponentDesp_DPA_t;

/**
 *  \brief  basic state of the function register 
 *          into the component block, 
 *          using for running the function on this component block
 *  \note   [1] func state can be modify by other component blocks
 */
typedef struct ComponentFuncState_DPA {
    /* ========== wrapper metadata ========== */
    ComponentFuncBaseState_t base_state;

    // IB Verbs resources
    struct ibv_pd			    *pd;			    // Protection domain for flexio process
    struct mlx5dv_devx_uar      *uar;			    // user access region for flexio process

    // Hanlder and PU
    struct flexio_process       *flexio_process;	// FlexIO process
    struct flexio_uar		    *flexio_uar;	    // FlexIO UAR
    struct flexio_event_handler	*event_handler;		// Event handler on device

    // Communication Channel
    Channel_DPA                 *channel;           // Communication channel for DPA

    // Debugging
    struct flexio_msg_stream    *stream;

    /* ========== global device metadata ========== */
    struct ibv_context          *ibv_ctx;		    // IB device context
} ComponentFuncState_DPA_t;


class ComponentBlock_DPA : public ComponentBlock {
 public:
    ComponentBlock_DPA() {
        this->component_id = kComponent_DPA;
        NICC_CHECK_POINTER(this->_desp = new ComponentDesp_DPA_t);
        NICC_CHECK_POINTER(this->_state = new ComponentState_DPA_t);
        NICC_CHECK_POINTER(this->_function_state = new ComponentFuncState_DPA_t);
        NICC_CHECK_POINTER(this->_base_desp = &this->_desp->base_desp);
        NICC_CHECK_POINTER(this->_base_state = &this->_state->base_state);
        NICC_CHECK_POINTER(this->_base_function_state = &this->_function_state->base_state);
    }
    ~ComponentBlock_DPA(){};

    /*!
     *  \brief  typeid of handlers register into DPA
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Event };

    /*!
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this component
     *  \param  device_state        global device state
     *  \return NICC_SUCCESS for successful registeration
     */
    nicc_retval_t register_app_function(AppFunction *app_func, device_state_t &device_state) override;

    /*!
     *  \brief  deregister a application function
     *  \return NICC_SUCCESS for successful unregisteration
     */
    nicc_retval_t unregister_app_function() override;

    /**
     *  \brief  connect to a neighbor component, this method will be called multiple times, and each may connect to prior or next or both components
     *  \param  prior_component_block [in] the previous component block
     *  \param  next_component_block  [in] the next component block
     *  \param  is_connected_to_remote [in] whether the current component is connected to the remote component
     *  \param  remote_host_qp_info    [in] the qp info of the remote component
     *  \param  is_connected_to_local [in] whether the current component is connected to the local component
     *  \param  local_host_qp_info     [in] the qp info of the local component
     *  \return NICC_SUCCESS for successful connection
     */
    nicc_retval_t connect_to_neighbour( const ComponentBlock *prior_component_block, 
                                        const ComponentBlock *next_component_block,
                                        bool is_connected_to_remote,
                                        const QPInfo *remote_host_qp_info,
                                        bool is_connected_to_local,
                                        const QPInfo *local_host_qp_info) override;

    /**
     *  \brief  add control plane rule to redirect all traffic to the component block
     *  \param  domain  [in] the domain of the component block
     *  \return NICC_SUCCESS for successful addition
     */
    nicc_retval_t add_control_plane_rule(struct mlx5dv_dr_domain *domain) override;

    /**
     *  \brief  run the component block
     *  \return NICC_SUCCESS for successful run
     */
    nicc_retval_t run_block() override;

    /**
     *  \brief  get the qp info of the current component
     *  \param  is_prior  [in] whether the qp is for the prior component
     *  \return the qp info of the current component
     */
    QPInfo *get_qp_info(bool is_prior) override {
        if (is_prior) {
            return this->_function_state->channel->qp_for_prior_info;
        } else {
            return this->_function_state->channel->qp_for_next_info;
        }
    }

/**
 * ----------------------Internel Methonds----------------------
 */ 
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
     *  \brief  (de)allocate wrapper resource for handlers running on DPA
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  func_state      state of the function on this DPA block
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_wrapper_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state);
    
    /*!
     *  \brief  deallocate wrapper resource for handlers running on DPA
     *  \note   this function is called within unregister_app_function
     *  \param  func_state  state of the function on this DPA block
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t __deallocate_wrapper_resources(ComponentFuncState_DPA_t *func_state);

    /*!
     *  \brief  init wrapper resource for handlers running on DPA
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  app_handler     the init handler to be registered
     *  \param  func_state      state of the function on this DPA block
     *  \return NICC_SUCCESS for successful initialization
     */
    nicc_retval_t __init_wrapper_resources(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state);

    /**
     * \brief temporary method to add control plane rule to redirect all traffic to the DPA block; \todo delete this
     */
    nicc_retval_t __add_control_plane_rule(struct mlx5dv_dr_domain *domain);

/**
 * ----------------------Internel Parameters----------------------
 */ 
    friend class Component_DPA;
 protected:
    /**
     * descriptor of the component block, recording total 
     * hardware resources allocated from the component
     */
    ComponentDesp_DPA_t *_desp;

    /**
     * state of the component block, recording runtime state 
     * for rescheduling, inter-block communication channel, and MT
     */
    ComponentState_DPA_t *_state;

    /**
     * basic state of the function register into the component  
     * block, using for running the function on this component block
     */
    ComponentFuncState_DPA_t *_function_state = nullptr;

};

} // namespace nicc
