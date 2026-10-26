// SPDX-License-Identifier: GPL-2.0-only
#include "mingol/package/archive.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "mingol/core/fs.hpp"

namespace mingol::package {

namespace {

using crypto::Bytes;

constexpr std::uint8_t kMagic[8] = {0x89, 'M', 'I', 'N', 'G', 'O', 'L', 0x0A};
constexpr std::size_t kHeaderSize = 80;
constexpr std::size_t kAuthedHeader = 48;   // bytes covered by the header hash and used as AEAD associated data
constexpr std::uint32_t kFlagEncrypted = 1;
constexpr std::uint64_t kMaxTocSize = 256ull << 20;
constexpr std::uint8_t kExeMagic[8] = {0x89, 'M', 'I', 'N', 'E', 'X', 'E', 0x0A};
constexpr std::size_t kTrailerSize = 32;

Error corrupt(const std::string& m) { return Error(ErrorCode::Corrupt, ".mingol: " + m); }

void put32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
void put64(Bytes& b, std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
void put16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24); }
std::uint64_t get64(const std::uint8_t* p) { return std::uint64_t{get32(p)} | (std::uint64_t{get32(p + 4)} << 32); }

std::uint64_t chunkCount(std::uint64_t plain) { return plain == 0 ? 1 : (plain + kArchiveChunkSize - 1) / kArchiveChunkSize; }
std::uint64_t storedSizeOf(std::uint64_t plain, bool encrypted) { return encrypted ? plain + 16 * chunkCount(plain) : plain; }

struct Keys { crypto::Key256 toc{}, data{}; };

Result<Keys> deriveKeys(const ArchiveKey& key, std::span<const std::uint8_t> salt) {
  Keys k;
  const auto info = [](const char* s) { return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s), std::strlen(s)); };
  MINGOL_TRY_ASSIGN(Bytes t, crypto::hkdfSha256(key.bytes, salt, info("mingol.archive.v1.toc"), 32));
  MINGOL_TRY_ASSIGN(Bytes d, crypto::hkdfSha256(key.bytes, salt, info("mingol.archive.v1.data"), 32));
  std::copy(t.begin(), t.end(), k.toc.begin());
  std::copy(d.begin(), d.end(), k.data.begin());
  crypto::secureZero(t.data(), t.size());
  crypto::secureZero(d.data(), d.size());
  return k;
}

crypto::Nonce96 chunkNonce(std::uint32_t entry, std::uint64_t chunk) {
  crypto::Nonce96 n{};
  for (int i = 0; i < 4; ++i) n[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(entry >> (8 * i));
  for (int i = 0; i < 8; ++i) n[static_cast<std::size_t>(4 + i)] = static_cast<std::uint8_t>(chunk >> (8 * i));
  return n;
}
Bytes dataAad(std::span<const std::uint8_t> header48, bool last) {
  Bytes a(header48.begin(), header48.end());
  a.push_back('D');
  a.push_back(last ? 1 : 0);
  return a;
}
Bytes tocAad(std::span<const std::uint8_t> header48) {
  Bytes a(header48.begin(), header48.end());
  a.push_back('T');
  return a;
}

}  // namespace

// ---- ArchiveKey --------------------------------------------------------------------------------------------

