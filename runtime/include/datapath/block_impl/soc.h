#pragma once
#include <iostream>
#include <thread>

#include "common.h"
#include "log.h"
#include "app_context.h"
#include "datapath/component_block.h"

// nicc_lib headers
#include "wrapper/soc/soc_wrapper.h"
#include "datapath/channel_impl/soc_channel.h"
#include "common/numautils.h"

namespace nicc {
/**
 * ----------------------General structure----------------------
 */ 
/**
 * \brief  specific state of the component block,
 *         using for control plane, including rescheduling,
 *         inter-block communication channel, and MT
 */
typedef struct ComponentState_SoC {
    /* ========== Common fields ========== */
    ComponentBaseState_t base_state;
    /* ========== Specific fields ========== */
    uint8_t mock_state;
} ComponentState_SoC_t;

/**
 * \brief  specific descriptor of the component block,
 *         using for allocating / deallocating component blocks
 *         from the resource pool
 */
typedef struct ComponentDesp_SoC {
    /* ========== Common fields ========== */
    ComponentBaseDesp_t base_desp;
    /* ========== Specific fields ========== */
    const char *device_name;
    uint8_t phy_port;
} ComponentDesp_SoC_t;


/**
 *  \brief  basic state of the function register 
 *          into the component block, 
 *          using for running the function on this component block
 *  \note   [1] func state can be modify by other component blocks
 */
typedef struct ComponentFuncState_SoC {
    /* ========== wrapper metadata ========== */
    ComponentFuncBaseState_t base_state;

    // wrapper thread, \todo: use vector for multiple threads
    SoCWrapper::SoCWrapperContext *context;
    std::thread *wrapper_thread;
    // Communication Channel
    Channel_SoC                 *channel;           // Communication channel for SoC
    /* ========== Specific fields ========== */
} ComponentFuncState_SoC_t;


class ComponentBlock_SoC : public ComponentBlock {
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    ComponentBlock_SoC() {
        this->component_id = kComponent_SoC;
        NICC_CHECK_POINTER(this->_desp = new ComponentDesp_SoC_t);
        NICC_CHECK_POINTER(this->_state = new ComponentState_SoC_t);
        NICC_CHECK_POINTER(this->_function_state = new ComponentFuncState_SoC_t);
        NICC_CHECK_POINTER(this->_base_desp = &this->_desp->base_desp);
        NICC_CHECK_POINTER(this->_base_state = &this->_state->base_state);
        NICC_CHECK_POINTER(this->_base_function_state = &this->_function_state->base_state);
    }
    ~ComponentBlock_SoC(){};

    /**
     *  \brief  typeid of handlers register into SoC
     */
    enum handler_typeid_t : appfunc_handler_typeid_t { Init = 0, Pkt_Handler, Msg_Handler };

    /**
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this 
     *                    component, at least contains Init
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
    nicc_retval_t add_control_plane_rule(struct mlx5dv_dr_domain *domain) override {
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

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
    QPInfo *get_qp_info(bool is_prior) override;

/**
 * ----------------------Internel Methonds----------------------
 */ 
private:
    /*!
     *  \brief  (de)allocate wrapper resource for handlers running on SoC
     *  \note   this function is called within register_app_function
     *  \param  app_func        application function which the event handler comes from
     *  \param  func_state      state of the function on this SoC block
     *  \return NICC_SUCCESS for successful (de)allocation
     */
    nicc_retval_t __allocate_wrapper_resources(AppFunction *app_func, ComponentFuncState_SoC_t *func_state);
    
    /*!
     *  \brief  deallocate wrapper resource for handlers running on SoC
     *  \note   this function is called within unregister_app_function
     *  \param  func_state  state of the function on this SoC block
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t __deallocate_wrapper_resources(ComponentFuncState_SoC_t *func_state);

    /**
     *  \brief  create wrapper process for the function
     *  \param  func_state  state of the function on this SoC block
     *  \return NICC_SUCCESS for successful creation
     */
    nicc_retval_t __create_wrapper_process(ComponentFuncState_SoC_t *func_state);

/**
 * ----------------------Internel Parameters----------------------
 */ 
    friend class Component_SoC;
 protected:
    /**
     * descriptor of the component block, recording total 
     * hardware resources allocated from the component
     */
    ComponentDesp_SoC_t *_desp;

    /**
     * state of the component block, recording runtime state 
     * for rescheduling, inter-block communication channel, and MT
     */
    ComponentState_SoC_t *_state;

    /**
     * basic state of the function register into the component  
     * block, using for running the function on this component block
     */
    ComponentFuncState_SoC_t *_function_state = nullptr;
    
};


} // namespace nicc
