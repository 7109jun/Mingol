# API: package (`mingol/package/{mindev,archive,crypto}.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/package` · 타깃 `Mingol::Package` · 네임스페이스 `mingol::package`. 의존: `core`.
**`.mindev` 프로젝트 파일**, **`.mingol` 아카이브**(읽기 전용 패키지, 선택적 암호화), 그리고 아카이브가 쓰는 **자체 구현 암호 프리미티브**. 아카이브 레이아웃 전체는 `docs/MINGOL.md`(있는 경우)와 이 문서를 따른다. 보안 모델은 [../SECURITY.md](../SECURITY.md).
**스레딩**: 전역 상태 없음. 서로 다른 객체는 동시에 사용 가능; 한 `ArchiveWriter`/`ArchiveReader`를 여러 스레드가 동시에 변경하지 말 것.

## mindev.hpp — `.mindev`
UTF-8 **JSON 객체 하나**. 사람, diff, VCS가 읽고 병합하도록 **압축·암호화하지 않는다**.
```json
{ "format": "mindev", "formatVersion": 1, "name": "My Game", "id": "<uuid>", "engineVersion": "0.1.0",
  "entryScene": "scenes/main.minseen", "scenes": ["scenes/main.minseen"],
  "assetDirs": ["assets"], "scriptDirs": ["scripts"],
  "plugins": [{"path": "plugins/physics_extras", "abiVersion": 1}],
  "settings": {}, "build": {"singleExe": false, "compress": false, "encrypt": false, "keyId": ""},
  "integrity": "crc32:0a1b2c3d" }
```
**규칙**
- `integrity` = 문서에서 `integrity` 키를 뺀 **compact JSON 직렬화의 CRC-32**. 공백·서식 변경은 무관, 값 변경은 감지. `integrity`가 없으면(손으로 쓴 파일) 허용; **있는데 틀리면 오류**(`LoadOptions::verifyIntegrity=false`가 아닌 한).
- `formatVersion`은 정수. **더 새로운 포맷은 추측하지 않고 `VersionMismatch`로 거부**. 알 수 없는 최상위 키는 `ProjectFile::extra`에 보존되어 다시 저장된다.
- 저장은 **크래시 안전**(임시 파일 + rename).

타입/상수: `kMindevFormatVersion = 1`, `kMaxProjectNameLength = 128`.
| 타입 | 필드 |
|---|---|
| `PluginRef` | `path`(확장자 없는 상대 경로; 플랫폼이 `.dll`/`.so`를 붙임), `abiVersion = 1` |
| `BuildSettings` | `singleExe`, `compress`(기본 off), `encrypt`, `keyId`(키 **식별자**, 키 자체는 절대 아님) |
| `ProjectFile` | `name`, `id`(UUID 문자열), `engineVersion`("major.minor.patch"), `entryScene`(`scenes` 중 하나, 씬이 없으면 빈 문자열), `scenes`, `assetDirs`, `scriptDirs`, `plugins`, `settings`(`json::Object`, 프로젝트가 소유), `build`, `extra` |
| `LoadOptions` | `verifyIntegrity = true` |

함수:
```cpp
ProjectFile makeNewProject(std::string name, std::string engineVersion);   // 새 id, 기본값(assets/, scripts/, 씬 없음)
bool isValidRelativePath(std::string_view) noexcept;    // 비어 있지 않음, '/' 구분, 선행 '/' 없음, 드라이브 문자 없음, '.' '..' 빈 세그먼트 없음, 역슬래시·제어문자 없음
Result<void> validate(const ProjectFile&);              // 모든 필드와 상호 참조(entryScene ∈ scenes, 경로 유일, id 유효 등)
Result<json::Value> toJson(const ProjectFile&);         // integrity 포함
Result<ProjectFile> fromJson(const json::Value&, const LoadOptions& = {});
Result<std::string> serialize(const ProjectFile&);
Result<ProjectFile> parseMindev(std::string_view, const LoadOptions& = {});
Result<ProjectFile> loadMindev(const std::filesystem::path&, const LoadOptions& = {});
Result<void> saveMindev(const std::filesystem::path&, const ProjectFile&);
```
`isValidRelativePath`는 프로젝트·아카이브 안 모든 경로의 **경로 탈출 방어선**이다. 파일 시스템에서 읽기 전에 항상 통과시킬 것.

