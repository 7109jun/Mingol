// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/package/crypto.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace mingol::package::crypto {

namespace {

constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept { return (x >> n) | (x << (32 - n)); }
constexpr std::uint32_t rotl(std::uint32_t x, int n) noexcept { return (x << n) | (x >> (32 - n)); }

std::uint32_t load32be(const std::uint8_t* p) noexcept { return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3]; }
std::uint32_t load32le(const std::uint8_t* p) noexcept { return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24); }
void store32le(std::uint8_t* p, std::uint32_t v) noexcept { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24); }
void store32be(std::uint8_t* p, std::uint32_t v) noexcept { p[3] = static_cast<std::uint8_t>(v); p[2] = static_cast<std::uint8_t>(v >> 8); p[1] = static_cast<std::uint8_t>(v >> 16); p[0] = static_cast<std::uint8_t>(v >> 24); }
void store64le(std::uint8_t* p, std::uint64_t v) noexcept { for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i)); }

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
    0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
    0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
    0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

// ---- SHA-256 ---------------------------------------------------------------------------------------------

Sha256::Sha256() noexcept
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, buf_{} {}

void Sha256::block(const std::uint8_t* p) noexcept {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = load32be(p + 4 * i);
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + S1 + ch + kK[i] + w[i];
    const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
  total_ += data.size();
  const std::uint8_t* p = data.data();
  std::size_t n = data.size();
  if (used_ > 0) {
    const std::size_t take = std::min(n, std::size_t{64} - used_);
    std::memcpy(buf_ + used_, p, take);
    used_ += take; p += take; n -= take;
    if (used_ == 64) { block(buf_); used_ = 0; }
  }
  while (n >= 64) { block(p); p += 64; n -= 64; }
  if (n > 0) { std::memcpy(buf_, p, n); used_ = n; }
}

Sha256Digest Sha256::finish() noexcept {
  const std::uint64_t bits = total_ * 8;
  std::uint8_t pad[72] = {0x80};
  const std::size_t padLen = (used_ < 56) ? (56 - used_) : (120 - used_);
  std::uint8_t lenBytes[8];
  for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
  update({pad, padLen});
  update({lenBytes, 8});
  Sha256Digest out;
  for (int i = 0; i < 8; ++i) store32be(out.data() + 4 * i, h_[i]);
  return out;
}

Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept {
  Sha256 s;
  s.update(data);
  return s.finish();
}

Sha256Digest hmacSha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) noexcept {
  std::uint8_t k[64] = {};
  if (key.size() > 64) {
    const auto d = sha256(key);
    std::memcpy(k, d.data(), 32);
  } else if (!key.empty()) {
    std::memcpy(k, key.data(), key.size());
  }
  std::uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
  Sha256 inner;
  inner.update(ipad);
  inner.update(data);
  const auto ih = inner.finish();
  Sha256 outer;
  outer.update(opad);
  outer.update(ih);
  const auto out = outer.finish();
  secureZero(k, sizeof k); secureZero(ipad, sizeof ipad); secureZero(opad, sizeof opad);
  return out;
}

Result<Bytes> hkdfSha256(std::span<const std::uint8_t> ikm, std::span<const std::uint8_t> salt, std::span<const std::uint8_t> info, std::size_t length) {
  if (length > 255 * 32) return makeError(ErrorCode::InvalidArgument, "HKDF output length {} exceeds 255 * 32", length);
  std::uint8_t zeroSalt[32] = {};
  const auto prk = hmacSha256(salt.empty() ? std::span<const std::uint8_t>(zeroSalt, 32) : salt, ikm);
  Bytes out;
  out.reserve(length + 32);
  Sha256Digest t{};
  std::size_t tLen = 0;
  for (std::uint8_t counter = 1; out.size() < length; ++counter) {
    Bytes msg(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(tLen));
    msg.insert(msg.end(), info.begin(), info.end());
    msg.push_back(counter);
    t = hmacSha256(prk, msg);
    tLen = 32;
    out.insert(out.end(), t.begin(), t.end());
  }
  out.resize(length);
  return out;
}

