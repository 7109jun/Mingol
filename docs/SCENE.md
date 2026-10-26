# 씬: GameObject와 Component

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

`engine/scene` — Unity 스타일 계층. 단일 스레드 전용.

## 핵심 타입
- `Scene`: 객체 소유자. `createObject(name, parent)`, `find(name)`(생성 순서 첫 일치, 깊이 무관), `clear()`, 서비스 등록(`Runtime`이 물리/오디오를 꽂음), `callbackFailures()`.
- `GameObject`: 이름, 활성 플래그, `transform()`, `addComponent<T>()`, `getComponent<T>()`, `destroy()`.
- `GameObjectId`: **세대 번호 핸들**. 객체가 파괴되고 슬롯이 재사용돼도 옛 핸들은 해석되지 않는다.
- `Transform`: 로컬 TRS + 지연 갱신되는 월드 행렬(반복적 dirty 전파라 깊이 1만 계층도 스택 안전). `setParent(p, keepWorld)`, `lookAt`(위 벡터와 평행해도 안전), 월드 setter. **순환 부모 설정은 거부**.
- 내장 컴포넌트: `MeshRenderer`, `Animator`, `CameraComponent`(우선순위), `LightComponent`. 런타임/스크립트가 `Rigidbody`, `Collider`, `AudioSource`, `AudioListener`, `ScriptComponent`를 추가한다.

## 생명주기 순서
`onAwake → onEnable → onStart(1회) → [onFixedUpdate*] → onUpdate → onLateUpdate → onDisable → onDestroy`
- 비활성 객체(부모 포함)는 건너뛴다. 파괴는 **지연**되어 콜백 중 생성/파괴가 안전하다(자손 포함).
- 콜백이 예외를 던지면 로그를 남기고 그 컴포넌트만 비활성화(`onDisable` 1회). 장면 전체는 계속 돈다.
- `Awake/Enable`은 컴포넌트를 만드는 즉시 실행된다(에디터 편집 모드에서도). `Update` 계열은 `Runtime::tick`에서만.

## 렌더 연동
`buildRenderScene(Scene&, aspect)` 또는 `(Scene&, renderer::Camera)` — 활성 카메라 중 우선순위 최고를 사용, 카메라 스케일은 뷰 행렬에서 제거, 그릴 수 없는 항목은 컴포넌트당 1회 경고 후 건너뜀.

## 직렬화
컴포넌트 레지스트리(이름→팩토리, `sceneio`)를 통해 `.minseen`으로 저장/로드 → [MINSEEN.md](MINSEEN.md).

## 한계
컬링/공간 분할 없음, `Animator`는 루트 모션·이벤트·레이어 블렌딩 없음, 형제 순서 변경 API 없음.
