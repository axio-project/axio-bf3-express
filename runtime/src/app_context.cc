#include "app_context.h"

namespace nicc {


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


/*!
 *  \brief  load functions genearted by compiler
 *  \param  path   path to the compilation result
 *  \return NICC_SUCCESS for successfully loading;
 *          NICC_FAILED otherwise
 */
nicc_retval_t AppContext::load_functions_from_binary(std::string path){
    nicc_retval_t retval = NICC_SUCCESS;

    

exit:
    return retval;
}


/*!
 *  \brief  load a function from compilation result from compiler
 *  \param  bin   pointer to the raw binary
 *  \return NICC_SUCCESS for successfully loading;
 *          NICC_FAILED otherwise
 */
nicc_retval_t AppContext::__load_single_function_from_binary(uint8_t *bin, AppFunction **func){
    nicc_retval_t retval = NICC_SUCCESS;

    NICC_CHECK_POINTER(bin);
    NICC_CHECK_POINTER(func);

exit:
    return retval;
}


} // namespace nicc
