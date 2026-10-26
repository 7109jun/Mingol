// SPDX-License-Identifier: GPL-2.0-only
// .minseen: malformed input, all-or-nothing loading, corruption and fuzzing.
#include <random>

#include "sceneio_util.hpp"

using namespace mingol;
using namespace mingol::test;
using namespace mingol::sceneio;

namespace {
struct Env {
  std::unique_ptr<scripting::ScriptHost> host;
  AssetTable assets;
  ComponentRegistry registry = ComponentRegistry::withBuiltins();
  SceneIoContext ctx;
  Env() {
    auto h = scripting::ScriptHost::create({{"s.mins", "class Hero extends Script { public int hp = 1; public string name = \"x\"; }"}});
    REQUIRE(h.ok());
    host = std::move(h).value();
    auto c = std::make_shared<assets::AudioClip>();
    c->sampleRate = 48000; c->channels = 1; c->samples.assign(48, 0.0F);
    assets.addAudioClip("a", c);
    ctx.assets = &assets;
    ctx.scripts = host.get();
  }
};

// A small valid document: 3 objects, several component kinds.
std::string validDoc(Env& e, bool pretty = false) {
  scene::Scene s;
  auto* a = s.createObject("a");
  a->transform().setLocalPosition({1, 2, 3});
  a->addComponent<scene::CameraComponent>();
  auto* b = s.createObject("b", a);
  b->addComponent<scene::LightComponent>()->intensity = 2.0F;
  auto* c = s.createObject("c");
  if (!e.host->addScript(*c, "Hero").ok()) { mingol::test::fail(__FILE__, __LINE__, "addScript failed"); return {}; }
  c->addComponent<runtime::Rigidbody>();
  runtime::Collider::addSphere(*c, 0.5F);
  SaveOptions so;
  so.pretty = pretty;
  auto t = saveScene(s, e.ctx, e.registry, so);
  if (!t.ok()) { mingol::test::fail(__FILE__, __LINE__, "saveScene failed: " + t.error().message); return {}; }
  return t.value();
}

std::string replaceOnce(std::string s, const std::string& from, const std::string& to) {
  const auto p = s.find(from);
  if (p == std::string::npos) { mingol::test::fail(__FILE__, __LINE__, "replaceOnce: '" + from + "' not found"); return s; }
  s.replace(p, from.size(), to);
  return s;
}

LoadOptions opts(bool verify = true, bool skipUnknown = false, bool replace = false, std::size_t maxObjects = 1u << 20) {
  LoadOptions o;
  o.verifyIntegrity = verify; o.skipUnknownComponents = skipUnknown; o.replaceExisting = replace; o.maxObjects = maxObjects;
  return o;
}

// Loading must fail AND leave the scene exactly as it was.
void expectRejected(Env& e, const std::string& text, ErrorCode code, const char* needle = nullptr, LoadOptions opt = {}) {
  scene::Scene s;
  s.createObject("pre-existing");
  const Census before;
  auto r = loadScene(s, text, e.ctx, e.registry, opt);
  CHECK_MSG(!r.ok(), "the document was accepted");
  if (r.ok()) return;
  CHECK_MSG(r.error().code == code, std::string("wrong error code: ") + r.error().describe());
  if (needle != nullptr) CHECK_MSG(r.error().message.find(needle) != std::string::npos, "message lacks '" + std::string(needle) + "': " + r.error().message);
  s.flushDestroyed();
  CHECK_EQ(s.objectCount(), std::size_t{1});
  CHECK(before == Census{});
}
}  // namespace

TEST(SceneIoErrors, NotJsonAndWrongShapes) {
  Env e;
  expectRejected(e, "", ErrorCode::Corrupt);
  expectRejected(e, "not json", ErrorCode::Corrupt);
  expectRejected(e, "[]", ErrorCode::Corrupt);
  expectRejected(e, "{}", ErrorCode::Corrupt, "format");
  expectRejected(e, "{\"format\":\"mindev\",\"formatVersion\":1,\"objects\":[]}", ErrorCode::Corrupt, "minseen");
  expectRejected(e, "{\"format\":\"minseen\",\"objects\":[]}", ErrorCode::Corrupt, "formatVersion");
  expectRejected(e, "{\"format\":\"minseen\",\"formatVersion\":0,\"objects\":[]}", ErrorCode::Corrupt);
  expectRejected(e, "{\"format\":\"minseen\",\"formatVersion\":1}", ErrorCode::Corrupt, "objects");
  expectRejected(e, "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":{}}", ErrorCode::Corrupt, "objects");
  expectRejected(e, "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[1]}", ErrorCode::Corrupt, "objects[0]");
}

