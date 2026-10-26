// SPDX-License-Identifier: GPL-2.0-only
// .mingol archive: round trips, encryption, exhaustive tamper/truncation detection, fuzzing, exe embedding.
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "mingol/package/archive.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::package;

namespace {
std::vector<std::uint8_t> blob(std::size_t n, int salt) {
  std::vector<std::uint8_t> b(n);
  std::uint32_t x = 0x9E3779B9u * static_cast<std::uint32_t>(salt + 1);
  for (auto& v : b) { x = x * 1664525u + 1013904223u; v = static_cast<std::uint8_t>(x >> 24); }
  return b;
}
ArchiveKey testKey(int n = 1) {
  ArchiveKey k;
  for (std::size_t i = 0; i < 32; ++i) k.bytes[i] = static_cast<std::uint8_t>(i * 3 + static_cast<std::size_t>(n));
  return k;
}
std::array<std::uint8_t, 16> fixedSalt() { std::array<std::uint8_t, 16> s{}; for (std::size_t i = 0; i < 16; ++i) s[i] = static_cast<std::uint8_t>(0xA0 + i); return s; }

struct Sample {
  ArchiveWriter w;
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
  Sample() {
    files = {{"project.mindev", blob(300, 1)}, {"scenes/main.minseen", blob(5000, 2)}, {"assets/empty.bin", {}},
             {"assets/tex/big.bin", blob(kArchiveChunkSize * 2 + 123, 3)},   // three chunks, ragged last one
             {"assets/exact.bin", blob(kArchiveChunkSize, 4)},                // exactly one full chunk
             {"scripts/héllo wörld.mins", blob(17, 5)}};
    for (const auto& f : files) REQUIRE(w.add(f.first, f.second).ok());
  }
};

WriteOptions encOpts(int k = 1) { WriteOptions o; o.key = testKey(k); o.salt = fixedSalt(); return o; }
WriteOptions plainOpts() { return {}; }

void expectContents(const ArchiveReader& r, const Sample& s) {
  CHECK_EQ(r.entries().size(), s.files.size());
  for (const auto& f : s.files) {
    CHECK(r.contains(f.first));
    auto d = r.read(f.first);
    CHECK_MSG(d.ok(), "reading " + f.first);
    if (d.ok()) CHECK_MSG(d.value() == f.second, "content of " + f.first);
  }
}
}  // namespace

TEST(Archive, RoundTripPlain) {
  Sample s;
  auto bytes = s.w.serialize(plainOpts());
  REQUIRE(bytes.ok());
  auto r = ArchiveReader::openMemory(bytes.value());
  REQUIRE(r.ok());
  CHECK(!r.value().encrypted());
  expectContents(r.value(), s);
  CHECK(r.value().verifyAll().ok());
}

TEST(Archive, RoundTripEncrypted) {
  Sample s;
  auto bytes = s.w.serialize(encOpts());
  REQUIRE(bytes.ok());
  auto r = ArchiveReader::openMemory(bytes.value(), testKey());
  REQUIRE(r.ok());
  CHECK(r.value().encrypted());
  expectContents(r.value(), s);
  CHECK(r.value().verifyAll().ok());
  // stored size accounts for one 16-byte tag per chunk
  for (const auto& e : r.value().entries()) {
    const std::uint64_t chunks = e.size == 0 ? 1 : (e.size + kArchiveChunkSize - 1) / kArchiveChunkSize;
    CHECK_EQ(e.storedSize, e.size + 16 * chunks);
  }
}

TEST(Archive, EncryptedBytesDoNotLeakPlaintext) {
  ArchiveWriter w;
  std::vector<std::uint8_t> secret(4000, 0);
  const std::string marker = "TOP-SECRET-MARKER-STRING";
  std::copy(marker.begin(), marker.end(), secret.begin() + 100);
  REQUIRE(w.add("assets/secret.txt", secret).ok());
  auto enc = w.serialize(encOpts());
  auto plain = w.serialize(plainOpts());
  REQUIRE(enc.ok());
  REQUIRE(plain.ok());
  auto has = [&](const std::vector<std::uint8_t>& b, const std::string& s) { return std::search(b.begin(), b.end(), s.begin(), s.end()) != b.end(); };
  CHECK(has(plain.value(), marker));
  CHECK(!has(enc.value(), marker));
  CHECK(!has(enc.value(), "secret.txt"));   // names are encrypted as well
  CHECK(has(plain.value(), "secret.txt"));
}

