#include "datapath/block_impl/flow_engine.h"

namespace nicc {


nicc_retval_t FlowMatcher_FlowEngine::__create_mlx5dv_dr_matcher(){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(this->_mlx5dv_dr_tbl);
    NICC_CHECK_POINTER(this->_mlx5dv_dr_match_parameters);

    this->_mlx5dv_matcher = mlx5dv_dr_matcher_create(
        this->_mlx5dv_dr_tbl,
        this->_mlx5dv_dr_match_priority,
        this->_mlx5dv_dr_match_criteria,
        this->_mlx5dv_dr_match_parameters
    );
    if(unlikely(this->_mlx5dv_matcher == nullptr)){
        NICC_WARN_C("failed to create mlx5dv_dr_matcher");
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    return retval;
}


nicc_retval_t FlowMatcher_FlowEngine::convert_flow_wildcard_to_mlx5dv_dr(
    flow_wildcards_t *wc, struct mlx5_ifc_dr_match_param_bits *match_param_bits
){
    nicc_retval_t retval = NICC_SUCCESS;
    flow_t *masks = nullptr;

    NICC_CHECK_POINTER(wc);
    NICC_CHECK_POINTER(match_param_bits);
    NICC_CHECK_POINTER(masks = &wc->masks);

    /* NICC platform fields */
    if(masks->core_retval != 0){
        // TODO: which register we want to use?
    }

    /* Physical fields */
    if(masks->in_port != 0){
        DEVX_SET(dr_match_set_misc, &match_param_bits->misc, source_port, 0xffff); // 16b
    }

    /* L2 fields */
    if(masks->dl_src != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, smac_47_16, 0xffffffff);   // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, smac_15_0, 0xffff);        // 16b
    }
    if(masks->dl_dst != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dmac_47_16, 0xffffffff);   // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dmac_15_0, 0xffff);        // 16b
    }
    if(masks->dl_type != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, ethertype, 0xffff);        // 16b
    }

    /* L3 fields */
    if(masks->nw_src != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, src_ip_127_96, 0xffffffff);    // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, src_ip_95_64, 0xffffffff);     // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, src_ip_63_32, 0xffffffff);     // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, src_ip_31_0, 0xffffffff);      // 32b
    }
    if(masks->nw_dst != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dst_ip_127_96, 0xffffffff);    // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dst_ip_95_64, 0xffffffff);     // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dst_ip_63_32, 0xffffffff);     // 32b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, dst_ip_31_0, 0xffffffff);      // 32b
    }

    /* L4 fields */
    if(masks->tp_src != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, tcp_sport, 0xffff);    // 16b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, udp_sport, 0xffff);    // 16b
    }
    if(masks->tp_dst != 0){
        DEVX_SET(dr_match_spec, &match_param_bits->outer, tcp_dport, 0xffff);    // 16b
        DEVX_SET(dr_match_spec, &match_param_bits->outer, udp_dport, 0xffff);    // 16b
    }

