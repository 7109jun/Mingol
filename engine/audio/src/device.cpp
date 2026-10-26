// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/audio/device.hpp"

#include <atomic>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"

#include "mingol/core/log.hpp"

namespace mingol::audio {

struct Device::Impl {
  Mixer* mixer = nullptr;
  ma_context context{};
  ma_device device{};
  bool contextReady = false;
  bool deviceReady = false;
  std::atomic<std::uint64_t> pulled{0};

  static void callback(ma_device* d, void* output, const void*, ma_uint32 frames) {
    auto* self = static_cast<Impl*>(d->pUserData);
    self->mixer->render(static_cast<float*>(output), frames);
    self->pulled.fetch_add(frames, std::memory_order_relaxed);
  }

  ~Impl() {
    if (deviceReady) ma_device_uninit(&device);  // stops and joins the audio thread
    if (contextReady) ma_context_uninit(&context);
  }
};

Result<std::unique_ptr<Device>> Device::create(Mixer& mixer, const DeviceDesc& desc) {
  std::unique_ptr<Device> dev(new Device());
  dev->impl_ = std::make_unique<Impl>();
  Impl& im = *dev->impl_;
  im.mixer = &mixer;

  ma_backend nullBackend[] = {ma_backend_null};
  ma_result r = desc.backend == DeviceBackend::Null ? ma_context_init(nullBackend, 1, nullptr, &im.context)
                                                    : ma_context_init(nullptr, 0, nullptr, &im.context);
  if (r != MA_SUCCESS) return makeError(ErrorCode::Unsupported, "no audio backend available: {}", ma_result_description(r));
  im.contextReady = true;

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = mixer.sampleRate();
  cfg.dataCallback = &Impl::callback;
  cfg.pUserData = &im;
  r = ma_device_init(&im.context, &cfg, &im.device);
  if (r != MA_SUCCESS) return makeError(ErrorCode::Unsupported, "cannot open the audio output device: {}", ma_result_description(r));
  im.deviceReady = true;
  dev->backendName_ = ma_get_backend_name(im.context.backend);

  r = ma_device_start(&im.device);
  if (r != MA_SUCCESS) return makeError(ErrorCode::Io, "cannot start the audio device: {}", ma_result_description(r));
  MINGOL_LOG_INFO("audio", "output device opened (backend {}, {} Hz)", dev->backendName_, im.device.sampleRate);
  return dev;
}

Device::~Device() = default;

std::uint64_t Device::framesPulled() const noexcept { return impl_->pulled.load(std::memory_order_relaxed); }

}  // namespace mingol::audio
