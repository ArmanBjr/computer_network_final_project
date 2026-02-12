#include "fsx/voice/jitter_buffer.h"

namespace fsx::voice {

JitterBuffer::JitterBuffer(uint32_t depth_frames)
  : depth_(depth_frames) {}

void JitterBuffer::push(VoiceFrame frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  frames_received_++;

  if (!started_) {
    // First frame ever — seed next_seq_
    next_seq_ = frame.seq;
    started_ = true;
  }

  // Drop if too old (already played)
  // Use signed comparison to handle wrap-around
  int32_t diff = static_cast<int32_t>(frame.seq) - static_cast<int32_t>(next_seq_);
  if (diff < 0) {
    frames_dropped_++;
    return;
  }

  // Drop if buffer is too full (far-future packet protection)
  if (buf_.size() >= depth_ * 4) {
    // evict oldest
    buf_.erase(buf_.begin());
    frames_dropped_++;
  }

  buf_[frame.seq] = std::move(frame);
}

std::optional<VoiceFrame> JitterBuffer::pop() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!started_) return std::nullopt;

  // Wait until we have at least depth_ frames buffered before first pop
  if (buf_.size() < depth_ && buf_.find(next_seq_) == buf_.end()) {
    return std::nullopt;
  }

  auto it = buf_.find(next_seq_);
  if (it != buf_.end()) {
    VoiceFrame f = std::move(it->second);
    buf_.erase(it);
    next_seq_++;
    return f;
  }

  // Frame missing — skip it (PLC opportunity)
  next_seq_++;
  return std::nullopt;
}

size_t JitterBuffer::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buf_.size();
}

void JitterBuffer::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  buf_.clear();
  next_seq_ = 0;
  started_ = false;
}

} // namespace fsx::voice
