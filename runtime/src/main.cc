#include <iostream>

#include "common.h"
#include "resources/component_impl/dpa_component.h"
#include "datapath_pipeline.h"
#include "resources/resource_pool.h"

// #include "app_context.h"
// #include "datapath/flow_engine.h"
// #include "datapath/dpa.h"

/**
 * \brief  extern declaration of DPA binaries
 * \todo   remove this
 */
#ifdef __cplusplus
    extern "C" {
#endif

extern struct flexio_app *l2_swap_wrapper;  // nicc/bin/l2_swap_wrapper.a
extern flexio_func_t dpa_event_handler;          // defined in nicc/lib/wrappers/dpa/src/dpa_wrapper.c
extern flexio_func_t dpa_device_init;     // defined in nicc/lib/wrappers/dpa/src/dpa_wrapper.c

#ifdef __cplusplus
    }
#endif

int main(){
    nicc::device_state_t dev_state = { .device_name = "mlx5_0" };
    dev_state.ibv_ctx = nicc::utils_ibv_open_device(dev_state.device_name);
    NICC_CHECK_POINTER(dev_state.ibv_ctx);

    /**
     * \brief  STEP 1: parse config file, create all component descriptors
     * \note   options: kComponent_FlowEngine | kComponent_DPA
     *          | kComponent_ARM | kComponent_Decompress | kComponent_SHA
     * \todo;  add more components   
     */
    nicc::ComponentDesp_DPA_t *dpa_desp = new nicc::ComponentDesp_DPA_t;
    NICC_CHECK_POINTER(dpa_desp);
    dpa_desp->base_desp.quota = 128;
    dpa_desp->device_name = "mlx5_0";
    dpa_desp->core_id = 0;

    nicc::component_typeid_t enabled_components = nicc::kComponent_DPA;
    /*----------------------------------------------------------------*/
    /**
     * \brief  STEP 2: initialize resource pool, created all enabld components
     *          based on descriptors
     */
    nicc::ResourcePool rpool(
        enabled_components,
        {
            { nicc::kComponent_DPA, reinterpret_cast<nicc::ComponentBaseDesp_t*>(dpa_desp) }
        }
    );
    /*----------------------------------------------------------------*/
    /**
     * \brief  STEP 3: create app contexts, including corresponding 
     *          functions and handlers
     */
    nicc::AppHandler app_init_handler;
    app_init_handler.tid = nicc::ComponentBlock_DPA::handler_typeid_t::Init;
    app_init_handler.host_stub.dpa_host_stub = &dpa_device_init;
    app_init_handler.binary.dpa_binary = l2_swap_wrapper;

    nicc::AppHandler app_event_handler;
    app_event_handler.tid = nicc::ComponentBlock_DPA::handler_typeid_t::Event;
    app_event_handler.host_stub.dpa_host_stub = &dpa_event_handler;
    app_event_handler.binary.dpa_binary = l2_swap_wrapper;

    nicc::ComponentDesp_DPA_t dpa_block_desp = {
        .base_desp = { .quota = 1 },
        .device_name = "mlx5_0"
    };
    nicc::AppFunction app_func = nicc::AppFunction(
        /* handlers_ */ { &app_init_handler, &app_event_handler },
        /* cb_desp_ */ reinterpret_cast<nicc::ComponentBaseDesp_t*>(&dpa_block_desp),
        /* cid */ nicc::kComponent_DPA
    );

    nicc::AppContext app_cxt;
    app_cxt.functions.push_back(&app_func);
    /*----------------------------------------------------------------*/
    /*!
     *  \brief  STEP 4: create the datapath pipeline
     */
    nicc::DatapathPipeline dp_pipeline(rpool, &app_cxt, dev_state);
    

    /* Init control path, including user-request channel, just-in-time verifier, and rule loader (SONiC) */
    /* Init control path MTs */
    // TODO: ctrl state [MTable] inialization

    /* Update control path rules to construct pipeline */
}
