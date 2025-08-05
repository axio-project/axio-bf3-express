#pragma once
#include "common.h"
#include "log.h"
#include "common/buffer.h"

// 用户定义的状态类
struct MyAppState {
    int message_counter;
    char processing_buffer[1024];
    double processing_time_sum;
    bool is_initialized;
    
    MyAppState() : message_counter(0), processing_time_sum(0.0), is_initialized(false) {
        memset(processing_buffer, 0, sizeof(processing_buffer));
    }
};

// Implementation of fixed function names (extern "C" linkage)
extern "C" {

// SoC initialization handler - allocates and returns user_state
nicc::user_state_info soc_init_handler() {
    // User allocates their own state here
    MyAppState* state = new MyAppState();
    state->message_counter = 0;
    state->processing_time_sum = 0.0;
    state->is_initialized = true;
    
    NICC_LOG("SoC kernel initialized: state allocated, size=%u", (unsigned int)sizeof(MyAppState));
    
    // Return state pointer and size information
    nicc::user_state_info result;
    result.state = static_cast<void*>(state);
    result.size = sizeof(MyAppState);
    return result;
}

// SoC cleanup handler - frees user_state
void soc_cleanup_handler(void* user_state) {
    if (user_state) {
        MyAppState* state = static_cast<MyAppState*>(user_state);
        NICC_LOG("Cleaning up MyAppState: processed %d messages, total time=%.3f", 
                 state->message_counter, state->processing_time_sum);
        delete state;
    }
}

// SoC message handler - processes messages
nicc::nicc_retval_t soc_msg_handler(nicc::Buffer* msg, void* user_state) {
    if (!user_state) {
        NICC_WARN("user_state is null");
        return nicc::NICC_ERROR;
    }
    
    MyAppState* state = static_cast<MyAppState*>(user_state);
    
    if (!state->is_initialized) {
        NICC_WARN("user_state not properly initialized");
        return nicc::NICC_ERROR;
    }
    
    // Increment message counter
    state->message_counter++;
    
    // Process message logic (example: simple echo and modify content)
    NICC_LOG("Processing message #%d, length=%u", 
             state->message_counter, msg->length_);
    
    // Do some processing in user buffer
    if (msg->length_ < sizeof(state->processing_buffer)) {
        memcpy(state->processing_buffer, msg->get_buf(), msg->length_);
        // Can modify message content
        // ...
    }
    
    // Record processing time (example)
    state->processing_time_sum += 0.001; // Assume processing time
    
    return nicc::NICC_SUCCESS;
}

// SoC packet handler - processes packets (optional)
nicc::nicc_retval_t soc_pkt_handler(nicc::Buffer* pkt, void* user_state) {
    if (!user_state) return nicc::NICC_ERROR;
    
    MyAppState* state = static_cast<MyAppState*>(user_state);
    NICC_LOG("Processing packet, total messages so far: %d", state->message_counter);
    return nicc::NICC_SUCCESS;
}

}  // extern "C"

// Note: AppHandler registration is now handled automatically in runtime/src/main.cc
// Users only need to implement the four fixed function names above:
// - soc_init_handler()
// - soc_msg_handler()
// - soc_cleanup_handler()  
// - soc_pkt_handler() 