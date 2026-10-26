// SPDX-License-Identifier: GPL-2.0-only
#include <filesystem>
#include <fstream>
#include <string>

#include "mingol/core/fs.hpp"
#include "mingol/core/uuid.hpp"
#include "mingol/package/mindev.hpp"
#include "mingol_test.hpp"

using namespace mingol;
using namespace mingol::package;
using mingol::test::fail;

namespace {

ProjectFile sample() {
  ProjectFile p = makeNewProject("My Game", "0.1.0");
  p.scenes = {"scenes/main.minseen", "scenes/menu.minseen"};
  p.entryScene = "scenes/main.minseen";
  p.plugins = {{"plugins/extras", 1}};
  p.settings.set("window", json::Value(json::Object{}));
  p.settings.find("window")->asObject().set("width", 1280);
  p.settings.find("window")->asObject().set("scale", 1.5);
  p.settings.set("title", "한글 제목 \"quoted\"");
  p.build.encrypt = true;
  p.build.keyId = "studio-key-1";
  p.extra.set("editorLayout", json::Value(json::Array{json::Value(1), json::Value(2)}));
  return p;
}

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() / ("mingol_mindev_" + Uuid::generate().toString());
    std::filesystem::create_directories(path);
  }
  ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

std::string replaceOnce(std::string s, const std::string& a, const std::string& b) {
  const auto pos = s.find(a);
  if (pos != std::string::npos) s.replace(pos, a.size(), b);
  return s;
}

}  // namespace

TEST(Mindev, IsReadableJsonText) {
  const std::string text = serialize(sample()).value();
  CHECK(text.front() == '{');
  CHECK(text.back() == '\n');
  CHECK(text.find("\"format\": \"mindev\"") != std::string::npos);
  CHECK(text.find("\"formatVersion\": 1") != std::string::npos);
  CHECK(text.find("\"name\": \"My Game\"") != std::string::npos);
  CHECK(text.find("한글 제목") != std::string::npos);  // UTF-8 stays readable, not \u-escaped
  CHECK(text.find("\"integrity\": \"crc32:") != std::string::npos);
  CHECK(json::parse(text).ok());  // any JSON tool can read it
}

TEST(Mindev, RoundTripsEverything) {
  const ProjectFile a = sample();
  auto b = parseMindev(serialize(a).value());
  REQUIRE(b.ok());
  const ProjectFile& r = b.value();
  CHECK_EQ(r.name, a.name);
  CHECK_EQ(r.id, a.id);
  CHECK_EQ(r.engineVersion, a.engineVersion);
  CHECK_EQ(r.entryScene, a.entryScene);
  CHECK(r.scenes == a.scenes);
  CHECK(r.assetDirs == a.assetDirs);
  CHECK(r.scriptDirs == a.scriptDirs);
  CHECK(r.plugins == a.plugins);
  CHECK(r.build == a.build);
  CHECK(json::Value(r.settings) == json::Value(a.settings));
  CHECK(json::Value(r.extra) == json::Value(a.extra));
  // and serialising the loaded project reproduces the exact same bytes
  CHECK_EQ(serialize(r).value(), serialize(a).value());
}

TEST(Mindev, MinimalHandWrittenFileLoadsWithDefaults) {
  auto r = parseMindev(R"({"format":"mindev","formatVersion":1,"name":"X","id":"123e4567-e89b-42d3-a456-426614174000","engineVersion":"1.0.0"})");
  REQUIRE(r.ok());
  CHECK(r.value().scenes.empty());
  CHECK(r.value().entryScene.empty());
  CHECK(!r.value().build.compress);
  CHECK(!r.value().build.singleExe);
  CHECK(r.value().settings.empty());
}

TEST(Mindev, IntegrityDetectsValueChangesButNotFormatting) {
  const std::string text = serialize(sample()).value();
  // reformatting (compact) is fine: the checksum is over the canonical form
  auto doc = json::parse(text).value();
  CHECK(parseMindev(json::write(doc, {.pretty = false}).value()).ok());
  // changing a value is caught
  CHECK(parseMindev(replaceOnce(text, "My Game", "My Gamf")).error().code == ErrorCode::Corrupt);
  CHECK(parseMindev(replaceOnce(text, "1280", "1281")).error().code == ErrorCode::Corrupt);
  CHECK(parseMindev(replaceOnce(text, "\"singleExe\": false", "\"singleExe\": true")).error().code == ErrorCode::Corrupt);
  // unless the caller opts out (hand editing)
  CHECK(parseMindev(replaceOnce(text, "My Game", "My Gamf"), {.verifyIntegrity = false}).ok());
  // a malformed integrity value is an error too
  CHECK(!parseMindev(replaceOnce(text, "crc32:", "crc99:")).ok());
}

