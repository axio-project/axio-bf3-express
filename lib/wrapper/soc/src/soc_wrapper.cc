#include "soc_wrapper.h"

namespace nicc {

SoCWrapper::SoCWrapper(soc_wrapper_type_t type) {
    if (type & kSoC_Dispatcher) {
        // init the dispatcher
    } else if (type & kSoC_Worker) {
        // init the worker
    }
}

} // namespace nicc