Result<ArchiveKey> ArchiveKey::fromHex(std::string_view hex) {
  if (hex.size() != 64) return makeError(ErrorCode::InvalidArgument, "an archive key is 64 hexadecimal characters (32 bytes), got {}", hex.size());
  ArchiveKey k;
  auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
  for (std::size_t i = 0; i < 32; ++i) {
    const int a = nib(hex[2 * i]), b = nib(hex[2 * i + 1]);
    if (a < 0 || b < 0) return makeError(ErrorCode::InvalidArgument, "the archive key contains a non-hexadecimal character");
    k.bytes[i] = static_cast<std::uint8_t>(a * 16 + b);
  }
  return k;
}
Result<ArchiveKey> ArchiveKey::fromPassphrase(std::string_view passphrase, std::string_view salt, std::uint32_t iterations) {
  if (passphrase.empty()) return makeError(ErrorCode::InvalidArgument, "the passphrase must not be empty");
  if (iterations < 10000) return makeError(ErrorCode::InvalidArgument, "use at least 10000 PBKDF2 iterations");
  MINGOL_TRY_ASSIGN(Bytes d, crypto::pbkdf2HmacSha256(passphrase, {reinterpret_cast<const std::uint8_t*>(salt.data()), salt.size()}, iterations, 32));
  ArchiveKey k;
  std::copy(d.begin(), d.end(), k.bytes.begin());
  return k;
}
Result<ArchiveKey> ArchiveKey::generate() {
  ArchiveKey k;
  MINGOL_TRY(crypto::randomBytes(k.bytes));
  return k;
}
std::string ArchiveKey::toHex() const {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (std::uint8_t v : bytes) { s += d[v >> 4]; s += d[v & 15]; }
  return s;
}

// ---- writer ------------------------------------------------------------------------------------------------

Result<void> ArchiveWriter::add(std::string name, std::vector<std::uint8_t> data) {
  if (!isValidRelativePath(name)) return makeError(ErrorCode::InvalidArgument, "invalid archive entry name '{}'", name);
  if (name.size() > 0xFFFF) return makeError(ErrorCode::InvalidArgument, "archive entry name is too long");
  for (const auto& e : entries_) if (e.name == name) return makeError(ErrorCode::AlreadyExists, "archive entry '{}' was added twice", name);
  if (entries_.size() >= kMaxArchiveEntries) return makeError(ErrorCode::OutOfRange, "too many archive entries");
  entries_.push_back({std::move(name), std::move(data)});
  return {};
}
Result<void> ArchiveWriter::addFile(std::string name, const std::filesystem::path& file) {
  MINGOL_TRY_ASSIGN(Bytes data, fs::readFile(file));
  return add(std::move(name), std::move(data));
}

Result<std::vector<std::uint8_t>> ArchiveWriter::serialize(const WriteOptions& options) const {
  const bool enc = options.key.has_value();
  std::array<std::uint8_t, 16> salt{};
  if (enc) {
    if (options.salt) salt = *options.salt;
    else MINGOL_TRY(crypto::randomBytes(salt));
  }
  Keys keys;
  if (enc) { MINGOL_TRY_ASSIGN(keys, deriveKeys(*options.key, salt)); }

  // Provisional header (tocOffset/tocSize/hash are patched at the end; bytes 0..32 are final already, and
  // AAD only uses bytes 0..48, so the AAD is built from the final header below).
  // Compute layout first.
  std::uint64_t dataEnd = kHeaderSize;
  for (const auto& e : entries_) dataEnd += storedSizeOf(e.data.size(), enc);

  Bytes header;
  header.insert(header.end(), kMagic, kMagic + 8);
  put32(header, kMingolFormatVersion);
  put32(header, enc ? kFlagEncrypted : 0);
  header.insert(header.end(), salt.begin(), salt.end());
  put64(header, dataEnd);   // tocOffset
  // tocSize depends on names only (plaintext TOC length), computed next
  Bytes toc;
  put32(toc, static_cast<std::uint32_t>(entries_.size()));
  std::uint64_t offset = kHeaderSize;
  for (const auto& e : entries_) {
    put16(toc, static_cast<std::uint16_t>(e.name.size()));
    toc.insert(toc.end(), e.name.begin(), e.name.end());
    put64(toc, offset);
    put64(toc, storedSizeOf(e.data.size(), enc));
    put64(toc, e.data.size());
    const auto h = crypto::sha256(e.data);
    toc.insert(toc.end(), h.begin(), h.end());
    offset += storedSizeOf(e.data.size(), enc);
  }
  const std::uint64_t tocStored = toc.size() + (enc ? 16 : 0);
  put64(header, tocStored);
  MINGOL_VERIFY(header.size() == kAuthedHeader);

  Bytes out = header;
  out.resize(kHeaderSize);   // hash filled in below
  // entry data
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    const auto& d = entries_[i].data;
    if (!enc) { out.insert(out.end(), d.begin(), d.end()); continue; }
    const std::uint64_t n = chunkCount(d.size());
    for (std::uint64_t c = 0; c < n; ++c) {
      const std::size_t from = static_cast<std::size_t>(c * kArchiveChunkSize);
      const std::size_t len = std::min<std::size_t>(kArchiveChunkSize, d.size() - from);
      const std::size_t at = out.size();
      out.resize(at + len);
      const Bytes aad = dataAad(std::span(header).first(kAuthedHeader), c + 1 == n);
      const auto tag = crypto::aeadSeal(keys.data, chunkNonce(static_cast<std::uint32_t>(i), c), aad, std::span(d).subspan(from, len), std::span(out).subspan(at, len));
      out.insert(out.end(), tag.begin(), tag.end());
    }
  }
  MINGOL_VERIFY(out.size() == dataEnd);
  // TOC
  if (enc) {
    const std::size_t at = out.size();
    out.resize(at + toc.size());
    const Bytes aad = tocAad(std::span(header).first(kAuthedHeader));
    const auto tag = crypto::aeadSeal(keys.toc, crypto::Nonce96{}, aad, toc, std::span(out).subspan(at, toc.size()));
    out.insert(out.end(), tag.begin(), tag.end());
  } else {
    out.insert(out.end(), toc.begin(), toc.end());
  }
  // header hash over header[0..48) || stored TOC
  crypto::Sha256 h;
  h.update(std::span(header).first(kAuthedHeader));
  h.update(std::span(out).subspan(static_cast<std::size_t>(dataEnd)));
  const auto digest = h.finish();
  std::memcpy(out.data(), header.data(), kAuthedHeader);
  std::memcpy(out.data() + kAuthedHeader, digest.data(), 32);
  crypto::secureZero(keys.toc.data(), 32);
  crypto::secureZero(keys.data.data(), 32);
  return out;
}

