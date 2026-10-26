// SPDX-License-Identifier: GPL-2.0-only
#include <algorithm>
#include <cstring>

#include "mingol/rhi/device.hpp"

namespace mingol::rhi {

std::uint32_t bytesPerPixel(Format format) noexcept {
  switch (format) {
    case Format::Undefined: return 0;
    case Format::R8Unorm: return 1;
    case Format::RG8Unorm: return 2;
    case Format::RGBA8Unorm:
    case Format::RGBA8Srgb:
    case Format::BGRA8Unorm:
    case Format::BGRA8Srgb: return 4;
    case Format::R16Float: return 2;
    case Format::RG16Float: return 4;
    case Format::RGBA16Float: return 8;
    case Format::R32Float: return 4;
    case Format::RG32Float: return 8;
    case Format::RGB32Float: return 12;
    case Format::RGBA32Float: return 16;
    case Format::D16Unorm: return 2;
    case Format::D32Float: return 4;
  }
  return 0;
}

bool isDepthFormat(Format format) noexcept { return format == Format::D16Unorm || format == Format::D32Float; }

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height) noexcept {
  std::uint32_t size = std::max(width, height);
  std::uint32_t levels = 1;
  while (size > 1) {
    size >>= 1;
    ++levels;
  }
  return levels;
}

namespace {

std::uint32_t mipExtent(std::uint32_t base, std::uint32_t mip) { return std::max(1u, base >> mip); }

// Records `fn` into a fresh command list, submits it and waits for completion.
template <class F>
Result<void> runOneShot(Device& device, F&& fn) {
  MINGOL_TRY_ASSIGN(auto list, device.createCommandList());
  MINGOL_TRY(list->begin());
  fn(*list);
  MINGOL_TRY(list->end());
  MINGOL_TRY_ASSIGN(std::uint64_t serial, device.submit(*list));
  return device.waitForSerial(serial);
}

}  // namespace

Result<std::shared_ptr<Buffer>> Device::createBufferWithData(const BufferDesc& descIn,
                                                             std::span<const std::uint8_t> data) {
  BufferDesc desc = descIn;
  if (desc.size == 0) desc.size = data.size();
  if (data.size() > desc.size) {
    return makeError(ErrorCode::InvalidArgument, "initial data ({} bytes) larger than buffer ({} bytes)", data.size(),
                     desc.size);
  }

  if (desc.memory != MemoryType::GpuOnly) {
    MINGOL_TRY_ASSIGN(auto buffer, createBuffer(desc));
    if (!data.empty()) {
      std::memcpy(buffer->mapped(), data.data(), data.size());
      buffer->flush(0, data.size());
    }
    return buffer;
  }

  desc.usage |= BufferUsage::TransferDst;
  MINGOL_TRY_ASSIGN(auto buffer, createBuffer(desc));
  if (data.empty()) return buffer;

  BufferDesc stagingDesc;
  stagingDesc.size = data.size();
  stagingDesc.usage = BufferUsage::TransferSrc;
  stagingDesc.memory = MemoryType::CpuToGpu;
  stagingDesc.debugName = "staging";
  MINGOL_TRY_ASSIGN(auto staging, createBuffer(stagingDesc));
  std::memcpy(staging->mapped(), data.data(), data.size());
  staging->flush();

  MINGOL_TRY(runOneShot(*this, [&](CommandList& cmd) { cmd.copyBuffer(*staging, *buffer, data.size()); }));
  return buffer;
}

Result<std::shared_ptr<Texture>> Device::createTextureWithData(TextureDesc desc, std::span<const std::uint8_t> pixels,
                                                               bool generateMips) {
  const std::uint32_t bpp = bytesPerPixel(desc.format);
  if (bpp == 0 || isDepthFormat(desc.format)) {
    return makeError(ErrorCode::InvalidArgument, "createTextureWithData needs a colour format");
  }
  const std::uint64_t expected = static_cast<std::uint64_t>(desc.width) * desc.height * bpp;
  if (pixels.size() != expected) {
    return makeError(ErrorCode::InvalidArgument, "pixel data is {} bytes, expected {} for {}x{} texture", pixels.size(),
                     expected, desc.width, desc.height);
  }

  desc.usage |= TextureUsage::Sampled | TextureUsage::TransferDst;
  if (generateMips) {
    desc.mipLevels = fullMipCount(desc.width, desc.height);
    desc.usage |= TextureUsage::TransferSrc;
  }
  MINGOL_TRY_ASSIGN(auto texture, createTexture(desc));

  BufferDesc stagingDesc;
  stagingDesc.size = pixels.size();
  stagingDesc.usage = BufferUsage::TransferSrc;
  stagingDesc.memory = MemoryType::CpuToGpu;
  stagingDesc.debugName = "texture staging";
  MINGOL_TRY_ASSIGN(auto staging, createBuffer(stagingDesc));
  std::memcpy(staging->mapped(), pixels.data(), pixels.size());
  staging->flush();

  MINGOL_TRY(runOneShot(*this, [&](CommandList& cmd) {
    cmd.copyBufferToTexture(*staging, *texture, 0, 0);
    if (generateMips && desc.mipLevels > 1) cmd.generateMipmaps(*texture);
    cmd.transition(*texture, TextureState::ShaderRead);
  }));
  return texture;
}

Result<std::vector<std::uint8_t>> Device::readbackTexture(Texture& texture, std::uint32_t mip, std::uint32_t layer) {
  const TextureDesc& d = texture.desc();
  if (!hasFlag(d.usage, TextureUsage::TransferSrc)) {
    return makeError(ErrorCode::InvalidArgument, "texture '{}' lacks TransferSrc usage", d.debugName);
  }
  if (mip >= d.mipLevels || layer >= d.arrayLayers) {
    return makeError(ErrorCode::OutOfRange, "mip {} / layer {} out of range", mip, layer);
  }
  const std::uint64_t bytes =
      static_cast<std::uint64_t>(mipExtent(d.width, mip)) * mipExtent(d.height, mip) * bytesPerPixel(d.format);

  BufferDesc readbackDesc;
  readbackDesc.size = bytes;
  readbackDesc.usage = BufferUsage::TransferDst;
  readbackDesc.memory = MemoryType::GpuToCpu;
  readbackDesc.debugName = "readback";
  MINGOL_TRY_ASSIGN(auto readback, createBuffer(readbackDesc));

  MINGOL_TRY(runOneShot(*this, [&](CommandList& cmd) { cmd.copyTextureToBuffer(texture, *readback, mip, layer); }));
  readback->invalidate();

  std::vector<std::uint8_t> out(static_cast<std::size_t>(bytes));
  std::memcpy(out.data(), readback->mapped(), out.size());
  return out;
}

}  // namespace mingol::rhi
