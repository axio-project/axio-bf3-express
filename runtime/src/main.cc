#include <iostream>

#include "common.h"
#include "datapath_pipeline.h"

#include "datapath/dpa.h"
#include "datapath/flow_engine.h"
#include "datapath/dpa.h"

// extern nicc::ComponentDesp_FlowEngine_t flow_engine_config;
extern nicc::ComponentDesp_DPA_t dpa_config;

int main(){
    // Parse config file
    // TODO:

    // init app contexts, including function and state for each datapath component
    // TODO:

    /*!
     *  \brief  init and allocate datapath components for all apps, then construct the datapath pipeline
     *  \note   options: kComponent_FlowEngine | kComponent_DPA | kComponent_ARM | kComponent_Decompress | kComponent_SHA
     */
    nicc::component_typeid_t enabled_components = nicc::kComponent_DPA;

    // create the resource pool of nicc runtime
    nicc::ResourcePool rpool(enabled_components, {
        // { kComponent_FlowEngine, &flow_engine_config },
        { nicc::kComponent_DPA, reinterpret_cast<nicc::ComponentBaseDesp_t*>(&dpa_config) }
    });

    // create
    // desp: components 占用量
    // function: 
    //  DAG -> compiler -> [match, modify field, SHA, -ARM-, Compress]
    //                      app-A: 01-11101 -> 01-11100 [不需要 per-hop MTable -> 省内存 + 动态切 chain] Overhead
    //                      app-B: 01-
    //                      [match, modify field, -SHA-, ARM, -Compress-]
    //                      [避免 context-switch] [避免 lock] Performance

    //                      两个表: [1] 将包归类为某个 app; [2] 该包在这个 app 的 functions chain 里要执行哪些 stages
    //      node: function -> dpa_kernel + dpa_state / SoC kernel + soc_state / SHA kernel
    //                                   [ctrl + user[*]]
    //                          10 cores    mtable


    // function: DatapathPipeline 为他们申请资源
    // function/component MTable next-hop -> general (支持 baseline, 也支持 nicc mask 方案)
    
    nicc::DatapathPipeline dp_pipeline(enabled_components, rpool);
    
    /* Init control path, including user-request channel, just-in-time verifier, and rule loader (SONiC) */
    /* Init control path MTs */
    // TODO: ctrl state [MTable] inialization

    /* Update control path rules to construct pipeline */
}