TEST(Archive, WrongOrMissingKeyAndSaltFreshness) {
  Sample s;
  auto bytes = s.w.serialize(encOpts());
  REQUIRE(bytes.ok());
  auto wrong = ArchiveReader::openMemory(bytes.value(), testKey(2));
  REQUIRE(!wrong.ok());
  CHECK(wrong.error().code == ErrorCode::AuthFailed);
  auto none = ArchiveReader::openMemory(bytes.value());
  REQUIRE(!none.ok());
  CHECK(none.error().code == ErrorCode::InvalidArgument);
  // a key given for a plain archive is simply unused
  auto plain = s.w.serialize(plainOpts());
  REQUIRE(plain.ok());
  CHECK(ArchiveReader::openMemory(plain.value(), testKey()).ok());
  // two saves with random salts differ and both open
  WriteOptions o;
  o.key = testKey();
  auto a = s.w.serialize(o), b = s.w.serialize(o);
  REQUIRE(a.ok());
  REQUIRE(b.ok());
  CHECK(a.value() != b.value());
  CHECK(ArchiveReader::openMemory(a.value(), testKey()).ok());
  // same inputs + fixed salt => byte-identical output (reproducible builds)
  CHECK(s.w.serialize(encOpts()).value() == s.w.serialize(encOpts()).value());
  CHECK(s.w.serialize(plainOpts()).value() == s.w.serialize(plainOpts()).value());
}

TEST(Archive, EmptyArchiveAndSingleEmptyEntry) {
  ArchiveWriter empty;
  for (bool enc : {false, true}) {
    auto bytes = empty.serialize(enc ? encOpts() : plainOpts());
    REQUIRE(bytes.ok());
    auto r = ArchiveReader::openMemory(bytes.value(), testKey());
    REQUIRE(r.ok());
    CHECK_EQ(r.value().entries().size(), std::size_t{0});
    CHECK(!r.value().read("x").ok());
  }
  ArchiveWriter one;
  REQUIRE(one.add("a", {}).ok());
  auto bytes = one.serialize(encOpts());
  REQUIRE(bytes.ok());
  auto r = ArchiveReader::openMemory(bytes.value(), testKey());
  REQUIRE(r.ok());
  auto d = r.value().read("a");
  REQUIRE(d.ok());
  CHECK(d.value().empty());
}

TEST(Archive, WriterValidatesNames) {
  ArchiveWriter w;
  CHECK(w.add("ok/file.bin", {}).ok());
  CHECK(!w.add("ok/file.bin", {}).ok());      // duplicate
  CHECK(!w.add("", {}).ok());
  CHECK(!w.add("/abs", {}).ok());
  CHECK(!w.add("a/../b", {}).ok());
  CHECK(!w.add("a\\b", {}).ok());
  CHECK(!w.add(std::string(70000, 'x'), {}).ok());
  CHECK_EQ(w.count(), std::size_t{1});
}

TEST(Archive, KeyHelpers) {
  CHECK(!ArchiveKey::fromHex("abc").ok());
  CHECK(!ArchiveKey::fromHex(std::string(64, 'g')).ok());
  auto k = ArchiveKey::fromHex(testKey().toHex());
  REQUIRE(k.ok());
  CHECK(k.value().bytes == testKey().bytes);
  CHECK(ArchiveKey::fromHex("0123456789ABCDEF0123456789abcdef0123456789ABCDEF0123456789abcdef").ok());
  auto g1 = ArchiveKey::generate(), g2 = ArchiveKey::generate();
  REQUIRE(g1.ok());
  REQUIRE(g2.ok());
  CHECK(g1.value().bytes != g2.value().bytes);
  auto p1 = ArchiveKey::fromPassphrase("correct horse", "project-1", 10000);
  auto p2 = ArchiveKey::fromPassphrase("correct horse", "project-1", 10000);
  auto p3 = ArchiveKey::fromPassphrase("correct horse", "project-2", 10000);
  REQUIRE(p1.ok());
  REQUIRE(p3.ok());
  CHECK(p1.value().bytes == p2.value().bytes);
  CHECK(p1.value().bytes != p3.value().bytes);
  CHECK(!ArchiveKey::fromPassphrase("", "s", 10000).ok());
  CHECK(!ArchiveKey::fromPassphrase("x", "s", 10).ok());
}

