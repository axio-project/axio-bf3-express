#include "resources/component.h"

namespace nicc {

nicc_retval_t Component::__allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) {

    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(desp);
    NICC_CHECK_POINTER(cb);
    NICC_CHECK_POINTER(this->_state);

    // check quota
    if(unlikely(this->_state->quota < desp->quota)){
        NICC_WARN_C(
            "failed to allocate block due to unsufficient resource remained:"
            " component_id(%u), request(%lu), remain(%lu)",
            this->_cid, desp->quota, this->_state->quota
        )
        retval = NICC_ERROR_EXSAUSTED;
        goto exit;
    }
    
    this->_state->quota -= desp->quota;

    // allocate quota to the block and assign default state
    cb->_desp->quota = desp->quota;
    // \note not implement          cb->_state = ;

    NICC_DEBUG_C(
        "allocate block to application context: "
        "component_id(%u), cb(%p), request(%lu), remain(%lu)",
        this->_cid, cb, desp->quota, this->_state->quota
    );

exit:
    return retval;
}

nicc_retval_t Component::__deallocate_block(ComponentBlock* cb) {
    nicc_retval_t retval = NICC_SUCCESS;
    
    NICC_CHECK_POINTER(cb);
    NICC_CHECK_POINTER(this->_state);

    this->_state->quota += cb->_desp->quota;
    delete cb;

exit:
    return retval;
}

}  // namespace nicc