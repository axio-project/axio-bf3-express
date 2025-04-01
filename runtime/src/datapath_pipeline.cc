#include "datapath_pipeline.h"

namespace nicc {

DatapathPipeline::DatapathPipeline(ResourcePool& rpool, AppContext* app_cxt, device_state_t& device_state, AppDAG* app_dag)
    : _app_cxt(app_cxt), _app_dag(app_dag) 
{                                    
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(app_cxt);
    NICC_CHECK_POINTER(device_state.device_name);

    // allocate component block from resource pool
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_component_blocks(rpool, device_state)
    ))){
        NICC_WARN_C("failed to allocate component blocks from resource pool: retval(%u)", retval);
        goto exit;
    }

    // register functions onto each component block
    if(unlikely(NICC_SUCCESS != (
        retval = this->__register_functions(device_state)
    ))){
        NICC_WARN_C("failed to register functions onto component blocks: retval(%u)", retval);
        goto deallocate_cb;
    }

    // \todo initialize control plane (e.g., update MAT and connect channels for each component)
    if(unlikely(NICC_SUCCESS != (
        retval = this->__init_control_plane(device_state)
    ))){
        NICC_WARN_C("failed to initialize control plane: retval(%u)", retval);
        goto deallocate_cb;
    }

    // run the pipeline
    if(unlikely(NICC_SUCCESS != (
        retval = this->__run_pipeline()
    ))){
        NICC_WARN_C("failed to run the pipeline: retval(%u)", retval);
        goto exit;
    }

    // \todo remove this
    while (1) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    goto exit;

deallocate_cb:
    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_component_blocks(rpool)
    ))){
        NICC_WARN_C("failed to return component blocks back to resource pool: retval(%u)", retval);
        goto exit;
    }

exit:
    ;
}


DatapathPipeline::~DatapathPipeline() {
  /* Free all resources */
}


/*!
 *  \brief  initialization of each dataplane component
 *  \param  rpool           global resource pool
 *  \param  device_state    global device state
 */ 
nicc_retval_t DatapathPipeline::__allocate_component_blocks(ResourcePool& rpool, device_state_t& device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    ComponentBlock *component_block = nullptr;
    ComponentBlock_FlowEngine *component_block_flow_engine = nullptr;
    AppFunction *app_func = nullptr;
    typename std::map<AppFunction*, ComponentBlock*>::iterator cb_map_iter;

    NICC_CHECK_POINTER(this->_app_cxt);

    if(unlikely(this->_app_cxt->functions.size() == 0)){
        NICC_WARN_C("no function contains in current app context, no component block allocated: app_cxt(%p)", this->_app_cxt);
    }
    
    for(i=0; i<this->_app_cxt->functions.size(); i++){
        NICC_CHECK_POINTER(app_func = this->_app_cxt->functions[i]);

        component_block = nullptr;
        switch (app_func->component_id) {
            case kComponent_DPA:
                NICC_CHECK_POINTER(component_block = new ComponentBlock_DPA());
                break;
            case kComponent_FlowEngine:
                NICC_CHECK_POINTER(component_block = new ComponentBlock_FlowEngine());
                break;
            case kComponent_SoC:
                NICC_CHECK_POINTER(component_block = new ComponentBlock_SoC());
                break;
            default:
                NICC_ERROR_C("unknown component id: component_id(%u)", app_func->component_id);
                break;
        }

        retval = rpool.allocate(
            /* cid */ app_func->component_id,
            /* desp */ app_func->cb_desp,
            /* cb */ component_block
        );
        if(unlikely(NICC_SUCCESS != retval)){
            NICC_WARN_C(
                "failed to allocate new component block from resource pool: "
                "retval(%u), component_id(%u)",
                retval, app_func->component_id
            );
            goto exit;
        }
        NICC_CHECK_POINTER(component_block);
        this->_component_app2block_map.insert({ app_func, component_block });
        this->_component_blocks.push_back(component_block);
        
        // componeng-specific initialization
        switch (app_func->component_id) {
            case kComponent_FlowEngine:
                component_block_flow_engine = reinterpret_cast<ComponentBlock_FlowEngine*>(component_block);
                retval = component_block_flow_engine->init(device_state);
            default:
                break;
        }
    }
    
exit:
    if(unlikely(NICC_SUCCESS != retval)){
        for(cb_map_iter = this->_component_app2block_map.begin(); cb_map_iter != this->_component_app2block_map.end(); cb_map_iter++){
            NICC_CHECK_POINTER(cb_map_iter->first);
            NICC_CHECK_POINTER(cb_map_iter->second);
            retval = rpool.deallocate(
                /* cid */ cb_map_iter->first->component_id,
                /* cb */ cb_map_iter->second
            );
            if(unlikely(NICC_SUCCESS != retval)){
                NICC_WARN_C(
                    "failed to deallocate component block: retval(%u), component_id(%u)", retval, cb_map_iter->first->component_id
                );
                continue;
            }
        }
    }

    return retval;
}


