# `.mindev` 프로젝트 파일

`.mindev`는 **UTF-8 텍스트(JSON 객체 하나)** 입니다. 사람이 읽고 편집하며 Git으로 diff/merge할 수 있도록 압축·암호화를 하지 않습니다.

```json
{
  "format": "mindev",
  "formatVersion": 1,
  "name": "My Game",
  "id": "3f2b8c1e-5d4a-4e0b-9a7c-0123456789ab",
  "engineVersion": "0.1.0",
  "entryScene": "scenes/main.minseen",
  "scenes": ["scenes/main.minseen"],
  "assetDirs": ["assets"],
  "scriptDirs": ["scripts"],
  "plugins": [{"path": "plugins/extras", "abiVersion": 1}],
  "settings": {},
  "build": {"singleExe": false, "compress": false, "encrypt": false, "keyId": ""},
  "integrity": "crc32:0a1b2c3d"
}
```

| 필드 | 규칙 |
|---|---|
| `format` / `formatVersion` | 필수. `"mindev"` / 정수 ≥ 1 |
| `name` | 1–128자, 제어 문자 불가 |
| `id` | UUID 문자열 |
| `engineVersion` | `major.minor.patch` |
| `entryScene` | `scenes`에 포함되어야 함 (씬이 없으면 빈 문자열) |
| `scenes`, `assetDirs`, `scriptDirs` | 프로젝트 상대 경로 목록, 중복 불가 |
| `plugins` | `path`(확장자 없음, 플랫폼이 .dll/.so 부여) + `abiVersion` ≥ 1 |
| `settings` | 프로젝트가 소유하는 자유 형식 JSON 객체 |
| `build` | `compress` 기본 false, `encrypt`를 켜면 `keyId` 필수 (키 자체는 파일에 저장하지 않음) |

**경로 규칙**: `/` 구분자, 절대 경로·드라이브 문자·`.`/`..`/빈 세그먼트·역슬래시·제어 문자 금지.

**무결성**: `integrity`는 `integrity` 키를 뺀 문서를 *공백 없는 정규 JSON*으로 직렬화한 값의 CRC-32입니다. 들여쓰기·줄바꿈 편집은 영향이 없고, 값이 바뀌면 `Corrupt` 오류가 납니다. 손으로 쓴 파일처럼 `integrity`가 없는 것은 허용하며, 손으로 값을 고친 파일은 `LoadOptions::verifyIntegrity = false`로 읽을 수 있습니다(저장 시 새 값이 기록됨). CRC-32는 우발적 손상 탐지용이며 변조 방지(보안)는 `.mingol`의 인증 암호화가 담당합니다.

**버전**: 더 새로운 `formatVersion`은 추측하지 않고 `VersionMismatch`로 거부합니다. 알 수 없는 최상위 키는 `extra`에 보존되어 다시 저장됩니다. 키 순서는 보존되어 저장 결과가 결정적입니다.

**파싱**: 엄격한 RFC 8259 — 주석, 후행 쉼표, 중복 키, 잘못된 UTF-8, 짝 없는 서로게이트는 오류. 정수와 실수는 구분되며(`1` ≠ `1.0`), 중첩 깊이는 256으로 제한됩니다. UTF-8 BOM은 허용합니다.

**저장**: 임시 파일에 쓴 뒤 rename하는 안전 저장(`saveMindev`). 검증 실패 시 기존 파일은 그대로입니다.

문서 라이선스: CC BY 4.0.
