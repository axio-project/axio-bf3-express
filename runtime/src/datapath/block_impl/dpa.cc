#include "datapath/block_impl/dpa.h"

namespace nicc {

/**
 * ===========================Public methods===========================
 */

nicc_retval_t ComponentBlock_DPA::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint8_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    ComponentFuncState_DPA_t *func_state; 

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state = this->_function_state);

    // TODO: this is ugly, remove ibv_ctx from func_state!
    NICC_CHECK_POINTER(func_state->ibv_ctx = device_state.ibv_ctx);
    if(unlikely(
        NICC_SUCCESS != (retval = this->__setup_ibv_device(func_state))
    )){
        NICC_WARN_C("failed to setu ibv device for newly applocated function, abort");
        goto exit;
    }

    if(unlikely(app_func->handlers.size() == 0)){
        NICC_WARN_C("no handlers included in app_func context, nothing registered");
        goto exit;
    }

    for(i=0; i<app_func->handlers.size(); i++){
        NICC_CHECK_POINTER(app_handler = app_func->handlers[i]);
        switch(app_handler->tid){
        case handler_typeid_t::Init:
            init_handler = app_handler;
            break;
        case handler_typeid_t::Event:
            event_handler = app_handler;
            break;
        default:
            NICC_ERROR_C_DETAIL("unregornized handler id for DPA, this is a bug: handler_id(%u)", app_handler->tid);
        }
    }

    // check handlers
    if(unlikely(init_handler == nullptr)){
        NICC_WARN_C("no init handlers included in app_func context, abort registering");
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }
    if(unlikely(event_handler == nullptr)){
        NICC_WARN_C("no event handlers included in app_func context, abort registering");
        retval = NICC_ERROR_NOT_FOUND;
        goto exit;
    }

    // register event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__register_event_handler(app_func, event_handler, func_state))
    )){
        NICC_WARN_C("failed to register handler on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // allocate resource for this event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__allocate_wrapper_resources(app_func, func_state))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // init resources
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__init_wrapper_resources(app_func, init_handler, func_state))
    )){
        NICC_WARN_C("failed to init reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        goto exit;
    }

    // insert to function map
    this->_function_state = func_state;


 exit:
    // TODO: destory if failed
    return retval;
}


nicc_retval_t ComponentBlock_DPA::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;

    // obtain function state
    NICC_CHECK_POINTER( this->_function_state );

    // deallocate resource for event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__deallocate_wrapper_resources( 
                        this->_function_state ))
    )){
        NICC_WARN_C("failed to allocate reosurce on DPA block: handler_tid(%u), retval(%u)", Event, retval);
        return retval;
    }

    // unregister event handler
    if(unlikely(NICC_SUCCESS !=
        (retval = this->__unregister_event_handler( 
                        this->_function_state ))
    )){
        NICC_WARN_C("failed to unregister event handler on DPA block: retval(%u)", retval);
        return retval;
    }

    // delete the function state
    delete this->_function_state;

    return retval;
}

nicc_retval_t ComponentBlock_DPA::connect_to_neighbour(const ComponentBlock *prior_component_block, 
                                                       const ComponentBlock *next_component_block, 
                                                       bool is_connected_to_remote, 
                                                       const QPInfo *remote_host_qp_info, 
                                                       bool is_connected_to_local, 
                                                       const QPInfo *local_host_qp_info) {
    nicc_retval_t retval = NICC_SUCCESS;
    NICC_CHECK_POINTER(this->_function_state->channel);

    if (is_connected_to_remote){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(true, nullptr, remote_host_qp_info)
        ))) {
            NICC_WARN_C("failed to connect to remote component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (is_connected_to_local){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(false, nullptr, local_host_qp_info)
        ))) {
            NICC_WARN_C("failed to connect to local component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (prior_component_block != nullptr){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(true, prior_component_block, nullptr)
        ))) {
            NICC_WARN_C("failed to connect to prior component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    if (next_component_block != nullptr){
        if(unlikely(NICC_SUCCESS != (
            retval = this->_function_state->channel->connect_qp(false, next_component_block, nullptr)
        ))) {
            NICC_WARN_C("failed to connect to next component: nicc_retval(%u)", retval);
            return retval;
        }
    }
    
    return retval;
}

    nicc_retval_t ComponentBlock_DPA::add_control_plane_rule(struct mlx5dv_dr_domain *domain) {
    nicc_retval_t retval = NICC_SUCCESS;
    /// Currently, only Ethernet Channel needs to add control plane rule to redirect all traffic to the DPA block
    if (this->_function_state->channel->dev_queues_for_prior->type == Channel::ETHERNET) {
        retval = this->__add_control_plane_rule(domain);
    }
    return retval;
}