TEST(Mindev, EverySingleCharacterChangeIsDetectedOrRejected) {
  const std::string text = serialize(sample()).value();
  int silent = 0;
  // Damaging the *name* of the integrity key itself turns it into an unknown key, which is the same as a
  // hand-written file without a checksum (accepted by design), so those bytes are skipped.
  const auto ik = text.find("\"integrity\"");
  REQUIRE(ik != std::string::npos);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i > ik && i < ik + 11) continue;
    std::string t = text;
    t[i] = static_cast<char>(t[i] ^ 0x01);
    auto r = parseMindev(t);
    if (r.ok()) {
      // Only a change that leaves the canonical document identical may pass (e.g. inside whitespace).
      auto again = serialize(r.value());
      if (!again.ok() || again.value() != text) ++silent;
    }
  }
  CHECK_EQ(silent, 0);
}

TEST(Mindev, VersionHandling) {
  const std::string text = serialize(sample()).value();
  auto newer = parseMindev(replaceOnce(text, "\"formatVersion\": 1", "\"formatVersion\": 2"), {.verifyIntegrity = false});
  REQUIRE(!newer.ok());
  CHECK(newer.error().code == ErrorCode::VersionMismatch);
  // a newer file is rejected as a newer file even before its checksum is looked at
  CHECK(parseMindev(replaceOnce(text, "\"formatVersion\": 1", "\"formatVersion\": 2")).error().code == ErrorCode::VersionMismatch);
  CHECK(parseMindev(replaceOnce(text, "\"formatVersion\": 1", "\"formatVersion\": 0"), {.verifyIntegrity = false}).error().code == ErrorCode::Corrupt);
  CHECK(parseMindev(replaceOnce(text, "\"formatVersion\": 1", "\"formatVersion\": \"1\""), {.verifyIntegrity = false}).error().code == ErrorCode::Corrupt);
  CHECK(parseMindev(replaceOnce(text, "\"formatVersion\": 1", "\"formatVersion\": 1.0"), {.verifyIntegrity = false}).error().code == ErrorCode::Corrupt);
  CHECK(!parseMindev(replaceOnce(text, "\"mindev\"", "\"minseen\""), {.verifyIntegrity = false}).ok());
}

TEST(Mindev, RejectsWrongTypesAndMissingFields) {
  const char* head = R"({"format":"mindev","formatVersion":1,"id":"123e4567-e89b-42d3-a456-426614174000","engineVersion":"1.0.0",)";
  auto load = [&](const std::string& rest) { return parseMindev(std::string(head) + rest); };
  CHECK(load(R"("name":"ok"})").ok());
  CHECK(!load(R"("name":5})").ok());
  CHECK(!parseMindev(R"({"format":"mindev","formatVersion":1})").ok());
  CHECK(!load(R"("name":"ok","scenes":"a"})").ok());
  CHECK(!load(R"("name":"ok","scenes":[1]})").ok());
  CHECK(!load(R"("name":"ok","plugins":[{"path":"p"}]})").ok());
  CHECK(!load(R"("name":"ok","plugins":[{"path":"p","abiVersion":0}]})").ok());
  CHECK(!load(R"("name":"ok","plugins":[{"path":"p","abiVersion":"1"}]})").ok());
  CHECK(!load(R"("name":"ok","plugins":["p"]})").ok());
  CHECK(!load(R"("name":"ok","settings":[]})").ok());
  CHECK(!load(R"("name":"ok","build":{"singleExe":1}})").ok());
  CHECK(!load(R"("name":"ok","build":[]})").ok());
  CHECK(!parseMindev("[]").ok());
  CHECK(!parseMindev("").ok());
  CHECK(!parseMindev("not json").ok());
}

TEST(Mindev, ValidatesFieldsAndCrossReferences) {
  auto bad = [](auto mutate) {
    ProjectFile p = sample();
    mutate(p);
    return !validate(p).ok() && !serialize(p).ok();
  };
  CHECK(validate(sample()).ok());
  CHECK(bad([](ProjectFile& p) { p.name.clear(); }));
  CHECK(bad([](ProjectFile& p) { p.name = std::string(129, 'x'); }));
  CHECK(bad([](ProjectFile& p) { p.name = "a\nb"; }));
  CHECK(bad([](ProjectFile& p) { p.id = "nope"; }));
  CHECK(bad([](ProjectFile& p) { p.engineVersion = "1.0"; }));
  CHECK(bad([](ProjectFile& p) { p.engineVersion = "1.0.x"; }));
  CHECK(bad([](ProjectFile& p) { p.engineVersion = "1..0"; }));
  CHECK(bad([](ProjectFile& p) { p.entryScene = "scenes/other.minseen"; }));
  CHECK(bad([](ProjectFile& p) { p.scenes.clear(); }));  // entryScene set but no scenes
  CHECK(bad([](ProjectFile& p) { p.scenes.push_back("scenes/main.minseen"); }));
  CHECK(bad([](ProjectFile& p) { p.assetDirs.push_back("../outside"); }));
  CHECK(bad([](ProjectFile& p) { p.plugins.push_back({"plugins/extras", 1}); }));
  CHECK(bad([](ProjectFile& p) { p.plugins[0].abiVersion = 0; }));
  CHECK(bad([](ProjectFile& p) { p.build.keyId.clear(); }));  // encrypt without a key id
  CHECK(bad([](ProjectFile& p) { p.extra.set("name", "dup"); }));
}