Result<void> ArchiveWriter::save(const std::filesystem::path& path, const WriteOptions& options) const {
  MINGOL_TRY_ASSIGN(Bytes bytes, serialize(options));
  return fs::writeFileAtomic(path, bytes);
}

// ---- reader ------------------------------------------------------------------------------------------------

namespace {

// Random-access byte source over a memory buffer or a window of a file.
class Source {
 public:
  virtual ~Source() = default;
  virtual std::uint64_t size() const = 0;
  virtual Result<void> readAt(std::uint64_t offset, std::span<std::uint8_t> out) const = 0;
};

class MemorySource final : public Source {
 public:
  explicit MemorySource(Bytes b) : bytes_(std::move(b)) {}
  std::uint64_t size() const override { return bytes_.size(); }
  Result<void> readAt(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    if (offset > bytes_.size() || out.size() > bytes_.size() - offset) return corrupt("read past the end of the archive");
    if (!out.empty()) std::memcpy(out.data(), bytes_.data() + offset, out.size());
    return {};
  }

 private:
  Bytes bytes_;
};

class FileSource final : public Source {
 public:
  FileSource(std::ifstream f, std::uint64_t base, std::uint64_t size) : f_(std::move(f)), base_(base), size_(size) {}
  std::uint64_t size() const override { return size_; }
  Result<void> readAt(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    if (offset > size_ || out.size() > size_ - offset) return corrupt("read past the end of the archive");
    std::lock_guard<std::mutex> lock(mutex_);
    f_.clear();
    f_.seekg(static_cast<std::streamoff>(base_ + offset));
    f_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f_ || static_cast<std::size_t>(f_.gcount()) != out.size()) return makeError(ErrorCode::Io, ".mingol: short read from the archive file");
    return {};
  }

 private:
  mutable std::ifstream f_;
  mutable std::mutex mutex_;
  std::uint64_t base_, size_;
};

}  // namespace

