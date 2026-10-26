// SPDX-License-Identifier: GPL-2.0-only
// Crypto primitives vs. reference output (see gen_crypto_vectors.py) and published RFC/FIPS vectors.
#include <set>
#include <string>

#include "mingol/package/crypto.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::package::crypto;

namespace {
Bytes pat(std::size_t n, int salt) {
  Bytes b(n);
  for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>((i * 7 + 3 + static_cast<std::size_t>(salt) * 31) & 0xFF);
  return b;
}
std::string hex(std::span<const std::uint8_t> b) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (std::uint8_t v : b) { s += d[v >> 4]; s += d[v & 15]; }
  return s;
}
Bytes unhex(const std::string& s) {
  Bytes out;
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
  return out;
}
template <class A>
auto arr(const Bytes& b) {
  A a{};
  std::copy(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(a.size()), a.begin());
  return a;
}
struct HashVec { std::size_t len; const char* hex; };
struct HmacVec { std::size_t keyLen, dataLen; const char* hex; };
struct HkdfVec { std::size_t ikm, salt, info, len; const char* hex; };
struct Pbkdf2Vec { const char* pw; std::size_t salt; std::uint32_t iters; std::size_t len; const char* hex; };
struct ChaChaVec { std::uint32_t counter; std::size_t len; const char* hex; };
struct PolyVec { std::size_t len; const char* hex; };
struct AeadVec { int index; std::size_t aad, pt; const char* hex; };
#include "crypto_vectors.inc"
}  // namespace

TEST(Crypto, Sha256KnownAnswers) {
  const std::string abc = "abc";
  CHECK_EQ(hex(sha256({reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()})), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK_EQ(hex(sha256({reinterpret_cast<const std::uint8_t*>(two.data()), two.size()})), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  for (const auto& v : kSha) CHECK_EQ(hex(sha256(pat(v.len, 1))), v.hex);
}

TEST(Crypto, Sha256IncrementalMatchesOneShot) {
  const Bytes data = pat(1000, 1);
  const auto whole = sha256(data);
  for (std::size_t split : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{64}, std::size_t{65}, std::size_t{500}, std::size_t{999}, std::size_t{1000}}) {
    Sha256 s;
    s.update({data.data(), split});
    s.update({data.data() + split, data.size() - split});
    CHECK(s.finish() == whole);
  }
  Sha256 byByte;
  for (std::uint8_t b : data) byByte.update({&b, 1});
  CHECK(byByte.finish() == whole);
}

TEST(Crypto, HmacMatchesReference) {
  for (const auto& v : kHmac) CHECK_EQ(hex(hmacSha256(pat(v.keyLen, 2), pat(v.dataLen, 3))), v.hex);
}

TEST(Crypto, HkdfMatchesReferenceAndRfc5869) {
  const Bytes ikm(22, 0x0b);
  Bytes salt(13), info(10);
  for (std::size_t i = 0; i < salt.size(); ++i) salt[i] = static_cast<std::uint8_t>(i);
  for (std::size_t i = 0; i < info.size(); ++i) info[i] = static_cast<std::uint8_t>(0xf0 + i);
  auto okm = hkdfSha256(ikm, salt, info, 42);
  REQUIRE(okm.ok());
  CHECK_EQ(hex(okm.value()), "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
  for (const auto& v : kHkdf) {
    auto r = hkdfSha256(pat(v.ikm, 4), pat(v.salt, 5), pat(v.info, 6), v.len);
    REQUIRE(r.ok());
    CHECK_EQ(hex(r.value()), v.hex);
  }
  CHECK(!hkdfSha256(ikm, salt, info, 255 * 32 + 1).ok());
}

TEST(Crypto, Pbkdf2MatchesReference) {
  for (const auto& v : kPbkdf2) {
    auto r = pbkdf2HmacSha256(v.pw, pat(v.salt, 7), v.iters, v.len);
    REQUIRE(r.ok());
    CHECK_EQ(hex(r.value()), v.hex);
  }
  CHECK(!pbkdf2HmacSha256("x", pat(4, 7), 0, 32).ok());
  CHECK(!pbkdf2HmacSha256("x", pat(4, 7), 1, 0).ok());
}

TEST(Crypto, ChaCha20KeystreamMatchesReference) {
  const auto key = arr<Key256>(pat(32, 8));
  const auto nonce = arr<Nonce96>(pat(12, 9));
  for (const auto& v : kChaCha) {
    Bytes z(v.len, 0);
    chacha20Xor(key, nonce, v.counter, z);
    CHECK_EQ(hex(z), v.hex);
  }
  // encrypt then decrypt is the identity, also for ragged lengths
  for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{63}, std::size_t{65}, std::size_t{300}}) {
    Bytes d = pat(n, 12);
    chacha20Xor(key, nonce, 1, d);
    if (n > 0) CHECK(d != pat(n, 12));
    chacha20Xor(key, nonce, 1, d);
    CHECK(d == pat(n, 12));
  }
}

