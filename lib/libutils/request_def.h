#pragma once
#include "common.h"
#include <infiniband/verbs.h>

namespace nicc {

#define kInvalidId 0xFFFFFFFF
/**
 * @brief Structure to restore user's  request
 */
struct request_t {
    uint32_t id;  
    volatile bool compl_flag;  // 1 for complete, 0 for failure
    void* buf;
    size_t len;
    uint32_t qpn;
    union ibv_gid gid;
    public:
    request_t() {
        id = 0;
        compl_flag = false;
        buf = nullptr;
        len = 0;
        qpn = 0;
        memset(&gid, 0, sizeof(union ibv_gid));
    }
    inline void set(void* buf, size_t len, uint32_t qpn, union ibv_gid gid) {
        this->buf = buf;
        this->len = len;
        this->qpn = qpn;
        this->gid = gid;
    }
    inline void reset() {
        compl_flag = false;
        buf = nullptr;
        len = 0;
        qpn = 0;
        memset(&gid, 0, sizeof(union ibv_gid));
    }
};

} // namespace nicc
