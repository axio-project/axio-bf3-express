#pragma once
#include <iostream>
#include <string>

#include <libflexio/flexio.h>

#include "common.h"
#include "log.h"
#include "datapath/component.h"

namespace nicc
{

using appfunc_handler_typeid_t = uint8_t;

/*!
 *  \brief  handler within a function
 */
class AppHandler {
 public:
    AppHandler(){}
    ~AppHandler(){}

    // host stub of the handler (for hetrogeneous process such as DPA)
    union {
        flexio_func_t *dpa_host_stub;
    } host_stub;

    // binary of the handler
    union {
        flexio_app  *dpa_binary;
    } binary;

    // typeid of this handler
    appfunc_handler_typeid_t tid;

 private:
};


/*!
 *  \brief  function within a context
 */
class AppFunction {
 public:
    /*!
     *  \brief  constructor
     *  \param  handlers   list of pointers to the actual function to be executed
     */
    AppFunction(std::vector<AppHandler*>&& handlers_, ComponentBaseDesp_t* cb_desp_, component_typeid_t cid)
        : handlers(handlers_), cb_desp(cb_desp_), component_id(cid)
    {
        if(unlikely(handlers.size() == 0)){
            NICC_WARN_C("try to create app function without handler inserted, empty app function created");
        }
    }
    ~AppFunction(){}

    // handler pointers -> handler type
    std::vector<AppHandler*> handlers;

    // component block descriptor of this function
    ComponentBaseDesp_t *cb_desp;

    // index of the deploy component of this function
    component_typeid_t component_id;

 private:
};


/*!
 *  \brief  application context, should be generated from compiler
 */
class AppContext {
 public:
    AppContext(){}
    ~AppContext(){}

    // all functions in this app context
    std::vector<AppFunction*> functions;

 private:
};

} // namespace nicc
