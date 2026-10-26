// SPDX-License-Identifier: GPL-2.0-only
// Software audio mixer: voices playing decoded clips, resampled, panned, attenuated by distance and
// summed into interleaved stereo float output. Deterministic and device-independent, so it can be
// tested sample-exactly and rendered offline; AudioDevice (device.hpp) only feeds it to hardware.
//
// Threading: the game thread calls play/stop/set*; the audio thread (or an offline caller) calls
// render(). The audio thread never blocks: commands are handed over through a mutex that render()
// only try-locks (if the game thread holds it, the commands are applied one block later).
// Memory held by finished clips is released on the game thread, not in the audio callback.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "mingol/assets/audio.hpp"
#include "mingol/core/error.hpp"
#include "mingol/core/math.hpp"

namespace mingol::audio {

struct VoiceId {
  std::uint64_t value = 0;
  bool valid() const noexcept { return value != 0; }
  friend bool operator==(VoiceId a, VoiceId b) { return a.value == b.value; }
};

struct MixerDesc {
  std::uint32_t sampleRate = 48000;  // output rate (8000..384000)
  std::uint32_t maxVoices = 64;      // 1..1024
};

struct PlayDesc {
  float volume = 1.0F;   // 0..16 linear gain
  float pan = 0.0F;      // -1 (left) .. +1 (right); ignored for spatial voices
  float pitch = 1.0F;    // playback speed multiplier, 0.01..16
  bool loop = false;
  bool paused = false;   // start paused
  int priority = 0;      // when all voices are busy the lowest priority (then oldest) is stolen
  // Spatial voices are attenuated and panned from the listener.
  bool spatial = false;
  Vec3 position{0, 0, 0};
  float minDistance = 1.0F;   // full volume up to here
  float maxDistance = 50.0F;  // no further attenuation beyond here
  float rolloff = 1.0F;       // 0 = no distance attenuation
};

struct MixerStats {
  std::uint64_t framesRendered = 0;
  std::uint32_t activeVoices = 0;
  std::uint64_t voicesStarted = 0;
  std::uint64_t voicesStolen = 0;   // evicted to make room
  std::uint64_t voicesRejected = 0; // new voice dropped because every voice outranked it
  std::uint64_t clippedSamples = 0; // output samples that hit the +-1 limit
};

class Mixer {
 public:
  static Result<std::unique_ptr<Mixer>> create(const MixerDesc& desc = {});
  ~Mixer();
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  std::uint32_t sampleRate() const noexcept { return desc_.sampleRate; }

  // ---- game thread --------------------------------------------------------------------------------
  // Fails for a null/empty clip or out-of-range / non-finite parameters. The returned id is valid
  // even if the voice was later dropped (see MixerStats::voicesRejected); isPlaying() tells.
  Result<VoiceId> play(std::shared_ptr<const assets::AudioClip> clip, const PlayDesc& desc = {});
  void stop(VoiceId id);
  void stopAll();
  Result<void> setVolume(VoiceId id, float volume);
  Result<void> setPitch(VoiceId id, float pitch);
  Result<void> setPan(VoiceId id, float pan);
  Result<void> setPosition(VoiceId id, Vec3 position);
  Result<void> setPaused(VoiceId id, bool paused);
  void setListener(Vec3 position, Vec3 forward, Vec3 up);
  Result<void> setMasterVolume(float volume);

  // True while the voice is audible/pending. Lock-free; reflects the state as of the last rendered
  // block (a just-issued play() becomes visible after the next render()).
  bool isPlaying(VoiceId id) const noexcept;
  MixerStats stats() const noexcept;

  // ---- audio thread / offline -----------------------------------------------------------------------
  // Writes `frames` interleaved stereo frames. Never blocks, never allocates after construction
  // (apart from the first block after a burst of commands growing internal reserves).
  void render(float* out, std::uint32_t frames) noexcept;

 private:
  explicit Mixer(const MixerDesc& d);
  struct Voice;
  struct Command;

  void applyCommands() noexcept;
  void startVoice(Command& c) noexcept;
  void releaseRetired();

  MixerDesc desc_;

  // Hand-over from the game thread.
  std::mutex mutex_;
  std::vector<Command> pending_;
  std::vector<Command> working_;  // audio-thread copy (reserved)
  std::vector<std::shared_ptr<const assets::AudioClip>> retired_;  // clips released by the audio thread
  std::atomic<std::uint64_t> nextId_{1};
  struct Listener {
    Vec3 position{0, 0, 0};
    Vec3 forward{0, 0, -1};
    Vec3 up{0, 1, 0};
  } pendingListener_;
  bool listenerDirty_ = false;

  // Audio-thread state.
  std::vector<Voice> voices_;
  Listener listener_;
  std::atomic<float> master_{1.0F};
  std::vector<std::atomic<std::uint64_t>> playingIds_;  // per voice slot: id or 0 (published for isPlaying)
  std::atomic<std::uint64_t> framesRendered_{0}, started_{0}, stolen_{0}, rejected_{0}, clipped_{0};
  std::atomic<std::uint32_t> active_{0};
  std::uint64_t startSerial_ = 0;
};

}  // namespace mingol::audio
