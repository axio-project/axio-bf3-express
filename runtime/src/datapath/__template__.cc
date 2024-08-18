#include <iostream>

#include "common.h"
#include "datapath/__template__.h"

namespace nicc {

/*!
 *  \brief  initialization of the components
 *  \param  desp    descriptor to initialize the component
 *  \return NICC_SUCCESS for successful initialization
 */
nicc_retval_t Component_Template::init(void* desp) {
    nicc_retval_t retval = NICC_SUCCESS;

    ComponentDesp_Template_t *template_desp 
        = reinterpret_cast<ComponentDesp_Template_t*>(desp);
    NICC_CHECK_POINTER(template_desp);

exit:
    return retval;
}

/*!
 *  \brief  apply block of resource from the component
 *  \param  desp    descriptor for allocation
 *  \param  cb      the handle of the allocated block
 *  \return NICC_SUCCESS for successful allocation
 */
nicc_retval_t Component_Template::allocate_block(void* desp, ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;

    ComponentDesp_Template_t *template_desp 
        = reinterpret_cast<ComponentDesp_Template_t*>(desp);
    NICC_CHECK_POINTER(template_desp);

exit:
    return retval;
}

/*!
 *  \brief  return block of resource back to the component
 *  \param  cb      the handle of the block to be deallocated
 *  \return NICC_SUCCESS for successful deallocation
 */
nicc_retval_t Component_Template::deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(cb);

exit:
    return retval;
}


} // namespace nicc
