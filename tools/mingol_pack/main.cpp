// SPDX-License-Identifier: GPL-2.0-only
// mingol-pack: command line tool for .mingol archives.
//
//   mingol-pack genkey
//   mingol-pack pack    <project.mindev> -o <out.mingol> [key options]
//   mingol-pack embed   <game-executable> <archive.mingol> -o <packed-executable>
//   mingol-pack list    <archive|packed-exe> [key options]
//   mingol-pack verify  <archive|packed-exe> [key options]
//   mingol-pack extract <archive|packed-exe> <dest-dir> [key options]
//
// Key options (keys are never accepted as plain command-line arguments, which would show up in process lists):
//   --key-file <file>        file containing the 64-hex-digit key
//   --key-env <VAR>          environment variable containing the 64-hex-digit key
//   --passphrase-env <VAR> --salt <text>   derive the key with PBKDF2-HMAC-SHA256 (200000 iterations)
// Exit status: 0 success, 1 failure, 2 usage error.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "mingol/core/fs.hpp"
#include "mingol/package/archive.hpp"

using namespace mingol;
using namespace mingol::package;
namespace stdfs = std::filesystem;

namespace {

int usage() {
  std::fputs(
      "usage:\n"
      "  mingol-pack genkey\n"
      "  mingol-pack pack    <project.mindev> -o <out.mingol> [key options]\n"
      "  mingol-pack embed   <game-executable> <archive.mingol> -o <packed-executable>\n"
      "  mingol-pack list    <archive|packed-exe> [key options]\n"
      "  mingol-pack verify  <archive|packed-exe> [key options]\n"
      "  mingol-pack extract <archive|packed-exe> <dest-dir> [key options]\n"
      "key options: --key-file <file> | --key-env <VAR> | --passphrase-env <VAR> --salt <text>\n",
      stderr);
  return 2;
}

struct Args {
  std::vector<std::string> positional;
  std::string out, keyFile, keyEnv, passEnv, salt;
};

bool parse(int argc, char** argv, int from, Args& a, std::string& err) {
  for (int i = from; i < argc; ++i) {
    const std::string s = argv[i];
    auto value = [&](std::string& dst) {
      if (i + 1 >= argc) { err = s + " needs a value"; return false; }
      dst = argv[++i];
      return true;
    };
    if (s == "-o") { if (!value(a.out)) return false; }
    else if (s == "--key-file") { if (!value(a.keyFile)) return false; }
    else if (s == "--key-env") { if (!value(a.keyEnv)) return false; }
    else if (s == "--passphrase-env") { if (!value(a.passEnv)) return false; }
    else if (s == "--salt") { if (!value(a.salt)) return false; }
    else if (!s.empty() && s[0] == '-' && s.size() > 1) { err = "unknown option " + s; return false; }
    else a.positional.push_back(s);
  }
  return true;
}

Result<std::optional<ArchiveKey>> loadKey(const Args& a) {
  const int given = (!a.keyFile.empty()) + (!a.keyEnv.empty()) + (!a.passEnv.empty());
  if (given == 0) return std::optional<ArchiveKey>();
  if (given > 1) return makeError(ErrorCode::InvalidArgument, "give only one of --key-file, --key-env, --passphrase-env");
  if (!a.keyFile.empty()) {
    MINGOL_TRY_ASSIGN(std::string text, fs::readTextFile(a.keyFile, 4096));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    MINGOL_TRY_ASSIGN(ArchiveKey k, ArchiveKey::fromHex(text));
    return std::optional<ArchiveKey>(k);
  }
  if (!a.keyEnv.empty()) {
    const char* v = std::getenv(a.keyEnv.c_str());
    if (v == nullptr) return makeError(ErrorCode::NotFound, "environment variable {} is not set", a.keyEnv);
    MINGOL_TRY_ASSIGN(ArchiveKey k, ArchiveKey::fromHex(v));
    return std::optional<ArchiveKey>(k);
  }
  const char* pw = std::getenv(a.passEnv.c_str());
  if (pw == nullptr) return makeError(ErrorCode::NotFound, "environment variable {} is not set", a.passEnv);
  if (a.salt.empty()) return makeError(ErrorCode::InvalidArgument, "--passphrase-env needs --salt");
  MINGOL_TRY_ASSIGN(ArchiveKey k, ArchiveKey::fromPassphrase(pw, a.salt));
  return std::optional<ArchiveKey>(k);
}

Result<ArchiveReader> openAny(const std::string& path, const std::optional<ArchiveKey>& key) {
  auto direct = ArchiveReader::openFile(path, key);
  if (direct.ok()) return direct;
  if (ArchiveReader::hasEmbeddedArchive(path)) return ArchiveReader::openExecutable(path, key);
  return direct;
}

int fail(const Error& e) {
  std::fprintf(stderr, "error: %s\n", e.message.c_str());
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  Args a;
  std::string err;
  if (!parse(argc, argv, 2, a, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }

  if (cmd == "genkey") {
    auto k = ArchiveKey::generate();
    if (!k.ok()) return fail(k.error());
    std::printf("%s\n", k.value().toHex().c_str());
    return 0;
  }
  auto key = loadKey(a);
  if (!key.ok()) return fail(key.error());

  if (cmd == "pack") {
    if (a.positional.size() != 1 || a.out.empty()) return usage();
    ProjectPackOptions opt;
    opt.write.key = key.value();
    ProjectPackReport rep;
    auto bytes = packProject(a.positional[0], opt, &rep);
    if (!bytes.ok()) return fail(bytes.error());
    auto w = fs::writeFileAtomic(a.out, bytes.value());
    if (!w.ok()) return fail(w.error());
    for (const auto& s : rep.skipped) std::fprintf(stderr, "skipped: %s\n", s.c_str());
    std::printf("packed %zu files (%llu bytes of content) into %s%s\n", rep.files, static_cast<unsigned long long>(rep.bytes), a.out.c_str(),
                key.value() ? " [encrypted]" : "");
    return 0;
  }
  if (cmd == "embed") {
    if (a.positional.size() != 2 || a.out.empty()) return usage();
    auto r = packExecutable(a.positional[0], a.positional[1], a.out);
    if (!r.ok()) return fail(r.error());
    std::printf("wrote %s\n", a.out.c_str());
    return 0;
  }
  if (cmd == "list" || cmd == "verify" || cmd == "extract") {
    if (a.positional.empty() || (cmd == "extract" ? a.positional.size() != 2 : a.positional.size() != 1)) return usage();
    auto r = openAny(a.positional[0], key.value());
    if (!r.ok()) return fail(r.error());
    if (cmd == "list") {
      std::printf("%s archive, %zu entries\n", r.value().encrypted() ? "encrypted" : "plain", r.value().entries().size());
      for (const auto& e : r.value().entries()) std::printf("%12llu  %s\n", static_cast<unsigned long long>(e.size), e.name.c_str());
      return 0;
    }
    if (cmd == "verify") {
      auto v = r.value().verifyAll();
      if (!v.ok()) return fail(v.error());
      std::printf("ok: %zu entries verified\n", r.value().entries().size());
      return 0;
    }
    const stdfs::path dest = a.positional[1];
    std::error_code ec;
    stdfs::create_directories(dest, ec);
    const stdfs::path destAbs = stdfs::weakly_canonical(dest, ec);
    for (const auto& e : r.value().entries()) {
      auto data = r.value().read(e.name);
      if (!data.ok()) return fail(data.error());
      const stdfs::path target = destAbs / e.name;
      const stdfs::path canon = stdfs::weakly_canonical(target, ec);
      if (ec || canon.string().rfind(destAbs.string(), 0) != 0) return fail(Error(ErrorCode::InvalidArgument, "entry '" + e.name + "' would be written outside the destination"));
      stdfs::create_directories(target.parent_path(), ec);
      auto w = fs::writeFileAtomic(target, data.value());
      if (!w.ok()) return fail(w.error());
    }
    std::printf("extracted %zu files to %s\n", r.value().entries().size(), dest.string().c_str());
    return 0;
  }
  return usage();
}
