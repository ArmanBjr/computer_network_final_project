#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <mutex>
#include <optional>

namespace fsx::voice {

/// A single voice frame carried over UDP.
struct VoiceFrame {
  uint32_t session_id  = 0;
  uint32_t seq         = 0;
  uint32_t timestamp_ms = 0;
  std::vector<uint8_t> opus_data;
};

/**
 * Simple jitter buffer that re-orders out-of-order UDP packets
 * and drops packets that arrive too late.
 *
 * Depth is measured in frames (each 20 ms).
 * Default depth = 3 → 60 ms of buffering.
 */
class JitterBuffer {
public:
  explicit JitterBuffer(uint32_t depth_frames = 3);

  /// Push a received frame into the buffer.
  void push(VoiceFrame frame);

  /// Pop the next frame in sequence order.
  /// Returns nullopt when the buffer has nothing ready yet.
  std::optional<VoiceFrame> pop();

  /// Number of frames currently buffered.
  size_t size() const;

  /// Reset (e.g. on session end).
  void reset();

  /// Stats
  uint64_t frames_received() const { return frames_received_; }
  uint64_t frames_dropped()  const { return frames_dropped_; }

private:
  uint32_t depth_;
  uint32_t next_seq_ = 0;       // next expected sequence number
  bool     started_  = false;    // have we received any frame yet?

  mutable std::mutex mutex_;
  std::map<uint32_t, VoiceFrame> buf_;  // seq → frame (sorted)

  uint64_t frames_received_ = 0;
  uint64_t frames_dropped_  = 0;
};

} // namespace fsx::voice