struct ArchiveReader::Impl {
  std::shared_ptr<Source> src;
  bool encrypted = false;
  Bytes header48;
  Keys keys;
  std::vector<EntryInfo> entries;
  struct Rec { std::uint64_t offset, stored, plain; crypto::Sha256Digest hash; };
  std::vector<Rec> recs;
  std::unordered_map<std::string, std::size_t> index;
  ~Impl() { crypto::secureZero(keys.toc.data(), 32); crypto::secureZero(keys.data.data(), 32); }
};

ArchiveReader::ArchiveReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ArchiveReader::~ArchiveReader() = default;
ArchiveReader::ArchiveReader(ArchiveReader&&) noexcept = default;
ArchiveReader& ArchiveReader::operator=(ArchiveReader&&) noexcept = default;

namespace {

Result<void> parseArchive(ArchiveReader::Impl& m, const std::optional<ArchiveKey>& key) {
  const std::uint64_t total = m.src->size();
  if (total < kHeaderSize) return corrupt("the file is too small to be an archive");
  std::uint8_t header[kHeaderSize];
  MINGOL_TRY(m.src->readAt(0, header));
  if (std::memcmp(header, kMagic, 8) != 0) return corrupt("bad magic (not a .mingol archive)");
  const std::uint32_t version = get32(header + 8);
  if (version == 0) return corrupt("format version 0 is invalid");
  if (version > kMingolFormatVersion) {
    return makeError(ErrorCode::VersionMismatch, ".mingol: archive format version {} is newer than this engine supports ({}); update Mingol", version, kMingolFormatVersion);
  }
  const std::uint32_t flags = get32(header + 12);
  if ((flags & ~kFlagEncrypted) != 0) return corrupt("unknown flags set");
  m.encrypted = (flags & kFlagEncrypted) != 0;
  const std::uint64_t tocOffset = get64(header + 32), tocStored = get64(header + 40);
  if (tocOffset < kHeaderSize || tocStored > kMaxTocSize || tocStored < (m.encrypted ? 4u + 16u : 4u)) return corrupt("invalid table-of-contents location");
  if (tocOffset > total || tocStored != total - tocOffset) return corrupt("the file size does not match the header (truncated, or data appended)");
  if (m.encrypted && !key) return makeError(ErrorCode::InvalidArgument, ".mingol: this archive is encrypted; a key is required");

  Bytes toc(static_cast<std::size_t>(tocStored));
  MINGOL_TRY(m.src->readAt(tocOffset, toc));
  crypto::Sha256 h;
  h.update({header, kAuthedHeader});
  h.update(toc);
  const auto digest = h.finish();
  if (!crypto::constantTimeEqual(digest, {header + kAuthedHeader, 32})) {
    return corrupt("the header or table of contents is damaged (hash mismatch)");
  }
  m.header48.assign(header, header + kAuthedHeader);

  Bytes plainToc;
  if (m.encrypted) {
    MINGOL_TRY_ASSIGN(m.keys, deriveKeys(*key, {header + 16, 16}));
    plainToc.resize(toc.size() - 16);
    crypto::Tag128 tag;
    std::memcpy(tag.data(), toc.data() + plainToc.size(), 16);
    MINGOL_TRY(crypto::aeadOpen(m.keys.toc, crypto::Nonce96{}, tocAad(m.header48), {toc.data(), plainToc.size()}, tag, plainToc));
  } else {
    plainToc = std::move(toc);
  }

  // parse the table of contents with explicit bounds checks
  std::size_t pos = 0;
  auto need = [&](std::size_t n) { return plainToc.size() - pos >= n; };
  if (!need(4)) return corrupt("table of contents is truncated");
  const std::uint32_t count = get32(plainToc.data());
  pos = 4;
  if (count > kMaxArchiveEntries) return corrupt("too many entries");
  std::uint64_t expect = kHeaderSize;
  m.entries.reserve(count);
  m.recs.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    if (!need(2)) return corrupt("table of contents is truncated");
    const std::size_t nameLen = static_cast<std::size_t>(plainToc[pos]) | (static_cast<std::size_t>(plainToc[pos + 1]) << 8);
    pos += 2;
    if (!need(nameLen + 24 + 32)) return corrupt("table of contents is truncated");
    std::string name(reinterpret_cast<const char*>(plainToc.data() + pos), nameLen);
    pos += nameLen;
    ArchiveReader::Impl::Rec r{};
    r.offset = get64(plainToc.data() + pos);
    r.stored = get64(plainToc.data() + pos + 8);
    r.plain = get64(plainToc.data() + pos + 16);
    pos += 24;
    std::memcpy(r.hash.data(), plainToc.data() + pos, 32);
    pos += 32;
    if (!isValidRelativePath(name)) return corrupt("an entry has an invalid name");
    if (!m.index.emplace(name, m.recs.size()).second) return corrupt("duplicate entry '" + name + "'");
    if (r.offset != expect) return corrupt("entry '" + name + "' is not contiguous with the previous data");
    if (r.plain > tocOffset || storedSizeOf(r.plain, m.encrypted) != r.stored) return corrupt("entry '" + name + "' has inconsistent sizes");
    if (r.stored > tocOffset - expect) return corrupt("entry '" + name + "' extends past the data region");
    expect += r.stored;
    m.entries.push_back({std::move(name), r.plain, r.stored});
    m.recs.push_back(r);
  }
  if (pos != plainToc.size()) return corrupt("extra bytes after the table of contents");
  if (expect != tocOffset) return corrupt("the data region has unaccounted bytes");
  return {};
}

}  // namespace

