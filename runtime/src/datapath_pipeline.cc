#include "datapath_pipeline.h"

namespace nicc {

DatapathPipeline::DatapathPipeline(ResourcePool& rpool, AppContext* app_cxt, device_state_t& device_state, AppDAG* app_dag)
    : _app_cxt(app_cxt), _app_dag(app_dag) 
{                                    
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(app_cxt);
    NICC_CHECK_POINTER(device_state.device_name);
    NICC_CHECK_POINTER(app_dag);
    
    // Create and initialize PipelineRouting
    this->_pipeline_routing = new PipelineRouting();
    NICC_CHECK_POINTER(this->_pipeline_routing);
    
    // Load routing configuration from AppDAG
    retval = this->_pipeline_routing->load_from_app_dag(this->_app_dag);
    if (retval != NICC_SUCCESS) {
        NICC_ERROR_C("Failed to load routing configuration from AppDAG: retval(%u)", retval);
        goto exit;
    }

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
  if (this->_pipeline_routing) {
    delete this->_pipeline_routing;
    this->_pipeline_routing = nullptr;
  }
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
    
    // Step 1: Register component routers to PipelineRouting and register local channels
    for (auto* component_block : this->_component_blocks) {
        NICC_CHECK_POINTER(component_block);
        
        ComponentRouting* component_routing = component_block->get_component_routing();
        if (component_routing != nullptr) {
            std::string component_name = component_block->get_block_name();
            
            // Register component router to pipeline routing
            retval = this->_pipeline_routing->register_component_router(component_name, component_routing);
            if (retval != NICC_SUCCESS) {
                NICC_ERROR_C("Failed to register component router for %s: retval(%u)", 
                           component_name.c_str(), retval);
                goto exit;
            }
            
            // Register local channels for SoC components
            if (component_block->get_component_id() == kComponent_SoC) {
                ComponentBlock_SoC* soc_block = static_cast<ComponentBlock_SoC*>(component_block);
                retval = soc_block->register_local_channels();
                if (retval != NICC_SUCCESS) {
                    NICC_ERROR_C("Failed to register local channels for SoC component %s: retval(%u)", 
                               component_name.c_str(), retval);
                    goto exit;
                }
            }

            // Register retval-to-channel mapping based on component's ctrl_path configuration
            retval = this->__init_retval_mappings(component_block, component_routing);
            if (retval != NICC_SUCCESS) {
                NICC_ERROR_C("Failed to initialize retval mappings for component %s: retval(%u)", 
                           component_name.c_str(), retval);
                goto exit;
            }
            
        } else {
            NICC_WARN("Component %s has no routing instance", component_block->get_block_name().c_str());
        }
    }
    
    // Step 2: Build channel connections based on DAG edge rules
    retval = this->__build_channel_connections(device_state);
    if (retval != NICC_SUCCESS) {
        NICC_ERROR_C("Failed to build channel connections: retval(%u)", retval);
        goto exit;
    }
    
    NICC_LOG("Control plane initialization completed successfully");
    
exit:
    return retval;
}

nicc_retval_t DatapathPipeline::__init_retval_mappings(ComponentBlock* component_block, ComponentRouting* component_routing) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    std::string component_name = component_block->get_block_name();
    
    // Get component configuration from AppDAG
    const DAGComponent* dag_component = this->_app_dag->get_component_config(component_name);
    if (!dag_component) {
        NICC_WARN("No DAG configuration found for component '%s'", component_name.c_str());
        return NICC_SUCCESS; // Not an error, component might not have ctrl_path
    }
    
    // Process ctrl_entries to build retval-to-channel mappings
    for (const auto& entry : dag_component->ctrl_entries) {
        auto match_it = entry.find("match");
        auto action_it = entry.find("action");
        
        if (match_it == entry.end() || action_it == entry.end()) {
            NICC_WARN("Invalid ctrl_entry format in component '%s'", component_name.c_str());
            continue;
        }
        
        std::string match_str = match_it->second;
        std::string action_str = action_it->second;
        
        // Parse retval from match string (e.g., "retval=1")
        if (match_str.find("retval=") == 0) {
            std::string retval_str = match_str.substr(7); // Skip "retval="
            try {
                nicc_core_retval_t kernel_retval = std::stoi(retval_str);
                
                // Parse action to determine target channel
                // For now, use default channel for all actions
                // TODO: Parse action properly and get target channel
                nicc::Channel* target_channel = component_routing->get_local_channel("default");
                if (target_channel) {
                    retval = component_routing->add_retval_mapping(kernel_retval, target_channel);
                    if (retval != NICC_SUCCESS) {
                        NICC_ERROR_C("Failed to add retval mapping for component %s: retval=%u -> channel: retval(%u)", 
                                   component_name.c_str(), kernel_retval, retval);
            return retval;
        }
                    
                } else {
                    NICC_ERROR("Default channel not found for component '%s'", component_name.c_str());
                    return NICC_ERROR;
                }
                
            } catch (const std::exception& e) {
                NICC_WARN("Failed to parse retval from match string '%s': %s", match_str.c_str(), e.what());
            }
        }
    }
    
