# 코딩 규약 (기여자용)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 언어/빌드
- C++20, GCC 13 기준으로 `-Wall -Wextra -Wpedantic -Wshadow -Werror`가 켜진 상태에서 경고 0. 모든 소스 첫 줄에 `// SPDX-License-Identifier: GPL-2.0-only`.
- 네임스페이스 `mingol::<모듈>`, 공개 헤더는 `engine/<모듈>/include/mingol/<모듈>/`, 구현은 `src/`.
- 의존 방향을 어기지 않는다([ARCHITECTURE.md](ARCHITECTURE.md)). 서드파티 타입을 공개 헤더에 노출하지 않는다.
- 새 하위 디렉터리를 추가하면 CMake를 다시 구성해야 한다.

## 에러 처리
`engine/core/error.hpp`:
- 실패할 수 있는 함수는 `Result<T>`(`[[nodiscard]]`)를 반환한다. 예외는 쓰지 않는다(스크립트 VM의 예외는 별개).
- `Error{ErrorCode, message}`. 코드: InvalidArgument, NotFound, AlreadyExists, Io, Corrupt, VersionMismatch, AuthFailed, OutOfRange, OutOfMemory, Unsupported, Timeout, OutOfDate, Internal.
- 전파: `MINGOL_TRY(expr)`(Result<void>), `MINGOL_TRY_ASSIGN(auto x, expr)`.
- 외부 입력(파일·아카이브·씬·스크립트·glTF)은 **항상 검증하고 오류로 반환**한다. 크래시/abort/무한 루프/무제한 할당이 나오면 버그다.
- `MINGOL_VERIFY`(항상 켜짐)와 `MINGOL_ASSERT`(디버그 전용)는 **내부 불변식**에만. 위반이 곧 미정의 동작이면 `VERIFY`. 사용자 입력 검사에 쓰지 않는다.
- 반환값을 무시해야 하면 `(void)`로 명시한다(테스트에서는 `REQUIRE(x.ok())`).
- `.value()`는 `ok()`를 확인한 뒤에만. 실제로 있었던 사고: 오류 결과를 `.value()`로 열어 abort(`integrityOf`).

## 로깅
`MINGOL_LOG_INFO("카테고리", "형식 {}", 값)` 등. 싱크: 콘솔/파일/링 버퍼(에디터 콘솔이 링 버퍼를 읽는다).

## 스레딩
기본은 단일 스레드. 예외: RHI 자원 생성/제출, 잡 시스템, 오디오 믹서(오디오 스레드는 try-lock만). 새로 스레드를 도입하면 TSan 프리셋으로 검증하고 문서에 계약을 쓴다.

## 수명 규칙 (자주 사고 나는 곳)
- `ScriptHost`는 그것을 쓰는 `Scene`보다 오래 산다. 씬을 만지는 모든 작업은 `ScriptHost::Scope`로 감싼다(에디터·플레이어 공통).
- 에디터에서 에셋(오디오/모델)은 리스캔을 넘어 유지돼야 씬이 가진 포인터가 유효하다.
- GPU 자원의 마지막 참조 해제는 안전하지만, 미제출 CommandList가 참조하는 자원은 살려 둔다([RHI.md](RHI.md)).

## 테스트 규약
- 버그를 고치면 그 버그를 잡는 테스트를 같이 추가한다. 새 기능은 **정상 경로 + 잘못된 입력 + 경계값**.
- "통과"만으로 충분하지 않다: 기대 값을 일부러 틀리게 바꾸면(변이) 테스트가 실패하는지 확인한다([TESTING.md](TESTING.md)).
- GPU 테스트는 `GPU_OR_SKIP()`로 시작한다. 임시 파일은 `TempDir`.
- 실행해 보지 못한 것은 "검증됨"이라고 쓰지 않는다. 문서/주석에 한계를 그대로 적는다.

## 커밋/문서
- 코드는 GPL-2.0-only, `docs/`는 CC BY 4.0. 새 서드파티는 GPL-2.0 호환 라이선스만, `third_party/THIRD_PARTY.md`에 기록.
- 동작이 바뀌면 해당 문서(`docs/`)와 `STATUS.md`의 한계 목록을 같이 갱신한다.
