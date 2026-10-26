# RHI (렌더 하드웨어 인터페이스)

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

`engine/rhi` — 백엔드 중립 인터페이스(`device.hpp`, `types.hpp`)와 Vulkan 1.3 구현(`src/vulkan`). 렌더러·GUI 렌더러는 Vulkan 타입을 보지 않는다.

## 구성
- `Device`: 버퍼/텍스처/샘플러/셰이더 모듈/바인드 그룹(+레이아웃)/파이프라인 생성, `submit(list, swapchain*)`, `waitForSerial`, `waitIdle`, `validationErrorCount`. 도우미: `createBufferWithData`, `createTextureWithData(…, generateMips)`, `readbackTexture`.
- `CommandList`: 기록 전용. 렌더 패스는 **dynamic rendering**(렌더패스 객체 없음), 텍스처 상태 전환은 `transition(texture, TextureState)`.
- `Swapchain`: `acquire`/`present`/`resize`. 표면이 사라지면 `ErrorCode::OutOfDate` → 호출자가 `resize` 후 재시도. **헤드리스 표면**(OS 창 없음)을 지원해 CI에서 전체 경로를 실행한다.
- 구현 세부: VMA 메모리, synchronization2 배리어, **타임라인 세마포어**로 제출 시리얼 관리, 자원 파괴 지연(GPU 사용 중에도 마지막 참조를 놓아도 안전).

## 계약 (어기면 정의되지 않음)
1. 자원 생성과 `submit()`은 다중 스레드 호출 가능. `CommandList` 기록은 한 번에 한 스레드.
2. 기록했지만 아직 제출하지 않은 리스트가 참조하는 자원은 제출 끝날 때까지 호출자가 살려 둔다.
3. 자원은 `Device`보다 오래 살아도 된다(마지막 자원이 사라질 때 장치 해제).
4. 텍스처 상태는 **텍스처 전체 단위**로 추적(서브리소스별 상태 없음).
5. 스왑체인에 제출할 때 이미지는 목록 끝에서 `Present` 상태여야 한다.

## 텍스처 상태 주의점 (실제로 겪은 함정)
- `readbackTexture` 후 텍스처는 `TransferSrc` 상태다. 이어서 샘플링하려면 `ShaderRead`로 다시 전환해야 한다.
- `Renderer::render(…, target)`은 타깃을 `ColorAttachment`로 남긴다. GUI가 뷰포트 텍스처로 샘플링하기 전에 `ShaderRead` 전환 제출이 필요하다(`EditorApp::renderViewport`).

## 한계
- 바인드 그룹당 디스크립터 풀 1개(머티리얼 수천 개면 풀 매니저 필요).
- 겹치는 `copyBuffer` 같은 드라이버 수준 오용은 검증 레이어가 보고할 뿐 RHI가 막지 않는다.
- 실제 GPU·실제 표면(Xlib/Wayland/Win32) 프레젠트는 검증하지 못했다(Xvfb의 X11 표면과 헤드리스만).
