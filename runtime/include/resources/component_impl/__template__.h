#pragma once
#include "common.h"
#include "resources/component.h"
#include "datapath/block_impl/template.h"

namespace nicc {

class Component_TEMPLATE : public Component {

 public:
    Component_TEMPLATE() : Component() {
        this->_cid = kComponent_TEMPLATE;
    }
    ~Component_TEMPLATE(){}

    /*!
     *  \brief  initialization of the components
     *  \param  desp    descriptor to initialize the component
     *  \return NICC_SUCCESS for successful initialization
     */
    nicc_retval_t init(ComponentBaseDesp_t* desp) override;

    /*!
     *  \brief  apply block of resource from the component
     *  \param  desp    descriptor for allocation
     *  \param  cb      the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    nicc_retval_t allocate_block(ComponentBaseDesp_t* desp, ComponentBlock* cb) override;

    /*!
     *  \brief  return block of resource back to the component
     *  \param  cb          the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t deallocate_block(ComponentBlock* cb) override;
};

} // namespace nicc
