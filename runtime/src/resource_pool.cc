#include "resource_pool.h"

namespace nicc {

/*!
 *  \brief  initialization of resource pool
 *  \param  enabled_components  identify which component to be activated
 *  \param  config_map          component id -> component configuration
 */
nicc_retval_t ResourcePool::ResourcePool(nicc_component_id_t enabled_components, std::map<nicc_component_id_t, void*> &&config_map){
    nicc_retval_t tmp_retval;
    typename std::map<nicc_component_id_t, void*>::iterator config_map_iter;
    nicc_component_id_t enabled_mask = NICC_ENABLE_EMPTY_MASK;
    Component *component;

    this->_enabled_components = enabled_components;

    // readin component configurations
    for(config_map_iter = config_map.begin(); config_map_iter != config_map.end(); config_map_iter++){
        if(unlikely(!(enabled_components & config_map_iter->first))){
            NICC_WARN_C("component is not enabled yet configuration is provided, skipped: component_id(%u)", config_map_iter->first);
            continue;
        }
        enabled_components ^= (config_map_iter->first & NICC_ENABLE_FULL_MASK);
    }
    if(unlikely(enabled_components != 0)){
        NICC_ERROR_C("failed to initialize resource pool, unmatch enabled mask and configurations provided");
    }

    // initialize components
    for(config_map_iter = config_map.begin(); config_map_iter != config_map.end(); config_map_iter++){
        switch(config_map_iter->first){
            case kComponent_FlowEngine:
                component = new Component_FlowEngine();
                break;
            case kComponent_DPA:
                component = new Component_DPA();
                break;
            default:
                NICC_ERROR_C_DETAIL(
                    "unregonized component id, this is a bug: component_id(%u)",
                    config_map_iter->first
                );
        }
        
        if(unlikely(NICC_SUCCESS != component->init(config_map_iter->second))){
            NICC_WARN_C("failed to initialize component: component_id(%u)", config_map_iter->first);
            continue;
        }
        
        this->_component_map.insert({ config_map_iter->first, component });
    }
}

/*!
 *  \brief  allocate resource from enabled components
 *  \param  cid     index of the componen to allocate resource on
 *  \param  desp    configration description of the block to be allocated
 *  \param  cb      the allocated component block
 */
nicc_retval_t ResourcePool::allocate(nicc_component_id_t cid, void *desp, ComponentBlock** cb){
    nicc_retval_t retval = NICC_SUCCESS;
    Component *component = nullptr;

    NICC_CHECK_POINTER(desp);
    NICC_CHECK_POINTER(cb);

    if(unlikely(this->_component_map.count(cid) == 0)){
        NICC_WARN_C("failed to allocate compoent block, not enabled: component_id(%u)", cid);
        retval = NICC_ERROR_NOT_FOUND;
    }
    NICC_CHECK_POINTER(component = reinterpret_cast<Component*>(this->_component_map[cid]));
    
    if(unlikely(
        NICC_SUCCESS != (retval = component->allocate_block(desp, cb))
    )){
        NICC_WARN_C("failed to allocate component block: component_id(%u), retval(%u)", cid, retval);
    }

exit:
    return retval;
}

} // namespace nicc
