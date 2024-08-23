#pragma once
#include <iostream>
#include <string>

#include <libflexio/flexio.h>

#include "common.h"
#include "log.h"

namespace nicc
{

using appfunc_handler_typeid_t = uint8_t;

/*!
 *  \brief  handler within a function
 */
class AppHandler {
 public:
  /*!
   *  \brief  constructor
   *  \param  tid_  type id of this app handler, note that type index
   *                is defined in each component
   */
  AppHandler(appfunc_handler_typeid_t tid_);
  ~AppHandler(){}

  /*!
   *  \brief  load binary into this app handler
   *  \param  raw   pointer to the binary area
   *  \param  size  size of the binary
   *  \return NICC_SUCCESS for successfully loading
   */
  nicc_retval_t load_from_binary(uint8_t *raw, uint64_t size){
    nicc_retval_t retval = NICC_ERROR_NOT_IMPLEMENTED;
    return retval;
  }

  typedef struct __dpa_host_stubs {
    flexio_func_t *dpa_pkt_func;
    flexio_func_t *dpa_pkt_func_init;
  } __dpa_host_stubs_t;

  // host stub of the handler (for hetrogeneous process such as DPA)
  union {
    __dpa_host_stubs_t dpa;
  } host_stubs;

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
  AppFunction(std::vector<AppHandler*> &handlers);
  ~AppFunction(){}

  // TODO: more definition
  nicc_retval_t modify_state();

  // handler pointers -> handler type
  std::vector<AppHandler*> handlers;

 private:
  // index of the deploy component of this function
  component_typeid_t _component_id;

  // TODO: more definition
  uint8_t *_state;
};


/*!
 *  \brief  application context, should be generated from compiler
 */
class AppContext {
 public:
  AppContext();
  ~AppContext();

  /*!
   *  \brief  load functions genearted by compiler
   *  \param  path   path to the compilation binaries
   *  \return NICC_SUCCESS for successfully loading;
   *          NICC_FAILED otherwise
   */
  nicc_retval_t load_functions_from_binaries(std::string path);

 private:
  // all functions in this app context
  std::vector<AppFunction*> _functions;

  /*!
   *  \brief  load a function from compilation result from compiler
   *  \param  bin   pointer to the raw binary (.a / .so)
   *  \param  func  the generated AppFunction instance
   *  \return NICC_SUCCESS for successfully loading;
   *          NICC_FAILED otherwise
   */
  nicc_retval_t __load_single_function_from_binary(uint8_t *bin, AppFunction **func);
};

} // namespace nicc
