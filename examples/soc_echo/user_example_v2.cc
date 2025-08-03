// 新的用户状态和处理器示例 - 用户自己管理内存
#include <iostream>
#include <cstring>
#include <cstdlib>
#include "../../lib/common.h"
#include "../../runtime/include/app_context.h"
#include "../../lib/wrapper/soc/soc_wrapper.h"

// 用户定义的状态类
struct MyAppState {
    int message_counter;
    char processing_buffer[1024];
    double processing_time_sum;
    bool is_initialized;
    
    MyAppState() : message_counter(0), processing_time_sum(0.0), is_initialized(false) {
        std::memset(processing_buffer, 0, sizeof(processing_buffer));
    }
};

// 用户定义的初始化处理器 - 分配并返回user_state和大小信息
nicc::soc_user_state_info my_init_handler() {
    // 用户在这里分配自己的状态
    MyAppState* state = new MyAppState();
    state->message_counter = 0;
    state->processing_time_sum = 0.0;
    state->is_initialized = true;
    
    std::printf("MyAppState allocated and initialized: counter=%d, size=%zu\n", 
               state->message_counter, sizeof(MyAppState));
    
    // 返回状态指针和大小信息
    return {static_cast<void*>(state), sizeof(MyAppState)};
}

// 用户定义的清理处理器 - 释放user_state
void my_cleanup_handler(void* user_state) {
    if (user_state) {
        MyAppState* state = static_cast<MyAppState*>(user_state);
        std::printf("Cleaning up MyAppState: processed %d messages, total time=%.3f\n", 
                   state->message_counter, state->processing_time_sum);
        delete state;
    }
}

// 用户定义的消息处理器
nicc::nicc_retval_t my_msg_handler(nicc::Buffer* msg, void* user_state) {
    if (!user_state) {
        std::printf("Warning: user_state is null\n");
        return nicc::NICC_ERROR;
    }
    
    MyAppState* state = static_cast<MyAppState*>(user_state);
    
    if (!state->is_initialized) {
        std::printf("Warning: user_state not properly initialized\n");
        return nicc::NICC_ERROR;
    }
    
    // 增加消息计数
    state->message_counter++;
    
    // 处理消息逻辑（示例：简单回显并修改内容）
    std::printf("Processing message #%d, length=%zu\n", 
           state->message_counter, msg->length_);
    
    // 在用户缓冲区中做一些处理
    if (msg->length_ < sizeof(state->processing_buffer)) {
        std::memcpy(state->processing_buffer, msg->get_buf(), msg->length_);
        // 可以修改消息内容
        // ...
    }
    
    // 记录处理时间（示例）
    state->processing_time_sum += 0.001; // 假设处理时间
    
    return nicc::NICC_SUCCESS;
}

// 用户定义的包处理器（如果需要）
nicc::nicc_retval_t my_pkt_handler(nicc::Buffer* pkt, void* user_state) {
    if (!user_state) return nicc::NICC_ERROR;
    
    MyAppState* state = static_cast<MyAppState*>(user_state);
    std::printf("Processing packet, total messages so far: %d\n", state->message_counter);
    return nicc::NICC_SUCCESS;
}

// 示例主函数：如何注册用户处理器
void example_register_user_handlers_v2() {
    // 创建 init handler
    nicc::AppHandler soc_init_handler;
    soc_init_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Init;
    soc_init_handler.binary.soc = (void*)my_init_handler;
    
    // 创建 message handler  
    nicc::AppHandler soc_msg_handler;
    soc_msg_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Msg_Handler;
    soc_msg_handler.binary.soc = (void*)my_msg_handler;
    
    // 创建 cleanup handler
    nicc::AppHandler soc_cleanup_handler;
    soc_cleanup_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Cleanup;
    soc_cleanup_handler.binary.soc = (void*)my_cleanup_handler;
    
    // 创建组件描述符
    nicc::ComponentDesp_SoC_t soc_block_desp = {
        .base_desp = { 
            .quota = 4,
            .block_name = "my_soc_component_v2"
        },
        .device_name = "mlx5_2",
        .phy_port = 0
    };

    // 创建 AppFunction - 不再需要传递user_state
    nicc::AppFunction soc_app_func = nicc::AppFunction(
        /* handlers_ */ { &soc_init_handler, &soc_msg_handler, &soc_cleanup_handler },
        /* cb_desp_ */ reinterpret_cast<nicc::ComponentBaseDesp_t*>(&soc_block_desp),
        /* cid */ nicc::kComponent_SoC
        // 不再需要 user_state 和 user_state_size 参数
    );
    
    std::printf("User handlers registered successfully!\n");
    std::printf("User state will be allocated dynamically by init_handler\n");
} 