TEST(Mindev, RelativePathRules) {
  for (const char* ok : {"a", "a/b", "scenes/main.minseen", "a b/c d", "한글/파일", "a.b/.hidden", "a..b"}) CHECK_MSG(isValidRelativePath(ok), ok);
  for (const char* no : {"", "/", "/abs", "a/", "a//b", ".", "..", "a/./b", "a/../b", "../a", "a\\b", "C:/x", "c:x", "a\nb", "a/\x01"}) CHECK_MSG(!isValidRelativePath(no), no);
  CHECK(!isValidRelativePath(std::string(1025, 'a')));
}

TEST(Mindev, UnknownKeysArePreservedAndKnownKeysCannotBeSmuggled) {
  auto r = parseMindev(R"({"format":"mindev","formatVersion":1,"name":"X","id":"123e4567-e89b-42d3-a456-426614174000","engineVersion":"1.0.0","futureThing":{"a":[1,2]},"zeta":1})");
  REQUIRE(r.ok());
  CHECK_EQ(r.value().extra.size(), 2u);
  const std::string out = serialize(r.value()).value();
  CHECK(out.find("futureThing") != std::string::npos);
  auto again = parseMindev(out);
  REQUIRE(again.ok());
  CHECK(json::Value(again.value().extra) == json::Value(r.value().extra));
}

TEST(Mindev, SaveIsDeterministicAndFilesRoundTrip) {
  TempDir dir;
  const auto path = dir.path / "game.mindev";
  const ProjectFile p = sample();
  REQUIRE(saveMindev(path, p).ok());
  auto bytes1 = fs::readTextFile(path).value();
  REQUIRE(saveMindev(path, p).ok());
  CHECK_EQ(fs::readTextFile(path).value(), bytes1);
  auto loaded = loadMindev(path);
  REQUIRE(loaded.ok());
  CHECK_EQ(loaded.value().name, p.name);
  // no temp files left behind
  int files = 0;
  for (auto& e : std::filesystem::directory_iterator(dir.path)) { (void)e; ++files; }
  CHECK_EQ(files, 1);
}

TEST(Mindev, FailedSaveLeavesTheOriginalIntact) {
  TempDir dir;
  const auto path = dir.path / "game.mindev";
  REQUIRE(saveMindev(path, sample()).ok());
  const std::string before = fs::readTextFile(path).value();
  ProjectFile broken = sample();
  broken.entryScene = "does/not/exist";
  CHECK(!saveMindev(path, broken).ok());  // validation fails before anything is written
  CHECK_EQ(fs::readTextFile(path).value(), before);
  CHECK(!saveMindev(dir.path / "no_such_dir" / "x.mindev", sample()).ok());
}

TEST(Mindev, LoadReportsMissingAndDamagedFiles) {
  TempDir dir;
  CHECK(loadMindev(dir.path / "missing.mindev").error().code == ErrorCode::NotFound);
  const auto path = dir.path / "bad.mindev";
  {
    std::ofstream f(path, std::ios::binary);
    f << "{ \"format\": \"mindev\", ";  // truncated
  }
  CHECK(loadMindev(path).error().code == ErrorCode::Corrupt);
  REQUIRE(saveMindev(path, sample()).ok());
  std::string text = fs::readTextFile(path).value();
  text.resize(text.size() / 2);
  { std::ofstream f(path, std::ios::binary); f << text; }
  CHECK(loadMindev(path).error().code == ErrorCode::Corrupt);
}

TEST(Mindev, AcceptsUtf8BomFromWindowsEditors) {
  const std::string text = "\xEF\xBB\xBF" + serialize(sample()).value();
  CHECK(parseMindev(text).ok());
}

TEST(Mindev, FuzzedFilesNeverCrash) {
  const std::string base = serialize(sample()).value();
  std::uint64_t s = 4242;
  auto rnd = [&]() { s = s * 6364136223846793005ull + 1442695040888963407ull; return static_cast<std::uint32_t>(s >> 33); };
  for (int i = 0; i < 20000; ++i) {
    std::string t = base;
    const auto n = 1 + rnd() % 4;
    for (std::uint32_t e = 0; e < n && !t.empty(); ++e) {
      const auto pos = rnd() % t.size();
      switch (rnd() % 3) {
        case 0: t[pos] = static_cast<char>(rnd() % 256); break;
        case 1: t.erase(pos, 1 + rnd() % 4); break;
        default: t.resize(pos); break;
      }
    }
    for (bool verify : {true, false}) {
      auto r = parseMindev(t, {.verifyIntegrity = verify});
      if (r.ok() && !validate(r.value()).ok()) { fail(__FILE__, __LINE__, "loader returned an invalid project"); return; }
    }
  }
}