namespace {
// Every single-bit modification must be detected by open() or by reading the entries (verifyAll).
void expectAllBitFlipsDetected(const std::vector<std::uint8_t>& bytes, const std::optional<ArchiveKey>& key, std::size_t stride) {
  std::size_t undetected = 0;
  for (std::size_t i = 0; i < bytes.size(); i += stride) {
    for (int bit : {0, 5}) {
      auto m = bytes;
      m[i] ^= static_cast<std::uint8_t>(1u << bit);
      auto r = ArchiveReader::openMemory(std::move(m), key);
      if (r.ok() && r.value().verifyAll().ok()) {
        ++undetected;
        if (undetected <= 3) CHECK_MSG(false, "modification at byte " + std::to_string(i) + " bit " + std::to_string(bit) + " went undetected");
      }
    }
  }
  CHECK_EQ(undetected, std::size_t{0});
}
}  // namespace

TEST(Archive, EveryBitFlipIsDetected_Plain) {
  ArchiveWriter w;
  REQUIRE(w.add("a.bin", blob(70, 1)).ok());
  REQUIRE(w.add("dir/b.bin", {}).ok());
  REQUIRE(w.add("dir/c.bin", blob(33, 2)).ok());
  auto bytes = w.serialize(plainOpts());
  REQUIRE(bytes.ok());
  expectAllBitFlipsDetected(bytes.value(), std::nullopt, 1);
}

TEST(Archive, EveryBitFlipIsDetected_Encrypted) {
  ArchiveWriter w;
  REQUIRE(w.add("a.bin", blob(70, 1)).ok());
  REQUIRE(w.add("dir/b.bin", {}).ok());
  REQUIRE(w.add("dir/c.bin", blob(33, 2)).ok());
  auto bytes = w.serialize(encOpts());
  REQUIRE(bytes.ok());
  expectAllBitFlipsDetected(bytes.value(), testKey(), 1);
}

TEST(Archive, MultiChunkEntriesDetectTamperingAnywhere) {
  ArchiveWriter w;
  REQUIRE(w.add("big.bin", blob(kArchiveChunkSize * 2 + 10, 7)).ok());
  auto bytes = w.serialize(encOpts());
  REQUIRE(bytes.ok());
  expectAllBitFlipsDetected(bytes.value(), testKey(), 997);   // sampled: ~130 KiB
  // chunk reordering / truncation of whole chunks is caught by the nonce + last-chunk flag
  auto m = bytes.value();
  const std::size_t chunkStored = kArchiveChunkSize + 16;
  std::swap_ranges(m.begin() + 80, m.begin() + 80 + static_cast<std::ptrdiff_t>(chunkStored), m.begin() + 80 + static_cast<std::ptrdiff_t>(chunkStored));
  auto r = ArchiveReader::openMemory(m, testKey());
  CHECK(!r.ok() || !r.value().verifyAll().ok());
}

TEST(Archive, TruncationAndExtensionAreDetected) {
  Sample s;
  for (bool enc : {false, true}) {
    auto bytes = s.w.serialize(enc ? encOpts() : plainOpts());
    REQUIRE(bytes.ok());
    const auto& b = bytes.value();
    for (std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{79}, std::size_t{80}, std::size_t{81}, b.size() / 2, b.size() - 17, b.size() - 1}) {
      auto t = std::vector<std::uint8_t>(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(cut));
      CHECK_MSG(!ArchiveReader::openMemory(t, testKey()).ok(), "truncation to " + std::to_string(cut));
    }
    auto e = b;
    e.push_back(0);
    CHECK(!ArchiveReader::openMemory(e, testKey()).ok());
    auto e2 = b;
    e2.insert(e2.begin(), 0);
    CHECK(!ArchiveReader::openMemory(e2, testKey()).ok());
  }
}

