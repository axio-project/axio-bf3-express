#include "datapath_pipeline.h"

namespace nicc {


 /*! 
  *  \brief  initialization of datapath pipeline
  *  \param  rpool               global resource pool
  *  \param  app_cxt             the application context of this datapath pipeline
  *  \param  device_state        global device state
  */
DatapathPipeline::DatapathPipeline(ResourcePool& rpool, AppContext *app_cxt, device_state_t &device_state)
    : _app_cxt(app_cxt) 
{                                    
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(app_cxt);
    NICC_CHECK_POINTER(device_state.ibv_ctx);

    // allocate component block from resource pool
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_component_blocks(rpool)
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
 *  \param  rpool   global resource pool
 */ 
nicc_retval_t DatapathPipeline::__allocate_component_blocks(ResourcePool& rpool){
    nicc_retval_t retval = NICC_SUCCESS;
    uint64_t i;
    ComponentBlock *component_block = nullptr;
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
                NICC_CHECK_POINTER(component_block = new ComponentBlock_DPA);
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
        this->_component_block_map.insert({ app_func, component_block });
    }
    
exit:
    if(unlikely(NICC_SUCCESS != retval)){
        for(cb_map_iter = this->_component_block_map.begin(); cb_map_iter != this->_component_block_map.end(); cb_map_iter++){
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

    for(cb_map_iter = this->_component_block_map.begin(); cb_map_iter != this->_component_block_map.end(); cb_map_iter++){
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

    for(cb_map_iter = this->_component_block_map.begin(); cb_map_iter != this->_component_block_map.end(); cb_map_iter++){
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


} // namespace nicc
