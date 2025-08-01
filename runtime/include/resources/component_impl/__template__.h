#pragma once
#include "common.h"
#include "resources/component.h"
#include "datapath/block_impl/template.h"

namespace nicc {

class Component_TEMPLATE : public Component {
/**
 * ----------------------Public Methods----------------------
 */ 
 public:
    Component_TEMPLATE() : Component() {
        this->_cid = kComponent_TEMPLATE;
    }
    ~Component_TEMPLATE(){}

    /**
     *  \brief  init the component
     *  \param  desp    descriptor to initialize the component,
     *                  generated by the device parser
     *  \return NICC_SUCCESS for successful initialization
     */
    nicc_retval_t init(ComponentBaseDesp_t* desp) override;

    /**
     *  \brief  apply block of resource from the component
     *  \param  desp    descriptor for allocation, generated
     *                  by the app funciton
     *  \param  cb      the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    nicc_retval_t allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) override;

    /**
     *  \brief  return block of resource back to the component
     *  \param  cb          the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t deallocate_block(ComponentBlock* cb) override;

/**
 * ----------------------Internel Parameters----------------------
 */ 
 private:
    // descriptor of the component, recording total hardware resources
    ComponentDesp_TEMPLATE_t *_desp;
    // state of the component, recording remained hardware resources
    ComponentState_TEMPLATE_t *_state;
};

} // namespace nicc