TEST(SceneIoErrors, NewerVersionIsRejectedClearly) {
  Env e;
  expectRejected(e, "{\"format\":\"minseen\",\"formatVersion\":2,\"objects\":[]}", ErrorCode::VersionMismatch, "newer");
}

TEST(SceneIoErrors, IntegrityMismatchAndOptOut) {
  Env e;
  const std::string good = validDoc(e);
  scene::Scene ok;
  REQUIRE(loadScene(ok, good, e.ctx, e.registry).ok());
  const std::string tampered = replaceOnce(good, "\"name\":\"a\"", "\"name\":\"z\"");
  expectRejected(e, tampered, ErrorCode::Corrupt, "integrity");
  scene::Scene s;
  CHECK(loadScene(s, tampered, e.ctx, e.registry, opts(false)).ok());
  CHECK(s.find("z") != nullptr);
  // a document without an integrity field is accepted (hand-written files)
  const auto p = good.rfind(",\"integrity\"");
  REQUIRE(p != std::string::npos);
  scene::Scene s2;
  CHECK(loadScene(s2, good.substr(0, p) + "}", e.ctx, e.registry).ok());
}

TEST(SceneIoErrors, ObjectStructureErrors) {
  Env e;
  const std::string head = "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[";
  const std::string tail = "]}";
  expectRejected(e, head + "{\"id\":0}" + tail, ErrorCode::Corrupt, "id");
  expectRejected(e, head + "{\"id\":1},{\"id\":1}" + tail, ErrorCode::Corrupt, "twice");
  expectRejected(e, head + "{\"id\":1,\"parent\":2},{\"id\":2}" + tail, ErrorCode::Corrupt, "earlier");
  expectRejected(e, head + "{\"id\":1,\"parent\":1}" + tail, ErrorCode::Corrupt, "earlier");
  expectRejected(e, head + "{\"id\":1,\"name\":5}" + tail, ErrorCode::Corrupt, "name");
  expectRejected(e, head + "{\"id\":1,\"position\":[1,2]}" + tail, ErrorCode::Corrupt, "position");
  expectRejected(e, head + "{\"id\":1,\"position\":[1,2,\"x\"]}" + tail, ErrorCode::Corrupt, "position");
  expectRejected(e, head + "{\"id\":1,\"position\":[1,2,1e999]}" + tail, ErrorCode::Corrupt);
  expectRejected(e, head + "{\"id\":1,\"rotation\":[0,0,0,0]}" + tail, ErrorCode::Corrupt, "zero quaternion");
  expectRejected(e, head + "{\"id\":1,\"active\":1}" + tail, ErrorCode::Corrupt, "active");
  expectRejected(e, head + "{\"id\":1,\"components\":{}}" + tail, ErrorCode::Corrupt, "components");
  expectRejected(e, head + "{\"id\":1,\"components\":[{\"data\":{}}]}" + tail, ErrorCode::Corrupt, "type");
  expectRejected(e, head + "{\"id\":1,\"components\":[{\"type\":\"Camera\"}]}" + tail, ErrorCode::Corrupt, "data");
  expectRejected(e, head + "{\"id\":1},{\"id\":2}" + tail, ErrorCode::Corrupt, "limit", opts(true, false, false, 1));
}

TEST(SceneIoErrors, UnknownComponentTypes) {
  Env e;
  const std::string doc = "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[{\"id\":1,\"name\":\"o\",\"components\":[{\"type\":\"Teleporter\",\"data\":{}}]}]}";
  expectRejected(e, doc, ErrorCode::Unsupported, "Teleporter");
  scene::Scene s;
  auto r = loadScene(s, doc, e.ctx, e.registry, opts(true, true));
  REQUIRE(r.ok());
  CHECK_EQ(r.value().warnings.size(), std::size_t{1});
  CHECK_EQ(r.value().componentsCreated, std::size_t{0});
  CHECK_EQ(s.objectCount(), std::size_t{1});
}