    return retval;
}

// nicc_retval_t DatapathPipeline::__init_control_plane(device_state_t &device_state) {
//     nicc_retval_t retval = NICC_SUCCESS;
//     std::vector<std::string> remote_connect_to = this->_app_dag->get_host_config("remote")->connect_to;
//     std::vector<std::string> local_connect_to = this->_app_dag->get_host_config("local")->connect_to;

//     // Obtain the remote host and local host's QPInfo
//     QPInfo remote_host_qp_info, local_host_qp_info;
//     bool is_connected_to_remote = false, is_connected_to_local = false;
//     TCPServer mgnt_server(this->_app_dag->get_host_config("remote")->mgnt_port);
//     TCPClient mgnt_client;
//     memset(&remote_host_qp_info, 0, sizeof(QPInfo));
//     memset(&local_host_qp_info, 0, sizeof(QPInfo));
//     mgnt_client.connectToServer(this->_app_dag->get_host_config("local")->ipv4.c_str(), this->_app_dag->get_host_config("local")->mgnt_port);
//     local_host_qp_info.deserialize(mgnt_client.receiveMsg());

//     mgnt_server.acceptConnection();
//     remote_host_qp_info.deserialize(mgnt_server.receiveMsg());

//     /* iteratively component blocks in the app DAG */
//     typename std::vector<ComponentBlock*>::iterator cb_iter;
//     ComponentBlock *prior_component_block = nullptr, *cur_component_block = nullptr;
//     for(cb_iter = this->_component_blocks.begin(); cb_iter != this->_component_blocks.end(); cb_iter++){
//         NICC_CHECK_POINTER(cur_component_block = *cb_iter);
//         std::string component_name = cur_component_block->get_block_name();

//         /* connect channels based on the app DAG */
//         if (std::find(remote_connect_to.begin(), remote_connect_to.end(), component_name) != remote_connect_to.end()) {
//             is_connected_to_remote = true;
//         }
        
//         if (std::find(local_connect_to.begin(), local_connect_to.end(), component_name) != local_connect_to.end()) {
//             is_connected_to_local = true;
//         }
        
//         if(unlikely(NICC_SUCCESS != (retval = cur_component_block->connect_to_neighbour(
//                                     /* prior block */ prior_component_block,
//                                     /* next block*/ nullptr,
//                                     is_connected_to_remote,
//                                     /* remote qp info */ &remote_host_qp_info,
//                                     is_connected_to_local,
//                                     /* local qp info */ &local_host_qp_info)))){
//             NICC_WARN_C("failed to connect to neighbour component: retval(%u)", retval);
//             return retval;
//         }
//         // check if the block is connected to both remote and local
//         if(is_connected_to_remote){
//             // complete the connection to remote host
//             mgnt_server.sendMsg(cur_component_block->get_qp_info(true)->serialize());
            
//         }
//         if(is_connected_to_local){
//             // complete the connection to local host
//             mgnt_client.sendMsg(cur_component_block->get_qp_info(false)->serialize());
//         }

//         if (prior_component_block != nullptr){
//             if(unlikely(NICC_SUCCESS != (retval = prior_component_block->connect_to_neighbour(
//                                         /* prior block */ nullptr,
//                                         /* next block*/ cur_component_block,
//                                         false,
//                                         /* remote qp info */ nullptr,
//                                         false,
//                                         /* local qp info */ nullptr)))){
//                 NICC_WARN_C("failed to connect to neighbour component: retval(%u)", retval);
//                 return retval;
//             }
//         }
//         prior_component_block = cur_component_block;

//         /* \todo add control plane rule to redirect all traffic to the component block */
//         /// Currently, only DPA component block has control plane rule
//         if (cur_component_block->get_component_id() == kComponent_DPA) {
//             ComponentBlock_DPA *dpa_block = reinterpret_cast<ComponentBlock_DPA*>(cur_component_block);
//             dpa_block->add_control_plane_rule(device_state.rx_domain);
//         }
//     }
//     // Check if remote host and local host are connected
//     if (!(is_connected_to_remote && is_connected_to_local)){
//         NICC_ERROR_C("failed to init control plane: failed to connect to both remote and local host");
//         return NICC_ERROR;
//     }
//     return retval;
// }

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