## archive.hpp — `.mingol`
레이아웃: `헤더(80바이트) | 엔트리 데이터(테이블 순서대로 연속) | 목차(끝)`. 이름으로 임의 접근, 선택적 암호화.
상수: `kMingolFormatVersion = 1`, `kArchiveChunkSize = 64 KiB`, `kMaxArchiveEntries = 1<<20`.

**보안 모델(솔직하게)**: 키는 게임 실행 파일이 접근할 수 있어야 하므로 **암호화는 우연한 추출을 막고 변조를 탐지할 뿐, 실행 파일을 가진 결심한 공격자는 막지 못한다**. 암호화 아카이브는 64 KiB 청크마다 **ChaCha20-Poly1305**를 쓰고, 키는 아카이브 키와 파일별 랜덤 salt에서 **HKDF-SHA256**으로 유도하며 **목차도 인증**된다. 비암호화 아카이브는 SHA-256 해시를 가진다(손상 탐지용; 파일 전체를 다시 쓸 수 있는 사람에겐 변조 방지가 아님).
**무결성**: 아카이브의 모든 바이트가 검사 대상이다. 수정·잘림·덧붙임은 **열 때 또는 해당 엔트리를 읽을 때** 보고된다.

### `ArchiveKey`
```cpp
struct ArchiveKey { crypto::Key256 bytes;
  static Result<ArchiveKey> fromHex(std::string_view hex64);
  static Result<ArchiveKey> fromPassphrase(std::string_view passphrase, std::string_view salt, std::uint32_t iterations = 200000);   // PBKDF2-HMAC-SHA256; salt는 프로젝트마다 유일하게(예: project id)
  static Result<ArchiveKey> generate();     // OS 난수
  std::string toHex() const; };
```
### 쓰기
```cpp
struct WriteOptions { std::optional<ArchiveKey> key;                    // 있으면 암호화
                      std::optional<std::array<uint8_t,16>> salt; };     // 고정 salt(재현 가능 빌드/테스트); 기본 랜덤
class ArchiveWriter {
  Result<void> add(std::string name, std::vector<uint8_t> data);          // name: '/' 구분 상대 경로(isValidRelativePath), 중복 거부
  Result<void> addFile(std::string name, const std::filesystem::path&);
  std::size_t count() const;
  Result<std::vector<uint8_t>> serialize(const WriteOptions& = {}) const;
  Result<void> save(const std::filesystem::path&, const WriteOptions& = {}) const;   // 크래시 안전(임시 파일 + rename)
};
```
### 읽기
```cpp
class ArchiveReader {  // 이동만 가능
  static Result<ArchiveReader> openFile(path, const std::optional<ArchiveKey>& = {});
  static Result<ArchiveReader> openMemory(std::vector<uint8_t>, const std::optional<ArchiveKey>& = {});
  static Result<ArchiveReader> openExecutable(exe, const std::optional<ArchiveKey>& = {});   // packExecutable이 덧붙인 아카이브
  static bool hasEmbeddedArchive(exe);
  bool encrypted() const;   const std::vector<EntryInfo>& entries() const;   bool contains(std::string_view) const;
  Result<std::vector<uint8_t>> read(std::string_view name) const;   // 읽고 검증. 알 수 없는 이름 → NotFound, 손상 → Corrupt, 잘못된 키/변조 → AuthFailed
  Result<void> verifyAll() const;                                    // 모든 엔트리를 읽어 전체 무결성 검사
};
struct EntryInfo { std::string name; uint64_t size /*평문*/, storedSize /*아카이브 내*/; };
```
### 실행 파일 / 프로젝트 패킹
```cpp
Result<void> packExecutable(exe, archive, output);   // exe + 아카이브 + 트레일러를 output에 원자적으로 기록. 결과는 여전히 실행 가능. exe에 이미 아카이브가 있으면 실패. POSIX에서는 exe의 권한을 복사
struct ProjectPackOptions { WriteOptions write; };
struct ProjectPackReport { size_t files; uint64_t bytes; std::vector<std::string> skipped; /* 심볼릭 링크, 읽을 수 없는 파일 등 */ };
Result<std::vector<uint8_t>> packProject(const std::filesystem::path& mindevPath, const ProjectPackOptions&, ProjectPackReport* = nullptr);
```
`packProject`: `.mindev`(→ `"project.mindev"`), 나열된 모든 씬, `assetDirs`/`scriptDirs` 아래 모든 파일을 담는다. 엔트리 이름은 프로젝트 기준 상대 경로, 결과는 **암호화 salt를 제외하면 결정적**이다. 이것을 `mingol-pack` 도구와 에디터의 Build가 사용한다.

