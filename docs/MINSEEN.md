# `.minseen` — 씬 파일 (JSON 텍스트)

문서 라이선스: CC BY 4.0. 구현: `engine/sceneio` (`Mingol::SceneIO`), 테스트: `tests/sceneio`.

사람이 읽고 diff/merge 할 수 있는 JSON 텍스트다(압축·암호화 없음; 배포 시에는 `.mingol` 안에 그대로 들어간다). JSON은 `core/json`의 엄격한 파서(RFC 8259, 중복 키·주석·후행 쉼표 거부, 깊이 제한)를 쓴다.

```json
{
  "format": "minseen",
  "formatVersion": 1,
  "name": "main",
  "environment": { "ambient": [0.1, 0.1, 0.1], "clearColor": [0.05, 0.05, 0.07, 1.0] },
  "objects": [
    { "id": 1, "name": "root", "parent": 0, "active": true,
      "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1],
      "components": [
        { "type": "Camera", "enabled": true, "data": { "fovY": 1.047, "near": 0.1, "far": 200, "priority": 0 } },
        { "type": "Script", "enabled": true, "data": { "class": "Hero", "fields": { "hp": 100, "inventory": [1, 2] } } }
      ] }
  ],
  "integrity": "crc32:0a1b2c3d"
}
```

## 규칙
- `objects`는 **부모가 항상 자식보다 앞**에 온다. `id`는 파일 안에서만 의미 있는 1 이상의 정수(런타임 핸들과 무관), `parent`는 0(루트) 또는 앞선 `id`.
- 변환은 부모 기준 로컬 값. `rotation`은 쿼터니언 `[x,y,z,w]`(0벡터 거부, 로드 시 정규화).
- 에셋은 **이름**으로 참조한다(`"mesh": "mesh/box"`). 이름→실제 에셋은 `AssetTable`이 해석한다(없으면 NotFound 오류).
- 지원 컴포넌트: `Camera, Light, MeshRenderer, Animator, Rigidbody, Collider, AudioSource, AudioListener, Script`. 사용자 정의 컴포넌트는 `ComponentRegistry::registerType(이름, save, load)`로 등록한다.
- Script 필드는 int/float/bool/string/enum(정수)과 이들의 배열만 저장한다. 객체 참조·정적 필드·`gameObject`는 저장하지 않는다. 알 수 없는 필드/타입 불일치는 `LoadReport::warnings`로 보고되고 건너뛴다.
- `integrity` = `integrity` 키를 뺀 문서의 compact JSON의 CRC-32(`.mindev`와 동일 방식). 없으면 허용, 틀리면 `Corrupt`(`LoadOptions::verifyIntegrity=false`로 해제). **변조 방지가 아니라 손상 검출**이다.
- 새 `formatVersion`은 `VersionMismatch`로 거부한다.

## 로드 의미론 (전부 아니면 전무)
1. 문서 전체를 먼저 검증한다(구조, id/parent, 숫자 범위, 알 수 없는 컴포넌트 — `skipUnknownComponents`로 경고 처리 가능).
2. 객체는 **비활성 상태로** 생성되어 컴포넌트가 붙는다 → 반쯤 만들어진 씬에서 스크립트/컴포넌트 콜백이 돌지 않는다.
3. 어느 컴포넌트든 실패하면 이번 로드가 만든 객체를 전부 파괴하고 오류를 반환한다(씬 불변, 누수 없음 — 테스트로 확인).
4. 성공하면 환경을 적용하고 부모부터 `active`를 복원한다(이때 Awake/Enable 실행).

## 검증된 것과 한계
- 모든 내장 컴포넌트 왕복, `save(load(save(x))) == save(x)`, float 비트 정확 왕복(최단 10진 표현), 5000 객체 씬, 손상/절단/변이 퍼징(ASan 4시드×8000회, 로드 실패 시 씬 불변·누수 0), valgrind 통과, GPU 경로(MeshRenderer 재로드 후 픽셀 동일; lavapipe).
- 한계: 에셋 **내용**은 저장하지 않고 이름만 저장(에셋 파이프라인은 `.mingol`/에디터 책임), 컴포넌트 런타임 상태(재생 중인 오디오 보이스, 물리 속도)는 저장하지 않음, 프리팹/씬 중첩 없음, 스키마 마이그레이션은 아직 없다(`formatVersion` 1뿐).
