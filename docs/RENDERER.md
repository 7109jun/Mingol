# 렌더러

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

`engine/renderer` — RHI 위의 **Forward + PBR** 렌더러. 입력은 평평한 `RenderScene`(`scene.hpp`), 출력은 `ColorAttachment` 텍스처(RGBA8/BGRA8, sRGB 또는 UNORM).

## 기능
| 항목 | 내용 |
|---|---|
| 셰이딩 | Cook-Torrance GGX + Smith + Schlick, 금속/거칠기, 베이스컬러·노멀·메탈릭러프니스·이미시브 텍스처, 양면, 알파 블렌드(거리 정렬) |
| 조명 | 방향광 1(그림자 맵) + 점/스포트광 최대 8(카메라에 가까운 순). 환경광/IBL **없음** |
| 그림자 | 방향광 단일 그림자 맵, 3×3 PCF, 노멀 오프셋 + 슬로프 바이어스. 점광·캐스케이드 없음 |
| 애니메이션 | GPU 스키닝(조인트 최대 128), CPU 쪽 포즈 블렌딩/크로스페이드(`animation.hpp`) |
| 프레임 | 프레임 슬롯 링으로 2프레임 동시 진행, 파이프라인 캐시 |
| 검증 | 잘못된 입력은 오류로 반환(크래시 없음) |

## 사용
```cpp
auto rnd  = renderer::Renderer::create(device);           // 셰이더는 실행 시 glslang으로 컴파일
auto mesh = rnd->createMesh(renderer::makeBox({0.5F, 0.5F, 0.5F}));  // 또는 makePlane, glTF에서 온 MeshData
auto mat  = rnd->createMaterial(desc);                    // 생성 후 불변
renderer::RenderScene rs = scene::buildRenderScene(scene, aspect).value();
auto serial = rnd->render(rs, *target /*, &swapchain */);
```
- 텍스처는 `ShaderRead` 상태여야 하고, 베이스컬러·이미시브는 sRGB 포맷, 노멀맵은 선형 포맷이어야 한다.
- 좌표계: `Mat4::perspective`는 Vulkan식(Y 반전, 깊이 0..1). 화면 y = (ndc.y·0.5+0.5)·h (추가 뒤집기 없음).

## 셰이더
`engine/shader`: GLSL·HLSL → SPIR-V 1.6(Vulkan 1.3), include 해석(재귀 차단), 위치 있는 진단. 사용자 셰이더 지원은 컴파일러 계층까지이며, 렌더러에 사용자 정의 머티리얼 셰이더를 꽂는 고수준 API는 아직 없다. **배포물에 SPIR-V 캐시가 없어** 플레이어도 시작 시 glslang으로 컴파일한다.

## 검증 방식
CPU 참조 BRDF 픽셀 비교(여러 각도·금속·거칠기), 그림자 위치를 기하로 예측해 비교, 스키닝 결과를 기하로 예측, 블렌드 정렬, 골든 이미지(회귀 가드일 뿐 정답 증명 아님), 변이 4종 검출 → [TESTING.md](TESTING.md).

## 한계
IBL·HDR·블룸·알파 컷아웃 없음, 컬링·인스턴싱 없음(모든 항목 제출), 스키닝 법선은 비균일 스케일 조인트에서 근사, 실제 GPU 성능 미측정(lavapipe 100박스 640×360 ≈ 34 ms/프레임은 소프트웨어 렌더 수치).
