#include "soc_wrapper.h"

namespace nicc {

SoCWrapper::SoCWrapper(soc_wrapper_type_t type, SoCWrapperContext *context) {
    nicc_retval_t retval = NICC_SUCCESS;
    if (type & kSoC_Dispatcher) {
        // init the dispatcher
    } else if (type & kSoC_Worker) {
        // init the worker
    }
}

nicc_retval_t SoCWrapper::__init_dispatcher() {
    return NICC_SUCCESS;
}

nicc_retval_t SoCWrapper::__init_worker() {
    return NICC_SUCCESS;
}

nicc_retval_t SoCWrapper::__run() {
    return NICC_SUCCESS;
}

} // namespace nicc