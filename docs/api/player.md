# API: player (`mingol/player/*.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/player` · 타깃 `Mingol::Player` · 네임스페이스 `mingol::player`. 의존: `package`, `runtime`, `sceneio`, `scripting`, `renderer`, `rhi`. 사용자 안내(명령줄 등)는 [../PLAYER.md](../PLAYER.md).
**게임을 `ContentSource`(프로젝트 디렉터리 또는 `.mingol` 아카이브)에서 실행**한다. 창 시스템과 무관: `mingol-player` 실행 파일이 프레임 시간과 렌더 타깃을 넘겨 준다.
**스레딩**: 게임 루프 스레드 하나(단일 스레드).

## content_source.hpp
게임 파일의 출처를 추상화한다. **이름은 프로젝트 상대 경로(`/` 구분)** 이고 프로젝트 파일 이름은 항상 **`project.mindev`**.
```cpp
class ContentSource {
 public:
  virtual Result<std::vector<std::uint8_t>> read(const std::string& name) const = 0;
  virtual bool contains(const std::string& name) const = 0;
  virtual std::vector<std::string> list(const std::string& prefix) const = 0;   // prefix = 끝 '/' 없는 디렉터리 이름 아래의 모든 파일, 정렬, 재귀
  virtual std::string describe() const = 0;                                     // 로그/진단용 설명
};
Result<std::unique_ptr<ContentSource>> openDirectorySource(const std::filesystem::path& mindev);          // 개발용: .mindev가 있는 디렉터리 기준
Result<std::unique_ptr<ContentSource>> openArchiveSource(const std::filesystem::path& archive, const std::optional<package::ArchiveKey>& key = {});   // 배포용 .mingol
Result<std::unique_ptr<ContentSource>> openEmbeddedSource(const std::filesystem::path& exe, const std::optional<package::ArchiveKey>& key = {});      // package::packExecutable로 실행 파일에 덧붙인 아카이브
```
- 아카이브 소스는 읽을 때 무결성/인증을 검사한다(잘못된 키 → `AuthFailed`, 손상 → `Corrupt`; [package.md](package.md)).
- 디렉터리 소스는 `package::isValidRelativePath`를 통과하지 못하는 이름(`..`, 절대 경로 등)을 **존재하지 않는 것으로 취급**한다(`contains`=false, `list`는 무시) — 프로젝트 밖은 읽지 않는다.

## game_app.hpp
```cpp
struct GameAppDesc { rhi::Device* device = nullptr; renderer::Renderer* renderer = nullptr;   // 선택: GPU 없으면 실행은 되지만 렌더 불가
                     runtime::RuntimeDesc runtime; std::string scene; /* 비면 프로젝트의 entryScene */ };
static Result<std::unique_ptr<GameApp>> create(std::unique_ptr<ContentSource>, const GameAppDesc&);
```
`create`는 프로젝트 파일, 스크립트, 오디오, 모델, 진입 씬을 모두 읽어 게임을 준비한다. 프로젝트 파일 파싱, 런타임/스크립트 컴파일, 진입 씬 로드가 실패하면 **오류**(부분 상태를 돌려주지 않음). 개별 오디오/모델 파일 문제나 씬 로드 경고는 **건너뛰고 `warnings()`에 쌓인다**(렌더러가 없으면 모델은 건너뜀).

| 멤버 | 설명 |
|---|---|
| `project()` | 로드된 `package::ProjectFile` |
| `runtime()`, `scene()`, `scripts()`, `assets()` | 서비스 접근 |
| `currentScene()` | 현재 씬 이름 |
| `warnings()` | 비치명적 경고 목록 |
| `loadScene(name) -> Result<void>` | 현재 씬을 교체. **전부-아니면-전무**: 실패하면 **이전 씬이 그대로 남는다** |
| `tick(dt)` | `Runtime::tick`(고정 스텝 → update → lateUpdate) |
| `render(rhi::Texture& target, rhi::Swapchain* presenting = nullptr) -> Result<void>` | 활성 `Camera`에서 `target`으로 렌더(종횡비는 타깃 크기에서). **렌더러가 없거나 활성 카메라가 없으면 실패.** `presenting`이 있으면 스왑체인 이미지를 프리젠트 가능 상태로 둔다 |

## 실행 순서 (실행 파일 `mingol-player`)
1. 인자 해석(`path`(.mindev/.mingol), `--key-file`/`--key-env`, `--scene`, `--headless`, `--frames`, `--screenshot`, `--width/--height`). 경로가 없으면 **내장 아카이브**(`openEmbeddedSource`)를 사용.
2. `ContentSource` 열기 → `GameApp::create`.
3. 루프: 이벤트 → `tick(dt)` → `render(...)`. `--headless`는 창 없이 지정 프레임 수만 실행하고 `--screenshot`으로 마지막 프레임을 저장.

## 사용 예 (스케치)
```cpp
auto src = player::openArchiveSource("MyGame.mingol", key).value();
player::GameAppDesc d; d.device = device.get(); d.renderer = renderer.get();
auto app = player::GameApp::create(std::move(src), d);
if (!app.ok()) { MINGOL_LOG_ERROR("player", "{}", app.error().message); return 1; }
for (auto& w : app.value()->warnings()) MINGOL_LOG_WARN("player", "{}", w);

while (running) {
  app.value()->tick(dt);
  if (auto r = app.value()->render(*backbuffer, swapchain.get()); !r.ok()) MINGOL_LOG_ERROR("player", "{}", r.error().message);
}
// 씬 전환(실패해도 기존 씬 유지)
(void)app.value()->loadScene("scenes/level2.minseen");
```

## 한계
입력(키보드/마우스/게임패드)을 스크립트·컴포넌트에 노출하는 게임 레벨 API가 아직 없다([scripting.md](scripting.md)). 에셋은 시작 시 전부 로드한다(스트리밍·지연 로드 없음). Windows 빌드는 CI에서 CPU 테스트까지만 검증되었고 GPU 렌더는 Windows에서 검증되지 않았다([../KNOWN-LIMITS.md](../KNOWN-LIMITS.md)).
