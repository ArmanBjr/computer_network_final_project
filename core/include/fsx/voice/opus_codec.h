#pragma once
#include <cstdint>
#include <vector>
#include <memory>

// Forward-declare opaque Opus types so we don't leak the C header.
struct OpusEncoder;
struct OpusDecoder;

namespace fsx::voice {

/// Opus codec parameters used throughout the voice system.
static constexpr int SAMPLE_RATE   = 48000;   // Hz
static constexpr int CHANNELS      = 1;       // mono
static constexpr int FRAME_MS      = 20;      // milliseconds per frame
static constexpr int FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS / 1000; // 960
static constexpr int BITRATE       = 32000;   // 32 kbit/s (good for voice)
static constexpr int MAX_OPUS_PACKET = 512;    // bytes (more than enough)

/**
 * Thin RAII wrapper around libopus encoder + decoder.
 *
 * Encode: pcm int16 mono 960 samples  →  opus bytes (variable length)
 * Decode: opus bytes                   →  pcm int16 mono 960 samples
 */
class OpusCodecWrapper {
public:
  OpusCodecWrapper();
  ~OpusCodecWrapper();

  // non-copyable
  OpusCodecWrapper(const OpusCodecWrapper&) = delete;
  OpusCodecWrapper& operator=(const OpusCodecWrapper&) = delete;

  /// Encode one frame (960 int16 samples) → opus packet.
  /// Returns empty vector on error.
  std::vector<uint8_t> encode(const int16_t* pcm, int frame_samples = FRAME_SAMPLES);

  /// Decode one opus packet → PCM samples (960 int16).
  /// Returns empty vector on error.
  std::vector<int16_t> decode(const uint8_t* opus_data, int opus_len);

  /// Generate a PLC (packet-loss-concealment) frame when a packet is missing.
  std::vector<int16_t> decode_plc();

  bool is_valid() const { return encoder_ != nullptr && decoder_ != nullptr; }

private:
  OpusEncoder* encoder_ = nullptr;
  OpusDecoder* decoder_ = nullptr;
};

} // namespace fsx::voice
