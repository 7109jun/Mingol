// SPDX-License-Identifier: GPL-2.0-only
// WAV and Ogg Vorbis decoding to interleaved 32-bit float PCM.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::assets {

struct AudioClip {
  std::uint32_t sampleRate = 0;
  std::uint32_t channels = 0;
  std::vector<float> samples;  // interleaved, nominal range [-1, 1]

  std::uint64_t frames() const noexcept { return channels == 0 ? 0 : samples.size() / channels; }
  double durationSeconds() const noexcept {
    return sampleRate == 0 ? 0.0 : static_cast<double>(frames()) / sampleRate;
  }
};

struct AudioLimits {
  std::uint32_t maxChannels = 8;
  std::uint32_t maxSampleRate = 768000;
  std::uint64_t maxFrames = 1ull << 28;  // ~ 1.5 hours at 48 kHz
};

enum class AudioFormat : std::uint8_t { Unknown, Wav, Ogg };
AudioFormat sniffAudioFormat(std::span<const std::uint8_t> data) noexcept;

// RIFF/WAVE: PCM 8/16/24/32-bit integer, IEEE float 32/64-bit, WAVE_FORMAT_EXTENSIBLE wrappers of
// those. Unknown chunks are skipped. Truncated data chunks are rejected (a data size of 0 or
// 0xFFFFFFFF, as written by streaming encoders, means "to the end of the file").
Result<AudioClip> decodeWav(std::span<const std::uint8_t> data, const AudioLimits& limits = {});

// Ogg Vorbis (via stb_vorbis). The stream length is checked against the limits before decoding.
Result<AudioClip> decodeOgg(std::span<const std::uint8_t> data, const AudioLimits& limits = {});

// Picks the decoder from the content.
Result<AudioClip> decodeAudio(std::span<const std::uint8_t> data, const AudioLimits& limits = {});

}  // namespace mingol::assets
