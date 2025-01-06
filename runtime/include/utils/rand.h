#pragma once

#include <random>

namespace nicc {

class SlowRand {
  std::random_device rand_dev_;  // Non-pseudorandom seed for twister
  std::mt19937_64 mt_;
  std::uniform_int_distribution<uint64_t> dist_;

 public:
  SlowRand() : mt_(rand_dev_()), dist_(0, UINT64_MAX) {}

  inline uint64_t next_u64() { return dist_(mt_); }
};

class FastRand {
 public:
  uint64_t seed_;

  /// Create a FastRand using a seed from SlowRand
  FastRand() {
    SlowRand slow_rand;
    seed_ = slow_rand.next_u64();
  }

  inline uint64_t next_u64() {
    seed_ = seed_ * 1103515245 + 12345;
    return static_cast<uint64_t>(seed_);
  }

  inline uint32_t next_u32() {
    seed_ = seed_ * 1103515245 + 12345;
    return static_cast<uint32_t>(seed_ >> 32);
  }

  inline uint16_t next_u16() {
    seed_ = seed_ * 1103515245 + 12345;
    return static_cast<uint16_t>(seed_ >> 48);
  }

  inline uint8_t next_u8() {
    seed_ = seed_ * 1103515245 + 12345;
    return static_cast<uint8_t>(seed_ >> 56);
  }
};

}  // namespace nicc
