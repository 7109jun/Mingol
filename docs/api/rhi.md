# API: rhi (`mingol/rhi/types.hpp`, `device.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/rhi` · 타깃 `Mingol::RHI` · 네임스페이스 `mingol::rhi`. 의존: `core`. 개요와 계약은 [../RHI.md](../RHI.md).
**백엔드 중립 GPU 인터페이스**. `types.hpp`는 Vulkan에 의존하지 않고, 구현은 `createVulkanDevice()`뿐이다.

## 목차
[types.hpp](#typeshpp) · [자원 클래스](#자원-클래스) · [CommandList](#commandlist) · [Swapchain](#swapchain) · [Device](#device) · [사용 예](#사용-예) · [오류와 규칙](#오류와-규칙)

---

## types.hpp
### 포맷
`enum class Format`: `Undefined, R8Unorm, RG8Unorm, RGBA8Unorm, RGBA8Srgb, BGRA8Unorm, BGRA8Srgb, R16Float, RG16Float, RGBA16Float, R32Float, RG32Float, RGB32Float, RGBA32Float, D16Unorm, D32Float`.
`bytesPerPixel(Format)`(Undefined는 0), `isDepthFormat(Format)`, `fullMipCount(w, h)`.

### 버퍼
`BufferUsage`(플래그): `Vertex, Index, Uniform, Storage, TransferSrc, TransferDst`.
`MemoryType`: `GpuOnly`(디바이스 로컬; 전송으로 채움) · `CpuToGpu`(상시 매핑, CPU가 씀: uniform·스테이징) · `GpuToCpu`(상시 매핑, CPU가 읽음).
`BufferDesc{size, usage, memory = GpuOnly, debugName}`. `kWholeSize` = 범위 전체.

### 텍스처
`TextureUsage`(플래그): `Sampled, ColorAttachment, DepthStencil, TransferSrc, TransferDst, Storage`.
`TextureState`(논리 상태; 레이아웃 전환·배리어는 명령 목록이 넣어 준다): `Undefined, ShaderRead, ColorAttachment, DepthAttachment, TransferSrc, TransferDst, Storage, Present`.
`TextureDesc{width=1, height=1, mipLevels=1, arrayLayers=1, format=RGBA8Unorm, usage=Sampled, cube=false(true면 arrayLayers=6), debugName}`.

### 샘플러
`FilterMode{Nearest, Linear}`, `AddressMode{Repeat, MirroredRepeat, ClampToEdge, ClampToBorder}`, `CompareOp{Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always}`.
`SamplerDesc{minFilter, magFilter, mipFilter = Linear, addressU/V/W = Repeat, maxAnisotropy = 1(끔), compareEnable(그림자 샘플러), compareOp = LessOrEqual, maxLod = 1000, borderWhite}`.

### 셰이더·파이프라인
- `ShaderStage`(플래그): `Vertex, Fragment, Compute`.
- `ShaderModuleDesc{span<const uint32_t> spirv, stage, entryPoint = "main", debugName}`.
- `BindingType{UniformBuffer, StorageBuffer, SampledTexture}`, `BindingDesc{binding, type, stages = Vertex|Fragment, count = 1}`, `BindGroupLayoutDesc{bindings, debugName}`.
- 래스터 상태: `Topology{TriangleList, TriangleStrip, LineList, PointList}`, `CullMode{None, Front, Back}`, `FrontFace{CounterClockwise, Clockwise}`, `BlendFactor{Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha}`, `BlendOp{Add, Subtract, ReverseSubtract, Min, Max}`, `BlendState{enable=false, srcColor=SrcAlpha, dstColor=OneMinusSrcAlpha, colorOp, srcAlpha=One, dstAlpha=OneMinusSrcAlpha, alphaOp}`.
- 정점: `VertexBinding{binding, stride, perInstance}`, `VertexAttribute{location, binding, format = RGB32Float, offset}`.
- `GraphicsPipelineDesc`: `vertexShader`, `fragmentShader`(깊이 전용이면 null 가능), `vertexBindings/vertexAttributes`, `topology`, `cullMode = Back`, `frontFace = CounterClockwise`, `depthTest = true`, `depthWrite = true`, `depthCompare = Less`, `depthBias`(+`depthBiasConstant/Slope`; 그림자 패스용), `colorFormats`(색 부착당 1개; 비어도 됨), `blend`(비면 블렌딩 없음, 아니면 부착당 1개), `depthFormat`, `setLayouts`(위치 = set 번호), `pushConstantSize`(바이트, 정점+프래그먼트에서 보임), `debugName`.

### 렌더링
`LoadOp{Load, Clear, DontCare}`, `StoreOp{Store, DontCare}`, `IndexType{U16, U32}`.
`ColorAttachment{texture*, mip, layer, load = Clear, store = Store, clear[4] = {0,0,0,1}}`, `DepthAttachment{texture*(null이면 깊이 없음), mip, layer, load, store, clearDepth = 1}`, `RenderPassDesc{colors, depth, debugName}`.

### 장치·표면
- `DeviceDesc{appName = "Mingol", enableValidation=false(레이어가 설치돼 있으면 켬), preferredDeviceName(부분 문자열; 비면 최선), allowSoftwareDevice = true(lavapipe 같은 CPU 장치 허용)}`.
- `DeviceType{Other, Integrated, Discrete, Virtual, Cpu}`, `DeviceInfo{name, driver, type, apiMajor/Minor/Patch, maxTextureSize, maxSamplerAnisotropy, maxPushConstantSize, validationEnabled}`.
- `NativeWindow{kind(Headless/Win32/Xlib/Xcb/Wayland), display, window, width, height}` — 창 라이브러리와 RHI를 분리하는 핸들. `Headless`는 표면 없이 스왑체인을 만든다(CI 테스트용).
- `SwapchainDesc{window, width, height, vsync = true}`.

---

## 자원 클래스
모두 `std::shared_ptr`로 반환된다. **마지막 참조를 놓는 것은 GPU가 아직 쓰는 중이어도 안전**(파괴는 지연). 단 *기록했지만 제출하지 않은* 리스트가 참조하는 자원은 호출자가 살려 둔다.

| 클래스 | 멤버 |
|---|---|
| `Buffer` | `desc()`, `size()`, `mapped()`(CpuToGpu/GpuToCpu만; GpuOnly는 nullptr), `flush(offset, size)`(CPU 쓰기를 GPU에 보이게), `invalidate(offset, size)`(GPU 쓰기를 CPU에 보이게). 일관성 메모리에서는 no-op |
| `Texture` | `desc()`, `state()` — **마지막으로 기록된 명령 기준** 상태(아직 실행 전일 수 있음). 상태는 텍스처 **전체(모든 밉/레이어)** 단위 |
| `Sampler` | `desc()` |
| `ShaderModule` | `stage()`, `entryPoint()` |
| `BindGroupLayout` | `desc()` |
| `BindGroup` | `desc()` — 묶인 자원에 대한 공유 참조를 가진다 |
| `GraphicsPipeline` | `desc()` |

`BindGroupDesc{layout, entries, debugName}`, `BindGroupEntry{binding, arrayElement, buffer, offset, range = kWholeSize, texture, sampler}` — 버퍼 바인딩은 `buffer/offset/range`, 샘플 텍스처 바인딩은 `texture + sampler`(그릴 때 텍스처는 **ShaderRead** 상태여야 함).

---

## CommandList
`Device::createCommandList()`. **한 번에 한 스레드만 기록.** `begin()`은 이전 제출이 끝나길 기다린 뒤 기록을 시작한다.

| 메서드 | 설명 |
|---|---|
| `begin()` / `end()` → `Result<void>` | |
| `transition(texture, newState)` | 명시적 상태 변경. **패스에서 샘플링하기 전, 프레젠트 전**에 필요(복사/부착 명령은 대상을 자동 전환) |
| `beginRendering(RenderPassDesc)` / `endRendering()` | dynamic rendering(렌더패스 객체 없음) |
| `setViewport(x, y, w, h, minDepth=0, maxDepth=1)`, `setScissor(x, y, w, h)` | |
| `bindPipeline`, `bindGroup(set, group)`, `bindVertexBuffer(binding, buf, offset)`, `bindIndexBuffer(buf, type, offset)`, `pushConstants(data, size, offset)` | |
| `draw(vertexCount, instanceCount=1, firstVertex=0, firstInstance=0)` | |
| `drawIndexed(indexCount, instanceCount=1, firstIndex=0, vertexOffset=0, firstInstance=0)` | |
| `copyBuffer(src, dst, size, srcOffset, dstOffset)` | 렌더 패스 밖. 결과는 이후 명령에 보인다 |
| `copyBufferToTexture(src, dst, mip, layer, srcOffset)` | 촘촘히 채운 픽셀 → 한 밉/레이어. **텍스처를 TransferDst로 남김** |
| `copyTextureToBuffer(src, dst, mip, layer, dstOffset)` | **텍스처를 TransferSrc로 남김** |
| `generateMipmaps(texture)` | 밉 1..N-1을 밉 0에서 생성(모든 레이어). `TransferSrc|TransferDst` 사용처와 선형 필터 가능 포맷 필요. TransferSrc로 남김 |

## Swapchain
`format()`, `width()`, `height()`, `imageCount()`.
- `acquire()` → `Result<AcquiredImage{texture*, index}>` — 텍스처는 스왑체인 소유, **다음 `acquire()`/`resize()`까지** 유효. 표면이 바뀌면 `ErrorCode::OutOfDate` → `resize()` 후 재시도.
- `present()` — 마지막 `acquire()`의 이미지를 표시. 그 전에 `Device::submit(list, &swapchain)`을 호출해야 한다. `OutOfDate`면 재생성.
- `resize(w, h)`.

## Device
| 메서드 | 설명 |
|---|---|
| `info()` | `DeviceInfo` |
| `createBuffer/Texture/Sampler/ShaderModule/BindGroupLayout/BindGroup/GraphicsPipeline` | `Result<shared_ptr<...>>` |
| `createCommandList()` | `Result<unique_ptr<CommandList>>` |
| `createSwapchain(SwapchainDesc)` | `Result<unique_ptr<Swapchain>>` |
| `submit(list, Swapchain* presenting = nullptr)` | 제출 시리얼 반환(단조 증가, 1부터). `presenting`이 있으면 그 스왑체인의 획득 이미지를 기다리고, 리스트 끝에서 이미지가 **Present** 상태여야 한다. 내부적으로 동기화됨 |
| `isComplete(serial)`, `waitForSerial(serial)`, `waitIdle()` | |
| `validationErrorCount()` | 검증 레이어 오류 누적 수(검증이 꺼져 있으면 항상 0). **테스트는 이 값이 늘지 않아야 통과** |
| `createBufferWithData(desc, data)` | 데이터가 든 버퍼. GpuOnly는 스테이징 복사 후 **완료까지 블록**, CpuToGpu는 직접 기록 |
| `createTextureWithData(desc, pixels, generateMips)` | mip 0 픽셀로 텍스처 생성(+밉 체인), **ShaderRead 상태로 완료까지 블록** |
| `readbackTexture(texture, mip=0, layer=0)` | `Result<vector<uint8_t>>`(촘촘한 픽셀, 블록). 텍스처에 `TransferSrc` 사용처 필요. **호출 후 상태는 TransferSrc** |

`Result<std::unique_ptr<Device>> createVulkanDevice(const DeviceDesc& = {})` — Vulkan 1.3 장치가 없으면 `Unsupported`.

---

## 사용 예
### 오프스크린 삼각형 그리기(개요 — 문서용 스케치이며 그대로 컴파일해 검증하지는 않았다)
```cpp
auto dev = rhi::createVulkanDevice({.enableValidation = true}).value();   // 지정 초기화
rhi::TextureDesc td; td.width = 256; td.height = 256; td.format = rhi::Format::RGBA8Unorm;
td.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
auto target = dev->createTexture(td).value();

auto list = dev->createCommandList().value();
MINGOL_VERIFY(list->begin().ok());
rhi::RenderPassDesc pass; pass.colors.push_back({.texture = target.get()});
list->beginRendering(pass);
list->bindPipeline(*pipeline);              // GraphicsPipelineDesc로 미리 생성
list->setViewport(0, 0, 256, 256); list->setScissor(0, 0, 256, 256);
list->draw(3);
list->endRendering();
MINGOL_VERIFY(list->end().ok());
auto serial = dev->submit(*list).value();
dev->waitForSerial(serial);
auto pixels = dev->readbackTexture(*target).value();   // 이후 target 상태: TransferSrc
```
### 스왑체인 프레임
```cpp
auto img = sc->acquire();                     // OutOfDate면 sc->resize(...) 후 다음 프레임
// ... img.value().texture에 그리고, 목록 끝에서 transition(*tex, TextureState::Present)
dev->submit(*list, sc.get());
sc->present();
```

## 오류와 규칙
- 오류 코드: `Unsupported`(장치/기능 없음), `OutOfDate`(스왑체인), `InvalidArgument`(잘못된 Desc), `Internal`(드라이버 오류).
- **자주 틀리는 것**: ① 샘플링 전 `ShaderRead` 전환 누락 ② `readbackTexture` 후 상태가 `TransferSrc`임을 잊음 ③ 미제출 리스트가 참조하는 자원을 먼저 해제 ④ 스왑체인보다 창을 먼저 파괴 ⑤ 렌더 패스 안에서 복사 명령 호출.
- **한계**: 바인드 그룹당 디스크립터 풀 1개, 텍스처 상태는 서브리소스 단위가 아님, `copyBuffer` 겹침 같은 오용은 검증 레이어만 보고, 실제 GPU·실제 표면은 미검증([../KNOWN-LIMITS.md](../KNOWN-LIMITS.md)).
