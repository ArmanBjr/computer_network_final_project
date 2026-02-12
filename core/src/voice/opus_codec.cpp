#include "fsx/voice/opus_codec.h"
#include <opus/opus.h>
#include <iostream>

namespace fsx::voice {

OpusCodecWrapper::OpusCodecWrapper() {
  int err = 0;

  encoder_ = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK || !encoder_) {
    std::cerr << "[opus] encoder create failed: " << opus_strerror(err) << "\n";
    encoder_ = nullptr;
    return;
  }
  opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(BITRATE));
  opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

  decoder_ = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
  if (err != OPUS_OK || !decoder_) {
    std::cerr << "[opus] decoder create failed: " << opus_strerror(err) << "\n";
    opus_encoder_destroy(encoder_);
    encoder_ = nullptr;
    decoder_ = nullptr;
    return;
  }
}

OpusCodecWrapper::~OpusCodecWrapper() {
  if (encoder_) opus_encoder_destroy(encoder_);
  if (decoder_) opus_decoder_destroy(decoder_);
}

std::vector<uint8_t> OpusCodecWrapper::encode(const int16_t* pcm, int frame_samples) {
  if (!encoder_) return {};

  std::vector<uint8_t> out(MAX_OPUS_PACKET);
  int nbytes = opus_encode(encoder_, pcm, frame_samples,
                           out.data(), static_cast<opus_int32>(out.size()));
  if (nbytes < 0) {
    std::cerr << "[opus] encode error: " << opus_strerror(nbytes) << "\n";
    return {};
  }
  out.resize(static_cast<size_t>(nbytes));
  return out;
}

std::vector<int16_t> OpusCodecWrapper::decode(const uint8_t* opus_data, int opus_len) {
  if (!decoder_) return {};

  std::vector<int16_t> pcm(FRAME_SAMPLES);
  int nsamples = opus_decode(decoder_, opus_data, opus_len,
                             pcm.data(), FRAME_SAMPLES, /*fec=*/0);
  if (nsamples < 0) {
    std::cerr << "[opus] decode error: " << opus_strerror(nsamples) << "\n";
    return {};
  }
  pcm.resize(static_cast<size_t>(nsamples));
  return pcm;
}

std::vector<int16_t> OpusCodecWrapper::decode_plc() {
  if (!decoder_) return {};

  std::vector<int16_t> pcm(FRAME_SAMPLES);
  int nsamples = opus_decode(decoder_, nullptr, 0, pcm.data(), FRAME_SAMPLES, /*fec=*/0);
  if (nsamples < 0) return {};
  pcm.resize(static_cast<size_t>(nsamples));
  return pcm;
}

} // namespace fsx::voice
