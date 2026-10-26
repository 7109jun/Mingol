// SPDX-License-Identifier: GPL-2.0-only
// Self-contained cryptographic primitives used by the .mingol archive: SHA-256, HMAC-SHA256, HKDF-SHA256,
// PBKDF2-HMAC-SHA256 and ChaCha20-Poly1305 (RFC 8439). Implemented from the RFCs/FIPS 180-4 and checked
// against published test vectors and reference output of an independent implementation (tests/package).
//
// These are straightforward, portable implementations: no hardware acceleration, and not audited. They are
// written without secret-dependent branches or table lookups (tag comparison is constant time), but no
// side-channel guarantees are made beyond that.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "mingol/core/error.hpp"

namespace mingol::package::crypto {

using Bytes = std::vector<std::uint8_t>;
using Sha256Digest = std::array<std::uint8_t, 32>;
using Key256 = std::array<std::uint8_t, 32>;
using Nonce96 = std::array<std::uint8_t, 12>;
using Tag128 = std::array<std::uint8_t, 16>;

class Sha256 {
 public:
  Sha256() noexcept;
  void update(std::span<const std::uint8_t> data) noexcept;
  Sha256Digest finish() noexcept;  // the object must not be reused afterwards

 private:
  void block(const std::uint8_t* p) noexcept;
  std::uint32_t h_[8];
  std::uint8_t buf_[64];
  std::uint64_t total_ = 0;
  std::size_t used_ = 0;
};

Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;
Sha256Digest hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) noexcept;
// RFC 5869. `length` <= 255 * 32.
Result<Bytes> hkdfSha256(std::span<const std::uint8_t> ikm, std::span<const std::uint8_t> salt, std::span<const std::uint8_t> info, std::size_t length);
// RFC 8018. iterations >= 1.
Result<Bytes> pbkdf2HmacSha256(std::string_view password, std::span<const std::uint8_t> salt, std::uint32_t iterations, std::size_t length);

// ChaCha20 stream (RFC 8439 2.4) starting at block `counter`. Encrypts or decrypts in place.
void chacha20Xor(const Key256& key, const Nonce96& nonce, std::uint32_t counter, std::span<std::uint8_t> data) noexcept;
Tag128 poly1305(const Key256& oneTimeKey, std::span<const std::uint8_t> data) noexcept;

// AEAD_CHACHA20_POLY1305 (RFC 8439 2.8). `ciphertext` receives plaintext.size() bytes; the tag is returned.
Tag128 aeadSeal(const Key256& key, const Nonce96& nonce, std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> ciphertext);
// Verifies the tag first (constant time); on mismatch writes nothing and fails with AuthFailed.
Result<void> aeadOpen(const Key256& key, const Nonce96& nonce, std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext, const Tag128& tag,
                      std::span<std::uint8_t> plaintext);

bool constantTimeEqual(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept;
void secureZero(void* p, std::size_t n) noexcept;
// Fills `out` from the operating system's random source. Fails if none is available.
Result<void> randomBytes(std::span<std::uint8_t> out);

}  // namespace mingol::package::crypto
