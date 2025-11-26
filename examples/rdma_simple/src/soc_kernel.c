// SoC kernel implementation for batch message processing
// This file should be compiled as C++ due to nicc namespace requirements

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

// Forward declarations for nicc types
struct nicc_Buffer;

// User-defined state structure
typedef struct {
    uint64_t message_counter;
    uint64_t batch_counter;
    double processing_time_sum;
} soc_app_state_t;

// SoC initialization handler - allocates and returns user_state
struct user_state_info {
    void* state;
    size_t size;
};

struct user_state_info soc_init_handler(void) {
    // Allocate user state
    soc_app_state_t* state = (soc_app_state_t*)malloc(sizeof(soc_app_state_t));
    struct user_state_info result;
    
    if (!state) {
        result.state = NULL;
        result.size = 0;
        return result;
    }
    
    state->message_counter = 0;
    state->batch_counter = 0;
    state->processing_time_sum = 0.0;
    
    // Return state pointer and size information
    result.state = (void*)state;
    result.size = sizeof(soc_app_state_t);
    return result;
}

// SoC cleanup handler - frees user_state
void soc_cleanup_handler(void* user_state) {
    if (user_state) {
        free(user_state);
    }
}

// SoC message handler - processes messages in batch mode
int soc_msg_handler(struct nicc_Buffer** msg_batch, size_t batch_size, void* user_state) {
    if (!user_state) return 1;  // NICC_ERROR
    
    soc_app_state_t* state = (soc_app_state_t*)user_state;
    
    // Increment batch counter
    state->batch_counter++;
    
    // Process batch of messages
    for (size_t i = 0; i < batch_size; i++) {
        struct nicc_Buffer* msg = msg_batch[i];
        if (!msg) continue;
        
        // Increment message counter
        state->message_counter++;
        
        // Example processing: echo message (no modification needed)
        // In a real application, you would process the message here
        // uint8_t* data = msg->buf_;
        // size_t length = msg->length_;
        
        // Example: modify some bytes in the message
        // if (length > 0) {
        //     data[0] ^= 0x01;  // flip a bit
        // }
    }
    
    return 0;  // NICC_SUCCESS
}

// SoC packet handler - processes packets (optional)
int soc_pkt_handler(struct nicc_Buffer* pkt, void* user_state) {
    if (!user_state) return 1;  // NICC_ERROR
    
    // Example packet processing
    // This handler is optional and may not be called in message-based mode
    
    return 0;  // NICC_SUCCESS
}

#ifdef __cplusplus
}
#endif

