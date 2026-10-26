# 에셋 파이프라인과 표준 이름

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 로더 (`engine/assets`, CPU 데이터만 생성)
| 종류 | 형식 | 비고 |
|---|---|---|
| 이미지 | PNG(전 색상형, 1~16비트, 인터레이스), JPEG(베이스라인/프로그레시브) | 헤더 단계에서 크기·메모리 한도 검사 후 할당(해상도 폭탄 방지). 16비트는 8비트로 축소, ICC 무시 |
| 오디오 | WAV(PCM 8/16/24/32, float, EXTENSIBLE), Ogg Vorbis | 전체를 메모리에 디코딩 |
| 모델 | glTF 2.0 / GLB | 메시, PBR 머티리얼, 노드 계층, 스킨, 애니메이션(LINEAR/STEP). 외부 파일은 리졸버를 통해서만 읽고 **경로 이탈 차단** |
지원하지 않는 glTF 기능(alpha MASK, CUBICSPLINE, 모프 타깃, 카메라/라이트, 두 번째 UV, KHR_texture_transform, KTX2/WebP 등)은 `ModelData::warnings`로 보고되며 조용히 넘어가지 않는다.

보안 처리: stb_image는 모든 할당을 0으로 초기화(손상 JPEG가 힙 내용을 이미지에 섞는 문제 방지), stb_vorbis는 고정 아레나(누수·메모리 상한 32 MiB).

## 표준 에셋 이름 (에디터·플레이어 공통, `sceneio/standard_assets.hpp`)
씬 파일(`.minseen`)은 에셋을 **이름 문자열**로 참조한다. 두 실행 파일이 같은 이름 규칙을 쓰므로 에디터에서 만든 씬이 그대로 배포에서 열린다.

| 대상 | 이름 |
|---|---|
| 내장 메시 | `builtin/box`, `builtin/sphere`, `builtin/plane` |
| 내장 머티리얼 | `builtin/default` |
| 모델의 메시 | `<경로>#mesh<m>.<p>` (m: 메시 번호, p: 프리미티브 번호) |
| 모델의 머티리얼 | `<경로>#material<k>`, 기본 `<경로>#default` |
| 스켈레톤 / 클립 | `<경로>#skin<k>`, `<경로>#skin<k>/<클립 이름>` |
| 오디오 클립 | 프로젝트 상대 경로(`assets/sfx/hit.wav`) |
`<경로>`는 프로젝트 상대 경로(`assets/hero.glb`). 경로는 `/` 구분, 절대 경로·`..`는 거부.

## 로딩 경로
- 에디터: `EditorDocument::rescanAssets/registerModel` — 프로젝트 `assetDirs`를 스캔.
- 플레이어: `GameApp::create` — 같은 `assetDirs`를 `ContentSource`에서 읽어 `addModelAssets`로 같은 이름을 등록. 개별 에셋 로드 실패는 `warnings()`에 남기고 계속 진행.
- 아직 없는 것: 텍스처 단독 에셋(모델 내부 텍스처만), 에셋 임포트 설정, 에셋 변경 감시 자동 리임포트.