## crypto.hpp
`mingol::package::crypto`. RFC/FIPS로 구현한 **이식 가능한 자체 구현**: SHA-256, HMAC-SHA256, HKDF-SHA256, PBKDF2-HMAC-SHA256, ChaCha20-Poly1305(RFC 8439). 공개된 테스트 벡터와 독립 구현의 참조 출력으로 검증한다(`tests/package`). **하드웨어 가속 없음, 감사(audit)를 받지 않았다.** 비밀 의존 분기·테이블 조회 없이 작성했고 태그 비교는 상수 시간이지만 그 이상의 사이드채널 보장은 없다.
타입: `Bytes`, `Sha256Digest`(32), `Key256`(32), `Nonce96`(12), `Tag128`(16).
```cpp
class Sha256 { update(span); Sha256Digest finish(); /* 이후 재사용 금지 */ };
Sha256Digest sha256(span);   Sha256Digest hmacSha256(key, data);
Result<Bytes> hkdfSha256(ikm, salt, info, size_t length);                       // RFC 5869, length ≤ 255*32
Result<Bytes> pbkdf2HmacSha256(std::string_view password, salt, uint32_t iterations, size_t length);   // iterations ≥ 1
void chacha20Xor(const Key256&, const Nonce96&, uint32_t counter, span<uint8_t> data);   // 제자리 암·복호화
Tag128 poly1305(const Key256& oneTimeKey, span data);
Tag128 aeadSeal(key, nonce, aad, plaintext, span<uint8_t> ciphertext);          // ciphertext 크기 = plaintext 크기, 태그 반환
Result<void> aeadOpen(key, nonce, aad, ciphertext, const Tag128&, span<uint8_t> plaintext);   // 태그를 먼저 상수 시간 검증; 불일치면 아무것도 쓰지 않고 AuthFailed
bool constantTimeEqual(span, span);   void secureZero(void*, size_t);
Result<void> randomBytes(span<uint8_t>);      // OS 난수원; 없으면 실패
```

## 사용 예 (스케치)
```cpp
// 프로젝트를 암호화된 아카이브로
auto key = package::ArchiveKey::generate().value();
package::ProjectPackOptions po; po.write.key = key;
package::ProjectPackReport rep;
auto bytes = package::packProject("MyGame/game.mindev", po, &rep).value();
fs::writeFile("MyGame.mingol", bytes);                 // 또는 ArchiveWriter로 직접

// 읽기
auto ar = package::ArchiveReader::openFile("MyGame.mingol", key);
if (!ar.ok()) { /* AuthFailed: 키가 틀리거나 변조 */ }
auto scene = ar.value().read("scenes/main.minseen");

// 단일 실행 파일
package::packExecutable("mingol-player", "MyGame.mingol", "MyGame");
```
## 한계
압축 미구현(`compress` 플래그는 예약된 설정이며 기본 off). 암호 구현은 비감사. 키는 실행 파일이 접근 가능해야 하므로 DRM이 아니다. 벤치(릴리스, 2코어): 평문 ≈175 MiB/s, 암호화 ≈98 MiB/s.
