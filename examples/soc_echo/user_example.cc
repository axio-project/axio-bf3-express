// 用户状态和处理器示例
#include <iostream>
#include <cstring>  // for memset, memcpy
#include <cstdio>   // for printf
#include "../../lib/common.h"
#include "../../runtime/include/app_context.h"
#include "../../lib/wrapper/soc/soc_wrapper.h"
#include "../../lib/common/buffer.h"

// 用户定义的状态类
struct MyAppState {
    int message_counter;
    char processing_buffer[1024];
    double processing_time_sum;
    
    MyAppState() : message_counter(0), processing_time_sum(0.0) {
        std::memset(processing_buffer, 0, sizeof(processing_buffer));
    }
};

// 用户定义的初始化处理器
nicc::nicc_retval_t my_init_handler(void* user_state) {
    MyAppState* state = static_cast<MyAppState*>(user_state);
    state->message_counter = 0;
    state->processing_time_sum = 0.0;
    std::printf("MyAppState initialized: counter=%d\n", state->message_counter);
    return nicc::NICC_SUCCESS;
}

// 用户定义的消息处理器
nicc::nicc_retval_t my_msg_handler(nicc::Buffer* msg, void* user_state) {
    MyAppState* state = static_cast<MyAppState*>(user_state);
    
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
    MyAppState* state = static_cast<MyAppState*>(user_state);
    std::printf("Processing packet, total messages so far: %d\n", state->message_counter);
    return nicc::NICC_SUCCESS;
}

// 示例主函数：如何注册用户处理器
void example_register_user_handlers() {
    // 创建用户状态实例
    MyAppState my_state;
    
    // 创建 init handler
    nicc::AppHandler soc_init_handler;
    soc_init_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Init;
    soc_init_handler.binary.soc = (void*)my_init_handler;
    
    // 创建 message handler  
    nicc::AppHandler soc_msg_handler;
    soc_msg_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Msg_Handler;
    soc_msg_handler.binary.soc = (void*)my_msg_handler;
    
    // 创建 packet handler（可选）
    nicc::AppHandler soc_pkt_handler;
    soc_pkt_handler.tid = nicc::ComponentBlock_SoC::handler_typeid_t::Pkt_Handler;
    soc_pkt_handler.binary.soc = (void*)my_pkt_handler;
    
    // 创建组件描述符
    nicc::ComponentDesp_SoC_t soc_block_desp = {
        .base_desp = { 
            .quota = 4,
            .block_name = "my_soc_component"
        },
        .device_name = "mlx5_2",
        .phy_port = 0
    };

    // 创建 AppFunction，传递用户状态
    nicc::AppFunction soc_app_func = nicc::AppFunction(
        /* handlers_ */ { &soc_init_handler, &soc_msg_handler, &soc_pkt_handler },
        /* cb_desp_ */ reinterpret_cast<nicc::ComponentBaseDesp_t*>(&soc_block_desp),
        /* cid */ nicc::kComponent_SoC,
        /* user_state */ &my_state,          // 传递用户状态
        /* user_state_size */ sizeof(MyAppState)  // 传递状态大小
    );
    
    std::printf("User handlers and state registered successfully!\n");
    std::printf("State size: %zu bytes\n", sizeof(MyAppState));
} 