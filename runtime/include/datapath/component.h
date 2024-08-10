#pragma once

#include <iostream>

#include "common.h"

namespace nicc {

// forward declaration
typedef struct ComponentBaseConfig ComponentBaseConfig_t;
class ComponentBlock;
class Component;

/*!
 *  \brief  basic configuration of the component
 *  \note   [1] this structure should be inherited and 
 *          implenented by each component
 *          [2] this structure could be used for describing
 *          either root component or component block
 */
typedef struct ComponentBaseConfig {
    // quota of the component
    uint64_t quota;
} ComponentBaseConfig_t;

/*!
 *  \brief  block of dataplane component, allocated to specific application
 */
class ComponentBlock {
 public:
    ComponentBlock(Component* component_, ComponentBaseConfig* config_) 
        : component(component_), config(config_) {}
    ~ComponentBlock(){}

 protected:
    // root component of this block
    Component *component;

    // configuration of this block
    ComponentBaseConfig *config;
};

/*!
 *  \brief  dataplane component
 */
class Component {
 public:
    /*!
     *  \brief  constructor
     *  \param  quota   initial quota of this component
     */
    Component(){}
    ~Component(){}
    
    /*!
     *  \brief  initialization of the components
     *  \param  desp    descriptor to initialize the component
     *  \return NICC_SUCCESS for successful initialization
     */
    virtual nicc_retval_t init(void* desp){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  apply block of resource from the component
     *  \param  desp    descriptor for allocation
     *  \param  cb      the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    virtual nicc_retval_t allocate_block(ComponentBaseConfig_t* desp, ComponentBlock* cb){
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
    // configuration of this component
    ComponentBaseConfig_t *config;   
};

} // namespace nicc

