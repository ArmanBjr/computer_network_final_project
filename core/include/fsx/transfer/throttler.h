#pragma once
#include <cstdint>
#include <chrono>
#include <mutex>
#include <atomic>

namespace fsx::transfer {

/**
 * Phase 9: Token-bucket rate limiter.
 *
 * set_rate(R) — R bytes/sec (0 = unlimited)
 * consume(N)  — returns delay in ms that the caller should wait
 *               before allowing the next N bytes through.
 */
class Throttler {
public:
  Throttler();
  ~Throttler() = default;

  /// Set rate limit (bytes/sec). 0 = unlimited.
  void set_rate(uint64_t bytes_per_second);
  uint64_t get_rate() const;

  /**
   * Consume `bytes` from the bucket.
   * @return delay in milliseconds the caller must wait
   *         before the data is "allowed" (0 = no delay).
   */
  uint32_t consume(size_t bytes);

  /// Record bytes for speed measurement (call after actual I/O).
  void record_bytes(size_t bytes);

  /// Measured throughput (bytes/sec) over recent window.
  uint64_t get_current_speed() const;

private:
  mutable std::mutex mutex_;

  // --- token bucket ---
  std::atomic<uint64_t> rate_{0};        // bytes/sec, 0 = unlimited
  double tokens_{0};
  double max_tokens_{0};                 // burst cap = 2 × rate
  std::chrono::steady_clock::time_point last_refill_;

  // --- speed measurement (1-sec window) ---
  uint64_t bytes_in_window_{0};
  std::chrono::steady_clock::time_point window_start_;
  std::atomic<uint64_t> current_speed_{0};
};

} // namespace fsx::transfer
