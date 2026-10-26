# `.mingol` — 배포용 아카이브

문서 라이선스: CC BY 4.0. 구현: `engine/package` (`archive.hpp`, `crypto.hpp`), CLI: `tools/mingol_pack`, 테스트: `tests/package`, `tests/tools`.

게임 파일(씬, 에셋, 스크립트, 프로젝트 파일)을 하나로 묶은 읽기 전용 바이너리. 이름으로 임의 접근하며 선택적으로 암호화한다. 압축은 **구현하지 않았다**(로드맵 후속).

## 보안 모델 (정직하게)
게임 실행 파일이 키를 가져야 하므로 암호화는 **캐주얼한 추출 방지와 변조 검출** 수준이다. 실행 파일을 가진 공격자는 키를 꺼낼 수 있다. 암호화되지 않은 아카이브의 SHA-256은 손상 검출용이며, 파일 전체를 다시 쓸 수 있는 공격자는 막지 못한다. 암호 구현은 RFC/FIPS 벡터와 독립 구현(Python `cryptography`, `hashlib`) 출력으로 검증했지만 **외부 감사는 받지 않았고** 하드웨어 가속·부채널 보증은 없다.

## 레이아웃 (리틀엔디언)
```
[0,8)    magic 89 4D 49 4E 47 4F 4C 0A
[8,12)   formatVersion (1)
[12,16)  flags: bit0 = encrypted (그 외 비트는 0이어야 함)
[16,32)  salt (암호화 시 무작위 16바이트, 아니면 0)
[32,40)  tocOffset      [40,48) tocStoredSize
[48,80)  SHA-256( header[0,48) || 저장된 TOC )
[80,..)  엔트리 데이터(표 순서대로 빈틈없이 이어짐) | TOC
```
TOC(평문): `u32 count`, 엔트리마다 `u16 nameLen, name(UTF-8, 상대경로), u64 offset, u64 storedSize, u64 plainSize, sha256(plain)[32]`.

**암호화**: 키 = 32바이트 아카이브 키. `HKDF-SHA256(키, salt, "mingol.archive.v1.toc"|"…data")`로 TOC 키/데이터 키 파생. 각 엔트리는 64KiB 청크마다 ChaCha20-Poly1305(nonce = 엔트리 번호(4) ‖ 청크 번호(8), AAD = header[0,48) ‖ 'D' ‖ 마지막 청크 플래그). TOC는 nonce 0, AAD = header[0,48) ‖ 'T'. 빈 엔트리도 태그 1개. 저장 크기 = plain + 16×청크 수. 파일마다 새 salt → 같은 키라도 키스트림 재사용 없음.

## 읽기 검증
열 때: magic/버전(새 버전 → `VersionMismatch`)/플래그, 크기 정확 일치(절단·추가 데이터 거부), 헤더+TOC 해시, (암호화) TOC 복호·인증(`AuthFailed`), 이름 유효성·중복 없음, 엔트리 연속성·크기 일관성·데이터 영역 완전 덮임. 읽을 때: 청크 인증(암호화) + 엔트리 SHA-256. **아카이브의 모든 바이트가 어떤 검사에 의해 덮인다.**

## 실행 파일 내장 (단일 EXE)
`packExecutable(exe, archive, out)` = exe ‖ archive ‖ 32바이트 트레일러(`89 4D 49 4E 45 58 45 0A`, archiveOffset, archiveSize, 0). 결과는 원래대로 실행된다(Linux ELF로 확인; PE의 오버레이 방식은 관례적으로 동작하지만 **Windows에서는 검증하지 못했다**). 이미 내장된 파일에는 거부.

## CLI `mingol-pack`
`genkey | pack <.mindev> -o out | embed <exe> <archive> -o out | list | verify | extract <dest>`. 키는 `--key-file`, `--key-env`, `--passphrase-env`+`--salt`(PBKDF2-HMAC-SHA256 200000회)로만 받는다(명령행 인자로 받지 않는다). 프로젝트 패킹은 `.mindev`(→`project.mindev`), 씬 목록, `assetDirs`/`scriptDirs` 하위 파일을 이름순으로 담으며 심볼릭 링크와 프로젝트 밖 경로는 건너뛰거나 거부한다. 암호화하지 않으면 결과는 결정적(바이트 동일)이다.

## 검증된 것
암호 프리미티브 전부 외부 기준값과 일치, 변이 테스트(해시 검사 제거·Poly1305 마스크 변경·TOC를 해시에서 제외)는 실패로 검출됨. 모든 비트 반전 검출(평문/암호화 소형 아카이브 전수), 다중 청크 표본 검출·청크 교환 검출, 절단/추가/앞 삽입 거부, 해시가 맞는 구조 공격(간격, 크기 불일치, `../`, 중복, 거대 크기) 거부, 변이 퍼징(ASan 3시드×20000회), TSan 동시 읽기, 원자적 저장, 프로젝트 패킹, EXE 내장 후 실행, CLI 전체 워크플로(ctest).
- 알려진 한계: 마지막-청크 플래그는 TOC 크기 검증과 중복되는 심층 방어라서 이를 제거해도 테스트가 실패하지 않는다. 대용량 아카이브는 `serialize`가 메모리에 전부 만든다(스트리밍 쓰기 없음). 읽기는 엔트리 단위로 메모리에 올린다(청크 스트리밍 읽기 없음). 암호화 키 회전/다중 키 없음.
