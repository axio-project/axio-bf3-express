#include "app_context.h"

/// !   \todo   remove this
#ifdef __cplusplus
extern "C" {
#endif

extern struct flexio_app *l2_swap_wrapper;  // nicc/bin/l2_swap_wrapper.a
extern flexio_func_t dpa_pkt_func;          // defined in nicc/lib/wrappers/dpa/src/dpa_wrapper.c
extern flexio_func_t dpa_pkt_func_init;     // defined in nicc/lib/wrappers/dpa/src/dpa_wrapper.c

#ifdef __cplusplus
}
#endif

namespace nicc {


/*!
 *  \brief  constructor
 *  \param  tid_  type id of this app handler, note that type index is defined in each component
 */
AppHandler::AppHandler(appfunc_handler_typeid_t tid_) : tid(tid_) {
    ///!    \todo   remove this
    this->host_stubs.dpa.dpa_pkt_func = &dpa_pkt_func;
    this->host_stubs.dpa.dpa_pkt_func_init = &dpa_pkt_func_init;
    this->binary.dpa_binary = l2_swap_wrapper;
}


/*!
 *  \brief  constructor
 *  \param  handlers   list of pointers to the actual function to be executed
 */
AppFunction::AppFunction(std::vector<AppHandler*> &handlers){
    if(unlikely(handlers.size() == 0)){
        NICC_WARN_C("try to create app function without handler inserted, empty app function created");
    }
    this->handlers = handlers;
}


} // namespace nicc