TEST(Archive, NewerVersionAndBadMagicHaveClearErrors) {
  ArchiveWriter w;
  REQUIRE(w.add("a", blob(5, 1)).ok());
  auto bytes = w.serialize(plainOpts());
  REQUIRE(bytes.ok());
  auto m = bytes.value();
  m[8] = 2;   // version 2: the header hash also breaks, but the version check comes first
  auto r = ArchiveReader::openMemory(m);
  REQUIRE(!r.ok());
  CHECK(r.error().code == ErrorCode::VersionMismatch);
  m = bytes.value();
  m[1] = 'X';
  auto r2 = ArchiveReader::openMemory(m);
  REQUIRE(!r2.ok());
  CHECK(r2.error().message.find("magic") != std::string::npos);
  CHECK(!ArchiveReader::openMemory({}).ok());
  CHECK(!ArchiveReader::openMemory(std::vector<std::uint8_t>(1000, 0)).ok());
}

TEST(Archive, StructuralAttacksWithValidHashes) {
  // An attacker without a key can rewrite a *plain* archive completely (it has no secret); the reader must
  // still reject structurally inconsistent tables instead of misbehaving. Build such files by hand.
  auto make = [](std::uint64_t entryOffset, std::uint64_t stored, std::uint64_t plain, const std::string& name, std::uint32_t count) {
    std::vector<std::uint8_t> toc;
    auto p32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) toc.push_back(static_cast<std::uint8_t>(v >> (8 * i))); };
    auto p64 = [&](std::uint64_t v) { for (int i = 0; i < 8; ++i) toc.push_back(static_cast<std::uint8_t>(v >> (8 * i))); };
    p32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      toc.push_back(static_cast<std::uint8_t>(name.size()));
      toc.push_back(0);
      toc.insert(toc.end(), name.begin(), name.end());
      p64(entryOffset); p64(stored); p64(plain);
      toc.insert(toc.end(), 32, 0);
    }
    std::vector<std::uint8_t> out = {0x89, 'M', 'I', 'N', 'G', 'O', 'L', 0x0A, 1, 0, 0, 0, 0, 0, 0, 0};
    out.insert(out.end(), 16, 0);
    auto o64 = [&](std::uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i))); };
    o64(80 + stored);   // tocOffset (assuming one entry of `stored` bytes)
    o64(toc.size());
    crypto::Sha256 h;
    h.update(std::span<const std::uint8_t>(out).first(48));
    h.update(toc);
    const auto d = h.finish();
    out.insert(out.end(), d.begin(), d.end());
    out.insert(out.end(), static_cast<std::size_t>(std::min<std::uint64_t>(stored, 64)), 0x41);
    out.insert(out.end(), toc.begin(), toc.end());
    return out;
  };
  // sanity: a consistent hand-made archive with a wrong content hash opens but fails on read
  auto good = make(80, 4, 4, "a", 1);
  auto r = ArchiveReader::openMemory(good);
  REQUIRE(r.ok());
  CHECK(!r.value().read("a").ok());   // the all-zero hash does not match
  CHECK(!ArchiveReader::openMemory(make(81, 4, 4, "a", 1)).ok());           // gap before the entry
  CHECK(!ArchiveReader::openMemory(make(80, 4, 5, "a", 1)).ok());           // stored != plain
  CHECK(!ArchiveReader::openMemory(make(80, 4, 4, "../x", 1)).ok());        // path traversal name
  CHECK(!ArchiveReader::openMemory(make(80, 4, 4, "", 1)).ok());            // empty name
  CHECK(!ArchiveReader::openMemory(make(80, 4, 4, "a", 2)).ok());           // duplicate / second entry has no data
  CHECK(!ArchiveReader::openMemory(make(80, 4, ~0ull, "a", 1)).ok());       // absurd size
  CHECK(!ArchiveReader::openMemory(make(80, ~0ull - 5, ~0ull - 5, "a", 1)).ok());
}

