# 런타임: 게임 루프·물리·오디오

문서 라이선스: CC BY 4.0. 코드: GPL-2.0-only.

## 게임 루프 (`runtime::Runtime`)
`tick(dt)` 한 번:
1. `dt`를 `maxFrameTime`(기본 0.25 s)로 클램프, 타임스케일 적용.
2. **고정 스텝** 누적(기본 1/60 s, 프레임당 최대 8회): `fixedUpdate` → 물리 스텝 → 포즈 반영 → 접촉 콜백. 한도를 넘는 시간은 **버리고** `droppedFixedSteps`에 집계(죽음의 나선 방지).
3. `update` → `lateUpdate`를 프레임 시간으로 한 번.
시간은 호출자가 공급하므로 테스트·리플레이가 정확하다. `Runtime::run`은 단조 시계로 `tick`을 구동하는 편의 함수. `RuntimeStats`: frames, fixedSteps, droppedFixedSteps, time.

## 물리 (`engine/physics`, Bullet 3.24)
- `physics::World`가 Bullet을 완전히 숨긴다. 바디: 정적/동적/키네마틱. 모양: 박스·구·캡슐·삼각형 메시. 레이어/마스크, 축 잠금, **2D 평면 모드**(3D 바디에 Z축 제약).
- 이벤트: 접촉 시작/종료, 트리거 진입/이탈(바디가 파괴되면 종료 이벤트가 먼저 발생). 질의: 레이캐스트(트리거 제외), 구 겹침.
- 컴포넌트: `Collider`, `Rigidbody`(첫 `onStart`에 바디 생성 → addComponent 직후 설정 가능, 스케일 반영, Transform을 직접 바꾸면 텔레포트, **동적 바디만** 포즈를 Transform으로 역반영), `ContactListener`.
- 한계: 연속 충돌 검출·조인트·차량·소프트바디 미노출. 결정성은 한 기계에서만 확인. Windows는 vcpkg Bullet 필요.

## 오디오 (`engine/audio`)
- `Mixer`: 게임 스레드 명령 큐, 오디오 스레드는 try-lock만 사용. 선형 보간 리샘플, 등전력 팬, 거리 감쇠, 게인 램프, 하드 클립 카운터, 우선순위 보이스 스틸링.
- `Device`: miniaudio(Default/Null 백엔드). `RuntimeDesc::openAudioDevice=false`면 믹서만 있고 출력은 없다(헤드리스/테스트 기본).
- 컴포넌트: `AudioSource`, `AudioListener`.
- 한계: **실제 장치 미검증(Null만)**, HRTF·리버브·스트리밍 디코딩 없음(클립 전체를 메모리에 디코딩).

## 모델 인스턴스화
`runtime::uploadModel`(sRGB/선형 구분, 밉맵) → `GpuModel`; `instantiateModel`이 노드 계층을 GameObject로 만든다(노드 순서 무관, 순환 거부, 스킨드 메시는 루트 아래 항등 변환 + `Animator`).