nicc_retval_t ComponentBlock_DPA::run_block() {
    nicc_retval_t retval = NICC_SUCCESS;
    // run the event handler; 
    flexio_status ret;
    ret = flexio_event_handler_run(this->_function_state->event_handler, 0);
    if(unlikely(ret != FLEXIO_STATUS_SUCCESS)){
        NICC_WARN_C("failed to run event handler on DPA block: flexio_retval(%d)", ret);
        return NICC_ERROR_HARDWARE_FAILURE;
    }
    return retval;
}

/**
 * ===========================Internel methods===========================
 */

nicc_retval_t ComponentBlock_DPA::__setup_ibv_device(ComponentFuncState_DPA_t *func_state) {
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(func_state);

    func_state->uar = mlx5dv_devx_alloc_uar(func_state->ibv_ctx, MLX5DV_UAR_ALLOC_TYPE_NC);
    if (unlikely(func_state->uar == nullptr)) {
        NICC_WARN_C("failed to allocate UAR on device %s", this->_desp->device_name);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    func_state->pd = ibv_alloc_pd(func_state->ibv_ctx);
    if (unlikely(func_state->pd == nullptr)) {
        NICC_WARN_C("failed to allocate UAR");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(func_state->uar != nullptr){
            mlx5dv_devx_free_uar(func_state->uar);
        }

        if(func_state->ibv_ctx != nullptr){
            ibv_close_device(func_state->ibv_ctx);
        }
    }
    
    return retval;
}


nicc_retval_t ComponentBlock_DPA::__register_event_handler(
    AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state
){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status res;
    struct flexio_event_handler_attr event_handler_attr = {0};

    // create stream for debugging DPA event handler
    /// \todo delete this
    flexio_msg_stream_attr_t stream_fattr;
    memset(&stream_fattr, 0, sizeof(stream_fattr));


    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(func_state);
    NICC_CHECK_POINTER(app_handler->binary.dpa.kernel);
    NICC_CHECK_POINTER(app_handler->binary.dpa.host_stub);

    // create flexio process
    if(unlikely(FLEXIO_STATUS_SUCCESS != 
        (res = flexio_process_create(
            func_state->ibv_ctx,
            reinterpret_cast<flexio_app*>(app_handler->binary.dpa.kernel),
            nullptr,
            &func_state->flexio_process
        ))
    )){
        NICC_WARN_C("failed to create event handler on flexio driver on DPA block: flexio_retval(%d), device: %s", 
                    res, func_state->ibv_ctx->device->name);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // obtain uar from the created process
    func_state->flexio_uar = flexio_process_get_uar(func_state->flexio_process);
    if(unlikely(func_state->flexio_uar == nullptr)){
        NICC_WARN_C("no uar extracted from flexio process, device: %s", func_state->ibv_ctx->device->name);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // creat event handler
    event_handler_attr.host_stub_func = reinterpret_cast<flexio_func_t*>(app_handler->binary.dpa.host_stub);
    event_handler_attr.affinity.type = FLEXIO_AFFINITY_STRICT;
    event_handler_attr.affinity.id = this->_desp->core_id;
    if(unlikely(FLEXIO_STATUS_SUCCESS !=
        (res = flexio_event_handler_create(func_state->flexio_process, &event_handler_attr, &func_state->event_handler))
    )){
        NICC_WARN_C("failed to create flexio event handler on flexio driver on DPA block: flexio_retval(%d)", res);
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // add stream for debugging DPA event handler
    /// \todo delete this
    stream_fattr.uar=func_state->flexio_uar;
    stream_fattr.data_bsize = 4 * 2048;
    stream_fattr.sync_mode = FLEXIO_LOG_DEV_SYNC_MODE_SYNC;
    stream_fattr.level = FLEXIO_MSG_DEV_DEBUG;
    stream_fattr.stream_name="DEFAULT STREAM";
    stream_fattr.mgmt_affinity.type = FLEXIO_AFFINITY_NONE;
    flexio_msg_stream_create(
        func_state->flexio_process,
        &stream_fattr,
        stdout,
        NULL,
        &func_state->stream
    );


 exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(func_state->flexio_process != nullptr){
            flexio_process_destroy(func_state->flexio_process);
        }
    }

    return retval;
}


nicc_retval_t ComponentBlock_DPA::__allocate_wrapper_resources(AppFunction *app_func, ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(func_state);

    // this->_function_state->channel = new Channel_DPA(Channel::ETHERNET, Channel::PAKT_UNORDERED, Channel::RDMA, Channel::PAKT_UNORDERED);
    this->_function_state->channel = new Channel_DPA(Channel::RDMA, Channel::PAKT_UNORDERED, Channel::RDMA, Channel::PAKT_UNORDERED);

    // allocate channel
    if(unlikely(NICC_SUCCESS !=(
        retval = this->_function_state->channel->allocate_channel(func_state->pd,       /* used in ethernet channel */
                                                                  func_state->uar,      /* used in ethernet channel */
                                                                  func_state->flexio_process, 
                                                                  func_state->event_handler, 
                                                                  func_state->ibv_ctx,
                                                                  "mlx5_0", /* RDMA device name, mlx5_0 does not support RoCEv2 */
                                                                  0         /* RDMA port */) 
    ))){
        NICC_WARN_C(
            "failed to allocate and init DPA channel: nicc_retval(%u)", retval
        );
        goto exit;
    }

 exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(this->_function_state->channel != nullptr){
            delete this->_function_state->channel;
        }
    }
    return retval;
}


nicc_retval_t ComponentBlock_DPA::__init_wrapper_resources(AppFunction *app_func, AppHandler *app_handler, ComponentFuncState_DPA_t *func_state){
    int ret = 0;
    uint64_t rpc_ret_val;
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(app_handler);
    NICC_CHECK_POINTER(app_handler->binary.dpa.host_stub);
    NICC_CHECK_POINTER(func_state);

    if(unlikely(FLEXIO_STATUS_SUCCESS != (
        ret = flexio_process_call(
            func_state->flexio_process,
            *(reinterpret_cast<flexio_func_t*>(app_handler->binary.dpa.host_stub)),
            &rpc_ret_val,
            func_state->channel->dev_metadata_for_prior,
            func_state->channel->dev_metadata_for_next
        )
    ))){
        NICC_WARN_C(
            "failed to call init handler for initializing on-device resources: "
            "flexio_process(%p), flexio_retval(%u)",
            func_state->flexio_process, ret
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        return retval;
    }
    return retval;
}


nicc_retval_t ComponentBlock_DPA::__unregister_event_handler(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;
    flexio_status ret;

    NICC_CHECK_POINTER(func_state);

    if(likely(func_state->flexio_process != nullptr)){
        if(unlikely(FLEXIO_STATUS_SUCCESS != (
            ret = flexio_process_destroy(func_state->flexio_process) 
        ))){
            NICC_WARN_C("failed to destory process on DPA block: flexio_retval(%d)", ret);
            retval = NICC_ERROR_HARDWARE_FAILURE;
            return retval;
        }
    }
    return retval;
}


nicc_retval_t ComponentBlock_DPA::__deallocate_wrapper_resources(ComponentFuncState_DPA_t *func_state){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(func_state);

    // deallocate channel
    if(unlikely(NICC_SUCCESS !=(
        retval = this->_function_state->channel->deallocate_channel(func_state->flexio_process)
    ))){
        NICC_WARN_C(
            "failed to allocate and init DPA channel: nicc_retval(%u)", retval
        );
        return retval;
    }    

    return retval;
}

nicc_retval_t ComponentBlock_DPA::__add_control_plane_rule(struct mlx5dv_dr_domain *domain){
    int ret = 0;
    struct mlx5dv_flow_match_parameters *match_mask;
    struct mlx5dv_dr_action *actions[1];
    nicc_retval_t retval = NICC_SUCCESS;
    size_t flow_match_size;
    doca_error_t result = DOCA_SUCCESS;
    const int actions_len = 1;

    struct dr_flow_table *rx_flow_table;
    struct dr_flow_rule *rx_rule;

    // allocate match mask
    flow_match_size = sizeof(*match_mask) + 64;
    match_mask = (struct mlx5dv_flow_match_parameters *)calloc(1, flow_match_size);
    if(unlikely(match_mask == nullptr)){
        NICC_WARN_C("failed to allocate match mask");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }
    match_mask->match_sz = 64;
    // fill match mask, match on all source mac bits
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_47_16, 0xffffffff);
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_15_0, 0xffff);

    // create flow table
    result = __create_flow_table(domain, 0, 0, match_mask, &rx_flow_table);
    if(unlikely(result != DOCA_SUCCESS)){
        NICC_WARN_C("failed to create RX flow table");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

    // create rule
    rx_rule = (struct dr_flow_rule *)calloc(1, sizeof(struct dr_flow_rule));
    if(unlikely(rx_rule == nullptr)){
        NICC_WARN_C("failed to allocate memory for rx rule");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit_with_table;
    }

    // Create action to forward traffic to the DPA RQ
    rx_rule->dr_action = mlx5dv_dr_action_create_dest_devx_tir(
        flexio_rq_get_tir(this->_function_state->channel->get_flexio_rq_ptr(true))
    );
    if(unlikely(rx_rule->dr_action == nullptr)){
        NICC_WARN_C("failed to create rx rule action");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit_with_rule;
    }

    // Set the rule with the action
    actions[0] = rx_rule->dr_action;

    // Fill rule match, using a wildcard match to catch all traffic
    // You might want to modify this to match specific traffic patterns
#define SRC_MAC (0xa088c2bf464e)
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_47_16, SRC_MAC >> 16);
    DEVX_SET(dr_match_spec, match_mask->match_buf, smac_15_0, SRC_MAC % (1 << 16));

    // Create the rule
    rx_rule->dr_rule = mlx5dv_dr_rule_create(rx_flow_table->dr_matcher, match_mask, actions_len, actions);
    if(unlikely(rx_rule->dr_rule == nullptr)){
        NICC_WARN_C("failed to create rx rule");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit_with_action;
    }

    // Clean up and return
    free(match_mask);
    return retval;

exit_with_action:
    if(rx_rule->dr_action)
        mlx5dv_dr_action_destroy(rx_rule->dr_action);

exit_with_rule:
    free(rx_rule);

exit_with_table:
    if(rx_flow_table) {
        if(rx_flow_table->dr_matcher)
            mlx5dv_dr_matcher_destroy(rx_flow_table->dr_matcher);
        if(rx_flow_table->dr_table)
            mlx5dv_dr_table_destroy(rx_flow_table->dr_table);
        free(rx_flow_table);
    }

exit:
    if(match_mask != nullptr){
        free(match_mask);
    }

    return retval;
}

} // namespace nicc
