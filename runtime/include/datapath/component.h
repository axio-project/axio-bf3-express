#pragma once

#include <iostream>
#include <map>
#include <set>

#include "common.h"
#include "log.h"

namespace nicc {


// forward declaration
class ComponentBlock;
class Component;
class AppHandler;
class AppFunction;
class AppContext;


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
    ComponentBlock(Component* component_, ComponentBaseDesp_t* desp) 
        : _component(component_), _desp(desp) {}
    virtual ~ComponentBlock(){}

    /*!
     *  \brief  register a new application function into this component
     *  \param  app_func  the function to be registered into this component
     *  \return NICC_SUCCESS for successful registeration
     */
    virtual nicc_retval_t register_app_function(AppFunction *app_func){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  deregister a application function
     *  \param  app_func    the function to be deregistered from this compoennt
     *  \return NICC_SUCCESS for successful unregisteration
     */
    virtual nicc_retval_t unregister_app_function(AppFunction *app_func){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

    friend class Component;

 protected:
    // root component of this block
    Component *_component;

    // descriptor of the component block
    ComponentBaseDesp_t *_desp;

    // state of the component block
    ComponentBaseState_t *_state;

    // map of function states which has been registered in to this component block
    std::map<AppFunction*, ComponentFuncBaseState_t*> _function_state_map;
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

    /*!
     *  \brief  return block of resource back to the component
     *  \param  app_cxt     app context which this block allocates to
     *  \param  cb          the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    virtual nicc_retval_t deallocate_block(AppContext* app_cxt, ComponentBlock* cb){
        return NICC_ERROR_NOT_IMPLEMENTED;
    }

 protected:
    // component id
    component_typeid_t _cid;

    // descriptor of the component
    ComponentBaseDesp_t *_desp;

    // state of the component
    ComponentBaseState_t *_state;

    // allocation table
    std::map<AppContext*, std::set<ComponentBlock*>> _allocate_map;

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
        NICC_CHECK_POINTER(cb);
        NICC_CHECK_POINTER(this->_state);

        // check quota
        if(unlikely(this->_state->quota < desp->quota)){
            NICC_WARN_C(
                "failed to allocate block due to unsufficient resource remained:"
                " component_id(%u), request(%lu), remain(%lu)",
                this->_cid, desp->quota, this->_state->quota
            )
            retval = NICC_ERROR_EXSAUSTED;
            goto exit;
        }

        NICC_CHECK_POINTER(*cb = new CBlock(/* component */ this, /* desp */ desp));
        
        this->_state->quota -= desp->quota;
        this->_allocate_map[app_cxt].insert(*cb);

        NICC_DEBUG_C(
            "allocate block to application context: "
            "component_id(%u), cb(%p), app_cxt(%p), request(%lu), remain(%lu)",
            this->_cid, *cb, app_cxt, desp->quota, this->_state->quota
        );

    exit:
        return retval;
    }

    /*!
     *  \brief  common procedure for block deallocation
     *  \param  app_cxt     app context which this block allocates to
     *  \param  cb          the handle of the block to be deallocated
     *  \return NICC_SUCCESS for successful deallocation
     */
    template<typename CBlock>
    nicc_retval_t __deallocate_block(AppContext* app_cxt, CBlock* cb){
        nicc_retval_t retval = NICC_SUCCESS;
        
        NICC_CHECK_POINTER(cb);
        NICC_CHECK_POINTER(app_cxt);
        NICC_CHECK_POINTER(this->_state);

        if(unlikely(this->_allocate_map.count(app_cxt) == 0)){
            NICC_WARN_C(
                "failed to return component block from unexisted app context, omited: "
                "component_id(%u), app_cxt(%p), cb(%p)",
                this->_cid, app_cxt, cb
            );
            retval = NICC_ERROR_DUPLICATED;
            goto exit;
        }

        if(unlikely(this->_allocate_map[app_cxt].count(cb) == 0)){
            NICC_WARN_C(
                "failed to return an unexisted component block, omited: "
                "component_id(%u), app_cxt(%p), cb(%p)",
                this->_cid, app_cxt, cb
            );
            retval = NICC_ERROR_DUPLICATED;
            goto exit;
        }

        this->_state->quota += cb->_desp->quota;
        this->_allocate_map[app_cxt].erase(cb);
        delete cb;

    exit:
        return retval;
    }

 private:
};

} // namespace nicc
