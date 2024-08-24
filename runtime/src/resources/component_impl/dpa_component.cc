#include "resources/component_impl/dpa_component.h"

namespace nicc {

/*!
 *  \brief  initialization of the components
 *  \param  desp    descriptor to initialize the component
 *  \return NICC_SUCCESS for successful initialization
 */
nicc_retval_t Component_DPA::init(ComponentBaseDesp_t* desp) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentState_DPA_t *dpa_state;

    // bind descriptor
    NICC_CHECK_POINTER(this->_desp = desp);

    // allocate and bind state
    NICC_CHECK_POINTER(dpa_state = new ComponentState_DPA_t);
    this->_state = reinterpret_cast<ComponentBaseState_t*>(dpa_state);
    this->_state->quota = this->_desp->quota;

    return retval;
}


/*!
 *  \brief  apply block of resource from the component
 *  \param  desp    descriptor for allocation
 *  \param  cb      the handle of the allocated block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t Component_DPA::allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentDesp_DPA_t *dpa_block_desp;
    ComponentDesp_DPA_t *func_input_desp;
    ComponentState_DPA_t *dpa_block_state;

    NICC_CHECK_POINTER(desp);
    NICC_CHECK_POINTER(cb);

    // allocate from resource pool
    if(unlikely(NICC_SUCCESS != (
        retval = this->__allocate_block(desp, cb)
    ))){
        NICC_WARN_C("failed to allocate block: retval(%u)", retval);
        goto exit;
    }

    // update DPA specific descriptor and state
    func_input_desp = reinterpret_cast<ComponentDesp_DPA_t*>(desp);
    dpa_block_desp = reinterpret_cast<ComponentDesp_DPA_t*>(cb->_desp);
    dpa_block_desp->device_name = func_input_desp->device_name;
    dpa_block_state = reinterpret_cast<ComponentState_DPA_t*>(cb->_state);
    dpa_block_state->mock_state = 0;
    // ComponentBlock_DPA *dpa_cb = reinterpret_cast<ComponentBlock_DPA*>(cb);

    // cb->_state = reinterpret_cast<ComponentBaseState_t*>(dpa_block_state);
    ///!    \todo   transfer state between component and the created block

exit:
    return retval;
}


/*!
 *  \brief  return block of resource back to the component
 *  \param  cb          the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_DPA::deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentBlock_DPA *dpa_cb = reinterpret_cast<ComponentBlock_DPA*>(cb);

    NICC_CHECK_POINTER(dpa_cb);

    if(unlikely(NICC_SUCCESS != (
        retval = this->__deallocate_block(dpa_cb)
    ))){
        NICC_WARN_C("failed to deallocate block: retval(%u)", retval);
        goto exit;
    }

exit:
    return retval;
}

}