Result<Bytes> pbkdf2HmacSha256(std::string_view password, std::span<const std::uint8_t> salt, std::uint32_t iterations, std::size_t length) {
  if (iterations == 0) return makeError(ErrorCode::InvalidArgument, "PBKDF2 needs at least one iteration");
  if (length == 0 || length > (1u << 20)) return makeError(ErrorCode::InvalidArgument, "PBKDF2 output length out of range");
  const std::span<const std::uint8_t> pw(reinterpret_cast<const std::uint8_t*>(password.data()), password.size());
  Bytes out;
  out.reserve(length + 32);
  for (std::uint32_t block = 1; out.size() < length; ++block) {
    Bytes msg(salt.begin(), salt.end());
    std::uint8_t be[4];
    store32be(be, block);
    msg.insert(msg.end(), be, be + 4);
    Sha256Digest u = hmacSha256(pw, msg);
    Sha256Digest acc = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
      u = hmacSha256(pw, u);
      for (int k = 0; k < 32; ++k) acc[static_cast<std::size_t>(k)] ^= u[static_cast<std::size_t>(k)];
    }
    out.insert(out.end(), acc.begin(), acc.end());
  }
  out.resize(length);
  return out;
}

// ---- ChaCha20 / Poly1305 ---------------------------------------------------------------------------------

namespace {

void quarter(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) noexcept {
  a += b; d ^= a; d = rotl(d, 16);
  c += d; b ^= c; b = rotl(b, 12);
  a += b; d ^= a; d = rotl(d, 8);
  c += d; b ^= c; b = rotl(b, 7);
}

void chachaBlock(const Key256& key, const Nonce96& nonce, std::uint32_t counter, std::uint8_t out[64]) noexcept {
  std::uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
  for (int i = 0; i < 8; ++i) s[4 + i] = load32le(key.data() + 4 * i);
  s[12] = counter;
  for (int i = 0; i < 3; ++i) s[13 + i] = load32le(nonce.data() + 4 * i);
  std::uint32_t x[16];
  std::memcpy(x, s, sizeof s);
  for (int r = 0; r < 10; ++r) {
    quarter(x[0], x[4], x[8], x[12]); quarter(x[1], x[5], x[9], x[13]); quarter(x[2], x[6], x[10], x[14]); quarter(x[3], x[7], x[11], x[15]);
    quarter(x[0], x[5], x[10], x[15]); quarter(x[1], x[6], x[11], x[12]); quarter(x[2], x[7], x[8], x[13]); quarter(x[3], x[4], x[9], x[14]);
  }
  for (int i = 0; i < 16; ++i) store32le(out + 4 * i, x[i] + s[i]);
  secureZero(x, sizeof x);
  secureZero(s, sizeof s);
}

}  // namespace

void chacha20Xor(const Key256& key, const Nonce96& nonce, std::uint32_t counter, std::span<std::uint8_t> data) noexcept {
  std::uint8_t ks[64];
  std::size_t off = 0;
  while (off < data.size()) {
    chachaBlock(key, nonce, counter++, ks);
    const std::size_t n = std::min<std::size_t>(64, data.size() - off);
    for (std::size_t i = 0; i < n; ++i) data[off + i] ^= ks[i];
    off += n;
  }
  secureZero(ks, sizeof ks);
}

