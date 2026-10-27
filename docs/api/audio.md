# API: audio (`mingol/audio/mixer.hpp`, `device.hpp`)

문서 라이선스: CC BY 4.0. 모듈: `engine/audio` · 타깃 `Mingol::Audio` · 네임스페이스 `mingol::audio`. 의존: `core`, `assets`(`AudioClip`), miniaudio(백엔드).
**소프트웨어 믹서**(`Mixer`)와 **출력 장치**(`Device`)로 분리되어 있다. 믹서는 장치와 무관하게 결정적이므로 샘플 단위로 테스트하고 오프라인 렌더가 가능하다. GameObject용 컴포넌트는 [runtime.md](runtime.md)의 `AudioSource`/`AudioListener`.

## 스레딩
- 게임 스레드: `play/stop/set*`. 오디오 스레드(또는 오프라인 호출자): `render()`.
- 오디오 스레드는 **블록하지 않는다**: 명령은 뮤텍스로 넘겨지고 `render()`는 `try_lock`만 한다(게임 스레드가 잡고 있으면 명령이 **한 블록 늦게** 적용됨).
- 끝난 클립의 메모리는 오디오 콜백이 아니라 게임 스레드에서 해제된다.

## Mixer (`mixer.hpp`)
출력: **인터리브 스테레오 float**.

### 타입
| 타입 | 설명 |
|---|---|
| `VoiceId{uint64 value}` | `valid()`(0이 아님), `==` |
| `MixerDesc` | `sampleRate = 48000`(8000..384000), `maxVoices = 64`(1..1024) |
| `PlayDesc` | `volume = 1`(0..16 선형 게인), `pan = 0`(−1 좌 .. +1 우; **spatial이면 무시**), `pitch = 1`(0.01..16), `loop`, `paused`(일시정지 상태로 시작), `priority = 0`(보이스가 가득 차면 **가장 낮은 priority, 그다음 가장 오래된 것**이 밀려남), `spatial`, `position`, `minDistance = 1`(여기까지 풀 볼륨), `maxDistance = 50`(이 이상은 추가 감쇠 없음), `rolloff = 1`(0 = 거리 감쇠 없음) |
| `MixerStats` | `framesRendered`, `activeVoices`, `voicesStarted`, `voicesStolen`(자리 확보로 쫓겨남), `voicesRejected`(모든 보이스가 더 높은 우선순위라 새 보이스가 버려짐), `clippedSamples`(±1 한계에 닿은 출력 샘플) |

### 메서드
```cpp
static Result<std::unique_ptr<Mixer>> create(const MixerDesc& = {});
std::uint32_t sampleRate() const;

// 게임 스레드
Result<VoiceId> play(std::shared_ptr<const assets::AudioClip>, const PlayDesc& = {});
void stop(VoiceId);  void stopAll();
Result<void> setVolume(VoiceId, float);  setPitch(VoiceId, float);  setPan(VoiceId, float);
Result<void> setPosition(VoiceId, Vec3);  setPaused(VoiceId, bool);
void setListener(Vec3 position, Vec3 forward, Vec3 up);
Result<void> setMasterVolume(float);

bool isPlaying(VoiceId) const noexcept;   // lock-free; 마지막으로 렌더된 블록 기준
MixerStats stats() const noexcept;

// 오디오 스레드 / 오프라인
void render(float* out, std::uint32_t frames) noexcept;   // frames 프레임(= 2*frames float)을 씀
```
- `play`는 **null/빈 클립이나 범위를 벗어난/비유한 파라미터면 실패**한다. 성공해도 보이스가 나중에 버려질 수 있으므로(`voicesRejected`) 실제 재생 여부는 `isPlaying()`으로 확인한다. 방금 호출한 `play()`는 **다음 `render()` 뒤에** `isPlaying`에 보인다.
- `render`는 블록하지 않고, 생성 이후 할당하지 않는다(명령이 몰린 뒤 첫 블록에서 내부 예약 공간이 늘 수 있음).
- 리샘플링은 클립 샘플레이트 → 믹서 샘플레이트(`pitch` 포함)로 수행된다.

## Device (`device.hpp`)
```cpp
enum class DeviceBackend { Default /*플랫폼의 실제 출력*/, Null /*조용하지만 실시간 속도로 당겨 가는 장치(서버, CI)*/ };
struct DeviceDesc { DeviceBackend backend = DeviceBackend::Default; };
static Result<std::unique_ptr<Device>> create(Mixer&, const DeviceDesc& = {});   // 장치를 못 열면 실패
const std::string& backendName() const;
std::uint64_t framesPulled() const noexcept;      // 하드웨어 콜백이 지금까지 요청한 프레임
```
- **`Mixer`가 `Device`보다 오래 살아야 한다.**
- 백엔드(miniaudio): Windows = WASAPI/DirectSound/WinMM, Linux = PulseAudio/ALSA/JACK(**런타임에 로드**). 사운드 카드가 없는 환경에서는 `Default`가 실패하므로 `Null`을 쓴다.

## 사용 예 (스케치)
```cpp
// 오프라인 렌더 (장치 없음)
auto mx = audio::Mixer::create().value();
audio::PlayDesc pd; pd.spatial = true; pd.position = {5, 0, 0};
mx->setListener({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
auto v = mx->play(clip, pd);                 // clip: shared_ptr<const assets::AudioClip> (assets.md)
std::vector<float> buf(2 * 480);
mx->render(buf.data(), 480);                 // 10 ms @48 kHz
bool playing = mx->isPlaying(v.value());

// 실제 출력
auto dev = audio::Device::create(*mx, {audio::DeviceBackend::Default});
if (!dev.ok()) dev = audio::Device::create(*mx, {audio::DeviceBackend::Null});
```

## 한계
믹서는 **스테레오 출력만** 지원한다. 3D는 거리 감쇠 + 팬(HRTF·도플러·반향 없음). 이펙트/버스/스트리밍 디코드 없음(클립은 전부 디코드되어 메모리에 있음). 실제 사운드 하드웨어 출력은 CI에서 검증하지 못했다(`Null` 백엔드와 오프라인 렌더로 테스트).
