// SPDX-License-Identifier: GPL-2.0-only
#include <cmath>

#include "asset_test_util.hpp"
#include "mingol/assets/audio.hpp"

using namespace mingol;
using namespace mingol::assets;
using namespace mingol::test;

namespace {

void put16(std::vector<std::uint8_t>& v, std::uint32_t x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); }
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
void putTag(std::vector<std::uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

struct WavSpec {
  std::uint16_t tag = 1;
  std::uint16_t channels = 1;
  std::uint32_t rate = 8000;
  std::uint16_t bits = 16;
  bool extensible = false;
  std::vector<std::uint8_t> payload;
  std::uint32_t dataSizeOverride = 0xFFFFFFFE;  // sentinel: use the real size
  bool listChunkBeforeData = false;
  int blockAlignDelta = 0;
};

std::vector<std::uint8_t> makeWav(const WavSpec& s) {
  std::vector<std::uint8_t> v;
  putTag(v, "RIFF");
  put32(v, 0);  // patched below
  putTag(v, "WAVE");
  putTag(v, "fmt ");
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(s.channels * s.bits / 8 + s.blockAlignDelta);
  put32(v, s.extensible ? 40 : 16);
  put16(v, s.extensible ? 0xFFFE : s.tag);
  put16(v, s.channels);
  put32(v, s.rate);
  put32(v, s.rate * blockAlign);
  put16(v, blockAlign);
  put16(v, s.bits);
  if (s.extensible) {
    put16(v, 22);
    put16(v, s.bits);
    put32(v, 3);
    put16(v, s.tag);  // sub-format GUID starts with the real format tag
    const std::uint8_t guidTail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
    v.insert(v.end(), guidTail, guidTail + 14);
  }
  if (s.listChunkBeforeData) {
    putTag(v, "LIST");
    put32(v, 3);  // odd size: must be followed by a pad byte
    v.push_back('a'); v.push_back('b'); v.push_back('c'); v.push_back(0);
  }
  putTag(v, "data");
  put32(v, s.dataSizeOverride == 0xFFFFFFFE ? static_cast<std::uint32_t>(s.payload.size()) : s.dataSizeOverride);
  v.insert(v.end(), s.payload.begin(), s.payload.end());
  const std::uint32_t riff = static_cast<std::uint32_t>(v.size() - 8);
  v[4] = riff & 255; v[5] = (riff >> 8) & 255; v[6] = (riff >> 16) & 255; v[7] = (riff >> 24) & 255;
  return v;
}

std::vector<std::uint8_t> pcm16(const std::vector<int>& s) {
  std::vector<std::uint8_t> p;
  for (int x : s) put16(p, static_cast<std::uint32_t>(x) & 0xFFFF);
  return p;
}

bool near(float a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Dominant frequency by counting rising zero crossings.
double frequencyOf(const AudioClip& c, std::uint32_t channel) {
  int crossings = 0;
  float prev = c.samples[channel];
  for (std::uint64_t f = 1; f < c.frames(); ++f) {
    const float cur = c.samples[f * c.channels + channel];
    if (prev < 0 && cur >= 0) ++crossings;
    prev = cur;
  }
  return crossings / c.durationSeconds();
}
double rmsOf(const AudioClip& c, std::uint32_t channel) {
  double sum = 0;
  for (std::uint64_t f = 0; f < c.frames(); ++f) {
    const double v = c.samples[f * c.channels + channel];
    sum += v * v;
  }
  return std::sqrt(sum / static_cast<double>(c.frames()));
}

}  // namespace

TEST(Wav, Pcm16IsExactAndInterleaved) {
  WavSpec s;
  s.channels = 2;
  s.payload = pcm16({0, 32767, -32768, 16384, 1, -1});  // 3 frames, L/R
  auto r = decodeWav(makeWav(s));
  REQUIRE(r.ok());
  const AudioClip& c = r.value();
  CHECK_EQ(c.channels, 2u);
  CHECK_EQ(c.sampleRate, 8000u);
  CHECK_EQ(c.frames(), std::uint64_t{3});
  CHECK(near(c.samples[0], 0.0));
  CHECK(near(c.samples[1], 32767 / 32768.0));
  CHECK(near(c.samples[2], -1.0));
  CHECK(near(c.samples[3], 0.5));
  CHECK(near(c.samples[4], 1 / 32768.0));
  CHECK(near(c.samples[5], -1 / 32768.0));
  CHECK_NEAR(c.durationSeconds(), 3.0 / 8000, 1e-9);
}

TEST(Wav, OtherBitDepthsAndFloat) {
  {  // 8-bit is unsigned with 128 = silence
    WavSpec s; s.bits = 8; s.payload = {0, 128, 255};
    auto r = decodeWav(makeWav(s));
    REQUIRE(r.ok());
    CHECK(near(r.value().samples[0], -1.0));
    CHECK(near(r.value().samples[1], 0.0));
    CHECK(near(r.value().samples[2], 127 / 128.0));
  }
  {  // 24-bit signed little endian
    WavSpec s; s.bits = 24;
    s.payload = {0x00, 0x00, 0x40,   // 0x400000 = +0.5
                 0x00, 0x00, 0x80,   // 0x800000 = -1.0
                 0xFF, 0xFF, 0x7F};  // max
    auto r = decodeWav(makeWav(s));
    REQUIRE(r.ok());
    CHECK(near(r.value().samples[0], 0.5));
    CHECK(near(r.value().samples[1], -1.0));
    CHECK(near(r.value().samples[2], 8388607 / 8388608.0, 1e-7));
  }
  {  // 32-bit signed
    WavSpec s; s.bits = 32;
    put32(s.payload, 0x40000000u);
    put32(s.payload, 0x80000000u);
    auto r = decodeWav(makeWav(s));
    REQUIRE(r.ok());
    CHECK(near(r.value().samples[0], 0.5));
    CHECK(near(r.value().samples[1], -1.0));
  }
  {  // float32, including NaN/Inf which must be neutralised
    WavSpec s; s.tag = 3; s.bits = 32;
    const float vals[4] = {0.25F, -0.75F, std::nanf(""), INFINITY};
    for (float f : vals) { std::uint32_t u; std::memcpy(&u, &f, 4); put32(s.payload, u); }
    auto r = decodeWav(makeWav(s));
    REQUIRE(r.ok());
    CHECK(near(r.value().samples[0], 0.25));
    CHECK(near(r.value().samples[1], -0.75));
    CHECK(near(r.value().samples[2], 0.0));
    CHECK(near(r.value().samples[3], 0.0));
  }
  {  // float64
    WavSpec s; s.tag = 3; s.bits = 64;
    const double d = -0.125;
    std::uint64_t u; std::memcpy(&u, &d, 8);
    put32(s.payload, static_cast<std::uint32_t>(u));
    put32(s.payload, static_cast<std::uint32_t>(u >> 32));
    auto r = decodeWav(makeWav(s));
    REQUIRE(r.ok());
    CHECK(near(r.value().samples[0], -0.125));
  }
}

TEST(Wav, ExtensibleHeaderAndSkippedChunks) {
  WavSpec s;
  s.extensible = true;
  s.tag = 1;
  s.channels = 2;
  s.listChunkBeforeData = true;  // odd-sized LIST chunk with padding
  s.payload = pcm16({100, -100});
  auto r = decodeWav(makeWav(s));
  REQUIRE(r.ok());
  CHECK_EQ(r.value().frames(), std::uint64_t{1});
  CHECK(near(r.value().samples[0], 100 / 32768.0));
  CHECK(near(r.value().samples[1], -100 / 32768.0));
  WavSpec f = s;
  f.tag = 3; f.bits = 32;
  f.payload.clear(); put32(f.payload, 0x3F000000u); put32(f.payload, 0xBF000000u);  // 0.5, -0.5
  auto rf = decodeWav(makeWav(f));
  REQUIRE(rf.ok());
  CHECK(near(rf.value().samples[0], 0.5));
}

TEST(Wav, StreamingSizesAndTruncation) {
  WavSpec s;
  s.payload = pcm16({1, 2, 3, 4});
  s.dataSizeOverride = 0xFFFFFFFFu;  // streaming encoder: size unknown
  auto open = decodeWav(makeWav(s));
  REQUIRE(open.ok());
  CHECK_EQ(open.value().frames(), std::uint64_t{4});
  s.dataSizeOverride = 0;
  CHECK_EQ(decodeWav(makeWav(s)).value().frames(), std::uint64_t{4});
  s.dataSizeOverride = 1000;  // claims more than present: truncated file
  auto bad = decodeWav(makeWav(s));
  REQUIRE(!bad.ok());
  CHECK(bad.error().code == ErrorCode::Corrupt);
  WavSpec partial;
  partial.channels = 2;
  partial.payload = pcm16({1, 2, 3});  // 1.5 frames: the partial frame is dropped
  auto p = decodeWav(makeWav(partial));
  REQUIRE(p.ok());
  CHECK_EQ(p.value().frames(), std::uint64_t{1});
}

TEST(Wav, RejectsMalformedAndUnsupported) {
  WavSpec s;
  s.payload = pcm16({1, 2});
  { WavSpec x = s; x.tag = 2; CHECK(decodeWav(makeWav(x)).error().code == ErrorCode::Unsupported); }  // ADPCM
  { WavSpec x = s; x.bits = 12; CHECK(!decodeWav(makeWav(x)).ok()); }
  { WavSpec x = s; x.channels = 0; CHECK(!decodeWav(makeWav(x)).ok()); }
  { WavSpec x = s; x.channels = 200; x.payload = std::vector<std::uint8_t>(400, 0); CHECK(!decodeWav(makeWav(x)).ok()); }
  { WavSpec x = s; x.rate = 0; CHECK(!decodeWav(makeWav(x)).ok()); }
  { WavSpec x = s; x.blockAlignDelta = 1; CHECK(decodeWav(makeWav(x)).error().code == ErrorCode::Corrupt); }
  CHECK(!decodeWav({}).ok());
  const std::vector<std::uint8_t> riffOnly = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'};
  CHECK(!decodeWav(riffOnly).ok());  // no fmt/data
  // data chunk before fmt
  std::vector<std::uint8_t> v;
  putTag(v, "RIFF"); put32(v, 20); putTag(v, "WAVE"); putTag(v, "data"); put32(v, 0);
  CHECK(!decodeWav(v).ok());
  // Limits
  AudioLimits l; l.maxFrames = 1;
  CHECK(decodeWav(makeWav(s), l).error().code == ErrorCode::OutOfRange);
}

TEST(Wav, RandomCorruptionNeverCrashes) {
  WavSpec s;
  s.channels = 2;
  s.listChunkBeforeData = true;
  s.payload = pcm16({1, 2, 3, 4, 5, 6, 7, 8});
  const auto src = makeWav(s);
  Lcg rng(777);
  int bad = 0;
  for (int i = 0; i < 5000; ++i) {
    auto r = decodeWav(mutate(src, rng));
    if (!r.ok()) ++bad;
    else CHECK(r.value().samples.size() % r.value().channels == 0);
  }
  CHECK(bad > 0);
}

TEST(Ogg, MonoSineHasRightFrequencyLevelAndDuration) {
  const auto bytes = fixture("sine440_mono.ogg");
  CHECK(sniffAudioFormat(bytes) == AudioFormat::Ogg);
  auto r = decodeOgg(bytes);
  REQUIRE(r.ok());
  const AudioClip& c = r.value();
  CHECK_EQ(c.channels, 1u);
  CHECK_EQ(c.sampleRate, 44100u);
  CHECK_NEAR(c.durationSeconds(), 0.5, 0.03);
  CHECK_NEAR(frequencyOf(c, 0), 440.0, 8.0);
  CHECK_NEAR(rmsOf(c, 0), 0.5 / std::sqrt(2.0), 0.03);  // amplitude 0.5 sine
  for (float f : c.samples) CHECK(std::isfinite(f) && std::fabs(f) < 1.2F);
}

TEST(Ogg, StereoChannelsStaySeparate) {
  auto r = decodeAudio(fixture("stereo_440_880.ogg"));  // dispatches by content
  REQUIRE(r.ok());
  const AudioClip& c = r.value();
  CHECK_EQ(c.channels, 2u);
  CHECK_NEAR(frequencyOf(c, 0), 440.0, 8.0);
  CHECK_NEAR(frequencyOf(c, 1), 880.0, 16.0);  // swapped channels would be caught here
}

TEST(Ogg, TruncatedAndCorruptStreamsAreHandled) {
  const auto bytes = fixture("sine440_mono.ogg");
  int accepted = 0;
  for (std::size_t n = 0; n < bytes.size(); n += 97) {
    auto r = decodeOgg(std::span<const std::uint8_t>(bytes.data(), n));
    if (r.ok()) ++accepted;
  }
  // A cut stream must not be reported as the complete clip.
  auto cut = decodeOgg(std::span<const std::uint8_t>(bytes.data(), bytes.size() / 2));
  CHECK_MSG(!cut.ok(), "a half-length Ogg stream was accepted as complete");
  (void)accepted;
  Lcg rng(99);
  for (int i = 0; i < 400; ++i) (void)decodeOgg(mutate(bytes, rng));  // must not crash (ASan)
  CHECK(!decodeOgg(std::vector<std::uint8_t>{'O', 'g', 'g', 'S', 0, 0, 0, 0}).ok());
}

TEST(Audio, UnknownFormatsAndSniffing) {
  CHECK(!decodeAudio({}).ok());
  const std::vector<std::uint8_t> mp3 = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 0};
  auto r = decodeAudio(mp3);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::Unsupported);
  CHECK(sniffAudioFormat(mp3) == AudioFormat::Unknown);
  WavSpec s; s.payload = pcm16({1});
  CHECK(sniffAudioFormat(makeWav(s)) == AudioFormat::Wav);
  CHECK(decodeAudio(makeWav(s)).ok());
}
