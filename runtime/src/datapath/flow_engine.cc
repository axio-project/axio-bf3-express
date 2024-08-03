#include <iostream>

#include "common.h"
#include "datapath/flow_engine.h"

namespace nicc {

/*!
 *  \brief  initialization of the components
 *  \return NICC_SUCCESS for successful initialization
 */
nicc_retval_t Component_FlowEngine::init(ComponentBaseConfig_t* desp) override {
    nicc_retval_t retval = NICC_SUCCESS;

    ComponentConfig_FlowEngine_t *flow_engine_desp 
        = reinterpret_cast<ComponentConfig_FlowEngine_t*>(desp);
    NICC_CHECK_POINTER(flow_engine_desp);

exit:
    return retval;
}

/*!
 *  \brief  apply block of resource from the component
 *  \param  desp    descriptor for allocation
 *  \param  cb      the handle of the allocated block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t Component_FlowEngine::allocate_block(ComponentBaseConfig_t* desp, ComponentBlock* cb) override {
    nicc_retval_t retval = NICC_SUCCESS;

    ComponentConfig_FlowEngine_t *flow_engine_desp 
        = reinterpret_cast<ComponentConfig_FlowEngine_t*>(desp);
    NICC_CHECK_POINTER(flow_engine_desp);

exit:
    return retval;
}

/*!
 *  \brief  return block of resource back to the component
 *  \param  cb      the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_FlowEngine::deallocate_block(ComponentBlock* cb) override {
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(cb);

exit:
    return retval;
}


} // namespace nicc