nicc_retval_t DatapathPipeline::__build_channel_connections(device_state_t& device_state) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    // Get remote and local host connection information from AppDAG
    std::vector<std::string> remote_connect_to = this->_app_dag->get_host_config("remote")->connect_to;
    std::vector<std::string> local_connect_to = this->_app_dag->get_host_config("local")->connect_to;
    
    // Obtain the remote host and local host's QPInfo for connection setup
    QPInfo remote_host_qp_info, local_host_qp_info;
    bool is_connected_to_remote = false, is_connected_to_local = false;
    TCPServer mgnt_server(this->_app_dag->get_host_config("remote")->mgnt_port);
    TCPClient mgnt_client;
    
    memset(&remote_host_qp_info, 0, sizeof(QPInfo));
    memset(&local_host_qp_info, 0, sizeof(QPInfo));
    
    // Connect to local host management server
    mgnt_client.connectToServer(this->_app_dag->get_host_config("local")->ipv4.c_str(), 
                               this->_app_dag->get_host_config("local")->mgnt_port);
    local_host_qp_info.deserialize(mgnt_client.receiveMsg());
    
    // Accept connection from remote host management client
    mgnt_server.acceptConnection();
    remote_host_qp_info.deserialize(mgnt_server.receiveMsg());
    
    // Iterate through component blocks to establish connections
    ComponentBlock *prior_component_block = nullptr, *cur_component_block = nullptr;
    for (auto cb_iter = this->_component_blocks.begin(); cb_iter != this->_component_blocks.end(); cb_iter++) {
        NICC_CHECK_POINTER(cur_component_block = *cb_iter);
        std::string component_name = cur_component_block->get_block_name();
        
        // Check if this component should connect to remote host
        if (std::find(remote_connect_to.begin(), remote_connect_to.end(), component_name) != remote_connect_to.end()) {
            is_connected_to_remote = true;
        }
        
        // Check if this component should connect to local host  
        if (std::find(local_connect_to.begin(), local_connect_to.end(), component_name) != local_connect_to.end()) {
            is_connected_to_local = true;
        }
        
        // Connect current component to its neighbors (prior/next components and hosts)
        if (unlikely(NICC_SUCCESS != (retval = cur_component_block->connect_to_neighbour(
                                    /* prior block */ prior_component_block,
                                    /* next block*/ nullptr,
                                    is_connected_to_remote,
                                    /* remote qp info */ &remote_host_qp_info,
                                    is_connected_to_local,
                                    /* local qp info */ &local_host_qp_info)))) {
            NICC_WARN_C("Failed to connect component %s to neighbour: retval(%u)", component_name.c_str(), retval);
            return retval;
        }
        
        // Complete the connection handshake with remote host if needed
        if (is_connected_to_remote) {
            mgnt_server.sendMsg(cur_component_block->get_qp_info(true)->serialize());
        }
        
        // Complete the connection handshake with local host if needed
        if (is_connected_to_local) {
            mgnt_client.sendMsg(cur_component_block->get_qp_info(false)->serialize());
        }
        
        // Connect prior component to current component (bidirectional linking)
        if (prior_component_block != nullptr) {
            if (unlikely(NICC_SUCCESS != (retval = prior_component_block->connect_to_neighbour(
                                        /* prior block */ nullptr,
                                        /* next block*/ cur_component_block,
                                        false,
                                        /* remote qp info */ nullptr,
                                        false,
                                        /* local qp info */ nullptr)))) {
                NICC_WARN_C("Failed to connect prior component to current component: retval(%u)", retval);
                return retval;
            }
        }
        
        // Update prior component for next iteration
        prior_component_block = cur_component_block;
        
        // Add component-specific control plane rules if needed
        if (cur_component_block->get_component_id() == kComponent_DPA) {
            ComponentBlock_DPA *dpa_block = reinterpret_cast<ComponentBlock_DPA*>(cur_component_block);
            dpa_block->add_control_plane_rule(device_state.rx_domain);
        }
    }
    
    // Verify that both remote and local hosts are connected
    if (!(is_connected_to_remote && is_connected_to_local)) {
        NICC_ERROR_C("Failed to build channel connections: not connected to both remote and local hosts");
        return NICC_ERROR;
    }
    
    NICC_LOG("Channel connections built successfully");
    return NICC_SUCCESS;
}


} // namespace nicc