Result<ArchiveReader> ArchiveReader::openMemory(std::vector<std::uint8_t> bytes, const std::optional<ArchiveKey>& key) {
  auto impl = std::make_unique<Impl>();
  impl->src = std::make_shared<MemorySource>(std::move(bytes));
  MINGOL_TRY(parseArchive(*impl, key));
  return ArchiveReader(std::move(impl));
}

namespace {
Result<std::ifstream> openStream(const std::filesystem::path& path, std::uint64_t& size) {
  std::error_code ec;
  size = std::filesystem::file_size(path, ec);
  if (ec) return makeError(ErrorCode::NotFound, "cannot open '{}': {}", path.string(), ec.message());
  std::ifstream f(path, std::ios::binary);
  if (!f) return makeError(ErrorCode::Io, "cannot open '{}'", path.string());
  return f;
}
}  // namespace

Result<ArchiveReader> ArchiveReader::openFile(const std::filesystem::path& path, const std::optional<ArchiveKey>& key) {
  std::uint64_t size = 0;
  MINGOL_TRY_ASSIGN(std::ifstream f, openStream(path, size));
  auto impl = std::make_unique<Impl>();
  impl->src = std::make_shared<FileSource>(std::move(f), 0, size);
  MINGOL_TRY(parseArchive(*impl, key));
  return ArchiveReader(std::move(impl));
}

namespace {
// Reads the trailer of an executable; returns archive offset and size.
Result<std::pair<std::uint64_t, std::uint64_t>> readTrailer(const std::filesystem::path& exe, std::uint64_t& fileSize) {
  MINGOL_TRY_ASSIGN(std::ifstream f, openStream(exe, fileSize));
  if (fileSize < kTrailerSize) return makeError(ErrorCode::NotFound, "'{}' has no embedded archive", exe.string());
  std::uint8_t t[kTrailerSize];
  f.seekg(static_cast<std::streamoff>(fileSize - kTrailerSize));
  f.read(reinterpret_cast<char*>(t), kTrailerSize);
  if (!f) return makeError(ErrorCode::Io, "cannot read '{}'", exe.string());
  if (std::memcmp(t, kExeMagic, 8) != 0) return makeError(ErrorCode::NotFound, "'{}' has no embedded archive", exe.string());
  const std::uint64_t off = get64(t + 8), size = get64(t + 16);
  if (get64(t + 24) != 0 || off > fileSize || size > fileSize - off || fileSize - off - size != kTrailerSize) {
    return corrupt("the executable trailer is inconsistent");
  }
  return std::make_pair(off, size);
}
}  // namespace

bool ArchiveReader::hasEmbeddedArchive(const std::filesystem::path& exe) {
  std::uint64_t fs = 0;
  return readTrailer(exe, fs).ok();
}

