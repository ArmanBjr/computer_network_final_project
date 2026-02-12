#include "fsx/transfer/throttler.h"
#include <algorithm>

namespace fsx::transfer {

Throttler::Throttler()
  : last_refill_(std::chrono::steady_clock::now()),
    window_start_(std::chrono::steady_clock::now()) {}

void Throttler::set_rate(uint64_t bytes_per_second) {
  std::lock_guard<std::mutex> lock(mutex_);
  rate_.store(bytes_per_second, std::memory_order_relaxed);
  max_tokens_ = static_cast<double>(bytes_per_second) * 2.0;  // 2-second burst
  tokens_ = max_tokens_;                                       // fill on rate change
  last_refill_ = std::chrono::steady_clock::now();
}

uint64_t Throttler::get_rate() const {
  return rate_.load(std::memory_order_relaxed);
}

uint32_t Throttler::consume(size_t bytes) {
  uint64_t rate = rate_.load(std::memory_order_relaxed);
  if (rate == 0) return 0;  // unlimited

  std::lock_guard<std::mutex> lock(mutex_);

  auto now = std::chrono::steady_clock::now();
  double elapsed_sec =
      std::chrono::duration<double>(now - last_refill_).count();
  last_refill_ = now;

  // refill
  tokens_ += elapsed_sec * static_cast<double>(rate);
  if (tokens_ > max_tokens_) tokens_ = max_tokens_;

  // consume
  tokens_ -= static_cast<double>(bytes);

  if (tokens_ >= 0.0) return 0;

  // deficit → delay (ms)
  double deficit = -tokens_;
  auto delay_ms =
      static_cast<uint32_t>((deficit / static_cast<double>(rate)) * 1000.0);
  return delay_ms > 0 ? delay_ms : 1;
}

void Throttler::record_bytes(size_t bytes) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto now = std::chrono::steady_clock::now();
  double elapsed =
      std::chrono::duration<double>(now - window_start_).count();

  bytes_in_window_ += bytes;

  if (elapsed >= 1.0) {
    current_speed_.store(
        static_cast<uint64_t>(bytes_in_window_ / elapsed),
        std::memory_order_relaxed);
    bytes_in_window_ = 0;
    window_start_ = now;
  }
}

uint64_t Throttler::get_current_speed() const {
  return current_speed_.load(std::memory_order_relaxed);
}

} // namespace fsx::transfer
