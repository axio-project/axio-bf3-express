#pragma once

#include <iostream>

#include "common.h"
#include "app_context.h"

namespace nicc {


/*!
 *  \brief  mask for enabling dataplane component
 */
enum nicc_component_id_t : uint16_t {
    kComponent_Unknown = 0x00,
    kComponent_FlowEngine = 0x01,
    kComponent_DPA = 0x02,
    kComponent_ARM = 0x04,
    kComponent_Decompress = 0x08,
    kComponent_SHA = 0x10
};
#define NICC_ENABLE_EMPTY_MASK  0x0000
#define NICC_ENABLE_FULL_MASK   0xFFFF


// forward declaration
class ComponentBlock;
class Component;


/*!
 *  \brief  basic descriptor of the component
 *  \note   [1] this structure should be inherited and 
 *          implenented by each component
 *          [2] this structure could be used for describing
 *          either root component or component block
 */
typedef struct ComponentBaseDesp {
    // quota of the component
    uint64_t quota;
} ComponentBaseDesp_t;


/*!
 *  \brief  basic state of the component
 *  \note   [1] this structure should be inherited and 
 *          implenented by each component
 *          [2] this structure could be used for describing
 *          either root component or component block
 */
typedef struct ComponentBaseState {
    // quota of the component
    uint64_t quota;
} ComponentBaseState_t;


/*!
 *  \brief  basic state of the function register 
            into the component
 *  \note   this structure should be inherited and 
 *          implenented by each component
 */
typedef struct ComponentFuncBaseState {
    AppContext *app_ctx;
    AppFunction *app_func;
} ComponentFuncBaseState_t;


/*!
 *  \brief  block of dataplane component, allocated to specific application
 */
class ComponentBlock {
 public:
    ComponentBlock(Component* component_, ComponentBaseDesp_t* desp_) 
        : component(component_), desp(desp_) {}
    ~ComponentBlock(){}

 protected:
    // root component of this block
    Component *component;

    // descriptor of the component block
    ComponentBaseDesp_t *_desp;

    // state of the component block
    ComponentBaseState_t *_state;

    // map of function states which has been registered in to this component block
    std::map<AppFunction*, ComponentFuncBaseState_t*> _function_state_map;

    /*!
     *  \brief  register a new application function into this component block
     *  \param  func    the function to be registered into this component
     *  \return NICC_SUCCESS for successful registeration
     */
    virtual nicc_retval_t register_function(AppFunction *func){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  deregister a application function
     *  \param  func    the function to be deregistered from this compoennt block
     *  \return NICC_SUCCESS for successful unregisteration
     */
    virtual nicc_retval_t unregister_function(AppFunction *func){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }
};


/*!
 *  \brief  dataplane component
 */
class Component {
 public:
    /*!
     *  \brief  constructor
     *  \param  
     */
    Component(){}
    ~Component(){}
    
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
     *  \param  app_cxt     app context which this block allocates to
     *  \param  cb          the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    virtual nicc_retval_t allocate_block(ComponentBaseDesp_t* desp, AppContext* app_cxt, ComponentBlock** cb){
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
    static nicc_component_id_t _cid = kComponent_Unknown;

    // descriptor of the component
    ComponentBaseDesp_t *_desp;

    // state of the component
    ComponentBaseState_t *_state;

    // allocation table
    std::map<AppContext*, std::vector<ComponentBlock*>> _allocate_map;

    /*!
     *  \brief  common procedure for block allocation
     *  \param  desp        descriptor for allocation
     *  \param  app_cxt     app context which this block allocates to
     *  \param  cb          the handle of the allocated block
     *  \return NICC_SUCCESS for successful allocation
     */
    template<typename CBlock>
    nicc_retval_t __allocate_block(ComponentBaseDesp_t* desp, AppContext* app_cxt, CBlock** cb){
        nicc_retval_t retval = NICC_SUCCESS;
        
        NICC_CHECK_POINTER(desp);
        NICC_CHECK_POINTER(app_cxt);
        NICC_CHECK_POINTER(cb)

        // check quota
        if(unlikely(this->_state.quota < desp->quota)){
            NICC_WARN_C(
                "failed to allocate block due to unsufficient resource remained:"
                " component_id(), request(), remain()",
                this->_cid, desp->quota, this->_state.quota
            )
            retval = NICC_ERROR_EXSAUSTED;
            goto exit;
        }

        NICC_CHECK_POINTER(*cb = new CBlock(/* component */ this, /* desp */ desp));
        
        this->_state.quota -= desp->quota;
        this->_allocate_map[app_cxt].push_back(*cb);

        NICC_DEBUG_C(
            "allocate block to application context: ",
            " component_id(), app_cxt(), request(), remain()",
            this->_cid, app_cxt, desp->quota, this->_state.quota
        );

    exit:
        return retval;
    }

    /*!
     *  \brief  common procedure for block deallocation
     *  \param  cb      the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    template<typename CBlock>
    nicc_retval_t __deallocate_block(CBlock* cb){
        nicc_retval_t retval = NICC_SUCCESS;

        // TODO

    exit:
        return retval;
    }

 private:
};

} // namespace nicc
