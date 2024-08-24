#pragma once

#include <iostream>

#if defined(NICC_DOCA_ENABLED)
    #include <doca_flow.h>
#else
    #include <rte_flow.h>
#endif // NICC_DOCA_ENABLED

namespace nicc {

/*!
 *  \brief  configuration of Template
 */
typedef struct ComponentDesp_Template {
    /*!
     *  \note   basic config of the Template
     */
    ComponentBaseConfig_t base_config;
} ComponentDesp_Template_t;

class Component_Template : public Component {
 public:
    Component_Template() : Component() {}
    ~Component_Template(){}

    /*!
     *  \brief  initialization of the components
     *  \param  desp    descriptor to initialize the component
     *  \return NICC_SUCCESS for successful initialization
     */
    nicc_retval_t init(void* desp) override;

    /*!
     *  \brief  apply block of resource from the component
     *  \param  desp    descriptor for allocation
     *  \param  cb      the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    nicc_retval_t allocate_block(void* desp, ComponentBlock** cb) override;

    /*!
     *  \brief  return block of resource back to the component
     *  \param  cb      the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t deallocate_block(ComponentBlock* cb) override;

 private:
};


} // namespace nicc