TEST(SceneIoErrors, InvalidComponentData) {
  Env e;
  auto doc = [](const std::string& comp) {
    return "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[{\"id\":1,\"name\":\"ok\"},{\"id\":2,\"name\":\"o\",\"components\":[" + comp + "]}]}";
  };
  expectRejected(e, doc("{\"type\":\"Camera\",\"data\":{\"fovY\":0}}"), ErrorCode::Corrupt, "fovY");
  expectRejected(e, doc("{\"type\":\"Camera\",\"data\":{\"near\":2,\"far\":1}}"), ErrorCode::Corrupt, "near");
  expectRejected(e, doc("{\"type\":\"Camera\",\"data\":[]}"), ErrorCode::Corrupt);
  expectRejected(e, doc("{\"type\":\"Light\",\"data\":{\"type\":\"laser\"}}"), ErrorCode::Corrupt, "type");
  expectRejected(e, doc("{\"type\":\"Light\",\"data\":{\"intensity\":-1}}"), ErrorCode::Corrupt, "intensity");
  expectRejected(e, doc("{\"type\":\"Rigidbody\",\"data\":{\"type\":\"dynamic\",\"mass\":0}}"), ErrorCode::Corrupt, "mass");
  expectRejected(e, doc("{\"type\":\"Rigidbody\",\"data\":{\"layer\":-1}}"), ErrorCode::Corrupt, "32 bits");
  expectRejected(e, doc("{\"type\":\"Rigidbody\",\"data\":{\"lockRotation\":[true]}}"), ErrorCode::Corrupt, "lockRotation");
  expectRejected(e, doc("{\"type\":\"Collider\",\"data\":{\"shape\":\"cone\"}}"), ErrorCode::Corrupt, "shape");
  expectRejected(e, doc("{\"type\":\"Collider\",\"data\":{\"shape\":\"sphere\",\"radius\":-1}}"), ErrorCode::Corrupt, "radius");
  expectRejected(e, doc("{\"type\":\"Collider\",\"data\":{\"shape\":\"mesh\",\"mesh\":\"missing\"}}"), ErrorCode::NotFound, "missing");
  expectRejected(e, doc("{\"type\":\"AudioSource\",\"data\":{\"clip\":\"nope\"}}"), ErrorCode::NotFound, "nope");
  expectRejected(e, doc("{\"type\":\"AudioSource\",\"data\":{\"pitch\":0}}"), ErrorCode::Corrupt);
  expectRejected(e, doc("{\"type\":\"AudioSource\",\"data\":{\"pan\":2}}"), ErrorCode::Corrupt);
  expectRejected(e, doc("{\"type\":\"Script\",\"data\":{}}"), ErrorCode::Corrupt, "class");
  expectRejected(e, doc("{\"type\":\"Script\",\"data\":{\"class\":\"Nope\"}}"), ErrorCode::InvalidArgument, "Nope");
  expectRejected(e, doc("{\"type\":\"Script\",\"data\":{\"class\":\"Hero\",\"fields\":[]}}"), ErrorCode::Corrupt, "fields");
  expectRejected(e, doc("{\"type\":\"Animator\",\"data\":{\"skeleton\":\"none\"}}"), ErrorCode::NotFound, "none");
  expectRejected(e, doc("{\"type\":\"MeshRenderer\",\"data\":{\"mesh\":\"m\"}}"), ErrorCode::NotFound, "mesh");
}

TEST(SceneIoErrors, MissingServicesAreReported) {
  Env e;
  const std::string withScript = "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[{\"id\":1,\"components\":[{\"type\":\"Script\",\"data\":{\"class\":\"Hero\"}}]}]}";
  SceneIoContext none;
  scene::Scene s;
  auto r = loadScene(s, withScript, none, e.registry);
  CHECK(!r.ok());
  CHECK(r.error().message.find("ScriptHost") != std::string::npos);
  CHECK_EQ(s.objectCount(), std::size_t{0});
  const std::string withClip = "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[{\"id\":1,\"components\":[{\"type\":\"AudioSource\",\"data\":{\"clip\":\"a\"}}]}]}";
  auto r2 = loadScene(s, withClip, none, e.registry);
  CHECK(!r2.ok());
  CHECK(r2.error().message.find("AssetTable") != std::string::npos);
}

TEST(SceneIoErrors, UnknownFieldsInScriptsBecomeWarnings) {
  Env e;
  const std::string doc =
      "{\"format\":\"minseen\",\"formatVersion\":1,\"objects\":[{\"id\":1,\"components\":[{\"type\":\"Script\",\"data\":{\"class\":\"Hero\","
      "\"fields\":{\"hp\":9,\"gone\":1,\"name\":5}}}]}]}";
  scene::Scene s;
  auto r = loadScene(s, doc, e.ctx, e.registry);
  REQUIRE(r.ok());
  CHECK_EQ(r.value().warnings.size(), std::size_t{2});   // unknown field + wrong type
  scene::GameObject* go = s.rootObjects().front();
  CHECK_EQ(go->getComponent<scripting::ScriptComponent>()->getInt("hp").value(), std::int64_t{9});
  CHECK_EQ(go->getComponent<scripting::ScriptComponent>()->getString("name").value(), "x");
}

