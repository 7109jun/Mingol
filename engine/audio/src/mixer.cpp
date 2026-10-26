// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/audio/mixer.hpp"

#include <algorithm>
#include <cmath>

#include "mingol/core/log.hpp"

namespace mingol::audio {
namespace {
constexpr std::size_t kMaxPendingCommands = 1u << 16;
constexpr float kHalfPi = 1.57079632679F;
constexpr float kQuarterPi = 0.78539816339F;

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

struct Mixer::Command {
  enum class Type : std::uint8_t { Play, Stop, StopAll, Volume, Pitch, Pan, Position, Paused } type = Type::Stop;
  std::uint64_t id = 0;
  float f = 0.0F;
  Vec3 v{0, 0, 0};
  bool b = false;
  std::shared_ptr<const assets::AudioClip> clip;
  PlayDesc desc;
};

struct Mixer::Voice {
  bool active = false;
  std::uint64_t id = 0;
  std::uint64_t serial = 0;  // start order, for "oldest" stealing
  std::shared_ptr<const assets::AudioClip> clip;
  PlayDesc d;
  double pos = 0.0;       // position in clip frames
  float gainL = 0.0F, gainR = 0.0F;  // gains applied at the end of the previous block
  bool fresh = true;      // first block: no ramp
};

Mixer::Mixer(const MixerDesc& d) : desc_(d), voices_(d.maxVoices), playingIds_(d.maxVoices) {
  pending_.reserve(256);
  working_.reserve(256);
  retired_.reserve(static_cast<std::size_t>(d.maxVoices) * 2);
}

Mixer::~Mixer() = default;

Result<std::unique_ptr<Mixer>> Mixer::create(const MixerDesc& desc) {
  if (desc.sampleRate < 8000 || desc.sampleRate > 384000) return makeError(ErrorCode::InvalidArgument, "sample rate {} out of range", desc.sampleRate);
  if (desc.maxVoices < 1 || desc.maxVoices > 1024) return makeError(ErrorCode::InvalidArgument, "maxVoices {} out of range 1..1024", desc.maxVoices);
  return std::unique_ptr<Mixer>(new Mixer(desc));
}

// ---- game thread ---------------------------------------------------------------------------------------

void Mixer::releaseRetired() {
  std::vector<std::shared_ptr<const assets::AudioClip>> trash;
  {
    std::lock_guard lock(mutex_);
    trash.swap(retired_);
    retired_.reserve(static_cast<std::size_t>(desc_.maxVoices) * 2);
  }
  // `trash` (and any clip memory it uniquely owns) is freed here, on the calling game thread.
}

Result<VoiceId> Mixer::play(std::shared_ptr<const assets::AudioClip> clip, const PlayDesc& d) {
  if (!clip || clip->channels == 0 || clip->sampleRate == 0 || clip->frames() == 0) {
    return makeError(ErrorCode::InvalidArgument, "cannot play an empty clip");
  }
  if (!std::isfinite(d.volume) || d.volume < 0.0F || d.volume > 16.0F) return makeError(ErrorCode::InvalidArgument, "volume must be within 0..16");
  if (!std::isfinite(d.pan) || d.pan < -1.0F || d.pan > 1.0F) return makeError(ErrorCode::InvalidArgument, "pan must be within -1..1");
  if (!std::isfinite(d.pitch) || d.pitch < 0.01F || d.pitch > 16.0F) return makeError(ErrorCode::InvalidArgument, "pitch must be within 0.01..16");
  if (d.spatial) {
    if (!finite(d.position)) return makeError(ErrorCode::InvalidArgument, "position must be finite");
    if (!(d.minDistance > 0.0F) || !std::isfinite(d.minDistance) || !(d.maxDistance >= d.minDistance) || !std::isfinite(d.maxDistance) ||
        !(d.rolloff >= 0.0F) || !std::isfinite(d.rolloff)) {
      return makeError(ErrorCode::InvalidArgument, "spatial parameters out of range (0 < min <= max, rolloff >= 0)");
    }
  }
  releaseRetired();
  Command c;
  c.type = Command::Type::Play;
  c.id = nextId_.fetch_add(1);
  c.clip = std::move(clip);
  c.desc = d;
  {
    std::lock_guard lock(mutex_);
    if (pending_.size() >= kMaxPendingCommands) return makeError(ErrorCode::OutOfRange, "audio command queue is full (is the audio device running?)");
    pending_.push_back(std::move(c));
    return VoiceId{pending_.back().id};
  }
}

namespace {
}  // namespace

void Mixer::stop(VoiceId id) {
  if (!id.valid()) return;
  Command c;
  c.type = Command::Type::Stop;
  c.id = id.value;
  std::lock_guard lock(mutex_);
  if (pending_.size() < kMaxPendingCommands) pending_.push_back(std::move(c));
  else MINGOL_LOG_WARN("audio", "command queue full: stop() dropped");
}

void Mixer::stopAll() {
  Command c;
  c.type = Command::Type::StopAll;
  std::lock_guard lock(mutex_);
  if (pending_.size() < kMaxPendingCommands) pending_.push_back(std::move(c));
}

#define MINGOL_AUDIO_SETTER(NAME, TYPE_ENUM, CHECK, FIELD, PARAM_T)                                          \
  Result<void> Mixer::NAME(VoiceId id, PARAM_T value) {                                                      \
    if (!id.valid()) return makeError(ErrorCode::InvalidArgument, "invalid voice id");                       \
    if (!(CHECK)) return makeError(ErrorCode::InvalidArgument, #NAME ": value out of range");                \
    Command c;                                                                                               \
    c.type = Command::Type::TYPE_ENUM;                                                                       \
    c.id = id.value;                                                                                         \
    c.FIELD = value;                                                                                         \
    std::lock_guard lock(mutex_);                                                                            \
    if (pending_.size() >= kMaxPendingCommands) return makeError(ErrorCode::OutOfRange, "audio command queue is full"); \
    pending_.push_back(std::move(c));                                                                        \
    return {};                                                                                               \
  }

MINGOL_AUDIO_SETTER(setVolume, Volume, std::isfinite(value) && value >= 0.0F && value <= 16.0F, f, float)
MINGOL_AUDIO_SETTER(setPitch, Pitch, std::isfinite(value) && value >= 0.01F && value <= 16.0F, f, float)
MINGOL_AUDIO_SETTER(setPan, Pan, std::isfinite(value) && value >= -1.0F && value <= 1.0F, f, float)
MINGOL_AUDIO_SETTER(setPosition, Position, finite(value), v, Vec3)
MINGOL_AUDIO_SETTER(setPaused, Paused, true, b, bool)
#undef MINGOL_AUDIO_SETTER

void Mixer::setListener(Vec3 position, Vec3 forward, Vec3 up) {
  if (!finite(position) || !finite(forward) || !finite(up) || lengthSq(forward) < 1e-12F || lengthSq(up) < 1e-12F) return;
  std::lock_guard lock(mutex_);
  pendingListener_ = {position, normalize(forward), normalize(up)};
  listenerDirty_ = true;
}

Result<void> Mixer::setMasterVolume(float v) {
  if (!std::isfinite(v) || v < 0.0F || v > 16.0F) return makeError(ErrorCode::InvalidArgument, "master volume must be within 0..16");
  master_.store(v, std::memory_order_relaxed);
  return {};
}

bool Mixer::isPlaying(VoiceId id) const noexcept {
  if (!id.valid()) return false;
  for (const auto& slot : playingIds_) {
    if (slot.load(std::memory_order_acquire) == id.value) return true;
  }
  return false;
}

MixerStats Mixer::stats() const noexcept {
  MixerStats s;
  s.framesRendered = framesRendered_.load();
  s.activeVoices = active_.load();
  s.voicesStarted = started_.load();
  s.voicesStolen = stolen_.load();
  s.voicesRejected = rejected_.load();
  s.clippedSamples = clipped_.load();
  return s;
}

// ---- audio thread -----------------------------------------------------------------------------------------

void Mixer::startVoice(Command& c) noexcept {
  Voice* slot = nullptr;
  for (Voice& v : voices_) {
    if (!v.active) { slot = &v; break; }
  }
  if (slot == nullptr) {
    Voice* victim = &voices_[0];
    for (Voice& v : voices_) {
      if (v.d.priority < victim->d.priority || (v.d.priority == victim->d.priority && v.serial < victim->serial)) victim = &v;
    }
    if (victim->d.priority > c.desc.priority) {
      rejected_.fetch_add(1, std::memory_order_relaxed);
      return;  // everything playing outranks the newcomer
    }
    stolen_.fetch_add(1, std::memory_order_relaxed);
    victim->active = false;
    slot = victim;
  }
  // Move the previous clip (if any) out through the retire path before reusing the slot.
  if (slot->clip) {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() && retired_.size() < retired_.capacity()) retired_.push_back(std::move(slot->clip));
    // else: the assignment below drops the reference here (rare; only when the queue is saturated)
  }
  slot->active = true;
  slot->id = c.id;
  slot->serial = ++startSerial_;
  slot->clip = std::move(c.clip);
  slot->d = c.desc;
  slot->pos = 0.0;
  slot->gainL = slot->gainR = 0.0F;
  slot->fresh = true;
  started_.fetch_add(1, std::memory_order_relaxed);
}

void Mixer::applyCommands() noexcept {
  {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return;  // the game thread is writing: apply next block
    working_.clear();
    working_.swap(pending_);
    if (listenerDirty_) {
      listener_ = pendingListener_;
      listenerDirty_ = false;
    }
  }
  for (Command& c : working_) {
    using T = Command::Type;
    if (c.type == T::Play) {
      startVoice(c);
      continue;
    }
    if (c.type == T::StopAll) {
      for (Voice& v : voices_) v.active = false;
      continue;
    }
    for (Voice& v : voices_) {
      if (!v.active || v.id != c.id) continue;
      switch (c.type) {
        case T::Stop: v.active = false; break;
        case T::Volume: v.d.volume = c.f; break;
        case T::Pitch: v.d.pitch = c.f; break;
        case T::Pan: v.d.pan = c.f; break;
        case T::Position: v.d.position = c.v; break;
        case T::Paused: v.d.paused = c.b; break;
        default: break;
      }
      break;
    }
  }
  working_.clear();
}

void Mixer::render(float* out, std::uint32_t frames) noexcept {
  std::fill(out, out + static_cast<std::size_t>(frames) * 2, 0.0F);
  applyCommands();

  const float master = master_.load(std::memory_order_relaxed);
  const Vec3 right = [&] {
    const Vec3 r = cross(listener_.forward, listener_.up);
    return lengthSq(r) > 1e-12F ? normalize(r) : Vec3{1, 0, 0};
  }();

  for (Voice& v : voices_) {
    if (!v.active) continue;
    const assets::AudioClip& clip = *v.clip;
    const std::uint64_t total = clip.frames();
    const std::uint32_t ch = clip.channels;

    // Target gains for this block.
    float pan = v.d.pan;
    float dist = 1.0F;
    if (v.d.spatial) {
      const Vec3 rel = v.d.position - listener_.position;
      const float d = length(rel);
      const float clamped = std::clamp(d, v.d.minDistance, v.d.maxDistance);
      dist = v.d.minDistance / (v.d.minDistance + v.d.rolloff * (clamped - v.d.minDistance));
      pan = d > 1e-4F ? std::clamp(dot(rel / d, right), -1.0F, 1.0F) : 0.0F;
    }
    float tL, tR;
    if (ch == 1) {  // equal-power panning of a mono source
      const float a = (pan + 1.0F) * kQuarterPi;
      tL = std::cos(a);
      tR = std::sin(a);
    } else {        // balance control for stereo and multichannel sources
      tL = pan > 0.0F ? std::cos(pan * kHalfPi) : 1.0F;
      tR = pan < 0.0F ? std::cos(-pan * kHalfPi) : 1.0F;
    }
    const float base = v.d.volume * dist * master;
    tL *= base;
    tR *= base;
    if (v.fresh) {
      v.gainL = tL;
      v.gainR = tR;
      v.fresh = false;
    }
    const float startL = v.gainL, startR = v.gainR;

    const double step = static_cast<double>(clip.sampleRate) / static_cast<double>(desc_.sampleRate) * static_cast<double>(v.d.pitch);
    const float invFrames = 1.0F / static_cast<float>(frames);
    bool finished = false;
    if (v.d.paused) {
      v.gainL = tL;
      v.gainR = tR;
    } else {
      for (std::uint32_t i = 0; i < frames; ++i) {
        std::uint64_t i0 = static_cast<std::uint64_t>(v.pos);
        if (i0 >= total) {
          if (v.d.loop) {
            v.pos = std::fmod(v.pos, static_cast<double>(total));
            i0 = static_cast<std::uint64_t>(v.pos);
          } else {
            finished = true;
            break;
          }
        }
        const float frac = static_cast<float>(v.pos - static_cast<double>(i0));
        std::uint64_t i1 = i0 + 1;
        if (i1 >= total) i1 = v.d.loop ? 0 : total - 1;
        float l0, r0, l1, r1;
        auto fetch = [&](std::uint64_t f, float& l, float& r) {
          const float* p = &clip.samples[f * ch];
          if (ch == 1) {
            l = r = p[0];
          } else if (ch == 2) {
            l = p[0];
            r = p[1];
          } else {  // downmix: even channels -> left, odd -> right
            float sl = 0, sr = 0;
            std::uint32_t nl = 0, nr = 0;
            for (std::uint32_t c = 0; c < ch; ++c) {
              if (c % 2 == 0) { sl += p[c]; ++nl; } else { sr += p[c]; ++nr; }
            }
            l = sl / static_cast<float>(nl);
            r = nr > 0 ? sr / static_cast<float>(nr) : l;
          }
        };
        fetch(i0, l0, r0);
        fetch(i1, l1, r1);
        const float l = l0 + (l1 - l0) * frac;
        const float r = r0 + (r1 - r0) * frac;
        const float t = static_cast<float>(i + 1) * invFrames;
        out[2 * i] += l * (startL + (tL - startL) * t);
        out[2 * i + 1] += r * (startR + (tR - startR) * t);
        v.pos += step;
      }
      v.gainL = tL;
      v.gainR = tR;
      if (!v.d.loop && v.pos >= static_cast<double>(total)) finished = true;
    }
    if (finished) v.active = false;
  }

  // Limit to the representable range and count how often that was needed.
  std::uint64_t clipped = 0;
  const std::size_t n = static_cast<std::size_t>(frames) * 2;
  for (std::size_t i = 0; i < n; ++i) {
    const float s = out[i];
    if (s > 1.0F) { out[i] = 1.0F; ++clipped; }
    else if (s < -1.0F) { out[i] = -1.0F; ++clipped; }
    else if (s != s) { out[i] = 0.0F; }  // NaN can only come from a broken clip; never forward it to the speakers
  }
  if (clipped != 0) clipped_.fetch_add(clipped, std::memory_order_relaxed);

  // Publish state and hand finished clips back to the game thread.
  std::uint32_t active = 0;
  for (std::size_t i = 0; i < voices_.size(); ++i) {
    Voice& v = voices_[i];
    playingIds_[i].store(v.active ? v.id : 0, std::memory_order_release);
    active += v.active ? 1u : 0u;
    if (!v.active && v.clip) {
      std::unique_lock lock(mutex_, std::try_to_lock);
      if (lock.owns_lock() && retired_.size() < retired_.capacity()) retired_.push_back(std::move(v.clip));
      // otherwise it is retried on the next block
    }
  }
  active_.store(active, std::memory_order_relaxed);
  framesRendered_.fetch_add(frames, std::memory_order_relaxed);
}

}  // namespace mingol::audio
