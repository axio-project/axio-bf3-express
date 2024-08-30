#include "resources/component_impl/template_component.h"

namespace nicc {

nicc_retval_t Component_TEMPLATE::init(ComponentBaseDesp_t* desp) {
    nicc_retval_t retval = NICC_SUCCESS;
    ComponentState_TEMPLATE_t *state;
    /* Step 1: init desp, recording all hardware resources*/
    NICC_CHECK_POINTER(this->_desp = desp);
    /* Step 2: init state, recording remained hardware resources*/
    NICC_CHECK_POINTER(state = new ComponentState_TEMPLATE_t);
    this->_state = reinterpret_cast<ComponentBaseState_t*>(state);
    return retval;
}


/*!
 *  \brief  apply block of resource from the component
 *  \param  desp    descriptor for allocation
 *  \param  cb      the handle of the allocated block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t Component_TEMPLATE::allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
exit:
    return retval;
}


/*!
 *  \brief  return block of resource back to the component
 *  \param  cb          the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_TEMPLATE::deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
   
exit:
    return retval;
}

} // namespace nicc
