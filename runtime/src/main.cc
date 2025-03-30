#include <iostream>

#include "common.h"
#include "resources/component_impl/dpa_component.h"
#include "datapath_pipeline.h"
#include "resources/resource_pool.h"
#include "utils/app_dag.h"

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
    /**
     * \brief  STEP 0: initialize the device
     * \todo;  these descriptors should be parsed by the resource pool, which
     *         queries the device and creates the descriptors
     */
    nicc::device_state_t dev_state = { .device_name = "mlx5_0"};
    dev_state.ibv_ctx = nicc::utils_ibv_open_device(dev_state.device_name);
    nicc::nicc_retval_t retval = nicc::utils_create_flow_engine_domains(dev_state);

    nicc::ComponentDesp_DPA_t *dpa_desp = new nicc::ComponentDesp_DPA_t;
    NICC_CHECK_POINTER(dpa_desp);
    dpa_desp->base_desp.quota = 128;
    dpa_desp->device_name = "mlx5_0";
    dpa_desp->core_id = 0;

    nicc::ComponentDesp_FlowEngine_t *flow_engine_desp = new nicc::ComponentDesp_FlowEngine_t;
    NICC_CHECK_POINTER(flow_engine_desp);
    flow_engine_desp->base_desp.quota = 2000;   // assume 2000 flow entries totally  

    nicc::ComponentDesp_SoC_t *soc_desp = new nicc::ComponentDesp_SoC_t;
    NICC_CHECK_POINTER(soc_desp);  
    soc_desp->base_desp.quota = 16; 
    soc_desp->device_name = "mlx5_2";
    soc_desp->phy_port = 0;
    /*----------------------------------------------------------------*/
    /**
     * \brief  STEP 1: parse config file
     * \note   options: kComponent_FlowEngine | kComponent_DPA
     *          | kComponent_SoC | kComponent_Decompress | kComponent_SHA
     */
    nicc::AppDAG app_dag("/home/ubuntu/hxy/proj/nicc/examples/rdma_simple/dp_spec.json");
    app_dag.print();
    nicc::component_typeid_t enabled_components = app_dag.get_enabled_components();
    /*----------------------------------------------------------------*/
    /**
     * \brief  STEP 2: initialize resource pool, created all enabld components
     *          based on descriptors
     */
    nicc::ResourcePool rpool(
        enabled_components,
        {
            { nicc::kComponent_DPA, reinterpret_cast<nicc::ComponentBaseDesp_t*>(dpa_desp) },
            { nicc::kComponent_FlowEngine, reinterpret_cast<nicc::ComponentBaseDesp_t*>(flow_engine_desp) },
            { nicc::kComponent_SoC, reinterpret_cast<nicc::ComponentBaseDesp_t*>(soc_desp) }
        }
    );
    /*----------------------------------------------------------------*/
    /**
     * \brief  STEP 3: create app contexts, including corresponding 
     *          functions and handlers
     * \note   Currently, datapath pipeline is a "chain", instead of a "DAG". We require the users to specify there components in a linear order.
     * \todo   support DAG
     * \todo   parse the component order from the config file
     */
    nicc::AppContext app_cxt;

    /// DPA app context
    nicc::AppHandler dpa_app_init_handler;
    dpa_app_init_handler.tid = nicc::ComponentBlock_DPA::handler_typeid_t::Init;
    dpa_app_init_handler.binary.dpa.host_stub = &dpa_device_init;
    dpa_app_init_handler.binary.dpa.kernel = l2_swap_wrapper;

    nicc::AppHandler dpa_app_event_handler;
    dpa_app_event_handler.tid = nicc::ComponentBlock_DPA::handler_typeid_t::Event;
    dpa_app_event_handler.binary.dpa.host_stub = &dpa_event_handler;
    dpa_app_event_handler.binary.dpa.kernel = l2_swap_wrapper;

    nicc::ComponentDesp_DPA_t dpa_block_desp = {
        .base_desp = { 
            .quota = 1,
            .block_name = "dpa" /* \todo: use the component name from the config file */
        },
        .device_name = "mlx5_0"
    };
    nicc::AppFunction dpa_app_func = nicc::AppFunction(
        /* handlers_ */ { &dpa_app_init_handler, &dpa_app_event_handler },
        /* cb_desp_ */ reinterpret_cast<nicc::ComponentBaseDesp_t*>(&dpa_block_desp),
        /* cid */ nicc::kComponent_DPA
    );
    app_cxt.functions.push_back(&dpa_app_func);

    /// Flow Engine app context
    // nicc::ComponentDesp_FlowEngine_t *flow_engine_block_desp = new nicc::ComponentDesp_FlowEngine_t;
    // NICC_CHECK_POINTER(flow_engine_desp);
    // flow_engine_desp->base_desp.quota = K(2);      // need 2k flow entries
    // size_t flow_match_size = sizeof(*flow_engine_desp->tx_match_mask) + 64;  // 64 bytes for match mask
    // flow_engine_desp->tx_match_mask = (struct mlx5dv_flow_match_parameters *) calloc(1, flow_match_size);
    // NICC_CHECK_POINTER(flow_engine_desp->tx_match_mask);
    // flow_engine_desp->tx_match_mask->match_sz = 64;
    // flow_engine_desp->rx_match_mask = (struct mlx5dv_flow_match_parameters *) calloc(1, flow_match_size);
    // NICC_CHECK_POINTER(flow_engine_desp->rx_match_mask);
    // flow_engine_desp->rx_match_mask->match_sz = 64;

    /// SoC app context
    nicc::AppHandler soc_app_init_handler;
    soc_app_init_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Init;

    nicc::ComponentDesp_SoC_t soc_block_desp = {
        .base_desp = { 
            .quota = 1,
            .block_name = "soc" /* \todo: use the component name from the config file */
        },
        .device_name = "mlx5_2",
        .phy_port = 0
    };

    nicc::AppFunction soc_app_func = nicc::AppFunction(
        /* handlers_ */ { &soc_app_init_handler },
        /* cb_desp_ */ reinterpret_cast<nicc::ComponentBaseDesp_t*>(&soc_block_desp),
        /* cid */ nicc::kComponent_SoC
    );
    // app_cxt.functions.push_back(&soc_app_func);

    /*----------------------------------------------------------------*/
    /*!
     *  \brief  STEP 4: create the datapath pipeline
     */
    nicc::DatapathPipeline dp_pipeline(rpool, &app_cxt, dev_state, &app_dag);
    

    /* Init control path, including user-request channel, just-in-time verifier, and rule loader (SONiC) */
    /* Init control path MTs */
    // TODO: ctrl state [MTable] inialization

    /* Update control path rules to construct pipeline */
}
