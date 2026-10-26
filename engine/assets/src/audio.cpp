// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/assets/audio.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace mingol::assets {
namespace {

std::uint16_t rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t rd32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool tag(const std::uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

Result<void> checkFormat(std::uint32_t channels, std::uint32_t rate, const AudioLimits& l) {
  if (channels == 0 || channels > l.maxChannels) {
    return makeError(ErrorCode::OutOfRange, "{} channels (allowed 1..{})", channels, l.maxChannels);
  }
  if (rate == 0 || rate > l.maxSampleRate) {
    return makeError(ErrorCode::OutOfRange, "sample rate {} (allowed 1..{})", rate, l.maxSampleRate);
  }
  return {};
}

}  // namespace

AudioFormat sniffAudioFormat(std::span<const std::uint8_t> d) noexcept {
  if (d.size() >= 12 && tag(d.data(), "RIFF") && tag(d.data() + 8, "WAVE")) return AudioFormat::Wav;
  if (d.size() >= 4 && tag(d.data(), "OggS")) return AudioFormat::Ogg;
  return AudioFormat::Unknown;
}

Result<AudioClip> decodeWav(std::span<const std::uint8_t> data, const AudioLimits& limits) {
  if (sniffAudioFormat(data) != AudioFormat::Wav) return makeError(ErrorCode::Unsupported, "not a RIFF/WAVE file");
  const std::uint8_t* base = data.data();
  const std::size_t size = data.size();

  bool haveFmt = false;
  std::uint16_t formatTag = 0, channels = 0, bits = 0, blockAlign = 0;
  std::uint32_t rate = 0;
  const std::uint8_t* pcm = nullptr;
  std::size_t pcmSize = 0;

  std::size_t pos = 12;
  while (pos + 8 <= size) {
    const std::uint8_t* hdr = base + pos;
    const std::uint32_t declared = rd32(hdr + 4);
    const std::size_t bodyPos = pos + 8;
    const std::size_t avail = size - bodyPos;
    if (tag(hdr, "fmt ")) {
      if (declared < 16 || declared > avail) return makeError(ErrorCode::Corrupt, "fmt chunk is truncated");
      const std::uint8_t* f = base + bodyPos;
      formatTag = rd16(f);
      channels = rd16(f + 2);
      rate = rd32(f + 4);
      blockAlign = rd16(f + 12);
      bits = rd16(f + 14);
      if (formatTag == 0xFFFE) {  // WAVE_FORMAT_EXTENSIBLE: the real tag is the first 2 bytes of the sub-format GUID
        if (declared < 40) return makeError(ErrorCode::Corrupt, "extensible fmt chunk is too short");
        formatTag = rd16(f + 24);
      }
      haveFmt = true;
    } else if (tag(hdr, "data")) {
      if (!haveFmt) return makeError(ErrorCode::Corrupt, "data chunk before fmt chunk");
      std::size_t n = declared;
      if (declared == 0 || declared == 0xFFFFFFFFu) {
        n = avail;  // streaming writers leave the size open
      } else if (declared > avail) {
        return makeError(ErrorCode::Corrupt, "data chunk claims {} bytes but only {} are present", declared, avail);
      }
      pcm = base + bodyPos;
      pcmSize = n;
      break;
    }
    const std::size_t step = static_cast<std::size_t>(declared) + (declared & 1u);  // chunks are word aligned
    if (step > avail) break;
    pos = bodyPos + step;
  }
  if (!haveFmt) return makeError(ErrorCode::Corrupt, "missing fmt chunk");
  if (pcm == nullptr) return makeError(ErrorCode::Corrupt, "missing data chunk");

  MINGOL_TRY(checkFormat(channels, rate, limits));
  const bool isFloat = formatTag == 3;
  if (formatTag != 1 && !isFloat) return makeError(ErrorCode::Unsupported, "WAV format tag {} (only PCM and float)", formatTag);
  if (isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
    return makeError(ErrorCode::Unsupported, "{}-bit {} samples", bits, isFloat ? "float" : "PCM");
  }
  const std::size_t bytesPerSample = bits / 8;
  if (blockAlign != bytesPerSample * channels) {
    return makeError(ErrorCode::Corrupt, "blockAlign {} does not match {} channels x {} bytes", blockAlign, channels, bytesPerSample);
  }
  const std::size_t frames = pcmSize / blockAlign;  // a partial trailing frame is dropped
  if (frames > limits.maxFrames) return makeError(ErrorCode::OutOfRange, "{} frames exceed the limit {}", frames, limits.maxFrames);

  AudioClip clip;
  clip.sampleRate = rate;
  clip.channels = channels;
  clip.samples.resize(frames * channels);
  const std::size_t count = clip.samples.size();
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t* p = pcm + i * bytesPerSample;
    float v = 0.0F;
    if (isFloat) {
      if (bits == 32) {
        std::uint32_t u = rd32(p);
        float f;
        std::memcpy(&f, &u, 4);
        v = f;
      } else {
        const std::uint64_t u = static_cast<std::uint64_t>(rd32(p)) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32);
        double d;
        std::memcpy(&d, &u, 8);
        v = static_cast<float>(d);
      }
      if (!std::isfinite(v)) v = 0.0F;  // NaN/inf would poison the mixer
    } else if (bits == 8) {
      v = (static_cast<int>(p[0]) - 128) / 128.0F;  // unsigned
    } else if (bits == 16) {
      v = static_cast<std::int16_t>(rd16(p)) / 32768.0F;
    } else if (bits == 24) {
      std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
      if (s & 0x800000) s -= 0x1000000;
      v = static_cast<float>(s) / 8388608.0F;
    } else {
      v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(rd32(p))) / 2147483648.0);
    }
    clip.samples[i] = v;
  }
  return clip;
}