Result<ArchiveReader> ArchiveReader::openExecutable(const std::filesystem::path& exe, const std::optional<ArchiveKey>& key) {
  std::uint64_t fileSize = 0;
  MINGOL_TRY_ASSIGN(auto loc, readTrailer(exe, fileSize));
  std::uint64_t size2 = 0;
  MINGOL_TRY_ASSIGN(std::ifstream f, openStream(exe, size2));
  auto impl = std::make_unique<Impl>();
  impl->src = std::make_shared<FileSource>(std::move(f), loc.first, loc.second);
  MINGOL_TRY(parseArchive(*impl, key));
  return ArchiveReader(std::move(impl));
}

bool ArchiveReader::encrypted() const noexcept { return impl_->encrypted; }
const std::vector<EntryInfo>& ArchiveReader::entries() const noexcept { return impl_->entries; }
bool ArchiveReader::contains(std::string_view name) const { return impl_->index.count(std::string(name)) != 0; }

Result<std::vector<std::uint8_t>> ArchiveReader::read(std::string_view name) const {
  const Impl& m = *impl_;
  const auto it = m.index.find(std::string(name));
  if (it == m.index.end()) return makeError(ErrorCode::NotFound, "the archive has no entry '{}'", name);
  const std::size_t idx = it->second;
  const auto& r = m.recs[idx];
  Bytes stored(static_cast<std::size_t>(r.stored));
  MINGOL_TRY(m.src->readAt(r.offset, stored));
  Bytes plain;
  if (!m.encrypted) {
    plain = std::move(stored);
  } else {
    plain.resize(static_cast<std::size_t>(r.plain));
    const std::uint64_t n = chunkCount(r.plain);
    std::size_t in = 0, out = 0;
    for (std::uint64_t c = 0; c < n; ++c) {
      const std::size_t len = std::min<std::size_t>(kArchiveChunkSize, plain.size() - out);
      crypto::Tag128 tag;
      std::memcpy(tag.data(), stored.data() + in + len, 16);
      const Bytes aad = dataAad(m.header48, c + 1 == n);
      auto ok = crypto::aeadOpen(m.keys.data, chunkNonce(static_cast<std::uint32_t>(idx), c), aad, {stored.data() + in, len}, tag, {plain.data() + out, len});
      if (!ok.ok()) return Error(ErrorCode::AuthFailed, ".mingol: entry '" + std::string(name) + "' failed authentication (wrong key, or the data was modified)");
      in += len + 16;
      out += len;
    }
  }
  if (!crypto::constantTimeEqual(crypto::sha256(plain), r.hash)) return corrupt("entry '" + std::string(name) + "' does not match its recorded hash");
  return plain;
}

Result<void> ArchiveReader::verifyAll() const {
  for (const auto& e : impl_->entries) {
    auto r = read(e.name);
    if (!r.ok()) return r.error();
  }
  return {};
}

// ---- executable packaging ----------------------------------------------------------------------------------

Result<void> packExecutable(const std::filesystem::path& exe, const std::filesystem::path& archive, const std::filesystem::path& output) {
  if (ArchiveReader::hasEmbeddedArchive(exe)) return makeError(ErrorCode::AlreadyExists, "'{}' already has an embedded archive", exe.string());
  MINGOL_TRY_ASSIGN(Bytes exeBytes, fs::readFile(exe));
  MINGOL_TRY_ASSIGN(Bytes arc, fs::readFile(archive));
  // The archive must be valid before we glue it to an executable (the key is not needed to check the structure).
  {
    std::uint8_t dummy = 0;
    (void)dummy;
    if (arc.size() < kHeaderSize || std::memcmp(arc.data(), kMagic, 8) != 0) return makeError(ErrorCode::InvalidArgument, "'{}' is not a .mingol archive", archive.string());
  }
  Bytes out = std::move(exeBytes);
  const std::uint64_t off = out.size();
  out.insert(out.end(), arc.begin(), arc.end());
  out.insert(out.end(), kExeMagic, kExeMagic + 8);
  put64(out, off);
  put64(out, arc.size());
  put64(out, 0);
  MINGOL_TRY(fs::writeFileAtomic(output, out));
  std::error_code ec;
  const auto perms = std::filesystem::status(exe, ec).permissions();
  if (!ec) std::filesystem::permissions(output, perms, ec);   // best effort: keeps the executable bit
  return {};
}