TEST(Archive, FileRoundTripSaveIsAtomicAndReadsAreThreadSafe) {
  Sample s;
  const auto dir = std::filesystem::temp_directory_path() / "mingol_archive_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto path = dir / "game.mingol";
  REQUIRE(s.w.save(path, encOpts()).ok());
  std::size_t n = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir)) { (void)e; ++n; }
  CHECK_EQ(n, std::size_t{1});   // no temp file left behind
  auto r = ArchiveReader::openFile(path, testKey());
  REQUIRE(r.ok());
  expectContents(r.value(), s);
  // concurrent reads of different entries
  std::vector<std::thread> threads;
  std::atomic<int> bad{0};
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 30; ++i) {
        for (const auto& f : s.files) {
          auto d = r.value().read(f.first);
          if (!d.ok() || d.value() != f.second) ++bad;
        }
      }
    });
  }
  for (auto& t : threads) t.join();
  CHECK_EQ(bad.load(), 0);
  CHECK(!ArchiveReader::openFile(dir / "missing.mingol").ok());
  // a failed save leaves the previous archive untouched
  ArchiveWriter bad2;
  REQUIRE(bad2.add("x", blob(3, 1)).ok());
  const auto before = std::filesystem::file_size(path);
  CHECK(!bad2.save(dir / "no_such_dir" / "x.mingol").ok());
  CHECK_EQ(std::filesystem::file_size(path), before);
  std::filesystem::remove_all(dir);
}

TEST(Archive, MutationFuzzNeverCrashesAndNeverAcceptsDamage) {
  Sample s;
  std::mt19937 rng(4242);
  if (const char* e = std::getenv("MINGOL_FUZZ_SEED")) rng.seed(static_cast<unsigned>(std::atoi(e)));
  const char* itEnv = std::getenv("MINGOL_FUZZ_ITERS");
  const int iterations = itEnv ? std::atoi(itEnv) : 1500;
  ArchiveWriter small;
  REQUIRE(small.add("a/b.txt", blob(200, 1)).ok());
  REQUIRE(small.add("c.bin", blob(70, 2)).ok());
  for (bool enc : {false, true}) {
    auto bytes = small.serialize(enc ? encOpts() : plainOpts());
    REQUIRE(bytes.ok());
    for (int iter = 0; iter < iterations; ++iter) {
      auto m = bytes.value();
      const int edits = 1 + static_cast<int>(rng() % 4);
      for (int k = 0; k < edits; ++k) {
        const std::size_t pos = rng() % m.size();
        switch (rng() % 4) {
          case 0: m[pos] = static_cast<std::uint8_t>(rng()); break;
          case 1: m[pos] ^= static_cast<std::uint8_t>(1u << (rng() % 8)); break;
          case 2: m.erase(m.begin() + static_cast<std::ptrdiff_t>(pos), m.begin() + static_cast<std::ptrdiff_t>(std::min(m.size(), pos + 1 + rng() % 16))); break;
          default: m.insert(m.begin() + static_cast<std::ptrdiff_t>(pos), static_cast<std::uint8_t>(rng())); break;
        }
        if (m.empty()) m.push_back(0);
      }
      if (m == bytes.value()) continue;
      auto r = ArchiveReader::openMemory(std::move(m), testKey());
      if (r.ok()) {
        // an edit that cancelled itself out cannot happen (m != original), so reading must fail somewhere
        CHECK_MSG(!r.value().verifyAll().ok(), "a damaged archive passed full verification");
      }
    }
  }
}

TEST(Archive, ExecutableEmbedding) {
  namespace stdfs = std::filesystem;
  const auto dir = stdfs::temp_directory_path() / "mingol_exe_test";
  stdfs::remove_all(dir);
  stdfs::create_directories(dir);
  Sample s;
  REQUIRE(s.w.save(dir / "game.mingol", encOpts()).ok());
  // a real executable: this test binary itself
#if defined(_WIN32)
  wchar_t selfBuf[MAX_PATH * 4] = {};
  REQUIRE(GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(sizeof selfBuf / sizeof selfBuf[0])) > 0);
  const stdfs::path self = selfBuf;
  const stdfs::path packed = dir / "game_packed.exe";
  const char* nullDevice = "NUL";
#else
  const stdfs::path self = stdfs::read_symlink("/proc/self/exe");
  const stdfs::path packed = dir / "game_packed";
  const char* nullDevice = "/dev/null";