Result<AudioClip> decodeOgg(std::span<const std::uint8_t> data, const AudioLimits& limits) {
  if (sniffAudioFormat(data) != AudioFormat::Ogg) return makeError(ErrorCode::Unsupported, "not an Ogg stream");
  if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return makeError(ErrorCode::OutOfRange, "Ogg data too large");
  }
  // stb_vorbis leaks its temporary setup allocations when a corrupt header makes opening fail
  // (found by valgrind). Giving it a fixed arena means it never calls malloc: nothing can leak,
  // and a crafted header cannot make it allocate more than the arena size.
  constexpr int kArenaBytes = 32 << 20;
  const std::unique_ptr<char[]> arena(new char[kArenaBytes]);  // uninitialised on purpose
  stb_vorbis_alloc alloc{};
  alloc.alloc_buffer = arena.get();
  alloc.alloc_buffer_length_in_bytes = kArenaBytes;
  int err = 0;
  stb_vorbis* v = stb_vorbis_open_memory(data.data(), static_cast<int>(data.size()), &err, &alloc);
  if (v == nullptr) {
    return makeError(err == VORBIS_outofmem ? ErrorCode::OutOfRange : ErrorCode::Corrupt,
                     "cannot open Ogg Vorbis stream (stb_vorbis error {})", err);
  }
  struct Closer {
    stb_vorbis* v;
    ~Closer() { stb_vorbis_close(v); }
  } closer{v};

  const stb_vorbis_info info = stb_vorbis_get_info(v);
  MINGOL_TRY(checkFormat(static_cast<std::uint32_t>(info.channels), info.sample_rate, limits));
  const unsigned total = stb_vorbis_stream_length_in_samples(v);
  if (total > limits.maxFrames) return makeError(ErrorCode::OutOfRange, "{} frames exceed the limit {}", total, limits.maxFrames);

  AudioClip clip;
  clip.sampleRate = info.sample_rate;
  clip.channels = static_cast<std::uint32_t>(info.channels);
  clip.samples.reserve(static_cast<std::size_t>(total) * clip.channels);
  std::vector<float> block(4096u * clip.channels);
  for (;;) {
    const int got = stb_vorbis_get_samples_float_interleaved(v, info.channels, block.data(), static_cast<int>(block.size()));
    if (got <= 0) break;
    clip.samples.insert(clip.samples.end(), block.begin(), block.begin() + static_cast<std::size_t>(got) * clip.channels);
    if (clip.frames() > limits.maxFrames) return makeError(ErrorCode::OutOfRange, "decoded stream exceeds the frame limit");
  }
  if (clip.samples.empty()) return makeError(ErrorCode::Corrupt, "Ogg stream contains no audio");
  if (total > 0 && clip.frames() + 1 < total) {
    return makeError(ErrorCode::Corrupt, "Ogg stream is truncated or damaged: decoded {} of {} frames", clip.frames(), total);
  }
  if (stb_vorbis_get_error(v) != VORBIS__no_error && stb_vorbis_get_error(v) != VORBIS_need_more_data) {
    return makeError(ErrorCode::Corrupt, "Ogg Vorbis decode error {}", stb_vorbis_get_error(v));
  }
  return clip;
}

Result<AudioClip> decodeAudio(std::span<const std::uint8_t> data, const AudioLimits& limits) {
  switch (sniffAudioFormat(data)) {
    case AudioFormat::Wav: return decodeWav(data, limits);
    case AudioFormat::Ogg: return decodeOgg(data, limits);
    case AudioFormat::Unknown: break;
  }
  return makeError(ErrorCode::Unsupported, "unrecognised audio format (expected WAV or Ogg Vorbis)");
}

}  // namespace mingol::assets