// Poly1305 with 5 x 26-bit limbs (the classic "donna-32" layout).
Tag128 poly1305(const Key256& key, std::span<const std::uint8_t> data) noexcept {
  const std::uint8_t* k = key.data();
  const std::uint32_t r0 = load32le(k + 0) & 0x3ffffff;
  const std::uint32_t r1 = (load32le(k + 3) >> 2) & 0x3ffff03;
  const std::uint32_t r2 = (load32le(k + 6) >> 4) & 0x3ffc0ff;
  const std::uint32_t r3 = (load32le(k + 9) >> 6) & 0x3f03fff;
  const std::uint32_t r4 = (load32le(k + 12) >> 8) & 0x00fffff;
  const std::uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  std::uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

  const std::uint8_t* p = data.data();
  std::size_t n = data.size();
  while (n > 0) {
    std::uint8_t blk[16] = {};
    std::uint32_t hibit = 1u << 24;
    if (n >= 16) {
      std::memcpy(blk, p, 16);
      p += 16; n -= 16;
    } else {
      std::memcpy(blk, p, n);
      blk[n] = 1;
      hibit = 0;
      n = 0;
    }
    h0 += load32le(blk + 0) & 0x3ffffff;
    h1 += (load32le(blk + 3) >> 2) & 0x3ffffff;
    h2 += (load32le(blk + 6) >> 4) & 0x3ffffff;
    h3 += (load32le(blk + 9) >> 6) & 0x3ffffff;
    h4 += (load32le(blk + 12) >> 8) | hibit;
    const std::uint64_t d0 = std::uint64_t{h0} * r0 + std::uint64_t{h1} * s4 + std::uint64_t{h2} * s3 + std::uint64_t{h3} * s2 + std::uint64_t{h4} * s1;
    std::uint64_t d1 = std::uint64_t{h0} * r1 + std::uint64_t{h1} * r0 + std::uint64_t{h2} * s4 + std::uint64_t{h3} * s3 + std::uint64_t{h4} * s2;
    std::uint64_t d2 = std::uint64_t{h0} * r2 + std::uint64_t{h1} * r1 + std::uint64_t{h2} * r0 + std::uint64_t{h3} * s4 + std::uint64_t{h4} * s3;
    std::uint64_t d3 = std::uint64_t{h0} * r3 + std::uint64_t{h1} * r2 + std::uint64_t{h2} * r1 + std::uint64_t{h3} * r0 + std::uint64_t{h4} * s4;
    std::uint64_t d4 = std::uint64_t{h0} * r4 + std::uint64_t{h1} * r3 + std::uint64_t{h2} * r2 + std::uint64_t{h3} * r1 + std::uint64_t{h4} * r0;
    std::uint64_t c = d0 >> 26; h0 = static_cast<std::uint32_t>(d0) & 0x3ffffff;
    d1 += c; c = d1 >> 26; h1 = static_cast<std::uint32_t>(d1) & 0x3ffffff;
    d2 += c; c = d2 >> 26; h2 = static_cast<std::uint32_t>(d2) & 0x3ffffff;
    d3 += c; c = d3 >> 26; h3 = static_cast<std::uint32_t>(d3) & 0x3ffffff;
    d4 += c; c = d4 >> 26; h4 = static_cast<std::uint32_t>(d4) & 0x3ffffff;
    h0 += static_cast<std::uint32_t>(c) * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += static_cast<std::uint32_t>(c);
  }
  // full carry, then conditional subtraction of p = 2^130 - 5
  std::uint32_t c = h1 >> 26; h1 &= 0x3ffffff;
  h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
  h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
  h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
  h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
  h1 += c;
  std::uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  std::uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  std::uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  std::uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  std::uint32_t g4 = h4 + c - (1u << 26);
  const std::uint32_t mask = (g4 >> 31) - 1;   // all ones when h >= p
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  const std::uint32_t nmask = ~mask;
  h0 = (h0 & nmask) | g0; h1 = (h1 & nmask) | g1; h2 = (h2 & nmask) | g2; h3 = (h3 & nmask) | g3; h4 = (h4 & nmask) | g4;
  // h = h % 2^128, then add s
  const std::uint32_t t0 = h0 | (h1 << 26);
  const std::uint32_t t1 = (h1 >> 6) | (h2 << 20);
  const std::uint32_t t2 = (h2 >> 12) | (h3 << 14);
  const std::uint32_t t3 = (h3 >> 18) | (h4 << 8);
  std::uint64_t f = std::uint64_t{t0} + load32le(k + 16);
  Tag128 tag;
  store32le(tag.data() + 0, static_cast<std::uint32_t>(f));
  f = std::uint64_t{t1} + load32le(k + 20) + (f >> 32);
  store32le(tag.data() + 4, static_cast<std::uint32_t>(f));
  f = std::uint64_t{t2} + load32le(k + 24) + (f >> 32);
  store32le(tag.data() + 8, static_cast<std::uint32_t>(f));
  f = std::uint64_t{t3} + load32le(k + 28) + (f >> 32);
  store32le(tag.data() + 12, static_cast<std::uint32_t>(f));
  return tag;
}