TEST(SceneIoErrors, FailureInTheLastObjectUndoesEverything) {
  Env e;
  std::string doc = validDoc(e);
  doc = replaceOnce(doc, "\"mass\":1", "\"mass\":-5");   // the Rigidbody is on the last object
  scene::Scene s;
  const Census before;
  auto r = loadScene(s, doc, e.ctx, e.registry, opts(false));
  REQUIRE(!r.ok());
  s.flushDestroyed();
  CHECK_EQ(s.objectCount(), std::size_t{0});
  CHECK(before == Census{});
}

TEST(SceneIoErrors, SaveFailures) {
  Env e;
  struct Odd final : scene::Component { const char* typeName() const override { return "Odd"; } };
  scene::Scene a;
  a.createObject("o")->addComponent<Odd>();
  auto r = saveScene(a, e.ctx, e.registry);
  REQUIRE(!r.ok());
  CHECK(r.error().message.find("Odd") != std::string::npos);

  scene::Scene b;
  auto clip = std::make_shared<assets::AudioClip>();   // never registered
  b.createObject("o")->addComponent<runtime::AudioSource>()->clip = clip;
  auto r2 = saveScene(b, e.ctx, e.registry);
  REQUIRE(!r2.ok());
  CHECK(r2.error().code == ErrorCode::NotFound);
  SceneIoContext noAssets;
  auto r3 = saveScene(b, noAssets, e.registry);
  CHECK(!r3.ok());

  scene::Scene c;
  c.createObject("o")->transform().setLocalPosition({0, 0, 0});
  // NaN positions cannot be represented in JSON
  c.find("o")->transform().setLocalPosition({std::nanf(""), 0, 0});
  CHECK(!saveScene(c, e.ctx, e.registry).ok());
}

TEST(SceneIoErrors, MutationFuzzNeverCrashesAndNeverLeaks) {
  Env e;
  const std::string base = validDoc(e, true);
  std::mt19937 rng(777);
  const char* seedEnv = std::getenv("MINGOL_FUZZ_SEED");
  if (seedEnv) rng.seed(static_cast<unsigned>(std::atoi(seedEnv)));
  const char* itEnv = std::getenv("MINGOL_FUZZ_ITERS");
  const int iterations = itEnv ? std::atoi(itEnv) : 3000;
  const std::string tokens[] = {"null", "-1", "1e999", "[", "]", "{", "}", "\"", ",", ":", "true", "0", "99999999999999999999", "\"Script\"", "\"\\u0000\""};
  std::size_t accepted = 0, rejected = 0;
  for (int iter = 0; iter < iterations; ++iter) {
    std::string s = base;
    const int edits = 1 + static_cast<int>(rng() % 3);
    for (int k = 0; k < edits; ++k) {
      const std::size_t pos = rng() % s.size();
      switch (rng() % 4) {
        case 0: s.erase(pos, 1 + rng() % 8); break;
        case 1: s.insert(pos, tokens[rng() % (sizeof(tokens) / sizeof(tokens[0]))]); break;
        case 2: s[pos] = static_cast<char>(rng() % 256); break;
        default: s.resize(pos); break;   // truncation
      }
      if (s.empty()) s = "x";
    }
    scene::Scene sc;
    sc.createObject("pre");
    const Census before;
    const bool skipIntegrity = (iter % 2) == 0;   // reach the deeper validation half of the time
    auto r = loadScene(sc, s, e.ctx, e.registry, opts(!skipIntegrity));
    sc.flushDestroyed();
    if (r.ok()) {
      ++accepted;
      CHECK(sc.objectCount() >= 1);
      // whatever was accepted must also be saveable and reloadable
      auto again = saveScene(sc, e.ctx, e.registry);
      if (again.ok()) {
        scene::Scene sc2;
        CHECK_MSG(loadScene(sc2, again.value(), e.ctx, e.registry).ok(), "a saved scene must load again");
      }
    } else {
      ++rejected;
      CHECK_EQ(sc.objectCount(), std::size_t{1});
      CHECK(before == Census{});
    }
  }
  CHECK(rejected > 100);
  (void)accepted;
}
