// SPDX-License-Identifier: GPL-2.0-only
// Output device: pulls audio from a Mixer on the platform's audio thread (miniaudio backends:
// WASAPI/DirectSound/WinMM on Windows; PulseAudio/ALSA/JACK on Linux, loaded at run time).
#pragma once

#include <memory>
#include <string>

#include "mingol/audio/mixer.hpp"
#include "mingol/core/error.hpp"

namespace mingol::audio {

enum class DeviceBackend : std::uint8_t {
  Default,  // the platform's real audio output
  Null,     // a silent device that still pulls audio at real-time pace (servers, CI, no sound card)
};

struct DeviceDesc {
  DeviceBackend backend = DeviceBackend::Default;
};

class Device {
 public:
  // The mixer must outlive the device. Fails when no output device can be opened.
  static Result<std::unique_ptr<Device>> create(Mixer& mixer, const DeviceDesc& desc = {});
  ~Device();
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  const std::string& backendName() const noexcept { return backendName_; }
  // Frames the hardware callback has requested so far.
  std::uint64_t framesPulled() const noexcept;

 private:
  Device() = default;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string backendName_;
};

}  // namespace mingol::audio
