#include "resources/component_impl/dpa_component.h"

namespace nicc {

nicc_retval_t Component_DPA::init(ComponentBaseDesp_t* desp) {
    nicc_retval_t retval = NICC_SUCCESS;
    /* Step 1: init desp, recording all hardware resources*/
    NICC_CHECK_POINTER(this->_desp = reinterpret_cast<ComponentDesp_DPA_t*>(desp));
    /* Step 2: init state, recording remained hardware resources*/
    NICC_CHECK_POINTER(this->_state = new ComponentState_DPA_t);
    this->_state->base_state.quota = desp->quota;
    /// specific state
    this->_state->mock_state = 0;
    return retval;
}

nicc_retval_t Component_DPA::allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentDesp_DPA_t *func_input_desp;
    ComponentBlock_DPA *desired_cb;

    NICC_CHECK_POINTER(func_input_desp = reinterpret_cast<ComponentDesp_DPA_t*>(desp));
    NICC_CHECK_POINTER(desired_cb = reinterpret_cast<ComponentBlock_DPA*>(cb));
    /* Step 1: Based on func_input_desp, update local state */
    /// base state
    // check quota
    if(unlikely(this->_state->base_state.quota < desp->quota)){
        NICC_WARN_C(
            "failed to allocate block due to unsufficient resource remained:"
            " component_id(%u), request(%lu), remain(%lu)",
            this->_cid, desp->quota, this->_state->base_state.quota
        )
        retval = NICC_ERROR_EXSAUSTED;
        goto exit;
    }
    this->_state->base_state.quota -= desp->quota;
    /// specific state
    this->_state->mock_state = 0;

    /* Step 2: allocate quota to the block (update desp) */
    /// base descriptor
    desired_cb->_desp->quota = desp->quota;
    /// specific descriptor
    desired_cb->_desp->device_name = func_input_desp->device_name;
    desired_cb->_desp->core_id = 0; // \todo allocate core group

    /* Step 3: set target cb's state to default */
    /// reset block state
    memset(desired_cb->_state, 0, sizeof(ComponentState_TEMPLATE_t));

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
    ComponentBlock_DPA *desired_cb = reinterpret_cast<ComponentBlock_DPA*>(cb);

    NICC_CHECK_POINTER(desired_cb);
    NICC_CHECK_POINTER(this->_state);
    /* Step 1: Based on cb, update local state */
    /// base state
    this->_state->quota += cb->_desp->quota;
    /// specific state
    /* ...... */
    delete desired_cb;

exit:
    return retval;
}

}