namespace {

Tag128 aeadTag(const Key256& key, const Nonce96& nonce, std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ct) {
  std::uint8_t block0[64];
  chachaBlock(key, nonce, 0, block0);
  Key256 otk;
  std::memcpy(otk.data(), block0, 32);
  secureZero(block0, sizeof block0);
  const auto pad16 = [](std::size_t n) { return (16 - n % 16) % 16; };
  Bytes mac;
  mac.reserve(aad.size() + ct.size() + 48);
  mac.insert(mac.end(), aad.begin(), aad.end());
  mac.insert(mac.end(), pad16(aad.size()), 0);
  mac.insert(mac.end(), ct.begin(), ct.end());
  mac.insert(mac.end(), pad16(ct.size()), 0);
  std::uint8_t lens[16];
  store64le(lens, aad.size());
  store64le(lens + 8, ct.size());
  mac.insert(mac.end(), lens, lens + 16);
  const Tag128 tag = poly1305(otk, mac);
  secureZero(otk.data(), otk.size());
  return tag;
}

}  // namespace

Tag128 aeadSeal(const Key256& key, const Nonce96& nonce, std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext, std::span<std::uint8_t> ciphertext) {
  MINGOL_VERIFY(ciphertext.size() == plaintext.size());
  if (!plaintext.empty()) std::memmove(ciphertext.data(), plaintext.data(), plaintext.size());
  chacha20Xor(key, nonce, 1, ciphertext);
  return aeadTag(key, nonce, aad, ciphertext);
}

Result<void> aeadOpen(const Key256& key, const Nonce96& nonce, std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext, const Tag128& tag,
                      std::span<std::uint8_t> plaintext) {
  MINGOL_VERIFY(plaintext.size() == ciphertext.size());
  const Tag128 expect = aeadTag(key, nonce, aad, ciphertext);
  if (!constantTimeEqual(expect, tag)) return makeError(ErrorCode::AuthFailed, "authentication failed (wrong key, or the data was modified)");
  if (!ciphertext.empty()) std::memmove(plaintext.data(), ciphertext.data(), ciphertext.size());
  chacha20Xor(key, nonce, 1, plaintext);
  return {};
}

bool constantTimeEqual(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept {
  if (a.size() != b.size()) return false;
  std::uint8_t d = 0;
  for (std::size_t i = 0; i < a.size(); ++i) d |= static_cast<std::uint8_t>(a[i] ^ b[i]);
  return d == 0;
}

void secureZero(void* p, std::size_t n) noexcept {
  volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
  for (std::size_t i = 0; i < n; ++i) v[i] = 0;
}

Result<void> randomBytes(std::span<std::uint8_t> out) {
  try {
    std::random_device rd;
    for (std::size_t i = 0; i < out.size();) {
      const std::uint32_t v = rd();
      for (int k = 0; k < 4 && i < out.size(); ++k, ++i) out[i] = static_cast<std::uint8_t>(v >> (8 * k));
    }
  } catch (const std::exception& e) {
    return makeError(ErrorCode::Unsupported, "no operating-system random source is available: {}", e.what());
  }
  return {};
}

}  // namespace mingol::package::crypto