TEST(Crypto, Poly1305MatchesReference) {
  const auto key = arr<Key256>(pat(32, 10));
  for (const auto& v : kPoly) CHECK_EQ(hex(poly1305(key, pat(v.len, 11))), v.hex);
  // RFC 8439 2.5.2
  const auto rfcKey = arr<Key256>(unhex("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b"));
  const std::string msg = "Cryptographic Forum Research Group";
  CHECK_EQ(hex(poly1305(rfcKey, {reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()})), "a8061dc1305136c6c22b8baf0c0127a9");
}

TEST(Crypto, AeadMatchesReferenceAndRoundTrips) {
  for (const auto& v : kAead) {
    const auto key = arr<Key256>(pat(32, 20 + v.index));
    const auto nonce = arr<Nonce96>(pat(12, 40 + v.index));
    const Bytes aad = pat(v.aad, 60 + v.index), pt = pat(v.pt, 80 + v.index);
    Bytes ct(pt.size());
    const Tag128 tag = aeadSeal(key, nonce, aad, pt, ct);
    Bytes both = ct;
    both.insert(both.end(), tag.begin(), tag.end());
    CHECK_EQ(hex(both), v.hex);
    Bytes back(ct.size());
    CHECK(aeadOpen(key, nonce, aad, ct, tag, back).ok());
    CHECK(back == pt);
  }
}

TEST(Crypto, AeadRejectsEveryKindOfTampering) {
  const auto key = arr<Key256>(pat(32, 1));
  const auto nonce = arr<Nonce96>(pat(12, 2));
  const Bytes aad = pat(20, 3), pt = pat(100, 4);
  Bytes ct(pt.size());
  const Tag128 tag = aeadSeal(key, nonce, aad, pt, ct);
  Bytes out(pt.size(), 0xAA);
  auto expectFail = [&](const Key256& k, const Nonce96& n, const Bytes& a, const Bytes& c, const Tag128& t, const char* what) {
    Bytes o(c.size(), 0xAA);
    auto r = aeadOpen(k, n, a, c, t, o);
    CHECK_MSG(!r.ok(), what);
    if (!r.ok()) CHECK(r.error().code == ErrorCode::AuthFailed);
    CHECK_MSG(o == Bytes(c.size(), 0xAA), "plaintext must not be written when authentication fails");
  };
  for (std::size_t i = 0; i < ct.size(); ++i) { Bytes c = ct; c[i] ^= 1; expectFail(key, nonce, aad, c, tag, "ciphertext bit flip"); }
  for (std::size_t i = 0; i < aad.size(); ++i) { Bytes a = aad; a[i] ^= 0x80; expectFail(key, nonce, a, ct, tag, "aad flip"); }
  for (std::size_t i = 0; i < tag.size(); ++i) { Tag128 t = tag; t[i] ^= 1; expectFail(key, nonce, aad, ct, t, "tag flip"); }
  { Key256 k = key; k[5] ^= 1; expectFail(k, nonce, aad, ct, tag, "wrong key"); }
  { Nonce96 n = nonce; n[11] ^= 1; expectFail(key, n, aad, ct, tag, "wrong nonce"); }
  { Bytes c = ct; c.pop_back(); expectFail(key, nonce, aad, c, tag, "truncated"); }
  { Bytes a = aad; a.push_back(0); expectFail(key, nonce, a, ct, tag, "extended aad"); }
  CHECK(aeadOpen(key, nonce, aad, ct, tag, out).ok());
  CHECK(out == pt);
}

TEST(Crypto, HelpersBehave) {
  const Bytes a = pat(16, 1), b = pat(16, 1), c = pat(16, 2);
  CHECK(constantTimeEqual(a, b));
  CHECK(!constantTimeEqual(a, c));
  CHECK(!constantTimeEqual(a, Bytes(15, 0)));
  Bytes z = pat(10, 1);
  secureZero(z.data(), z.size());
  CHECK(z == Bytes(10, 0));
  std::set<std::string> seen;
  for (int i = 0; i < 20; ++i) {
    Bytes r(16);
    REQUIRE(randomBytes(r).ok());
    seen.insert(hex(r));
  }
  CHECK_EQ(seen.size(), std::size_t{20});
}
