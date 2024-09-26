#include "resources/component_impl/soc_component.h"

namespace nicc {

nicc_retval_t Component_SoC::init(ComponentBaseDesp_t* desp) {
    nicc_retval_t retval = NICC_SUCCESS;
    /* Step 1: init desp, recording all hardware resources*/
    NICC_CHECK_POINTER(this->_desp = reinterpret_cast<ComponentDesp_SoC_t*>(desp));
    NICC_CHECK_POINTER(this->_base_desp = &this->_desp->base_desp)
    /* Step 2: init state, recording remained hardware resources*/
    NICC_CHECK_POINTER(this->_state = new ComponentState_SoC_t);
    NICC_CHECK_POINTER(this->_base_state = &this->_state->base_state)
    this->_base_state->quota = desp->quota;
    return retval;
}

nicc_retval_t Component_SoC::allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentDesp_SoC_t *func_input_desp;
    ComponentBlock_SoC *desired_cb;

    NICC_CHECK_POINTER(func_input_desp = reinterpret_cast<ComponentDesp_SoC_t*>(desp));
    NICC_CHECK_POINTER(desired_cb = reinterpret_cast<ComponentBlock_SoC*>(cb));
    /* Step 1: Based on func_input_desp, update local state */
    /// base state
    // check quota
    if(unlikely(this->_base_state->quota < desp->quota)){
        NICC_WARN_C(
            "failed to allocate block due to unsufficient resource remained:"
            " component_id(%u), request(%lu), remain(%lu)",
            this->_cid, desp->quota, this->_base_state->quota
        )
        retval = NICC_ERROR_EXSAUSTED;
        goto exit;
    }
    this->_base_state->quota -= desp->quota;
    /// specific state
    /* ...... */

    /* Step 2: allocate quota to the block (update desp) */
    /// base descriptor
    desired_cb->_base_desp->quota = desp->quota;
    /// specific descriptor
    /* ...... */

    /* Step 3: set target cb's state to default */
    /// reset block state
    memset(desired_cb->_state, 0, sizeof(ComponentState_SoC_t));

    NICC_DEBUG_C(
        "allocate block to application context: "
        "component_id(%u), cb(%p), request(%lu), remain(%lu)",
        this->_cid, desired_cb, desp->quota, this->_base_state->quota
    );
exit:
    return retval;
}


/*!
 *  \brief  return block of resource back to the component
 *  \param  cb          the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_SoC::deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentBlock_SoC *desired_cb = reinterpret_cast<ComponentBlock_SoC*>(cb);
    NICC_CHECK_POINTER(desired_cb);
    NICC_CHECK_POINTER(this->_state);
    /* Step 1: Based on cb, update local state */
    /// base state
    this->_base_state->quota += desired_cb->_base_desp->quota;
    /// specific state
    /* ...... */
    delete desired_cb;
exit:
    return retval;
}

} // namespace nicc