/*!
 *  \brief  return allocated component block to the resource pool
 *  \param  rpool               global resource pool
 *  \return NICC_SUCCESS for successfully deallocation
 */ 
nicc_retval_t DatapathPipeline::__deallocate_component_blocks(ResourcePool& rpool){
    nicc_retval_t retval = NICC_SUCCESS;
    typename std::map<AppFunction*, ComponentBlock*>::iterator cb_map_iter;

    for(cb_map_iter = this->_component_app2block_map.begin(); cb_map_iter != this->_component_app2block_map.end(); cb_map_iter++){
        NICC_CHECK_POINTER(cb_map_iter->first);
        NICC_CHECK_POINTER(cb_map_iter->second);
        retval = rpool.deallocate(
            /* cid */ cb_map_iter->first->component_id,
            /* cb */ cb_map_iter->second
        );
        if(unlikely(NICC_SUCCESS != retval)){
            NICC_WARN_C(
                "failed to deallocate component block: retval(%u), component_id(%u)", retval, cb_map_iter->first->component_id
            );
            continue;
        }
    }

    return retval;
}


/*!
 *  \brief  register all functions onto the component block after allocation
 *  \param  device_state        global device state
 *  \return NICC_SUCCESS for successfully registration
 */ 
nicc_retval_t DatapathPipeline::__register_functions(device_state_t &device_state){
    nicc_retval_t retval = NICC_SUCCESS;
    typename std::map<AppFunction*, ComponentBlock*>::iterator cb_map_iter;
    AppFunction *app_func;
    ComponentBlock *component_block;
    std::map<AppFunction*, ComponentBlock*> __register_pairs;

    for(cb_map_iter = this->_component_app2block_map.begin(); cb_map_iter != this->_component_app2block_map.end(); cb_map_iter++){
        NICC_CHECK_POINTER(app_func = cb_map_iter->first);
        NICC_CHECK_POINTER(component_block = cb_map_iter->second);

        if(unlikely(NICC_SUCCESS != (
            retval = component_block->register_app_function(app_func, device_state)
        ))){
            NICC_WARN_C(
                "failed to register app function onto the component block: retval(%u), component_id(%u)",
                retval, app_func->component_id
            );
            goto exit;
        }
        else{
            NICC_DEBUG_C(
                "successfully register app function onto the component block: component_id(%u)",
                app_func->component_id
            );
        }

        __register_pairs.insert({ app_func, component_block });
    }

exit:
    if(unlikely(retval != NICC_SUCCESS)){
        for(cb_map_iter = __register_pairs.begin(); cb_map_iter != __register_pairs.end(); cb_map_iter++){
            NICC_CHECK_POINTER(app_func = cb_map_iter->first);
            NICC_CHECK_POINTER(component_block = cb_map_iter->second);

            if(unlikely(NICC_SUCCESS != (
                retval = component_block->unregister_app_function()
            ))){
                NICC_WARN_C(
                    "failed to unregister app function on the component block: retval(%u), component_id(%u)",
                    retval, app_func->component_id
                );
                continue;
            }
        }
    }

    return retval;
}