#endif
  CHECK(!ArchiveReader::hasEmbeddedArchive(self));
  REQUIRE(packExecutable(self, dir / "game.mingol", packed).ok());
  CHECK(ArchiveReader::hasEmbeddedArchive(packed));
  auto r = ArchiveReader::openExecutable(packed, testKey());
  REQUIRE(r.ok());
  expectContents(r.value(), s);
  CHECK(r.value().verifyAll().ok());
  CHECK(!ArchiveReader::openExecutable(packed, testKey(9)).ok());   // wrong key
  CHECK(!ArchiveReader::openExecutable(self, testKey()).ok());      // nothing appended
  CHECK(!packExecutable(packed, dir / "game.mingol", dir / "twice").ok());   // already packed
  CHECK(!packExecutable(self, dir / "missing.mingol", dir / "x").ok());
  // the packed file is still an executable that runs (--list prints the registered tests and exits 0)
  const std::string cmd = "\"" + packed.string() + "\" --list > " + nullDevice + " 2>&1";
  CHECK_EQ(std::system(cmd.c_str()), 0);
  // corrupt the trailer
  {
    std::fstream f(packed, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(-16, std::ios::end);
    f.put('\x7f');
  }
  CHECK(!ArchiveReader::openExecutable(packed, testKey()).ok());
  stdfs::remove_all(dir);
}

TEST(Archive, PackProject) {
  namespace stdfs = std::filesystem;
  const auto dir = stdfs::temp_directory_path() / "mingol_pack_project";
  stdfs::remove_all(dir);
  stdfs::create_directories(dir / "assets/tex");
  stdfs::create_directories(dir / "scripts");
  stdfs::create_directories(dir / "scenes");
  stdfs::create_directories(dir / "notes");   // not part of the project
  auto write = [&](const char* rel, const std::string& text) { std::ofstream(dir / rel, std::ios::binary) << text; };
  write("assets/tex/a.png", "PNGDATA");
  write("assets/readme.txt", "hello");
  write("scripts/hero.mins", "class Hero extends Script { }");
  write("scenes/main.minseen", "{}");
  write("notes/todo.txt", "not packed");
  stdfs::create_symlink(dir / "assets/readme.txt", dir / "assets/link.txt");
  auto p = makeNewProject("Demo", "0.1.0");
  p.scenes = {"scenes/main.minseen"};
  p.entryScene = "scenes/main.minseen";
  REQUIRE(saveMindev((dir / "demo.mindev").string(), p).ok());

  ProjectPackReport rep;
  auto bytes = packProject(dir / "demo.mindev", {}, &rep);
  REQUIRE(bytes.ok());
  CHECK_EQ(rep.files, std::size_t{5});   // mindev + 2 assets + script + scene
  CHECK_EQ(rep.skipped.size(), std::size_t{1});   // the symlink
  auto r = ArchiveReader::openMemory(bytes.value());
  REQUIRE(r.ok());
  CHECK(r.value().contains("project.mindev"));
  CHECK(r.value().contains("assets/tex/a.png"));
  CHECK(r.value().contains("scripts/hero.mins"));
  CHECK(r.value().contains("scenes/main.minseen"));
  CHECK(!r.value().contains("notes/todo.txt"));
  CHECK(!r.value().contains("assets/link.txt"));
  auto d = r.value().read("assets/tex/a.png");
  REQUIRE(d.ok());
  CHECK_EQ(std::string(d.value().begin(), d.value().end()), "PNGDATA");
  // deterministic (unencrypted)
  CHECK(packProject(dir / "demo.mindev", {}).value() == bytes.value());
  // the packed project file parses back
  auto pm = r.value().read("project.mindev");
  REQUIRE(pm.ok());
  CHECK(parseMindev(std::string(pm.value().begin(), pm.value().end())).ok());

  // errors: listed scene missing; entry directory escaping the root
  p.scenes = {"scenes/missing.minseen"};
  p.entryScene = "scenes/missing.minseen";
  REQUIRE(saveMindev((dir / "bad.mindev").string(), p).ok());
  CHECK(!packProject(dir / "bad.mindev", {}).ok());
  CHECK(!packProject(dir / "nope.mindev", {}).ok());
  stdfs::remove_all(dir);
}