exit:
    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__create_mlx5dv_dr_table(){
    nicc_retval_t retval = NICC_SUCCESS;
    uint8_t criteria_enable;

    NICC_CHECK_POINTER(this->_mlx5dv_dr_fdb_domain);
    NICC_CHECK_POINTER(this->_mlx5dv_dr_fdb_domain);

    // create table
    this->_mlx5dv_dr_tbl = mlx5dv_dr_table_create(this->_mlx5dv_dr_fdb_domain, this->_mlx5dv_dr_tbl_level);
    if(unlikely(this->_mlx5dv_dr_tbl == nullptr)){
        NICC_WARN_C(
            "failed to create mlx5dv_dr table: fdb_domain(%p), level(%d)",
            this->_mlx5dv_dr_fdb_domain, this->_mlx5dv_dr_tbl_level
        );
        retval = NICC_ERROR_HARDWARE_FAILURE;
        goto exit;
    }

exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(this->_mlx5dv_dr_tbl != nullptr){ mlx5dv_dr_table_destroy(this->_mlx5dv_dr_tbl); }
    }

    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__create_matcher(flow_wildcards_t wc, int priority, FlowMatcher** matcher){
    nicc_retval_t retval = NICC_SUCCESS;
    FlowMatcher_FlowEngine *matcher_fe;
    nicc_uint8_t criteria;
    struct mlx5dv_flow_match_parameters *match_param = nullptr;
    struct mlx5_ifc_dr_match_param_bits *match_param_bits = nullptr;

    NICC_CHECK_POINTER(matcher);
    NICC_CHECK_POINTER(this->_mlx5dv_dr_tbl);

    ///! \ref see doc 8.12.1
    NICC_SET_MASK(criteria, 0, 0xFF);   // set to all zero
    NICC_SET_BIT(criteria, 0, 0xFF);    // matching packet header field
    NICC_SET_BIT(criteria, 2, 0xFF);    // matching packet meta-data field

    // create mlx5dv_flow_match_parameters
    NICC_CHECK_POINTER(match_param = (struct mlx5dv_flow_match_parameters *)
            calloc(1, sizeof(uint64_t)+sizeof(struct mlx5_ifc_dr_match_param_bits))
    );
    match_param->match_sz = sizeof(struct mlx5_ifc_dr_match_param_bits);

    // convert flow_wildcard to struct mlx5_ifc_dr_match_param_bits*
    NICC_CHECK_POINTER(match_param_bits = (struct mlx5_ifc_dr_match_param_bits*)match_param->match_buf);
    if(unlikely(NICC_SUCCESS != (
        retval = FlowMatcher_FlowEngine::convert_flow_wildcard_to_mlx5dv_dr(&wc, match_param_bits)
    ))){
        goto exit;
    }

    NICC_CHECK_POINTER(matcher_fe = new FlowMatcher_FlowEngine(
        /* match_wc */ wc,
        /* tbl */ this->_mlx5dv_dr_tbl,
        /* match_param */ match_param,
        /* priority */ priority,
        /* criteria */ criteria
    ));

    *matcher = reinterpret_cast<FlowMatcher*>(matcher_fe);

exit:
    if(unlikely(retval != NICC_SUCCESS)){
        if(match_param_bits != nullptr){ delete match_param_bits; }
    }
    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__destory_matcher(FlowMatcher* matcher){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__load_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__unload_entries(FlowMatcher& matcher, flow_entry_t *entries, nicc_uint64_t len){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


nicc_retval_t FlowDomain_FlowEngine::__create_table(int level, FlowMAT** table){
    nicc_retval_t retval = NICC_SUCCESS;
    FlowMAT_FlowEngine *table_fe = nullptr;

    NICC_CHECK_POINTER(table);

    NICC_CHECK_POINTER( table_fe = new FlowMAT_FlowEngine(level, this->_fdb_domain) );

    *table = reinterpret_cast<FlowMAT*>(table_fe);

exit:
    return retval;
}


nicc_retval_t FlowDomain_FlowEngine::__detory_table(int level, FlowMAT* table){
    nicc_retval_t retval = NICC_SUCCESS;

exit:
    return retval;
}


nicc_retval_t ComponentBlock_FlowEngine::init(device_state_t& dev_state){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(dev_state.ibv_ctx);
    NICC_CHECK_POINTER(dev_state.fdb_domain);

    this->_base_domain = new FlowDomain_FlowEngine(
        /* fdb_domain */ dev_state.fdb_domain
    );
    NICC_CHECK_POINTER(this->_base_domain);

exit:
    return retval;
}


nicc_retval_t ComponentBlock_FlowEngine::register_app_function(AppFunction *app_func, device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    AppHandler *app_handler = nullptr, *init_handler = nullptr, *event_handler = nullptr;
    NICC_CHECK_POINTER(this->_function_state = new ComponentFuncState_FlowEngine_t());

    /* Step 1: Register app_func */
    NICC_CHECK_POINTER(app_func);
    NICC_CHECK_POINTER(this->_desp);    // register func must be after component block alloaction
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
    /* Step 2: Allocate and record funciton state */
    /* ...... */
    
 exit:
    return retval;
}


nicc_retval_t ComponentBlock_FlowEngine::unregister_app_function(){
    nicc_retval_t retval = NICC_SUCCESS;
    // obtain function state
    NICC_CHECK_POINTER( this->_function_state );
    // deallocate all resources for registered funcitons
    /* ...... */
exit:
    return retval;
}


} // namespace nicc