nicc_retval_t DatapathPipeline::__init_control_plane(device_state_t &device_state) {
    nicc_retval_t retval = NICC_SUCCESS;
    std::vector<std::string> remote_connect_to = this->_app_dag->get_host_config("remote")->connect_to;
    std::vector<std::string> local_connect_to = this->_app_dag->get_host_config("local")->connect_to;

    // Obtain the remote host and local host's QPInfo
    QPInfo remote_qp_info, local_qp_info;
    bool is_connected_to_remote = false, is_connected_to_local = false;
    TCPServer mgnt_server(this->_app_dag->get_host_config("remote")->mgnt_port);
    TCPClient mgnt_client;
    memset(&remote_qp_info, 0, sizeof(QPInfo));
    memset(&local_qp_info, 0, sizeof(QPInfo));
    mgnt_client.connectToServer(this->_app_dag->get_host_config("local")->ipv4.c_str(), this->_app_dag->get_host_config("local")->mgnt_port);
    local_qp_info.deserialize(mgnt_client.receiveMsg());

    mgnt_server.acceptConnection();
    remote_qp_info.deserialize(mgnt_server.receiveMsg());

    /* iteratively component blocks in the app DAG */
    typename std::vector<ComponentBlock*>::iterator cb_iter;
    ComponentBlock *prior_component_block = nullptr, *cur_component_block = nullptr;
    for(cb_iter = this->_component_blocks.begin(); cb_iter != this->_component_blocks.end(); cb_iter++){
        NICC_CHECK_POINTER(cur_component_block = *cb_iter);
        std::string component_name = cur_component_block->get_block_name();

        /* connect channels based on the app DAG */
        if (std::find(remote_connect_to.begin(), remote_connect_to.end(), component_name) != remote_connect_to.end()) {
            is_connected_to_remote = true;
        }
        
        if (std::find(local_connect_to.begin(), local_connect_to.end(), component_name) != local_connect_to.end()) {
            is_connected_to_local = true;
        }
        
        if(unlikely(NICC_SUCCESS != (retval = cur_component_block->connect_to_neighbour(
                                    /* prior block */ prior_component_block,
                                    /* next block*/ nullptr,
                                    is_connected_to_remote,
                                    /* remote qp info */ &remote_qp_info,
                                    is_connected_to_local,
                                    /* local qp info */ &local_qp_info)))){
            NICC_WARN_C("failed to connect to neighbour component: retval(%u)", retval);
            return retval;
        }
        // check if the block is connected to both remote and local
        if(is_connected_to_remote){
            // complete the connection to remote host
            mgnt_server.sendMsg(cur_component_block->get_qp_info(true)->serialize());
            
        }
        if(is_connected_to_local){
            // complete the connection to local host
            mgnt_client.sendMsg(cur_component_block->get_qp_info(false)->serialize());
        }

        if (prior_component_block != nullptr){
            if(unlikely(NICC_SUCCESS != (retval = prior_component_block->connect_to_neighbour(
                                        /* prior block */ nullptr,
                                        /* next block*/ cur_component_block,
                                        false,
                                        /* remote qp info */ nullptr,
                                        false,
                                        /* local qp info */ nullptr)))){
                NICC_WARN_C("failed to connect to neighbour component: retval(%u)", retval);
                return retval;
            }
        }
        prior_component_block = cur_component_block;

        /* \todo add control plane rule to redirect all traffic to the component block */
        /// Currently, only DPA component block has control plane rule
        if (cur_component_block->get_component_id() == kComponent_DPA) {
            ComponentBlock_DPA *dpa_block = reinterpret_cast<ComponentBlock_DPA*>(cur_component_block);
            dpa_block->add_control_plane_rule(device_state.rx_domain);
        }
    }
    // Check if remote host and local host are connected
    if (!(is_connected_to_remote && is_connected_to_local)){
        NICC_ERROR_C("failed to init control plane: failed to connect to both remote and local host");
        return NICC_ERROR;
    }
    return retval;
}

nicc_retval_t DatapathPipeline::__run_pipeline() {
    nicc_retval_t retval = NICC_SUCCESS;
    typename std::vector<ComponentBlock*>::iterator cb_iter;
    for(cb_iter = this->_component_blocks.begin(); cb_iter != this->_component_blocks.end(); cb_iter++){
        NICC_CHECK_POINTER(*cb_iter);
        retval = (*cb_iter)->run_block();
        if(unlikely(retval != NICC_SUCCESS)){
            NICC_WARN_C("failed to run component block: retval(%u)", retval);
            goto exit;
        }
    }

exit:
    /// \todo remove all allocated resources
    return retval;
}

nicc_retval_t DatapathPipeline::__reschedule_block() {
    return NICC_SUCCESS;
}


} // namespace nicc
