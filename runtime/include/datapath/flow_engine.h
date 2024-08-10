#pragma once

#include <iostream>

#if defined(NICC_DOCA_ENABLED)
    #include <doca_flow.h>
#else
    #include <rte_flow.h>
#endif // NICC_DOCA_ENABLED

#include "datapath/component.h"

namespace nicc {

/*!
 *  \brief  configuration of flow engine
 */
typedef struct ComponentConfig_FlowEngine {
    /*!
     *  \note   basic config of the flow engine
     */
    ComponentBaseConfig_t base_config;

    /*!
     *  \ref    https://docs.nvidia.com/doca/sdk/doca+flow/index.html
     *  \note   mode of flow engine, options including:
     *          [1] vnf:    steering destination is port
     *          [2] switch: steering destination is interface descriptor
     *  \note   addon options including:
     *          [1] hws:    hardware steering enable
     *          [2] ...
     */
    const char* mode;
    

} ComponentConfig_FlowEngine_t;

class Component_FlowEngine : public Component {
 public:
    Component_FlowEngine() : Component() {}
    ~Component_FlowEngine(){}

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
    nicc_retval_t allocate_block(ComponentBaseConfig_t* desp, ComponentBlock* cb) override;

    /*!
     *  \brief  return block of resource back to the component
     *  \param  cb      the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    nicc_retval_t deallocate_block(ComponentBlock* cb) override;

 private:

};

} // namespace nicc
