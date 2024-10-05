#include "datapath/block_impl/flow_engine.h"

namespace nicc {


nicc_retval_t FlowMatcher_FlowEngine::__create_mlx5dv_dr_matcher(){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(this->_mlx5dv_dr_tbl);

    // TODO: convert flow_wildcards_t to struct mlx5dv_flow_match_parameters
    
exit:
    return retval;
}


nicc_retval_t FlowMAT_FlowEngine::__create_mlx5dv_dr_table(){
    nicc_retval_t retval = NICC_SUCCESS;
    uint8_t criteria_enable;

    NICC_CHECK_POINTER(this->_mlx5dv_dr_fdb_domain);

    // TODO: delete this
    ///! \ref see doc 8.12.1
    criteria_enable = 0;
    NICC_SET_BIT(criteria_enable, 0, 0xFF); // matching packet header field
    NICC_SET_BIT(criteria_enable, 2, 0xFF); // matching packet meta-data field

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


nicc_retval_t FlowMAT_FlowEngine::__create_matcher(flow_wildcards_t wc, int priority, nicc_uint8_t criteria, FlowMatcher** matcher){
    nicc_retval_t retval = NICC_SUCCESS;
    FlowMatcher_FlowEngine *matcher_fe;

    NICC_CHECK_POINTER(matcher);
    NICC_CHECK_POINTER(this->_mlx5dv_dr_tbl);

    NICC_CHECK_POINTER(matcher_fe = new FlowMatcher_FlowEngine(
        /* match_wc */ wc,
        /* tbl */ this->_mlx5dv_dr_tbl,
        /* priority */ priority,
        /* criteria */ criteria
    ));

    *matcher = reinterpret_cast<FlowMatcher*>(matcher_fe);

exit:
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