// ---- project packing ---------------------------------------------------------------------------------------

Result<std::vector<std::uint8_t>> packProject(const std::filesystem::path& mindevPath, const ProjectPackOptions& options, ProjectPackReport* report) {
  namespace stdfs = std::filesystem;
  MINGOL_TRY_ASSIGN(ProjectFile project, loadMindev(mindevPath.string()));
  const stdfs::path root = mindevPath.parent_path().empty() ? stdfs::path(".") : mindevPath.parent_path();
  std::error_code ec;
  const stdfs::path canonRoot = stdfs::weakly_canonical(root, ec);
  if (ec) return makeError(ErrorCode::Io, "cannot resolve the project directory: {}", ec.message());

  ProjectPackReport local;
  ProjectPackReport& rep = report != nullptr ? *report : local;
  std::vector<std::pair<std::string, stdfs::path>> files;   // entry name -> file
  std::unordered_set<std::string> seen;
  auto addFileEntry = [&](const std::string& name, const stdfs::path& p) {
    if (seen.insert(name).second) files.emplace_back(name, p);
  };
  auto inside = [&](const stdfs::path& p) {
    std::error_code e2;
    const stdfs::path c = stdfs::weakly_canonical(p, e2);
    if (e2) return false;
    auto [a, b] = std::mismatch(canonRoot.begin(), canonRoot.end(), c.begin(), c.end());
    return a == canonRoot.end();
  };
  auto addDir = [&](const std::string& rel) -> Result<void> {
    const stdfs::path dir = root / rel;
    if (!stdfs::exists(dir, ec)) return {};   // an empty/missing directory is fine
    if (!inside(dir)) return makeError(ErrorCode::InvalidArgument, "'{}' resolves outside the project directory", rel);
    for (stdfs::recursive_directory_iterator it(dir, stdfs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
      const stdfs::path& p = it->path();
      if (it->is_symlink(ec)) { rep.skipped.push_back(p.generic_string() + " (symbolic link)"); continue; }
      if (!it->is_regular_file(ec)) continue;
      const std::string name = stdfs::relative(p, root, ec).generic_string();
      if (ec || !isValidRelativePath(name)) { rep.skipped.push_back(p.generic_string() + " (unusable name)"); continue; }
      addFileEntry(name, p);
    }
    if (ec) return makeError(ErrorCode::Io, "cannot scan '{}': {}", dir.string(), ec.message());
    return {};
  };
  for (const auto& d : project.assetDirs) MINGOL_TRY(addDir(d));
  for (const auto& d : project.scriptDirs) MINGOL_TRY(addDir(d));
  for (const auto& s : project.scenes) {
    const stdfs::path p = root / s;
    if (!stdfs::is_regular_file(p, ec)) return makeError(ErrorCode::NotFound, "scene '{}' listed in the project does not exist", s);
    if (!inside(p)) return makeError(ErrorCode::InvalidArgument, "scene '{}' resolves outside the project directory", s);
    addFileEntry(s, p);
  }
  std::sort(files.begin(), files.end());
  ArchiveWriter w;
  MINGOL_TRY_ASSIGN(Bytes proj, fs::readFile(mindevPath));
  MINGOL_TRY(w.add("project.mindev", std::move(proj)));
  rep.files = 1;
  for (const auto& [name, p] : files) {
    if (name == "project.mindev") return makeError(ErrorCode::AlreadyExists, "a project file named 'project.mindev' collides with the packed project entry");
    MINGOL_TRY_ASSIGN(Bytes data, fs::readFile(p));
    rep.bytes += data.size();
    MINGOL_TRY(w.add(name, std::move(data)));
    ++rep.files;
  }
  return w.serialize(options.write);
}

}  // namespace mingol::package
