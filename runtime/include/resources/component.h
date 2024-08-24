#pragma once

#include <iostream>
#include <map>
#include <set>

#include "common.h"
#include "log.h"

#include "datapath/component_block.h"

namespace nicc {

// forward declaration
class Component;
class AppHandler;
class AppFunction;
class AppContext;

/**
 * ----------------------General Structures----------------------
 */ 

/*!
 *  \brief  dataplane component
 */
class Component {
 public:
    /*!
     *  \brief  constructor
     *  \param  
     */
    Component() : _cid(kComponent_Unknown) {}
    virtual ~Component(){}
    
    /*!
     *  \brief  initialization of the components
     *  \param  desp    descriptor to initialize the component
     *  \return NICC_SUCCESS for successful initialization
     */
    virtual nicc_retval_t init(ComponentBaseDesp_t* desp){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  apply block of resource from the component
     *  \param  desp        descriptor for allocation
     *  \param  cb          the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    virtual nicc_retval_t allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  return block of resource back to the component
     *  \param  cb      the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    virtual nicc_retval_t deallocate_block(ComponentBlock* cb){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }


 protected:
    // component id
    component_typeid_t _cid;

    // descriptor of the component
    ComponentBaseDesp_t *_desp;

    // state of the component
    ComponentBaseState_t *_state;

    /*!
     *  \brief  common procedure for block allocation
     *  \param  desp        descriptor for allocation
     *  \param  cb          the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    nicc_retval_t __allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb);

    /*!
     *  \brief  common procedure for block deallocation
     *  \param  cb          the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t __deallocate_block(ComponentBlock* cb);

 private:
};

} // namespace nicc
