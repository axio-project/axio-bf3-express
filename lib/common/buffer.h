#pragma once

#include "common.h"
#include "common/iphdr.h"
#include <netinet/udp.h>

namespace nicc {
/// A class to hold a fixed-size buffer. The size of the buffer is read-only
/// after the Buffer is created.
class Buffer {
 public:
  static constexpr uint8_t kPOSTED = 0;
  static constexpr uint8_t kAPP_OWNED_BUF = 1;
  static constexpr uint8_t kFREE_BUF = 2;
  Buffer(uint8_t *buf, size_t class_size, uint32_t lkey)
      : buf_(buf), class_size_(class_size), lkey_(lkey) {}

  Buffer() {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  /// Return a string representation of this Buffer (excluding lkey)
  std::string to_string() const {
    std::ostringstream ret;
    ret << "[buf " << static_cast<void *>(buf_) << ", "
        << "class sz " << class_size_ << "]";
    return ret.str();
  }

  void set_lkey(uint32_t lkey) { lkey_ = lkey; }

  uint8_t* get_buf() { return buf_; }
  uint8_t* get_buf_offset(size_t offset) { return buf_ + offset; }
  uint8_t* get_uh() { return buf_ + 14 + sizeof(struct iphdr); }
  uint8_t* get_iph() { return buf_ + 14; }
  void set_length(uint32_t length) { length_ = length; }

  /// The backing memory of this Buffer. The Buffer is invalid if this is null.
  uint8_t *buf_;
  size_t class_size_;  ///< The allocator's class size
  uint32_t lkey_;      ///< The memory registration lkey
  uint32_t length_ = 0;    ///< The length of the buffer
  /// Using for RX
  Buffer *next_;       ///< Next Buffer
  uint8_t state_ = kFREE_BUF;  /// 0: owned by nic; 1: owned by app; 2: free, waiting for post_recv
};

}  // namespace nicc
