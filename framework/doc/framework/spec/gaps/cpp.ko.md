# C++ — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> **framework의 레퍼런스 구현이다.** 스펙은 이쪽이 완전할 것을 기대한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 0. 작업 방식 (이 문서를 맡은 사람이 먼저 읽는다)

**이 문서는 위에서부터 훑는 목록이 아니다.** 순서와 게이트가 있다.

### 0.1 왜 순서가 있나 — 이 감사의 핵심 발견

**e2e가 갭을 *못 잡는* 게 아니라, 잡을 수 없게 배치돼 있다.**

그래서 순진하게 framework부터 고치면 **고쳤는지 확인할 방법이 없다.** 게이트는 고치기 전에도
초록, 고친 뒤에도 초록이다. 실제 사례:

- Config 6은 store 장애 갭을 검증하라고 존재하는데 **`docker pause`만 쓴다.** 문서가 요구한
  stop/restart를 안 하므로 **결함이 터지는 조건 자체가 안 생긴다.**
- `MON-A2`(P0)는 monitoring 결함(매 tick 무조건 발행) **덕분에** 통과한다.
- `OBS-A2`는 결함으로 지목된 C++ 전용 토큰(`outcome=`)을 **오히려 못박는다.**

**그래서 규칙 하나**: **구현을 고쳤는데 해당 e2e·샘플이 여전히 통과한다면, 틀린 것은 e2e다.**

### 0.2 작업 단계

```
단계 0  재검증        이 문서의 행이 사실인지 확인한다        (전 항목, 항상 먼저)
단계 1  진짜 버그      게이트와 무관하게 지금 깨져 있다        (바로 고친다)
단계 2  게이트 복구    그 갭을 "실패시킬 수 있게" 만든다       (3단계의 선행 조건)
단계 3  책임 묶음      묶음 단위로 구현 + POSD 리팩토링        (묶음별로 닫는다)
단계 4  전체 감사      LOOP CLEAN 될 때까지 반복
```

#### 단계 0 — 재검증: **이 문서의 행은 증거이지 판결이 아니다**

감사 과정에서 **일곱 건이 검증에 무너졌다** — 갭이 아니었던 것, 수치가 과장된 것, 존재하지 않는
줄을 가리킨 인용, **잘못 기각된 진짜 결함**까지 있었다. 같은 비율이 남아 있다고 가정한다.

갭 하나를 시작할 때 **가장 먼저 하는 일은 구현이 아니라 재검증**이다.

1. **계약 열**의 문서·줄을 열어 그 문구가 정말 그렇게 적혀 있는지 본다.
2. **구현 열**의 코드·줄을 열어 그 코드가 정말 그렇게 도는지 본다.
3. 어긋나면 **행을 고친다.** 갭이 아니면 `~~취소선~~` + 근거를 남기고 닫는다.

**틀린 갭을 구현하는 것이 갭을 놓치는 것보다 나쁘다.**

#### 단계 1 — 진짜 버그: 게이트를 기다리지 않는다

`(**버그**)`로 표시된 항목은 **계약 위반이 아니라 동작이 깨진 것**이다. 사용자가 지금 당하고 있다.
게이트 복구를 기다리지 말고 먼저 고친다. 다만 **고치면서 그 버그를 잡는 단언을 함께 넣는다** —
안 그러면 다음에 또 들어온다.

#### 단계 2 — 게이트 복구: **3단계의 선행 조건**

**손댈 묶음이 정해지면, 그 묶음을 덮는 e2e·샘플 게이트부터 실패시킬 수 있게 만든다.**

1. 그 묶음의 갭을 드러내는 단언을 **먼저** 만들거나 고친다.
2. **실패하는 것을 눈으로 확인한다.** ← 이걸 못 하면 그 갭은 아직 이해하지 못한 것이다.
3. 그 다음에야 구현을 만진다.

**"실패할 수 없는 단언" 11가지 형태**를 체크리스트로 쓴다 — 느슨한 `>=`, 하드코딩 상수 단언,
무조건 PASS 출력, 자기가 던진 오류를 자기가 확인, 실패를 경고로 강등, 계약의 반대를 단언,
재시도·sleep으로 세탁, 경합 창을 스스로 닫음, 설치한 적 없는 것이 비었음을 단언,
존재하지 않는 것의 부재를 단언, 금지된 결과를 통과로 수용.
**새로 쓰는 단언이 그중 하나면 다시 쓴다.**

#### 단계 3 — 책임 묶음: 번호가 아니라 **책임**으로 짠다

이 문서는 감사 라운드 순으로 적혀 있다. **그 순서로 작업하지 마라.**
아래 축으로 이 문서의 ID를 재배열해 **자기 묶음 목록을 먼저 만든다.**

| 묶음 | 무엇을 공유하나 | 이 묶음을 덮는 게이트 |
|------|-----------------|----------------------|
| **A. 근거 없는 공개 표면** | 스펙 근거 없는 타입·메서드, 검증만 되고 안 읽히는 옵션, no-op 설정 | (게이트 없음 — 표면을 지우고 컴파일로 확인) |
| **B. Channel · messaging** | dispatch 키, timeout 분리, retry/dead-letter, session relay, packet registry | Config 1 · 4 |
| **C. Location runtime · store** | owner lease join, generation guard, 페이징, 원자성, auto-connect reconcile | **Config 6** (가장 심하게 깨진 게이트) · Config 1 |
| **D. 관측** | diff 기반 이벤트, polling 주기, source 축, 계기, flow 토큰 | **Config 7 · 11** (둘 다 결함 덕에 통과 중) |
| **E. Stream connector** | 재연결, connect 상태, timeout, heartbeat, dispatch 모드, metadata·압축 wire | Config 2 · 9 |
| **F. Spot · actor lifecycle · 동시성** | close/timer 취소, callback 직렬성, join admission/commit, transfer | Config 2 · 10 |
| **G. 샘플 도메인 · 레이어** | Domain/Application/Infrastructure 경계, framework 타입 누출, owner routing 책임 | 샘플 self-check |
| **H. 샘플 wire 계약** | nullable, enum 표현, 필드 drift, 계약에 없는 메시지 | 샘플 + 교차 언어 대조 — **§0.8 먼저 읽어라** |
| **I. 샘플 릴리스 게이트** | 번호 매긴 client 검증 흐름, push 대기 표면, 단언 강도 | (이것 자체가 게이트다 — 단계 2에 속한다) |
| **J. E2E 구조 · 러너** | 역할 프로젝트 분리, 시나리오 파일 분리, 대기 기준, 설정 전달, 축 변형 | (이것 자체가 게이트다 — 단계 2에 속한다) |

> **I와 J는 다른 묶음의 peer가 아니다.** 게이트 그 자체이므로 **단계 2에서 먼저 닫는다.**
> **H(wire 계약)는 네가 혼자 결정할 수 없다** — §0.8을 읽어라.

**한 묶음을 닫는 순서**

```
근거 재검증 → 공통 원인·의존성 파악 → 설계 대안 2개 이상 검토
  → 게이트 먼저 실패시키기 → 항목 구현 → 테스트 통과
  → 묶음 전체 POSD 리뷰 → 의미 있는 리팩토링 → 테스트·성능 재검증
  → 체크박스 [x] + 근거 한 줄
```

#### 단계 4 — 전체 감사 (아래 0.6)

### 0.3 POSD 리팩토링을 언제 하는가 — 3단계 게이트

**항목마다 구조를 뜯지 않는다. 마지막에 몰아서 하지도 않는다.**
[POSD 원칙](../../../../../doc/principal/software-design-principles.ko.md)은 개발 시간의 **10–20%를 지속적인
설계 개선**에 쓰라고 한다.

| 시점 | 하는 일 |
|------|---------|
| **각 항목 시작 전** | 책임 경계를 어디에 둘지 대안을 **둘 이상** 검토한다 |
| **각 항목 완료 후** | **위험 신호만 짧게 확인**한다. 구조는 건드리지 않는다 |
| **묶음 완료 후** | **실제 POSD 리팩토링을 수행**한다 |
| **문서 전체 완료 후** | 전체 구조 감사 → 수정 → 재리뷰를 **남는 게 없을 때까지 반복** |

**항목마다 전체 리팩토링을 하면 안 되는 이유**: 인접 항목이 같은 코드를 어떻게 바꿀지 아직 모른다.
그때마다 정리하면 다음 항목에서 또 바뀌고, **여러 결함의 공통 원인을 보기도 전에 성급한 추상화**가 생긴다.

**마지막에 몰아서 하면 안 되는 이유**: 초기의 잘못된 책임 경계 위에 후속 구현이 쌓인다.
나중에 고치면 변경 증폭과 테스트 범위가 폭발한다.

### 0.4 항목 완료 시 확인할 위험 신호 (리팩토링이 아니라 **감지**다)

- 새 public surface가 늘었는가?
- 호출자가 더 많은 내부 지식을 알아야 하는가?
- 같은 결정이 여러 파일로 퍼졌는가?
- 인자를 그대로 넘기는 pass-through 계층·메서드가 생겼는가?
- 특정 테스트나 샘플만을 위한 분기가 생겼는가?
- 고쳐야 할 파일 수가 예상보다 많았는가?

하나라도 걸리면 **그 묶음의 리팩토링 목록에 적어 둔다.** 지금 뜯지 않는다.

**다만 아래는 미루지 않는다** — "나중에 할 리팩토링"이 아니라 **지금 방향이 틀렸다는 신호**다.
항목 구현의 일부로 즉시 고친다.

- 샘플 전용 helper가 필요해진다
- 같은 정책을 두 모듈이 알아야 한다
- 내부 transport 정보(node rid, connection id, route channel)가 public API로 새어 나간다
- 호출자가 lifecycle 순서를 알아야 한다
- 같은 option이 여러 호출부에 반복된다

### 0.5 성능 게이트

아래 묶음은 **리팩토링 전 기준값을 먼저 재고**, 같은 runner·같은 runtime으로 후 비교한다.
구조가 좋아졌어도 **allocation · lock contention · Redis 왕복 · dispatch hop이 늘었으면 완료가 아니다.**

- send/request timeout 경로 · pending request 관리 (묶음 B)
- actor/session routing · dispatch 계층 (묶음 B·F)
- Redis lease · 페이징 · 원자성 (묶음 C)
- serialization · nullable 처리 (묶음 H)
- lock 제거 · 전역 상태 제거

### 0.6 문서를 닫는 조건

체크박스를 전부 `[x]`로 만드는 것이 끝이 아니다.

1. POSD 위험 신호(0.4) 전수 검색
2. public contract ↔ 실제 헤더/표면 재대조
3. 안 쓰이는 타입·helper·DI 등록 검색
4. 샘플·E2E에서 내부 타입 사용 여부 검색
5. 이 언어의 전체 테스트 실행
6. 성능 민감 변경 벤치 재실행
7. 수정 후 **다시** POSD 리뷰
8. **의미 있는 항목이 남지 않을 때까지 반복** → `LOOP CLEAN`

**파일 크기나 형식만을 이유로 리팩토링하지 않는다.** 책임 혼합·정보 누출·변경 증폭·호출자 복잡성을
**실제로 줄이는** 변경만 한다.

### 0.7 선행 관계 (뒤집으면 깨진다)

- **join orchestration**(§12.24) → **`yield` terminator**(§12.21) → **샘플의 `yield` 사용처**(SMP-X1)
  — 뒤집으면 `yield` 없이 `async`로 흉내 내게 되어 **샘플이 보여 주려던 대비 자체가 사라진다.**
- **framework의 owner-lease join** → **store의 lease script·페이징** — store부터 고치면 아무도 안 쓴다.
- **wire 계약(묶음 H)은 한 언어만 고치면 오히려 깨진다.** 결정은 §0.8의 2단계 프로토콜을 따른다.
- 어떤 갭이 다른 갭 **덕분에** 가려져 있는 경우가 있다(`MON-A2` ← monitoring 결함). **그런 쌍은 함께 연다.**

### 0.8 이 문서의 경계 — **네가 혼자 결정하면 안 되는 것**

**너는 한 언어를 맡았다. 그런데 어떤 갭은 계약을 바꿔야 닫힌다.**
계약은 다섯 언어가 공유한다. **개별 에이전트가 계약을 바꾸면 다른 넷이 즉시 깨진다.**

그래서 이 리포는 **결정과 구현을 분리**한다. 이미 있는 규약이다 —
「교차 언어 결함 — 이 언어에서 무엇을 고치나」 절이 그것을 명시한다:
**갭 인덱스가 "왜"(계약과 결정)를 소유하고, 이 문서의 표가 "무엇을"(이 언어의 작업)을 소유한다.**

#### 2단계 프로토콜

```
1단계  계약 결정   ← 네 소관이 아니다. 멈추고 올린다.
2단계  언어 구현   ← 네 소관이다. 결정이 내려온 뒤에 한다.
```

**1단계에 해당하는 것 (멈춰라)**

- 공유 wire 메시지의 **필드 추가·삭제·이름 변경·타입 변경**
  (예: `QuestProgress`에 `Version`을 넣을 것인가, `DeliveryStatus`를 문자열로 할 것인가)
- **`SMP-X*` · `IMP-X*`** 로 표시된 항목 — 정의상 전 언어 공통이다
- 공통 spec·공통 sample·공통 e2e 문서(`common/**`, `spec/**`)의 **계약 변경**
- 새 public API 추가 — 다른 언어에 있다는 것은 근거가 아니다([AGENTS.md](../../../../../AGENTS.md))

**멈춘 뒤 할 일**

1. **구현하지 마라.** 우회 helper·내부 API·프레임 직접 조작으로 메우지도 마라.
2. 이 문서의 해당 행에 **무엇을 결정해야 하는지, 선택지가 무엇인지** 적는다.
3. [갭 인덱스](../90-implementation-gap.ko.md)에 올린다. **결정은 거기서 한 번만 내린다.**
4. 결정이 내려오면 **그때 네 언어에만** 구현한다.

**2단계에 해당하는 것 (네가 한다)**

- 이미 계약이 정해져 있는데 **네 언어만 안 지키는 것** — 대부분의 갭이 여기다
- 네 언어의 e2e·샘플·러너·내부 구조
- 갭 인덱스가 **이미 결정을 내려 둔** `IMP-X*`의 "이 언어의 작업" 열

#### 판별법

> **"내가 이걸 고치면 다른 언어의 테스트가 깨지는가?"**
> — 깨지면 1단계다. 안 깨지면 2단계다.

교차 언어 wire 호환은 **이 감사가 실제로 파손을 확인한 자리**다
(`DeliveryStatus`가 `.NET`만 정수, `QuestProgress.Version`이 `.NET`에만 없음).
**혼자 고치면 파손을 다른 방향으로 옮길 뿐이다.**

## 1. 진행 체크리스트

**전체 20건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [x] **IMP-CP-01** (결함) — 20 §3.3
  - 근거: 수정 전 target-contract gate가 subscription lookup에서 wire packet name 비교 부재를 검출했다. fanout envelope에서 decoded `message_name`을 보존하고 topic과 packet name이 모두 일치하는 descriptor만 선택하도록 바꿨다. 같은 topic에 `state_update_t`와 `stage_closed_t` handler를 등록해 두 event를 연속 발행하는 회귀에서 각각 9와 17을 올바른 handler가 받았고 target gate와 spot runtime test가 통과했다.
- [ ] **IMP-CP-02** (결함) — 31
- [ ] **IMP-CP-03** (결함) — 24 §3
- [ ] **IMP-CP-04** (미구현) — 20 §8·30 §7.2
- [ ] **IMP-CP-05** (결함) — 40 §2.1·02 §4
- [ ] **IMP-CP-06** (결함) — 40 §8.2·§6.1
- [ ] **IMP-CP-07** (결함) — 40 §2.3·§5.1
- [ ] **IMP-CP-08** (미구현) — 30 §6
- [ ] **IMP-CP-09** (미구현) — 40 §9
- [ ] **IMP-CP-10** (결함) — 40 §7
- [ ] **IMP-CP-11** (미구현) — 31

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X1** — pending actor row(`ActorRef` 비어 있음)를 resolve 성공으로 반환한다
- [ ] **IMP-X2** — location event source(`location-peer/spot/actor/route`, `StoreFailure`/`StoreRecovered`)가 없다
- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.2** — actor join admission이 선택 사항 (Java, C++)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

**C++은 framework의 레퍼런스 구현이다.** 스펙은 이쪽이 완전할 것을 기대하는데, 가장 많이 나왔다.

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-CP-01** | 결함 | [20 §3.3](../server/20-spot-messaging.ko.md): SPOT subscribe의 dispatch 키는 **topic + packet name** | `spot_runtime.cpp:4848-4854` — `descriptor.topic == topic`만 보고 **첫 번째 descriptor를 집는다.** wire의 packet name을 **읽고도 쓰지 않는다.** 한 topic에 handler 둘을 등록하면(등록은 허용됨) 발행자가 `RoomClosed`를 보내도 **`RoomJoined` handler가 불리고 payload를 그 타입으로 디코드**한다 |
| **IMP-CP-02** | 결함 | [31](../server/31-session-actor-dispatch.ko.md): unbind는 **같은 `sessionId + bindingToken`인 entry만** 제거한다. **이전 stream의 늦은 정리가 새 binding token을 지우면 안 된다** | `actor_gateway_runtime.cpp:1070-1079` — `unbind_session_stream(actor_id)`, **토큰도 세션 식별자도 없다.** framework가 disconnect 시 자동 해제하지도 않아 샘플들이 직접 부른다. ⇒ 죽은 소켓의 늦은 정리가 **재접속한 세션의 sink를 지운다.** 재접속한 플레이어가 **조용히 벙어리**가 된다 |
| **IMP-CP-03** | 결함 | [24 §3](../server/24-spot-address-messaging.ko.md): snapshot 갱신 트리거는 셋(**location event · 주기 재조회 · stale 1회**). **spot rid와 node rid를 나란히 받는 전송 overload는 없다** | handle registry가 **없어서** 트리거 ①②가 존재하지 않고, 갱신은 request 재시도 1곳뿐(`channel_runtime.cpp:1804`). 게다가 `spot.hpp:556,581`의 spot-context outbound는 `request_to(node_rid, spot_rid, …)` — **스펙이 금지한 그 overload**이며 snapshot이 없어 refresh도 없다. ⇒ 방이 노드를 옮기면 이후 전송이 **영원히 죽은 노드로 간다** |
| **IMP-CP-04** | 미구현 | [20 §8](../server/20-spot-messaging.ko.md)·[30 §7.2](../server/30-stream-session.ko.md) | **registration validator가 아예 없다**(`app.cpp:689`). router/pub-sub 없는 SpotNode는 **조용히 버려지고**(`spot_node_host_service.cpp:377-380`), bind 없는 stream node는 **연결을 0개 받으며 healthy로 기동**하고, stream node 이름 중복은 **마지막 것이 이긴다** |
| **IMP-CP-05** | 결함 | [40 §2.1](../server/40-location-runtime.ko.md)·[02 §4](../02-interaction-model.ko.md): **RouteMesh row의 Role은 항상 `Router`**다. runtime은 RouteMesh의 dealer row를 **호환 입력으로 받지 않는다** | `location_auto_connect_host_service.hpp:120-126` — endpoint 없는 route channel이 **`dealer` role row를 게시**하고, `role_allowed()`(:405-406)가 dealer를 **받아들인다.** ⇒ 공유 store에 dealer가 들어가 같은 mesh의 `.NET`/Java peer가 그 row를 **거부한다. 교차 언어에서 깨진다** |
| **IMP-CP-06** | 결함 | [40 §8.2·§6.1](../server/40-location-runtime.ko.md): polling interval·store failure grace를 따르고, 복구 후 disconnect diff는 **heartbeat 1회 유예 뒤** 적용한다 | `location_auto_connect_host_service.hpp:280` — reconcile 주기가 **`sleep_for(100ms)` 하드코딩**. `store_failure_grace`는 **읽는 곳이 없다.** 복구 유예도 없다. ⇒ Redis가 빈 데이터로 재시작하면 **다음 100ms tick에 mesh 연결을 전부 끊는다** |
| **IMP-CP-07** | 결함 | [40 §2.3·§5.1](../server/40-location-runtime.ko.md) | pending actor row를 성공 resolve로 반환하고(IMP-X1), **observed generation 상태가 아예 없다.** ⇒ `.NET`이 쓴 pre-activation claim row를 C++가 **커밋된 위치로 착각**한다 |
| **IMP-CP-08** | 미구현 | [30 §6](../server/30-stream-session.ko.md): session에 귀속되는 transport 오류 → **session 오류 callback** | `on_error`가 선언돼 있고 `dispatch_error`가 정의돼 있는데 **호출하는 곳이 없다.** `stream_session_error_t::transport_error`가 **한 번도 생성되지 않는다.** ⇒ 앱이 정상 로그아웃과 전송 장애를 구분할 수 없다 |
| **IMP-CP-09** | 미구현 | [40 §9](../server/40-location-runtime.ko.md) | `location_event_kind_t`에 `StoreFailure`/`StoreRecovered`가 **없고**(`events.hpp:56-61`), per-row source 4개도 없다(IMP-X2). ⇒ **store 장애가 관측자에게 보이지 않는다** |
| **IMP-CP-10** | 결함 | [40 §7](../server/40-location-runtime.ko.md): topology는 row + connection state + **lease** + generation의 projection. [54 §3.2](../server/54-graceful-drain-handoff.ko.md): **`Weight`는 전송 부하 가중치이지 lifecycle 신호가 아니다** | `store_location_resolvers.hpp:269-303` — peer만 투영하고 `state = weight==0 ? lost : ready`. ⇒ 부하를 덜려고 weight를 0으로 두면 **멀쩡한 노드가 `Lost`로 보고**되고, spot/actor/route topology 조회는 **항상 빈 페이지**를 반환한다 |
| **IMP-CP-11** | 미구현 | [31](../server/31-session-actor-dispatch.ko.md): session context는 **packet handler registry**를 갖는다. 같은 packet name 중복 등록은 startup 오류. **packet-name switch를 금지한다** | `stream.hpp:253-270` — session context 타입도 `Configure()`도 **없다.** 그래서 모든 C++ session이 **packet-name switch를 손으로 쓴다** — 스펙이 명시적으로 금지한 형태다 |

## 3. 언어별 표면 차이 상세

### §12.2 actor join admission이 선택 사항 (Java, C++)

**미충족(Java, C++).** [22 §8](../server/22-actor-model.ko.md)과 [23 §12](../server/23-spot-actor.ko.md)는 actor join
admission을 **필수 등록 축**으로 규정한다. `.NET`은 이를 default 구현 없는 interface member로 두어
구현 누락 자체가 불가능하다.

Java는 `onActorJoin`에 default 구현이 있고 그 기본값이 **거절**이다. C++은 duck typing으로 존재할
때만 호출하며, 일반 spot에서 없으면 **거절**로 대체한다. 두 경우 모두 admission을 빠뜨리면
컴파일과 시작은 통과하고 **모든 actor join이 조용히 거절**되는 실패 모드가 생긴다.

## 라운드 2 (2026-07-14) — Stage · 관측 · connector · HTTP client

**C++은 레퍼런스 구현인데 이번 라운드에서도 가장 많이 나왔다.**

### 체크리스트

- [x] **IMP-CP-12** (결함) — **spot을 닫아도 그 spot의 timer가 멈추지 않는다.** 닫힌 spot에 tick이 계속 들어가고 컨텍스트가 누수된다
  - 근거: 수정 전 spot timer 회귀 테스트에서 callback 안에서 close가 완료된 뒤에도 timer가 disposed되지 않아 종료 코드 22로 실패했다. `close_now()`가 lifecycle `on_closing` 뒤 context의 모든 native timer를 중지·dispose하도록 framework 책임으로 묶은 뒤 spot timer/runtime ctest 2개와 Bingo 전체 runner가 통과했다.
- [ ] **IMP-CP-13** (결함) — location 모니터링이 **diff 없이 매 tick 이벤트를 발행**한다
- [ ] **IMP-CP-14** (결함) — 등록한 location polling 간격에 **숨은 1초 상한**이 걸린다
- [ ] **IMP-CP-15** (결함) — 스펙이 정한 **spot source가 timer 실패 이벤트를 내지 못한다**
- [ ] **IMP-CP-16** (미구현) — 계기 8개 결측
- [ ] **IMP-CP-17** (결함) — `add_spot_events(name, interval)`의 **interval을 읽는 곳이 없다**
- [x] **IMP-CP-18** (결함) — 폴백 로그가 `phase=` 대신 **`outcome=`**을 쓴다
  - 근거: 수정 전 message-flow unit gate를 `phase=` 계약으로 바꾸자 종료 코드 3으로 실패했다. fallback structured field를 `phase` 하나로 교체하고 SpotService·ObservabilityOps의 실제 로그 판정을 갱신한 뒤 unit test, shell syntax/no-hit gate, `ObservabilityOps/run_e2e.sh flow`의 OBS-A1·A2가 통과했다.
- [ ] **IMP-CP-19** (미구현) — **connector에 자동 재연결이 없다.** 수립된 연결이 끊기면 영원히 `Disconnected`
- [ ] **IMP-CP-20** (결함) — connector `connect()`가 **현재 상태를 무시**한다(`Closed`도 되살아나고, `Connected`면 소켓이 하나 더 열린다)
- [ ] **IMP-CP-21** (미구현) — connector `connect_timeout`(기본 5초)을 **적용하지 않는다**
- [ ] **IMP-CP-22** (결함) — connector heartbeat이 **`dispatch()` 안에서만** 돈다
- [ ] **IMP-CP-23** (결함) — `Manual` 모드인데 error/state/disconnected callback을 **IO 스레드에서 인라인 호출**한다
- [x] **IMP-CP-24** (결함) — connector metadata 디코더가 **빈 값을 거부**한다 (교차 언어 wire 파손)
  - 근거: 수정 전 metadata codec 회귀 테스트에서 encoder가 만든 `{key: ""}` 프레임을 decoder가 거부해 종료 코드 70으로 실패했다. decoder는 key 길이만 1 이상으로 유지하고 0 길이 value를 허용하도록 수정한 뒤 connector 전체 protocol/integration ctest가 통과했다.
- [ ] **IMP-CP-25** (결함) — HTTP: streaming body가 **redirect에서 빈 채로 재전송**된다
- [x] **IMP-CP-26** (결함) — HTTP: 압축 해제 후 **`content-length`를 제거하지 않는다**
  - 근거: 수정 전 gzip·deflate 회귀 테스트에서 압축 해제된 body에 압축 전송 길이 `content-length`가 각각 남아 두 테스트가 실패했다. 두 압축 경로가 `content-encoding`과 `content-length`를 함께 제거하도록 수정한 뒤 대상 테스트 2개와 HTTP client 전체 ctest가 통과했다.
- [x] **IMP-CP-27** (결함) — connector send payload 한도를 **압축 전** payload에 적용한다
  - 근거: 수정 전 실제 connector/server 회귀에서 128바이트 원본이 압축 후 16바이트 한도 안인데도 종료 코드 179로 거절됐다. 원본 크기 사전 검사를 제거하고 frame encoder가 압축 후 wire payload를 한 번만 검사하도록 수정한 뒤 전체 stream connector unit binary가 통과했다.

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-CP-12** | [25 §2·§8](../server/25-stage-wrapper-on-spot.ko.md): timer 등록·취소는 **framework**가 한다. **spot 종료 뒤 추가 callback을 만들지 않는다** | `spot_runtime.hpp:183-213` — `close_now()`가 `on_closing`을 부르고 location row를 놓고 spot을 레지스트리에서 **지우지만 `timers`는 건드리지 않는다.** 유일한 취소 경로 `cancel_timers()`(`spot_runtime.cpp:4122`)는 **노드 종료 시에만** 돌고, 하필 `close_now()`가 이미 지운 그 맵을 순회한다. native timer는 `max()` 반복으로 시작했고 lambda가 context를 `shared_ptr`로 잡고 있다. ⇒ 방을 닫아도 **타이머가 프로세스 수명 내내 틱을 돌리며 닫힌 spot의 handler를 호출**하고, 방마다 컨텍스트·직렬 큐·handler 그래프가 **통째로 누수**된다. `.NET`·Node·Java는 종료 시 dispose한다 |
| **IMP-CP-13** | [50 §2](../server/50-runtime-monitoring.ko.md): 주기적으로 상태를 읽고 **직전 상태와 비교해 바뀐 때만** event를 합성한다 | `monitoring_runtime.cpp:277-308` — 직전 스냅샷 상태가 **아예 없고** 매 tick `status_changed` + `topology_changed` + `service_summary_changed`를 **무조건** 발행한다. ⇒ 유휴 클러스터에서도 **초당 3개**가 영원히 나가고, `TopologyChanged`에 붙은 앱 handler(재dial·캐시 재구축)가 **1초마다 같은 데이터로** 돈다 |
| **IMP-CP-14** | [50 §4](../server/50-runtime-monitoring.ko.md): **숨은 기본 주기를 두지 않는다** | `location_monitoring_host_service.hpp:63-72` — `polling_interval()`이 **1초에서 시작해 더 작은 값만** 취한다. ⇒ `add_location_events("loc", 30s)`가 **1초마다** 돈다 — 설정값의 30배이고, 설정한 값은 동작에서 **읽어낼 수조차 없다** |
| **IMP-CP-15** | [50 §3.1](../server/50-runtime-monitoring.ko.md): "timer handler 실패"·"예외로 timer 중단"은 **spot source**의 event | `monitoring_runtime.cpp:336-345` — `publish_timer_failure()`가 **C++ 전용** `add_spot_timer_events()`로만 채워지는 별도 source를 요구한다. ⇒ 스펙대로 `add_spot_events("play", 1s)`만 등록한 앱은 timer handler가 터져 멈춰도 **이벤트를 하나도 못 받는다** |
| **IMP-CP-16** | [51](../server/51-runtime-metrics.ko.md) | 계기 8개 결측 — `stream.session.bind.duration`, `stream.{inbound,outbound}.bytes`, `spot.timer.tick.lateness`, `actor.count`, `actor.mailbox.depth`, **`channel.messages.dropped`**, **`observability.observer.overflow`**. observer drop은 **프로세스 static atomic**으로만 세고 있어 계기가 없다 |
| **IMP-CP-17** | [50 §2·§4](../server/50-runtime-monitoring.ko.md) | `monitoring_runtime.cpp:148-157` — `add_spot_events`의 `interval`을 **검증만 하고 쓰지 않는다.** spot event는 대신 **변경 지점마다** push된다. ⇒ 설정한 polling 비용이 **허구**이고, 요동치는 peer가 tick당 diff 하나가 아니라 **변경마다 이벤트 하나**를 낸다 |
| **IMP-CP-18** | [52 §5·§5.1](../server/52-message-flow-tracing.ko.md): 폴백 로그는 **전 언어 동일 토큰** — `zlink flow: phase=… surface=…` | `message_flow_tracer.hpp:211` — `add("outcome", …)`. `.NET`·Java·Node는 `phase=`를 쓴다. ⇒ `grep phase=`로 수집하는 파이프라인이 **C++ 라인을 전부 놓친다** |
| **IMP-CP-19** | [32 §6](../stream-connector/32-stream-connector.ko.md): **자동 reconnect는 기본으로 켜져 있다.** `Reconnecting` = 자동 재연결 진행 중 | `connector_runtime.cpp:756-828` — `options.reconnect`를 읽는 **유일한 곳**이 `connect()` 안의 재시도 루프다. 수립된 연결이 끊기면(`zlink_stream_calls.cpp:854-872`, `:1439-1448`) `disconnected`로 바꾸고 pending을 실패시킬 뿐 **다시 dial하지 않는다.** ⇒ 서버가 재시작하면 .NET/Java/TS는 250ms 만에 붙는데 **Unreal/Godot 클라이언트만 영원히 죽는다.** C++의 `Reconnecting`은 **"첫 연결을 재시도 중"이라는 뜻일 뿐**이다 |
| **IMP-CP-20** | [32 §6](../stream-connector/32-stream-connector.ko.md): `Closed` → **오류로 실패**. `Connected` → 즉시 성공 반환. `Connecting` → 진행 중인 시도를 기다린다 | `connector_runtime.cpp:745-751` — `state->state`를 **읽지 않고** 무조건 dial 루프로 들어간다. ⇒ `close()` 뒤 `connect()`가 **닫힌 connector를 되살리고**(스펙 금지), `Connected` 상태의 `connect()`는 **소켓을 하나 더 열어** 이전 소켓을 read pump가 붙은 채로 누수시키고 서버엔 **중복 세션**이 보인다 |
| **IMP-CP-21** | [32 §6.1·§9](../stream-connector/32-stream-connector.ko.md): connect timeout 기본 5초 | `options.connect_timeout`의 **읽는 곳이 0개**다. `boost::asio::connect`에 deadline이 없다. `connect_timeout` 오류 코드는 **모든 연결 실패에 붙이면서** 정작 timeout은 적용하지 않는다. ⇒ 블랙홀 IP에 5초가 아니라 **OS SYN 재시도 기본값(~130초)** 동안 블록된다 |
| **IMP-CP-22** | [32 §6](../stream-connector/32-stream-connector.ko.md): 켜져 있으면 주기마다 control ping을 보내고, timeout 동안 inbound frame이 없으면 끊긴 것으로 처리한다 | `zlink_stream_calls.cpp:1437-1454` — heartbeat 검사와 발송이 **`dispatch()`에서만** 불린다. ⇒ `Immediate` 모드 앱은 `dispatch()`를 안 부르므로 **ping을 하나도 안 보내고 죽은 peer를 감지하지 못한다.** half-open TCP에서 `Connected`로 남고 pending은 30초 request timeout에서야 실패한다 |
| **IMP-CP-23** | [32 §7](../stream-connector/32-stream-connector.ko.md): `Manual` = receive loop가 handler·**error**·**disconnect**·request callback을 직접 호출하지 않고 큐에 넣는다 | `connector_runtime.cpp:319-326`·`zlink_stream_calls.cpp:168-173` — state·disconnected·error handler를 **인라인 호출**한다. packet handler와 request callback만 큐에 넣는다. ⇒ Unreal/Godot의 `on_disconnected`가 엔진 객체를 만지면 **Boost.Asio 워커 스레드에서 불려 크래시**한다 — **Manual 모드가 존재하는 바로 그 이유**인데 |
| **IMP-CP-24** | [32 §4.4](../stream-connector/32-stream-connector.ko.md): 유일한 제약은 `key_len ≥ 1`. `val_len`에 최소값이 없다 | `metadata_codec.cpp:105-108` — `value_size == 0`이면 `frame_decode_failed`. `.NET`·Java·TS는 빈 값을 **정상 인코딩한다.** ⇒ 어떤 peer가 `metadata("tenant","")`를 보내면 C++ connector가 **세션을 끊는다. 교차 언어 wire 파손** |
| **IMP-CP-25** | [http 06 §6.1](../http-client/06-redirect-retry-cookie.ko.md): 307·308은 보존하되 **streaming body는 rewind 불가라 드롭**한다 | `request_performer.cpp:99-124,272-276` — redirect 루프가 method/body를 바꾸면서 **provider를 비우지 않고**, 다음 홉에서 원본 `_request.body_provider`를 다시 읽는다. ⇒ `307`이면 이미 소진된 provider가 즉시 `nullopt`를 내서 서버가 **0바이트 파일을 저장하는데 호출은 200으로 성공**한다. `303`이면 chunked **GET**을 내보내 많은 서버가 거부한다 |
| **IMP-CP-26** | [http 08](../http-client/08-compression.ko.md): 해제 후 `content-encoding`**과 관련 length** 헤더를 제거한다 | `request_performer.cpp:127-135` — `content-encoding`만 지우고 **`content-length`는 남긴다.** ⇒ 그 값으로 버퍼를 잡는 호출자가 18KB 본문에 **압축 길이 1.4KB**를 읽고 잘린다 |
| **IMP-CP-27** | [32 §4.7](../stream-connector/32-stream-connector.ko.md) | `zlink_stream_calls.cpp:122-126` — 압축 전 크기로 검사한다. `.NET`(IMP-DN-13)·Java(IMP-JV-20)와 **같은 결함** |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합 · Redis store

### 체크리스트

- [x] **IMP-CP-28** (결함) — `extension_boundaries.hpp` — **설치되는 공개 헤더인데 스펙 근거도 구현도 0**
  - 근거: 수정 전 target-contract gate가 무근거 extension 설치 헤더 2개와 no-op CMake export 표면을 검출했다. 11개 placeholder target, umbrella/boundary header, 이를 양성으로 고정하던 unit·layout·package test 의존성을 제거한 뒤 target/header/layout contract test와 실제 install-consumer package test가 모두 통과했다.
- [ ] **IMP-CP-29** (결함) — `unhandled_dispatch_options_t` 5개 필드가 **검증만 되고 읽히지 않는다**
- [ ] **IMP-CP-30** (결함) — `on_retry`/`on_dead_letter` — **C++에만 있는 메시지 신뢰성 계약**, 스펙 근거 0
- [x] **IMP-CP-31** (결함) — send backpressure 기한이 **30초** — 스펙은 1000ms이고, request timeout을 재사용한다
  - 근거: 수정 전 contract gate가 one-way send의 request timeout 재사용과 독립된 1000ms 기본 부재를 모두 검출했다. send backpressure 기본을 1000ms로 분리하고 명시적 timeout만 우선하도록 수정한 뒤 gate와 channel messaging unit binary가 통과했다.
- [ ] **IMP-CP-32** (결함) — `zlink_builder_t`·`message_bus_t`가 **C++ 스펙 스스로 비계약이라 선언한 내부 타입**을 노출한다
- [x] **IMP-CP-33** (결함) — `include_native_diagnostics`를 **읽는 곳이 없다**
  - 근거: 수정 전 target-contract gate가 runtime에서 읽히지 않는 `include_native_diagnostics` public option을 검출했다. 계약에 없는 setter·getter·storage와 이를 효과가 있는 것처럼 확인하던 test 기대를 제거한 뒤 target/header contract test와 module-hosted unit test가 통과했다.
- [x] **IMP-CP-34** (결함) — **`close_erased()`가 `callback_depth`/`close_requested`를 잘못된 mutex로 읽고 쓴다**
  - 근거: 수정 전 구조 게이트가 `close_erased()`의 callback 상태 전이가 `callback_mutex` 밖에서 수행됨을 검출했다. depth 확인·close 요청 기록과 `close_now()`의 요청 초기화를 기존 `callback_mutex`로 보호하고, 별도 스레드의 callback을 barrier로 유지한 채 close가 callback 종료까지 Spot을 보존하는 회귀 테스트를 추가했다. spot timer/runtime ctest 2개가 통과했다.
- [ ] **IMP-CP-35** (결함) — framework runtime에 **owner-lease join이 아예 없다.** store에 떠넘겼다
- [x] **IMP-CP-36** (결함) — Redis 페이징이 SSCAN 커서가 아니라 **SMEMBERS + 정수 오프셋**이다
  - 근거: 수정 전 target-contract gate가 `SSCAN`·cursor state 부재와 정수 `parse_offset` 사용을 모두 검출했다. location page가 Redis cursor와 batch의 미처리 key를 opaque JSON continuation token에 보존하고 filter를 적용하면서 page size를 지키도록 바꿨다. live Redis에서 25개 actor를 page size 3과 actor-type filter로 순회한 회귀 test가 여러 page의 cursor·pending token, 13개 결과의 무누락·무중복을 확인했고 target gate와 기존 round-trip도 통과했다.
- [x] **IMP-CP-37** (결함) — actor row에 **다른 셋에는 없는 `mesh` hash 필드**를 쓴다
  - 근거: 수정 전 target-contract gate가 actor write에 `spot_mesh_name`을 넘기는 C++ 전용 물리 schema를 검출했다. actor write도 remove와 같이 global stamp용 `nullopt`를 사용하도록 바꾸고 live Redis integration test에서 actor hash field가 정확히 4개이며 `mesh`가 없음을 직접 확인한 뒤 gate와 `RedisServerRoundTripUsesStoreSchema`가 통과했다.
- [x] **IMP-CP-38** (결함) — lease remove/list가 **자기 Lua script를 쓰지 않는다**(그 script는 dead code)
  - 근거: 수정 전 target-contract gate가 lease remove와 list의 단일-script `eval` 부재를 각각 검출했다. remove script가 삭제 여부를 함께 반환하고 list script가 같은 Redis `TIME` 기준 lease triples를 반환하도록 연결한 뒤 gate와 Redis unit test가 통과했다. 임시 Redis 7 container를 사용한 실제 integration run에서도 `RedisServerRoundTripUsesStoreSchema`와 `LuaScriptsPreserveDotnetAtomicStoreContract`를 포함한 13개 test가 통과했다.

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-CP-34** | [04](../04-async-execution-policy.ko.md): 한 spot의 두 callback은 동시에 실행되지 않는다. [21 §close](../server/21-spot-node.ko.md) | `callback_depth`·`callback_thread`·`close_requested`는 **`callback_mutex`**가 지킨다(`spot_runtime.hpp:253-261`). 그런데 `close_erased()`(`spot_runtime.cpp:994-1005`)는 **`node->mutex`만 잡고** 그 둘을 읽고 쓴다. 결과가 두 갈래다 — ① **살아 있는 callback 한가운데서 close가 실행된다**: 낡은 `callback_depth == 0`을 보고 `close_now()`로 직행해 다른 스레드가 handler를 실행 중인 spot의 컨텍스트를 헐어 버린다. **한 spot에서 두 callback이 동시에 도는 것** — 직렬 줄이 존재하는 유일한 이유가 그걸 막는 건데. ② **close가 조용히 유실된다**: `leave_callback`이 먼저 `close_requested == false`를 읽고 나가면, 뒤늦게 A가 낡은 `callback_depth == 1`을 보고 `close_requested = true`를 쓴다. **`close_spot()`은 성공을 반환했는데 방은 영원히 안 닫힌다** |
| **IMP-CP-28** | [00 §3](../00-public-contract-governance.ko.md): 스펙 근거 없이 public API를 만들지 않는다 | `extensions/include/.../extension_boundaries.hpp` — `cpp/CMakeLists.txt:633`이 **consumer 패키지로 설치한다.** `kafka_bridge_extension_t::map_topic`, `grpc_bridge_extension_t`, `http_gateway_extension_t`, `dead_letter_storage_extension_t`, `flatbuffers_codec_extension_t`, `custom_transport_extension_point_t`… 스펙 트리 grep **0건**, 헤더 밖 사용처 **0건**, 구현 **0줄**. **리포에서 가장 큰 무근거 공개 표면**이고 전부 no-op이다 |
| **IMP-CP-29** | [11 §3.1](../server/11-channel-messaging.ko.md): 미처리 dispatch 정책은 framework가 고정한다 | `contracts/dispatch/execution.hpp:55-62`의 5개 필드가 **startup에서 검증된다** — 그래서 앱은 "먹혔다"는 확인을 받는다. 그런데 **읽는 곳이 0**이고 실제 정책은 `dispatch_error_reporter.hpp:73-87`에 **박혀 있다** |
| **IMP-CP-30** | [24](../server/24-spot-address-messaging.ko.md): **도메인 idempotency가 필요한 일반 retry는 application 정책이며 handle이 대신하지 않는다** | `zlink_builder.hpp:49-50`의 `on_retry`/`on_dead_letter`가 **살아 있다**(`channel_runtime.cpp:1847-1856, 521-542`에서 등록·호출). C++ 카탈로그는 `zlink_builder_t`를 **정확히 7개 메서드**로 고정하고, 스펙 트리에 `on_retry`/`dead_letter` grep **0건**. ⇒ **C++에만 존재하는 메시지 신뢰성 계약**이며, 스펙이 명시적으로 application 몫이라고 한 것을 framework가 가져갔다 |
| **IMP-CP-31** | [05](../05-framework-api.ko.md): framework 기본값은 core socket 기본 send timeout과 같은 **1000ms**. `Timeout(...)`은 **reply 대기 시간만** 정하고 전송 backpressure는 `SendTimeout` 정책이 처리한다 | `channel_outbound_exchange.cpp:1014-1016` → `resolve_channel_wait_timeout`(:259-272)이 **`default_request_timeout` = 30초**로 폴백한다. C++엔 **send timeout 개념 자체가 없다**(`grep send_timeout` → 0건). ⇒ 포화된 peer에 대고 `send(...).submit()`이 **30초 매달린다**(.NET/Java는 1초). 그리고 스펙이 **명시적으로 분리하라고 한** request/reply timeout을 send timeout으로 재사용한다 |
| **IMP-CP-32** | C++ 카탈로그 §16.24: `*_state_t`·`*_snapshot_t`·`*_access_t`는 **application이 직접 다루지 않는다**. §3.2: **pending request table은 runtime이지 계약이 아니다** | `zlink_builder.hpp:56-59`의 public 메서드 4개가 `channel_snapshot_t`/`spot_node_snapshot_t`/`stream_snapshot_t`를 반환하고(스펙의 클래스에는 없는 메서드다), `channels/channel.hpp:461-462`가 앱에게 `message_bus_t::pending_count()`/`pending_limit()`을 준다(스펙 grep 0건) |
| **IMP-CP-33** | — | `contracts/dispatch/execution.hpp:74,96,254-258`이 전부. 형제 `include_message_sizes`는 살아 있다 |
| **IMP-CP-35** | [40 §1·§5.1](../server/40-location-runtime.ko.md): **store는 저장만 하고, owner lease join·generation guard는 framework runtime의 책임**이다. 성공 결과에 stale row가 섞이면 안 된다 | `store_location_resolvers.hpp:230` — 트리 전체에서 `list_owner_leases()`를 부르는 **유일한 곳**이 `get_status()` 안의 **버려지는 health probe**다. resolver도 list도 auto-connect도 lease를 join하지 않는다. **join을 store 안으로 떠넘겼다**(`redis.hpp:1411-1426`). ⇒ 스펙대로("store는 저장만") 작성한 **사용자 store는 죽은 owner의 row를 영원히 반환한다.** auto-connect가 죽은 노드를 계속 dial하고, `resolve_spot`이 사라진 노드의 방을 계속 돌려준다. 게다가 Redis 확장의 대체 구현은 `HMGET` 뒤에 **별도 `PTTL`**을 쏘므로, 그 사이에 lease가 만료되면 **멀쩡한 row를 버린다** |
| **IMP-CP-36** | [40 §3](../server/40-location-runtime.ko.md): 목록은 페이지 커서로 순회한다 | `redis.hpp:1357-1371` — `SMEMBERS` 전체를 받아 **정수 오프셋**으로 자른다. 나머지 셋은 전부 **SSCAN 커서**를 쓴다. ⇒ drain이 actor row를 페이지로 훑는 도중 다른 노드가 actor 하나를 만들거나 지우면(`SADD`/`SREM`) **Redis 반복 순서가 바뀌어** 오프셋이 엉뚱한 곳에 떨어진다. **1페이지에 나온 row가 또 나오고, 2페이지에 나왔어야 할 row는 영영 안 나온다.** drain handoff가 **actor를 조용히 건너뛴다** |
| **IMP-CP-37** | `framework/testdata/location/redis/actor-location-v2.json`: **네 Redis 확장이 이 fixture와 바이트 단위로 일치해야 한다.** `hashFields`는 `owner`/`gen`/`json`/`updatedAtMs` | `redis.hpp:959` — actor row에 **`mesh` 다섯 번째 hash 필드**를 쓴다. 나머지 셋은 전부 null을 넘긴다. ⇒ **fixture의 바이트 단위 일치 주장이 이미 거짓이다.** 게다가 비대칭이다 — `remove_actor`는 mesh를 안 넘겨서 `P:stamp:actor:{mesh}`가 **쓸 때만 INCR되고 지울 때는 안 된다.** C++ fixture 테스트가 row JSON만 검사해서 **아무도 못 잡는다** |
| **IMP-CP-38** | [41 §3·§4](../server/41-location-store-redis.ko.md): 모든 write 결정은 **Lua script 한 번**으로 원자 실행한다. `ListOwnerLeases`는 lease 목록과 Redis `TIME` 기준 `StoreNow`를 **한 script로 함께** 반환한다 | 두 script(`redis.hpp:159-188`)가 **dead code**다. `remove_lease()`는 `DEL` + `SREM`을 **따로** 쏘고, `list_owner_leases()`는 `TIME` → `SMEMBERS` → owner마다 `PTTL` + `GET`을 **각각 블로킹 왕복**으로 쏜다. `store_now`는 맨 앞에서 한 번 재고, owner *k*의 `PTTL`은 **2k번째 왕복 뒤에** 읽는다. 그런데 코드는 `lease_expires_at = store_now + remaining`으로 계산한다(:1055) — 실제 만료는 `t_read + remaining`이므로 **스캔에 걸린 시간만큼 만료를 과소평가한다.** owner 200개·0.5ms 링크면 **~200ms 체계적 과소평가**다. 지금은 IMP-CP-35 때문에 그 스냅샷을 lease join에 안 써서 가려져 있을 뿐, **CP-35를 고치는 순간 살아난다** |

## 라운드 4 (2026-07-14) — 근거 없는 공개 표면 (샘플·E2E 감사에서 역으로 발굴)

**샘플이 스펙에 없는 API를 쓰는 걸 보고 역추적했더니 framework 표면 자체의 갭이 나왔다.**
라운드 3의 IMP-CP-28/30/32와 같은 종류지만, 이번엔 **샘플·e2e가 실제로 의존하고 있어서**
걷어내면 그쪽도 같이 고쳐야 한다.

- [ ] **IMP-CP-39** (결함) — **`actor_gateway_t`** — C++ 카탈로그가 **스스로 "공개 계약이 아니다"라고 이름까지 적어 명시한 타입**인데, 샘플·e2e **18개 파일**이 DI로 주입받아 쓴다
- [ ] **IMP-CP-40** (결함) — **`bind_session_route(...)`** — 스펙이 **이름을 짚어 금지한 세 값**(route client·route channel 이름·target node rid)을 application handler에 넘긴다

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-CP-39** | [C++ 카탈로그 §16.24](../server/languages/cpp/02-framework-interfaces.ko.md): `actor_gateway_t`는 `src/runtime/`의 **runtime 내부 타입**이며 "**application은 이 타입을 직접 다루지 않는다**"고 **목록에 이름까지 넣어** 명시한다. 스펙이 정한 bind 표면은 `session_actor_manager_t::bind(actor_ref_t)` **인자 1개**다(`:886`, 스펙 31의 `IZLinkSessionActors.BindAsync(...)`와 대응) | `contracts/actors/actor.hpp:670-693`이 `actor_gateway_t`를 **public contract 헤더에 두고** `manager()`·`actor_context()`·`bind_session_stream()`·`bind_session_route()`·`unbind_session_stream()`을 노출한다. 샘플·e2e **18개 파일**이 이걸 session 의존성으로 **DI 주입**받는다(`Bingo/.../bingo_session.hpp:22,26,98`, `TicTacToe/.../play_session.hpp:24,28,102`, `SupportChat/Server/Session/main.cpp:27,31,206`, `DeliveryDispatch/.../CourierSession/main.cpp:25,30,133`, `GameQuest/Server/GameApi/main.cpp:265`, `e2e/SpotService/.../play_control_handlers.hpp:63` …). `bind_session_stream(actor_id, stream_t, stream_codec_t)`는 스펙 트리 grep **0건**. **`.NET`엔 `ActorGateway`라는 타입이 아예 없다**(grep 0건). ⇒ IMP-CP-32와 **같은 §16.24 조항**이지만 그쪽은 builder가 snapshot을 *반환*하는 문제였고, 이건 **disown된 타입을 통째로 앱의 session 의존성으로 건네주는** 문제다. [IMP-CP-02](#2-구현-감사-상세)(`unbind_session_stream`에 토큰·세션 식별자가 없다)가 **바로 이 타입의 결함**이다 |
| **IMP-CP-40** | [C++ 카탈로그 §1516-1518](../server/languages/cpp/02-framework-interfaces.ko.md): "ActorGateway session relay는 `session_actor_manager_t`·`session_actor_t`·`actor_context_t`·`bound_session_t`가 담당한다. **이 표면은 route mesh channel을 직접 보여 주지 않는다.**" [31 §375-377](../server/31-session-actor-dispatch.ko.md): "resolver가 반환하는 target node 식별값(`RoutingId`류)은 **사용자가 일반 handler에서 직접 다루는 값이 아니다.** transport 위치값은 resolver 구현체와 framework routed transport **내부에만** 머물러야 한다" | `contracts/actors/actor.hpp:685` — `bind_session_route(actor_ref_t, route_client_t, std::string route_channel_name, routing_id_t target_node_rid, …)`. **스펙이 금지한 세 값을 전부 앱에 넘긴다.** 스펙 트리 grep **0건**, `.NET` grep **0건**. e2e가 이걸 **application request handler 안에서** 부르고(`e2e/SpotService/Server/Play/Handlers/play_control_handlers.hpp:126`), 같은 handler가 **raw transport 신원으로 분기까지 한다**(`:121-123` — `context.source_node_rid`를 자기 `_state.node_rid`와 비교) |

**`add_spot_timer_events(source_name)`도 같은 종류다** — `contracts/eventing/events.hpp:246`에
선언되고 `e2e/RuntimeMonitoring/.../service_host.hpp:66`이 부르는데, 스펙의 monitoring 등록 축은
**정확히 7개**로 열거돼 있고(`AddSocketEvents`·`AddSpotEvents`·`AddLocationRuntimeEvents`·
`AddLocation{Peer,Spot,Actor,Route}Events`) timer source는 없다. `.NET`도 7개뿐이다.
**[IMP-CP-15](#상세)에 접어 넣는다** — 결함(spot source가 timer 실패를 못 낸다)과 governance
문제(근거 없는 8번째 축)가 **같은 수정으로 닫히기** 때문이다. E2E-CP-36이 그 e2e 쪽 짝이다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

**앞선 세 라운드는 spec ↔ framework 구현만 봤다. 샘플과 e2e는 감사 대상이 아니었다.**
이 라운드가 그 둘을 본다. 계약은 [공통 샘플](../../common/sample/README.ko.md)과
[공통 E2E](../../common/e2e/README.ko.md), 그리고 각 샘플·config 문서다.

**C++은 자동 등록 축에서 면제다**([§13](../90-implementation-gap.ko.md#13-샘플-연결등록-축-준수-현황)).
runtime scanner가 없으므로 compile-time 명시 등록이 정답이다. 아래에 그 항목은 없다.

### 체크리스트 — 샘플

- [x] **SMP-CP-01** (결함) — SupportChat의 **릴리스 게이트가 위조**돼 있다. framework를 거치지 않는 in-memory 도메인 단언이 `verified`를 찍는다
  - 근거: 수정 전 contract gate가 runner의 in-memory Probe 실행과 위조된 server-invariants marker를 검출했다. Probe를 릴리스 게이트에서 제거하고 실제 stream client의 request·push·reconnect·idle-close 단언만 성공 기준으로 남긴 뒤 gate와 `./run_sample.sh`가 `PASS SupportChat.Cpp`로 통과했다.
- [x] **SMP-CP-02** (결함) — Bingo에서 **2번째 참가자가 `GameStartedNotify`를 영원히 못 받는다**
  - 근거: 수정 전 두 번째 client 대기가 `client2 game started wait failed`로 실패했다. joining actor를 기존 참가자 broadcast에서 제외한 뒤 완료된 join lifecycle의 bound session으로 보상하고, 원격 commit이 callback 전에 gateway ref를 갱신하도록 수정한 뒤 spot runtime 회귀 테스트와 `./run_sample.sh`가 통과했다.
- [ ] **SMP-CP-03** (결함) — TicTacToe가 **스펙 근거 0인 public API**(`add_spot_resolver`)를 샘플에서 쓴다
- [ ] **SMP-CP-04** (결함) — TicTacToe 발행 메시지에 `Msg` 접미어(`PlayerWinMilestoneMsg`)
- [ ] **SMP-CP-05** (결함) — TicTacToe가 문서에 없는 push(`GameEndedNotify`)를 추가로 쏜다
- [ ] **SMP-CP-06** (결함) — Bingo wire 이름이 **샘플 안에서 갈라져 있다**(`...Msg` vs proto `...Event`)
- [x] **SMP-CP-07** (결함) — TicTacToe `NextTurn`이 mark가 아니라 **actor id**를 싣는다
  - 근거: 수정 전 도메인 회귀 테스트가 `NextTurn`의 실제값을 `player-x`·`player-o`로 검출했다. 공개 state에는 X/O mark를 저장하고 turn actor는 mark와 참가자 정보에서 도출하도록 수정한 뒤 sample parity와 `./run_sample.sh`가 통과했다.
- [ ] **SMP-CP-08** (미구현) — SupportChat·DeliveryDispatch에 **문서에 없는 `Probe` 프로세스**
- [x] **SMP-CP-09** (미구현) — ShoppingMall에 **`ClientScenario`가 없다**
  - 근거: 수정 전 sample parity gate가 named scenario 부재와 `main.cpp`의 workflow orchestration 소유를 검출했다. HTTP 호출·polling·단언을 `shoppingmall_client_scenario_t`로 옮기고 entrypoint를 CLI 검증과 scenario 호출만 남긴 뒤 parity와 `./run_sample.sh`가 `PASS ShoppingMall.Cpp`로 통과했다.
- [x] **SMP-CP-10** (결함) — SupportChat이 typed connector wait 대신 **raw packet + 수동 JSON 파싱**
  - 근거: 수정 전 sample parity gate가 raw `packet_t` wait, 수동 `parse_json`, 네 typed wait 부재를 검출했다. assignment·participant join·chat·typing push를 각각 connector의 `wait_for<T>()`로 직접 받도록 수정한 뒤 parity와 `./run_sample.sh`가 `PASS SupportChat.Cpp`로 통과했다.
- [ ] **SMP-CP-11** (미구현) — ShoppingMall·GameQuest의 **"동시" 시나리오가 실제로는 순차**다
- [ ] **SMP-CP-12** (결함) — Bingo·GameQuest runner가 **고정 sleep을 readiness로 쓴다**(문서가 명시적으로 금지)
- [ ] **SMP-CP-13** (미구현) — ShoppingMall·GameQuest에 **Domain/Application/Infrastructure 레이어가 없다.** SupportChat엔 **Infrastructure가 없다**

**아래는 흐름·레이어·runner 축을 따로 훑어 나온 것이다. 여기서 진짜 기능 버그가 나왔다.**

- [x] **SMP-CP-14** (**버그**) — **SupportChat idle timer가 메시지 순번을 wall-clock으로 착각**해 첫 tick에 방을 닫는다
  - 근거: 수정 전 client의 Unix-ms 범위 단언이 실패했고 첫 500ms tick에 idle 알림이 발생했다. `send_message`가 실제 현재 시각을 넘기도록 수정한 뒤 `./run_sample.sh`가 `PASS SupportChat.Cpp`로 통과했다.
- [x] **SMP-CP-15** (**버그**) — **ShoppingMall owner spot이 자기 재개를 예약하지 않는다.** HTTP edge가 대신 한다
  - 근거: 수정 전 sample parity gate가 owner의 예약 부재와 CommerceApi의 잘못된 예약을 각각 검출했다. owner가 첫 tick에서 취소하는 timer로 같은 continuation 경로를 예약하고 edge의 예약을 제거한 뒤 gate와 `./run_sample.sh`가 `PASS ShoppingMall.Cpp`로 통과했다.
- [ ] **SMP-CP-16** (결함) — **ShoppingMall이 전역 Redis 키 하나 + 전역 락 하나**로 모든 주문을 직렬화한다. owner-spot 모델이 무효화된다
- [x] **SMP-CP-17** (결함) — **SupportChat customer가 자기를 상담원으로 등록할 수 있다**(role 검사 없음)
  - 근거: 수정 전 공개 client에 customer의 `SetAgentAvailableReq` 실패 단언을 추가하자 전체 runner가 `customer must not set agent availability`로 실패했다. Entry Spot handler가 인증된 actor role을 검사해 customer 요청을 `request_rejected`로 반환하도록 수정한 뒤 `./run_sample.sh`가 `PASS SupportChat.Cpp`로 통과했다.
- [x] **SMP-CP-18** (결함) — **TicTacToe notification publisher가 아무것도 발행하지 않는다.** vector에 쌓기만 하고 읽는 곳이 0
  - 근거: 수정 전 contract gate가 publisher의 session 전송 부재와 읽히지 않는 vector 보관을 모두 검출했다. room actor registry를 주입받은 publisher가 제외 대상을 처리하고 bound session으로 직접 전송하도록 책임을 모은 뒤 TicTacToe parity 8건과 `./run_sample.sh`가 `PASS TicTacToe.Cpp`로 통과했다.
- [ ] **SMP-CP-19** (결함) — **SupportChat이 Api hop을 Support actor에서 Session으로 옮기고 payload를 고쳐 쓴다**
- [ ] **SMP-CP-20** (결함) — **GameQuest가 event마다 문서에 없는 blocking ensure 왕복**을 하고, owner를 샘플이 직접 해시한다
- [ ] **SMP-CP-21** (결함) — **ShoppingMall이 owner routing 대신 named mesh 2개 + 샘플 해시 + node 지정 request**를 쓴다
- [ ] **SMP-CP-22** (결함) — **Domain이 framework 헤더를 include하고 wire DTO를 직접 만든다**(Bingo·TicTacToe)
- [x] **SMP-CP-23** (결함) — **TicTacToe Spot이 도메인 aggregate를 상속**한다. slicing 대입 + actor마다 `join()` 2회
  - 근거: 수정 전 새 contract test가 무변경 admission API 부재로 컴파일에 실패했고 구조 gate가 domain 상속·slicing 대입을 검출했다. Spot이 optional match를 합성하고 admission은 `evaluate_join()`, commit은 단일 `join()`을 사용하도록 바꾼 뒤 TicTacToe parity 10건과 `./run_sample.sh`가 `PASS TicTacToe.Cpp`로 통과했다.
- [ ] **SMP-CP-24** (결함) — **SupportChat Application 레이어가 serving 경로에서 dead code**다(위조 self-check에서만 쓰인다)
- [ ] **SMP-CP-25** (결함) — **Domain이 이미 내린 판정을 Infrastructure가 다시 내린다**(SupportChat idle/close, Bingo join notify)
- [x] **SMP-CP-26** (결함) — **runner 3개가 framework 동작 knob(`ZLINK_CPP_AUTO_CONNECT_TRACE`)을 export**하고, Bingo는 그 덕에 생긴 로그로 self-check한다
  - 근거: 수정 전 contract gate가 세 runner의 내부 trace 환경 변수와 Bingo·DeliveryDispatch의 trace 문자열 오라클을 검출했다. 이를 제거하고 실제 client 흐름과 공개 flow·metric evidence만 남긴 뒤 gate와 세 `./run_sample.sh`가 모두 통과했다.
- [x] **SMP-CP-27** (결함) — **runner 4개가 빌드 전에 Redis container를 띄운다**
  - 근거: 수정 전 contract gate가 SupportChat·ShoppingMall·GameQuest·DeliveryDispatch 네 runner 모두에서 Redis 시작이 build보다 앞서는 것을 검출했다. build 성공 뒤에만 scoped Redis를 시작하도록 순서를 바꾼 뒤 gate와 네 `./run_sample.sh`가 모두 통과했다.
- [x] **SMP-CP-28** (결함) — **통합 runner의 transient-bind 패턴이 `already bound` 토큰을 빠뜨린다**
  - 근거: 수정 전 target-contract gate가 공통 transient bind 토큰 누락을 검출했다. 재시도 정규식에 `already bound`를 추가한 뒤 shell 매칭 확인, runner 문법 검사, target-contract gate가 모두 통과했다.
- [ ] **SMP-CP-29** (결함) — **계약에 없는 wire 메시지 5종**(TicTacToe·DeliveryDispatch·GameQuest·ShoppingMall)
- [ ] **SMP-CP-30** (결함) — **GameQuest event store가 프로세스 로컬 맵**이라 재시작 복구를 증명하지 못한다

**아래는 §Client 검증 흐름(릴리스 게이트)을 번호 단계별로 걸어 나온 것이다.
게이트가 약해서 위의 버그들이 통과해 왔다.**

- [x] **SMP-CP-31** (결함) — **Bingo 게이트가 SMP-CP-02를 구조적으로 잡을 수 없다.** client1만 start를 기다리고 `status`도 안 본다
  - 근거: 두 player client 모두 typed wait로 start push를 받고 각 state의 room id와 `Running` 상태를 단언하게 했다. 강화된 게이트가 수정 전 client2 누락을 검출했고 수정 후 `./run_sample.sh`가 `bingo full client/server self-check completed`로 통과했다.
- [ ] **SMP-CP-32** (결함) — **GameQuest 멱등성 단언이 `>=`라 실패할 수 없다.** 중복 증가해도 통과한다
- [x] **SMP-CP-33** (결함) — **TicTacToe가 모든 move에서 `board`·`next_turn`을 버린다.** 미러 push는 존재 여부만 본다
  - 근거: 수정 전 sample parity 게이트가 네 move의 deterministic board·next mark와 상대 push state 대조 12개가 모두 없음을 검출했다. 각 response를 정확값으로 단언하고 상대 push의 전체 state를 비교한 뒤 게이트와 `./run_sample.sh`가 `PASS TicTacToe.Cpp`로 통과했다.
- [ ] **SMP-CP-34** (결함) — **Bingo 게이트 5·7·8·9·11단계가 문서보다 약하다**
- [x] **SMP-CP-35** (결함) — **TicTacToe 게이트 1·3·7·11단계가 필드를 빠뜨린다.** level 입장 조건은 아예 평가되지 않는다
  - 근거: sample parity 회귀 테스트가 Play endpoint 매핑, 두 player의 level 입장 조건, join push의 사용자·상태 필드, milestone display name 단언 부재를 모두 검출했다. 단계별 단언을 보강한 뒤 해당 테스트와 `./run_sample.sh`가 `PASS TicTacToe.Cpp`로 통과했다.
- [ ] **SMP-CP-36** (결함) — **GameQuest reconnect가 정의하는 두 반쪽(unbind·복원 조회) 없이 돈다.** "다른 owner"도 미단언
- [x] **SMP-CP-37** (결함) — **DeliveryDispatch가 상태 "순서"를 단언하지 않는다.** 독립 future를 선언 순서로 `.get()`할 뿐이다
  - 근거: 수정 전 source gate가 status별 독립 wait와 200ms sleep을 검출했고, 순차 wait를 적용하자 재배정 경로의 계약 밖 `PickedUp`이 `delivery-reassign status sequence failed`로 드러났다. 직접 수락에서만 `PickedUp`을 발행하도록 고친 뒤 sample parity와 `./run_sample.sh`가 통과했다.

**아래는 6개 샘플의 메시지 계약을 문서/C++/`.NET` 3자 필드 단위로 대조해 나온 것이다.
`.NET`은 `null`을 wire에 싣고 enum을 정수로 내보내는데, C++ nlohmann은 `null`을 만나면 던진다.
그래서 여기 wire 파손이 몰려 있다.**

- [ ] **SMP-CP-38** (**wire 파손**) — **DeliveryDispatch `DeliveryStatus`가 C++는 문자열, `.NET`은 정수**다. client-facing push에서 깨진다
- [ ] **SMP-CP-39** (**wire 파손**) — **GameQuest `QuestProgress`가 필드 이름이 다르고 `.NET`엔 `Version`이 아예 없다**
- [ ] **SMP-CP-40** (**wire 파손**) — **TicTacToe `GameState`의 nullable 5개를 C++가 sentinel로 뭉갠다.** 양방향으로 깨진다
- [ ] **SMP-CP-41** (결함) — **SupportChat `JoinConversationReq`가 C++에선 빈 구조체**다. 문서가 요구한 참가자 신원을 **보낼 수단이 없다**
- [ ] **SMP-CP-42** (결함) — **SupportChat이 framework의 `ActorRefSnapshot`을 자기 것으로 포크**했다. 문서가 "샘플이 정의하지 않는다"고 명시한 타입이다
- [ ] **SMP-CP-43** (결함) — **ShoppingMall이 금액을 `double`로 쓴다.** 문서·`.NET`은 `decimal`이다
- [ ] **SMP-CP-44** (결함) — **DeliveryDispatch `BindCourierSessionRes.Actor`가 언어마다 다른 타입**이다. `.NET`엔 `actorId`·`generation`이 없다
- [ ] **SMP-CP-45** (결함) — **GameQuest의 entry→owner hop이 언어마다 packet 이름도 호출 방식도 다르다**(C++ one-way vs `.NET` request)
- [ ] **SMP-CP-46** (미구현) — **TicTacToe `TicTacToeGameJoinRes`가 C++에 없다**
- [ ] **SMP-CP-47** (결함) — **문서에 없는 필드가 응답·push에 실린다**(TicTacToe 4개, GameQuest 1개, `.NET` DeliveryDispatch 1개)
- [ ] **SMP-CP-48** (결함) — **계약에 없는 wire 타입이 9종 더 있다**(GameQuest 4, ShoppingMall 5)
- [ ] **SMP-CP-49** (**wire 파손**) — **`.NET`이 `null`을 wire에 싣는데 C++엔 null 가드가 없다.** Bingo 0·TicTacToe 0·GameQuest 0개
- [ ] **SMP-CP-50** (결함) — **Bingo `BingoActorEntrySpotNotify`가 `.NET`에만 있고** `target_node_rid`를 흘린다. C++·문서엔 0건

**아래는 샘플 서버 인프라를 정독해 나온 것이다. 진짜 버그가 5건 더 나왔다.**

- [x] **SMP-CP-51** (**버그**) — **거절된 actor join이 정상 응답으로 나간다.** `std::visit`이 **양쪽 대안에 다 컴파일**된다 (Bingo·TicTacToe·SupportChat)
  - 근거: 수정 전 sample parity gate가 세 샘플의 성공 평탄화를 검출했다. accepted 대안을 명시적으로 확인하고 rejected를 request failure로 바꾼 뒤 gate와 세 샘플의 전체 runner가 통과했다. level 거절을 client에서 만드는 검증은 SMP-CP-35에 남아 있다.
- [x] **SMP-CP-52** (**버그**) — **Bingo가 draw 진행 중 카드 재제출을 받아** 이미 뽑힌 번호를 새 카드에 소급 적용한다. **승리를 조작할 수 있다**
  - 근거: 수정 전 공개 client의 중복 제출 오류 단언이 실패했다. domain이 이미 제출된 카드를 거부하도록 수정한 뒤 `./run_sample.sh`의 전체 client/server self-check와 관련 ctest 3개가 통과했다.
- [x] **SMP-CP-53** (**버그**) — **Bingo `StopObservingBingoEventsReq`에 가드가 0개**다. **게임 중인 player를 방에서 쫓아낼 수 있다**
  - 근거: 수정 전 game-room player의 stop-observing 오류 단언이 실패했다. observer room 목적·대상 room·observer 등록 여부를 검증하도록 수정한 뒤 `./run_sample.sh`의 전체 self-check와 관련 ctest 3개가 통과했다.
- [x] **SMP-CP-54** (**버그**) — **TicTacToe `LeaveGameReq`가 상태·소속 검사 없이** 게임 도중 나가기를 허용한다
  - 근거: 수정 전 진행 중 leave가 상대에게 `GameEndedNotify`를 보내 bounded negative가 실패했다. domain이 final 상태와 참가자 여부를 검증하도록 수정한 뒤 `./run_sample.sh`가 `PASS TicTacToe.Cpp`로 통과했다.
- [ ] **SMP-CP-55** (**버그**) — **DeliveryDispatch Tracking이 고객을 `"customer-1"`로 하드코딩**한다. 계약에 `customer_id`가 없다
  - §0.8 중단: C++만 고치면 공유 wire가 달라진다. 선택지 A는 `DeliveryStatusChangedReq`에 `CustomerId`를 추가해 모든 언어가 전달하게 하는 것이고, 선택지 B는 Tracking이 생성 시점부터 `DeliveryId → CustomerId` 관계를 저장해 상태 변경 메시지에 고객 필드를 싣지 않는 것이다. 공통 계약에서 한 방식을 결정한 뒤 구현해야 한다.
- [x] **SMP-CP-56** (결함) — **Bingo room Spot이 절대 닫히지 않는다.** `close()` 호출이 0건이고 observer 방 timer가 **프로세스 수명 내내** 돈다
  - 근거: 수정 전 sample parity gate가 마지막 actor 이탈 뒤 빈 player·observer 방의 종료 요청이 없음을 검출했다. `on_leave_actor`가 두 점유 집합이 모두 비면 `close()`를 요청하도록 수정한 뒤 Bingo parity 테스트 6개, 관련 ctest 3개, `./run_sample.sh` 전체 client/server self-check가 통과했다.
- [x] **SMP-CP-57** (결함) — **TicTacToe game Spot에 timer가 없다.** 문서가 요구한 turn timeout이 통째로 미구현
  - 근거: 수정 전 sample parity gate가 game timer 등록, domain tick, `TurnTimedOut` 상태가 모두 없음을 검출했다. match가 15초 `steady_clock` deadline과 timeout 상태 전이를 소유하고 Spot timer handler가 변경된 state를 room actor에게 전달하도록 구현한 뒤 timeout domain 회귀 테스트, TicTacToe parity 테스트 7개, 관련 ctest 3개, `./run_sample.sh` 전체 client/server self-check가 통과했다.
- [x] **SMP-CP-58** (결함) — **Bingo room id가 프로세스별 카운터**라 두 Play 노드가 **같은 spot rid**를 만든다
  - 근거: 수정 전 두 독립 allocator의 첫 room id가 모두 `two-player-room-1`이어서 회귀 테스트가 실패했다. allocator가 128비트 난수 기반 routing id를 내부에서 만들도록 수정한 뒤 독립 allocator 테스트, Bingo parity 테스트 7개, 관련 ctest 3개, `./run_sample.sh` 전체 client/server self-check가 통과했다.
- [x] **SMP-CP-59** (결함) — **TicTacToe entry spot이 disconnect 시 milestone observer를 정리하지 않는다.** 죽은 세션에 계속 push한다
  - 근거: 수정 전 sample parity gate가 disconnect callback의 observer 정리 누락을 검출했다. disconnect에서 actor 상태를 갱신한 뒤 milestone observer map에서도 제거하도록 수정한 뒤 TicTacToe parity 테스트 5개, 관련 ctest 3개, `./run_sample.sh` 전체 client/server self-check가 통과했다.
- [x] **SMP-CP-60** (결함) — **DeliveryDispatch Tracking이 framework `actor_directory_t` 대신 blocking 왕복을 손으로 짠다.** 샘플 트리 전체에서 `actor_directory_t` 사용 0건
  - 근거: 수정 전 sample parity gate가 Tracking handler의 `resolve_spot_handle`→`request_to_spot(FindCustomerActorReq)` 수동 조회와 `actor_directory_t` 누락을 검출했다. public `actor_directory_t::find()`로 actor ref를 얻어 `actor_client_t`로 전송하도록 바꾼 뒤 Tracking target 빌드, DeliveryDispatch parity 테스트 4개, `./run_sample.sh`가 통과했다. 고객 식별자 선택 문제는 SMP-CP-55의 §0.8 결정으로 남아 있다.
- [x] **SMP-CP-61** (결함) — **DeliveryDispatch `Tracking/Spots/`·`Tracking/Actors/`가 전부 dead code**다. ZLink spot도 actor도 없다
  - 근거: 수정 전 layout gate가 Tracking의 미생성 actor·entry spot·tracking spot 파일과 읽히지 않는 병렬 history directory를 검출했다. dead 파일과 handler의 directory 의존성을 제거하고 inventory를 실제 CustomerGateway 소유 구조로 고쳤다. outbound customer actor 전달에 필요한 spot mesh 참여는 runner 실패로 확인해 유지했다. Tracking target 빌드, DeliveryDispatch parity 테스트 3개, `./run_sample.sh`가 통과했다.
- [x] **SMP-CP-62** (결함) — **Bingo reward pub/sub 구독이 방 teardown을 구동한다.** 문서가 "game state를 바꾸는 경로가 아니다"라고 못박은 것
  - 근거: 수정 전 sample parity gate가 reward subscription handler의 `leave_finished_actors()` 호출을 검출했다. owner room lifecycle 분기를 제거하고 observer 알림만 남긴 뒤 gate와 `./run_sample.sh`가 통과했다.
- [x] **SMP-CP-63** (결함) — **TicTacToe에 도달 불가능한 status를 검사하는 dead code**가 있다(`"playing"`·`"ended"`는 존재하지 않는 값)
  - 근거: 수정 전 layout gate가 미사용 snapshot wrapper·항등 mapper·미등록 created handler 세 파일을 검출했다. 해당 cluster와 EntrySpot의 미사용 match를 제거한 뒤 no-hit 검색, layout/sample parity gate, `./run_sample.sh`가 모두 통과했다.
- [x] **SMP-CP-64** (결함) — **Bingo `bingo_room_game_t`가 자기 멤버를 가리키는 raw 포인터를 갖고 복사된다**(rule-of-five 위반)
  - 근거: 수정 전 복사 독립성 회귀 테스트에서 복사본 카드 제출이 원본 player를 변경하고 복사본은 변경하지 않는 aliasing을 재현했다. `bingo_game_t`가 player vector 포인터를 저장하지 않도록 바꾼 뒤 회귀 테스트와 `./run_sample.sh`가 통과했다.

### 체크리스트 — E2E

- [x] **E2E-CP-01** (결함) — **Config 10이 통합 게이트에서 아예 안 돈다.** 대신 문서에 없는 config가 돈다
  - 근거: 수정 전 target-contract gate가 `SpotActorTransfer` 누락과 문서 밖 `DeliveryDispatch` 등록을 모두 검출했다. 통합 목록을 공통 11개 config에 맞춘 뒤 gate가 통과했고, `./run_e2e_all.sh SpotActorTransfer:ST-A1`이 forward·reverse·shuffle 세 변형에서 모두 통과했다.
- [ ] **E2E-CP-02** (결함) — **Config 9 Track A(P0 전부)가 이름만 그렇다.** session gateway 역할도 stream connector도 없다
- [ ] **E2E-CP-03** (결함) — **Config 11이 구조 자체가 규약 밖**이다(Client 없음·env role 스위치·`OrderWorkflow` 역할 0건)
- [ ] **E2E-CP-04** (결함) — **PubSub client 시나리오가 아무것도 단언하지 않는다**
- [x] **E2E-CP-05** (결함) — **SpotService `all`이 문서 시나리오를 빼먹고**(SM-F3·F4·F5) **문서에 없는 걸 돌린다**(SM-Q9)
  - 근거: 수정 전 target-contract gate가 `all` 목록의 SM-F3·F4·F5 누락과 비계약 SM-Q9 포함을 네 건 모두 검출했다. 목록을 공통 scenario inventory에 맞춘 뒤 gate와 `./run_e2e.sh SM-F3`, `SM-F4`, `SM-F5`의 client·server evidence 검증이 모두 통과했다.
- [ ] **E2E-CP-06** (미구현) — actor ref **`generation`이 리터럴 0**이고 검증하는 곳이 없다
- [x] **E2E-CP-07** (결함) — **Config 10의 순서 계약이 검증되지 않는다**(포함 여부만 본다)
  - 근거: 수정 전 target-contract gate가 cross-kind 순서 helper와 `location_committed` marker 부재를 검출했다. join 완료 지점에 marker를 기록하고 ST-A1이 다섯 lifecycle evidence의 상대 순서를 검사하도록 수정한 뒤 gate와 `./run_e2e.sh ST-A1`이 통과했다.
- [ ] **E2E-CP-08** (결함) — **§2.6 설정 정책을 `ToActorMessaging` 빼고 전 config가 위반**하고, feature-map에 기록한 곳이 0개다
- [ ] **E2E-CP-09** (결함) — **§2.1 대기 기준 위반**(SpotActorTransfer readiness 30초, ObservabilityOps 상수 0개)
- [ ] **E2E-CP-10** (결함) — **§2.5 시나리오 파일 분리 위반** — 4개 config가 단일 `main.cpp`
- [ ] **E2E-CP-11** (결함) — **Config 11 feature-map이 자기 모순**이다(pending을 `구현`으로 적는다)
- [ ] **E2E-CP-12** (결함) — **Config 6 기본 실행이 단일 시나리오**(`SF-A1`)이고 프레임워크 knob이 env로 뚫려 있다
- [ ] **E2E-CP-13** (결함) — **Config 10에 `feature-map.ko.md`가 없다**
- [ ] **E2E-CP-14** (미구현) — **§3.1 "route mesh 없음 × 분리 배치" 조합이 아예 만들어지지 않는다**
- [ ] **E2E-CP-15** (결함) — Config 4의 **`RC-A6`(P0)에 client scenario 파일이 없다**(shell runner가 대신 단언)
- [x] **E2E-CP-16** (결함) — **`SM-D2`(P0, 원격 bind·relay)가 `all` 목록에 없어 게이트에서 안 돈다**
  - 근거: 수정 전 target-contract gate가 SpotService `all` 목록의 `SM-D2` 누락을 검출했다. 기본 scenario inventory에 `SM-D2`를 추가한 뒤 gate와 `./run_e2e.sh SM-D2`의 client·play-a·play-b·session-a evidence 검증이 모두 통과했다.
- [ ] **E2E-CP-17** (결함) — **`SM-F5`가 자기 계약의 정반대를 단언한다.** Spot을 닫지 않고 "살아 있음"을 확인한다
- [x] **E2E-CP-18** (결함) — **`SM-E1`(P0)이 자기 존재 이유인 message-flow error evidence를 단언하지 않는다**
  - 근거: 수정 전 target-contract gate가 SM-E1 블록의 missing request `reply_error`와 missing send `drop` flow 단언 부재를 각각 검출했다. target node인 `play-b-flow.log`에서 `MissingSpotReq`·`MissingSpotMsg`의 surface·reason·action을 검사하도록 바꾼 뒤 gate와 `./run_e2e.sh SM-E1`의 client·server evidence가 모두 통과했다.
- [ ] **E2E-CP-19** (결함) — **`SM-F4`(P0)가 request 절반만 본다.** send drop·failure counter·flow 분류가 없다
- [ ] **E2E-CP-20** (미구현) — **`SM-A1`(P0)이 spot location row 조회를 하지 않는다.** config 전체에 location runtime query가 0건
- [ ] **E2E-CP-21** (결함) — **"수렴 직후 첫 요청"이 10초 retry 루프에 가려져 관측 불가**
- [ ] **E2E-CP-22** (결함) — **start-order 축이 11개 config 중 9개에서 no-op**이다. 같은 실행을 3번 반복한다
- [ ] **E2E-CP-23** (결함) — **`RM-A4`(P0) failover가 실제로 일어나지 않는다.** 새 프로세스에 직접 물어본다
- [ ] **E2E-CP-24** (결함) — **`RM-B2`(P0)가 문서가 금지한 "죽은 endpoint 반복 timeout"을 삼킨다.** scale-in 중 트래픽이 0
- [ ] **E2E-CP-25** (결함) — **`RM-A2`(P0)가 manual-vs-auto 우선순위를 전혀 검증하지 않는다**
- [ ] **E2E-CP-26** (미구현) — **Config 1·5 어디에서도 public error kind를 분류하지 않는다.** 초과 payload 거절이 timeout과 구분 불가
- [ ] **E2E-CP-27** (결함) — **`RC-A1`·`RC-A2`(P0)가 `RC-A3`와 완전히 같은 등록 호출**이다. config의 변주 축이 0개
- [ ] **E2E-CP-28** (결함) — **`RC-B5`가 "뭔가 실패했다"만 본다.** feature-map은 JSON fallback이라 적고 코드는 거절을 단언한다
- [ ] **E2E-CP-29** (미구현) — **`RC-B4`(P0)의 JSON fallback 규칙이 검증되지 않는다.** 미지원 타입을 보내지 않는다
- [x] **E2E-CP-30** (결함) — **`./run_e2e.sh RC-A6`가 항상 실패한다**(client에 branch가 없다)
  - 근거: 수정 전 target-contract gate가 `rc-a*` glob이 RC-A6를 일반 client로 보내는 것과 A1~A5 명시 selector 부재를 검출했다. client selector를 `rc-a[1-5]`로 좁힌 뒤 gate와 `./run_e2e.sh RC-A6`의 duplicate·wrong-group·unsupported-channel 기동 실패 검증이 모두 통과했다.
- [x] **E2E-CP-31** (결함) — **`RL-D1`·`RL-C2` client scenario가 dead code**다. `main.cpp`가 부르지 않는다
  - 근거: 수정 전 target-contract gate가 runner 소유 검증과 중복된 RL-C2·RL-D1 client include와 파일 네 곳을 모두 검출했다. 약한 dead wrapper와 RL-C2의 도달하지 않는 branch를 제거한 뒤 client target 빌드와 gate가 통과했고, 실제 runner 소유 경로인 `./run_e2e.sh RL-C2`의 crash·복구 및 `./run_e2e.sh RL-D1`의 120회 burst가 각각 통과했다.
- [x] **E2E-CP-32** (결함) — **`RL-B2`(P1) 단언이 실패할 수 없다.** 500ms timeout이라 crash와 무관하게 통과
  - 근거: 수정 전 target-contract gate가 1초 provider handler보다 짧은 500ms outer HTTP timeout과 내부 3초 channel deadline보다 긴 제한의 부재를 검출했다. outer timeout을 5초로 바꿔 provider가 유지되면 정상 reply가 도착해 negative가 실패하도록 만든 뒤 client target 빌드, gate, 실제 SIGKILL을 수행한 `./run_e2e.sh RL-B2`가 통과했다.
- [ ] **E2E-CP-33** (결함) — **`RL-D5` soak가 순차 burst**이고, **`RL-D4` wire 호환이 검증되지 않는다**
- [ ] **E2E-CP-34** (결함) — **`MON-A2`(P0)에 trigger가 없고 원리적으로 실패할 수 없다**(IMP-CP-13이 매 tick 발행)
- [ ] **E2E-CP-35** (결함) — **`MON-D1`·`MON-A4`가 전이가 아니라 카운터를 센다.** `MON-A1`은 `RoutingId`를 아예 기록하지 않는다
- [ ] **E2E-CP-36** (결함) — **`MON-A3`·`MON-A5`가 스펙에 없는 source로만 timer를 관측**해 IMP-CP-15를 **가린다**

**Config 6은 IMP-CP-06·IMP-CP-35를 "못 잡는" 게 아니라 잡을 수 없게 배치돼 있다.
아래 9건이 그 구조다.**

- [ ] **E2E-CP-37** (결함) — **장애가 `docker pause`뿐이다.** 문서가 요구한 stop/restart를 한 번도 안 해 **IMP-CP-06의 발동 조건이 생기지 않는다**
- [ ] **E2E-CP-38** (결함) — **`SF-B2`가 읽는 곳이 0인 knob을 세팅하고 `SF-B1`과 같은 걸 단언한다.** `store_failure_grace`가 미구현이어도 통과
- [ ] **E2E-CP-39** (결함) — **`SF-C1`·`C2`·`D2`가 framework가 아니라 Redis 확장의 lease 필터를 증명한다.** IMP-CP-35가 안 보인다
- [ ] **E2E-CP-40** (결함) — **`SF-D1`·`SF-D2`(P0)가 장애·복구 구간에 트래픽을 하나도 안 흘린다.** 복구 순서·grace·disconnect marker 전부 미단언
- [ ] **E2E-CP-41** (결함) — **consumer의 30초 retry 루프가 SF 전반의 "요청 성공" 단언을 세탁한다**
- [ ] **E2E-CP-42** (결함) — **`SF-A2`가 항진명제**이고(`watch_enabled`가 하드코딩 `false`) provider를 안 띄워 **`SF-C2`의 복제본**이다
- [ ] **E2E-CP-43** (결함) — **`SF-C2`가 lease 만료만으로 통과한다.** 문서가 명시적으로 금지한 것이고, `draining` 필드를 e2e가 버린다
- [ ] **E2E-CP-44** (결함) — **`SF-D3`·`SF-A1`의 상태 필드 3개가 실제로는 bit 하나**로 붕괴한다
- [ ] **E2E-CP-45** (결함) — **`SF-E1`이 store가 아니라 앱 데코레이터에 지연을 넣어** 자기 `sleep`을 잰다
- [ ] **E2E-CP-46** (결함) — **`PS-A1`의 오라클이 계약이 허용하는 것보다 강하고**(무손실 전량), warm-up barrier가 없고, 순서를 안 본다
- [x] **E2E-CP-47** (결함) — **`PS-B1`의 격리 단언에 시간 제한이 없어** 격리와 head-of-line 블로킹을 구분하지 못한다
  - 근거: 수정 전 target-contract gate가 fast subscriber의 순차 wait, 2초 제한 부재, 2.5초 초과 실패 부재를 모두 검출했다. 두 fast subscriber의 16개 수신을 2초 server timeout으로 동시에 기다리고 총 2.5초를 넘으면 실패하도록 바꾼 뒤 gate와 `./run_e2e.sh PS-B1`이 통과했으며 실제 fast 완료는 28ms였다. 이는 느린 subscriber의 직렬 처리 시간 16×250ms=4초보다 짧다.
- [x] **E2E-CP-48** (미구현) — **`PS-C1`의 publisher 쪽 negative**(dispatch marker 없음)가 단언되지 않는다
  - 근거: 수정 전 target-contract gate가 publisher negative 성공 marker와 실패 가능한 dispatch-error 검사를 모두 누락으로 검출했다. PS-C1 종료 시 저장한 publisher evidence에서 `error|kind=publish|reason=handlerMissing|action=drop` 조합의 부재를 검사하도록 바꾼 뒤 gate와 `./run_e2e.sh PS-C1`이 `publisher dispatch negative passed`를 포함해 통과했다.

**Config 9·10·11 심층 — 여기서 "단언이 실패할 수 없다"가 가장 많이 나왔다.**

- [ ] **E2E-CP-49** (결함) — **`ST-E2`(P0)가 계약과 정반대 시나리오를 돈다.** 실패한 transfer를 검증해야 하는데 **성공한 transfer**를 돌린다
- [ ] **E2E-CP-50** (결함) — **Track F의 필수 marker가 단언이 아니라 경고로 강등**돼 있다. `require_runtime_marker()`가 **절대 실패하지 못한다**
- [ ] **E2E-CP-51** (미구현) — **필수 order marker 9개 중 2개가 서버에 존재하지 않는다**(`commit_ack`·`source_cleanup`)
- [ ] **E2E-CP-52** (결함) — **`ST-D2`·`ST-B2`·`ST-C1`·`ST-C3`·`ST-F5`가 이름뿐**이거나 **실패할 수 없는 단언**을 갖는다
- [ ] **E2E-CP-53** (결함) — **`ST-F2`(P0)·`ST-F3`이 자기 경합 창을 스스로 닫는다.** `join_task.get()` 뒤에 보내 backlog가 이미 빠졌다
- [ ] **E2E-CP-54** (결함) — **`ST-F4`가 G1/G2의 message kind를 바꿔** 진짜 발산을 피해 간다. **send였다면 문서가 실패라 부른 동작이 조용히 일어난다**
- [ ] **E2E-CP-55** (결함) — **`ST-D1`(P0)의 local 절반이 항진명제**다(`>=`)
- [ ] **E2E-CP-56** (결함) — **Config 10의 topology가 문서와 다르다.** actor 노드 3개, session gateway·transfer controller **0개**
- [ ] **E2E-CP-57** (결함) — **Track F 관측이 env로 게이트된 stderr 사이드 채널**이다. 문서가 **"단순 로그 문자열 grep이 아니라"**고 명시적으로 금지한 것
- [ ] **E2E-CP-58** (결함) — **`TA-B1`의 error kind를 e2e caller가 스스로 만들어 던진다.** framework 분류를 **지워도 통과**한다. send를 존재 확인 수단으로 쓴다(문서가 이름 짚어 금지)
- [ ] **E2E-CP-59** (결함) — **`TA-B2`·`TA-B3`가 앱 코드로 location row를 위조한다.** actor 노드가 1개뿐이라 owner 교체가 **구성 불가능**하고, B3는 route를 **끊지도 복구하지도 않는다**
- [ ] **E2E-CP-60** (미구현) — **Track B의 negative 역할서버 evidence**가 세 시나리오 전부 미단언
- [ ] **E2E-CP-61** (결함) — **`OBS-C1`(P0)이 문서가 금지한 row 삭제를 통과로 받아들이고**, create-rejection은 **무조건 PASS를 찍는다**
- [ ] **E2E-CP-62** (결함) — **`OBS-A1`~`A4`가 판별력이 없다.** `OBS-A2`는 **C++ 전용 `outcome=` 토큰을 고정**해 IMP-CP-18을 못박는다
- [ ] **E2E-CP-63** (결함) — **`OBS-B2`·`B3`·`B4`·`C3`가 항진명제이거나 절반만 돈다**
- [x] **E2E-CP-64** (결함) — **stray `e2e/DeliveryDispatch/`가 샘플의 갈라진 fork**다. **같은 wire에 계약 헤더가 둘**이고, 게이트는 fork 쪽을 돌린다
  - 근거: 수정 전 target-contract gate가 비계약 DeliveryDispatch e2e runner와 전용 CMake target 군을 각각 검출했다. 샘플과 공유되지 않던 fork의 tracked file 27개와 전용 executable target 9개를 제거한 뒤 CMake 재구성, target/layout contract test, CMake·runner·test 범위의 no-hit 검색이 통과했다. DeliveryDispatch 공개 예시는 `samples/DeliveryDispatch` 한 곳만 남는다.

### 상세 — 샘플

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-CP-01** | [공통 샘플 §Client self-check](../../common/sample/README.ko.md#client-self-check-기준): client가 **실제 서버에 접속해** request·push·final state를 확인한다. [supportchat](../../common/sample/supportchat/README.ko.md)의 §Client 검증 흐름이 릴리스 게이트다 | `SupportChat/Server/Support/main.cpp:733-806` — `supportchat_server_story_t::run()`이 **평범한 in-memory 도메인 객체**(`agent_availability_directory_t`·`conversation_t`)를 새로 만들어 메서드를 호출하고 `agent-join=verified` 같은 문자열을 쌓는다. **Spot·actor·session·framework가 하나도 개입하지 않는다.** 그리고 문서에 없는 `SupportChat/Probe/main.cpp:60-73`이 그 문자열 목록을 확인하고 `supportchat server-invariants=verified`를 찍는다. ⇒ **단위 테스트를 샘플 self-check으로 위장**했다. 샘플이 통과해도 framework가 도는지는 **아무것도 증명하지 못한다** |
| **SMP-CP-02** | [bingo](../../common/sample/bingo/README.ko.md): 2번째 참가자가 들어오면 방이 시작되고 **참가자 전원**이 game start를 받는다 | `Bingo/.../BingoRoomSpot/bingo_room_spot.hpp:133-136` — `send_to_players(started, actor.actor.actor_id)`. 그런데 `send_to_players`의 2번째 인자는 **제외 대상**이다(`:172-180`). ⇒ **게임을 시작시킨 바로 그 참가자**가 start notify에서 빠진다. `.NET`은 이 자리를 bound-session send로 보상하는데(`BingoRoom.cs:59-63`) C++엔 보상 경로가 **없다** |
| **SMP-CP-03** | [공통 샘플 §공통 작성 원칙](../../common/sample/README.ko.md): 필요한 기능이 **공개 계약에 없으면 샘플에서 우회하지 않고** framework의 public contract를 먼저 보완한다 | `TicTacToe/Server/Play/play_server_host_factory.hpp:77-86` — `add_spot_resolver("redis-room-route", …)`를 등록한다. **spec/common 트리 grep 0건**, `.NET`에 대응 API **없음**. 샘플이 스펙에 없는 표면을 만들어 쓰고 있다 |
| **SMP-CP-04** | [공통 샘플 §메시지 이름 원칙](../../common/sample/README.ko.md): `Event`는 **publish 호출에만** 쓴다 | `TicTacToe/Shared/Contracts/messages.hpp:203` — `PlayerWinMilestoneMsg`. 문서는 `PlayerWinMilestoneEvent`([tictactoe README:377](../../common/sample/tictactoe/README.ko.md)), `.NET`도 `PlayerWinMilestoneEvent`(`Messages.cs:93`). publish인데 one-way send 접미어를 달았다 |
| **SMP-CP-05** | [tictactoe](../../common/sample/tictactoe/README.ko.md): push 계약은 `PlayerJoinedNotify`·`GameStateNotify`·`WinMilestoneNotify` **셋** | `TicTacToe/Shared/Contracts/messages.hpp:228-235` — `GameEndedNotify`를 선언하고 발행한다. 문서 grep **0건** |
| **SMP-CP-06** | 샘플 wire 이름은 하나여야 한다 | `Bingo/Shared/Contracts/messages.hpp:310` = `BingoRewardAcquiredMsg` vs **같은 샘플의** `bingo_messages.proto:166` = `BingoRewardAcquiredEvent` |
| **SMP-CP-07** | [tictactoe](../../common/sample/tictactoe/README.ko.md): `NextTurn`은 다음 차례의 **mark** | `TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp:33` — `_state.next_turn = actor_id` |
| **SMP-CP-08** | 각 샘플 문서의 **process·role 표**가 프로세스 구성을 고정한다 | `SupportChat/Probe/main.cpp`·`DeliveryDispatch/Probe/main.cpp` — 두 문서의 역할 표에 **Probe가 없다.** node는 최근 커밋에서 Probe를 삭제했다 |
| **SMP-CP-09** | [공통 샘플:306-309](../../common/sample/README.ko.md): client 검증 흐름은 **`<Sample>ClientScenario`** 이름으로 둔다 | `ShoppingMall/Client/`에 `main.cpp` 하나뿐이고 흐름이 인라인이다. `.NET`엔 `ShoppingMallClientScenario.cs`가 있다 |
| **SMP-CP-10** | [공통 샘플 §Client self-check](../../common/sample/README.ko.md): push 대기는 **connector의 public wait API**를 직접 쓴다. codec wrapper나 샘플 전용 함수 뒤에 숨기지 않는다 | `SupportChat/Client/supportchat_client_scenario.hpp:295-319` — `packet_t`를 이름으로 기다린 뒤 `parse_json<TMessage>()`로 손수 판다 |
| **SMP-CP-11** | [shoppingmall:964-965,981-982](../../common/sample/event/shoppingmall.ko.md): 같은 idempotency key를 **두 CommerceApi에 동시에** 보내고, 서로 다른 owner가 **동시에** 처리한다 | `ShoppingMall/Client/main.cpp:104-110` — API-A 호출을 **끝내고** API-B를 부른다. scale-out도 나중에 따로 돈다(`:186-190`). GameQuest도 Alice→Bob 순차다(`gamequest_client_scenario.hpp:35-123`) |
| **SMP-CP-12** | [bingo README:338-340](../../common/sample/bingo/README.ko.md): runner는 필요한 TCP endpoint가 열린 것을 확인하고 **곧바로** client self-check를 시작한다. **고정 sleep을 준비 상태 확인으로 사용하지 않는다** | `Bingo/run_sample.sh:347` — `sleep "${BINGO_STARTUP_SETTLE_SECONDS:-4}"`. readiness 확인을 다 하고도 **4초를 더 잔다.** `GameQuest/run_sample.sh:270`도 같다(1초). ⇒ 문서가 이름까지 짚어 금지한 것을 그대로 한다. 게다가 이 sleep이 **수렴 레이스를 가려서**, 실제로는 깨진 자동 연결이 통과할 수 있다 |
| **SMP-CP-13** | [공통 샘플 §상태 소유 서버 공통 디렉토리 구조](../../common/sample/README.ko.md): 상태를 소유하는 서버는 `Domain`/`Application`/`Infrastructure` **책임 분리와 의존 방향**을 지킨다. 이름은 바꿔도 되지만 분리는 유지한다 | `ShoppingMall`·`GameQuest` — Domain·Application·Infrastructure 디렉터리가 **0개**다. 도메인 규칙이 role `main.cpp`에 들어 있다(`GameQuest/Server/GameApi/main.cpp` 570줄, `ShoppingMall/Server/CommerceApi/main.cpp` 371줄). `.NET`은 둘 다 Domain 디렉터리를 **2개씩** 가진다. `SupportChat`은 Domain·Application은 있는데 **Infrastructure가 없어** ZLink Spot·actor·session 어댑터가 **914줄짜리 `Server/Support/main.cpp`**에 들어 있다. (DeliveryDispatch는 `.NET`도 Domain이 없어 제외한다.) 디렉터리 이름만 보면 `Domain/`이 zlink 헤더를 직접 include하진 않지만, **실제 의존 방향은 SMP-CP-22가 깬다** |
| **SMP-CP-14** | [supportchat:836-840](../../common/sample/supportchat/README.ko.md): idle timeout은 **3초**이고, timer handler는 시간 신호만 전달한다 | **단위가 섞여 있다.** domain(`Server/Support/Domain/SupportChat/conversation.hpp:67-85`)은 3번째 인자를 **wall-clock unix-ms**로 보고 `_idle_deadline_unix_ms = now_unix_ms + idle_timeout_ms`를 계산한다. 그런데 spot(`Server/Support/main.cpp:349-352`)이 넘기는 값은 **`1000 + last_message_seq + 1`** — 즉 `1001`, `1002`…다. tick(`:283`)은 그걸 **진짜 `now_unix_ms()`**(≈1.7e12)와 비교한다. ⇒ `now >= idle_deadline(≈4001)`이 **항상 참**이라, **아무 메시지 뒤 첫 500ms tick에 방이 `WaitingForClose`로 넘어간다.** 3초 idle timeout이 **한 번도 적용되지 않고**, `ChatMessage.SentAtUnixMs`는 wire에 **1970-01-01**로 나간다 |
| **SMP-CP-15** | [shoppingmall:833](../../common/sample/event/shoppingmall.ko.md) §9.3: `O->>O: ContinueOrderWorkflowReq 예약 (기다리지 않음, fire-and-forget)` — **예약은 owner spot 안에서** 한다 | `Server/OrderWorkflow/main.cpp:62-71` — `start()`가 `run_workflow(..., max_steps=1)`을 돌리고 **아무것도 예약하지 않고 끝난다.** 트리 전체에서 `ContinueOrderWorkflowMsg`를 만드는 **유일한 곳이 `Server/CommerceApi/main.cpp:234-245`(`schedule_continue`)**이고, HTTP 응답 직후에 불린다(`:74`). ⇒ **`CommerceApi`가 응답과 send 사이에 죽으면 주문이 `Created`에서 영영 멈춘다** — 하필 §9.5 복구가 살아남으라고 있는 그 실패다. 샘플의 대표 주장("saga 인프라가 순차 코드로 접힌다", §9.2)이 **edge에서 구동되고 있다** |
| **SMP-CP-16** | [shoppingmall §2.2·§4·§16](../../common/sample/event/shoppingmall.ko.md): owner-spot 모델의 **존재 이유가 단일 병목 제거**다 | `Server/Common/store.hpp:163-171` — 모든 주문의 event stream·read model·commerce state를 **`shoppingmall:commerce-state` 단일 블롭**에 넣는다. `redis_lock_t`(`:94-108`)가 `SET <prefix>shoppingmall:commerce-state:lock NX PX 30000`으로 **전역 락 하나**를 잡고, 읽기(`:37`)와 갱신(`:52`) 양쪽에서 건다. ⇒ **두 `OrderWorkflow` 인스턴스가 락 하나 뒤에 완전히 직렬화된다.** 위층 spot-mesh 라우팅은 맞는데 **아래층 저장소가 그걸 통째로 무효화한다** |
| **SMP-CP-17** | [supportchat:269-271](../../common/sample/supportchat/README.ko.md): "`SetAgentAvailableHandler`는 상담원 roster actor의 상태를 반영한다. **customer actor가 이 request를 보내면 오류를 반환한다**" | `Server/Support/main.cpp:548-553` — `set_available`에 **role 검사가 없고** 곧장 `_runtime.set_agent_available(actor.actor_id, …)`로 넘긴다. ⇒ **customer가 자기 actor id를 상담 가능한 상담원으로 등록**할 수 있다 |
| **SMP-CP-18** | [공통 샘플:302-303,394](../../common/sample/README.ko.md): `<DomainSpot>/Notifications/`에 **실제 notification publisher**를 둔다 | `TicTacToe/.../TicTacToeGameSpot/Notifications/game_notification_publisher.hpp:15-32` — `publish_player_joined`·`publish_game_state`·`publish_game_ended`가 전부 **`std::vector`에 `push_back`만** 한다. **읽는 곳이 트리 전체에 0건.** 호출부는 5곳(`tictactoe_game_spot.hpp:92,96,106`, `Handlers/play_actor_place_mark_handler.hpp:19,23`)이라 **spot 수명 내내 vector가 무한히 자란다.** 샘플의 진짜 publish는 milestone 하나뿐이다(`tictactoe_game_spot.hpp:138`) |
| **SMP-CP-19** | [supportchat:96,697-700](../../common/sample/supportchat/README.ko.md): "**Support actor**가 `OpenConversationReq`를 처리해 API 서버에 `OpenConversationApiReq`로 요청한다". §9.2(`:339-341`)는 Session이 **relay만** 한다고 못박는다 | `Server/Session/main.cpp:99-115` — **Session이** `OpenConversationReq`를 파싱해 **자기가 `supportchat.api`에 request하고**, 그 결과로 **payload를 고쳐 쓴** `open_conversation_req_t{opened.subject, allocated.conversation_id}`를 relay한다(`:112`). Support entry spot은 그 hop을 아예 거부한다 — 자기 주석이 "대화 배정은 API -> Support 채널이 이미 끝냈다"고 적어 놨다(`Server/Support/main.cpp:559-560`). ⇒ **relay여야 할 Session이 request를 발원하고 payload를 변조한다.** 덤으로 상담원 선택도 spot 밖으로 나갔다 — `allocate_conversation_handler_t`(`:652-662`)가 **spot이 생기기도 전에** `assign_agent()`를 부르고, 용량 예약은 아예 없다(`_available_agent`가 단일 `optional`, `:130-145` — 마지막 등록자가 이긴다) |
| **SMP-CP-20** | [gamequest:302-304,500-511,557-560](../../common/sample/event/gamequest.ko.md): entry-spot→owner 메시지는 **`GameplayMsg` 하나**이고, owner 선택은 **`PlayerId` owner routing**이다 | `Server/GameApi/main.cpp:465-475`(`apply_event`) — 이벤트마다 `co_await ensure_player_spot(...)`(`:481-489`)로 **blocking `channel.request(EnsurePlayerQuestSpotReq)`**를 먼저 쏘고, `resolve_player_spot`을 거친 **뒤에야** one-way `send_to_spot`을 한다. 문서에 없는 왕복이 **모든 gameplay event마다** 붙는다. owner 선택도 owner routing이 아니라 **샘플이 직접 해시해 만든 named channel**(`quest_owner_channel_for(owner_mission_id(player_id))`)이다 |
| **SMP-CP-21** | [shoppingmall:380-381,426](../../common/sample/event/shoppingmall.ko.md): "`CommerceApi`가 **`OrderWorkflow` mesh로** 주문 명령을 **owner 라우팅**" | `Server/Configuration/sample_topology.hpp:144-145` — `stable_owner_index(order_id) == 1 ? "workflow-b" : "workflow-a"`로 **샘플이 직접 해시**한다. `CommerceApi/main.cpp:342-347`이 **route mesh를 2개** 등록하고, `:251-257`이 고른 쪽을 **`request_to_node(channel, owner.route_rid, …)`**로 노드 지정해 부른다 ⇒ framework의 owner routing을 쓰지 않는다 |
| **SMP-CP-22** | [공통 샘플:295-306,403-406](../../common/sample/README.ko.md): **`Domain`은 framework 타입에 의존하지 않는다.** `Infrastructure`는 domain state를 직접 조작하지 않고 **domain 객체의 method를 호출한다** | `Bingo/Server/Play/Domain/Bingo/bingo_game.hpp:5`와 `TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp:4`가 `Shared/Contracts/messages.hpp`를 include하고, 그게 **`<zlink/framework/codecs/json.hpp>`·`<zlink/framework/contracts/actors/actor.hpp>`를 끌어온다**(`Bingo/Shared/Contracts/messages.hpp:4-5`, `TicTacToe/…:4-5`). include보다 나쁜 건 **Domain이 wire 메시지를 직접 만든다**는 것이다 — `bingo_game.hpp:35`가 `number_drawn_notify_t`를, `bingo_room_game.hpp:28,50`이 `player_joined_notify_t`/`submit_bingo_card_res_t`를 반환하고, `tictactoe_match.hpp:23,51`이 `place_mark_req_t`를 받아 `join_game_res_t`를 반환하며, **`tictactoe_match_t`의 상태 자체가 wire DTO `tictactoe_state_t`**다. ⇒ **계약 뒤에 도메인 모델이 없다. 계약이 곧 모델이다.** `TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp:8,20,29`도 `<zlink/framework.hpp>`와 `dependency_list_t`(DI)에 의존하고 wire `create_game_res_t`를 반환한다 |
| **SMP-CP-23** | 위와 같음 — Infrastructure가 Domain을 **사용**하지 **상속**하지 않는다 | `TicTacToe/.../TicTacToeGameSpot/tictactoe_game_spot.hpp:25` — `class tictactoe_game_spot_t : public spot_t, public tictactoe_match_t`. **framework Spot이 도메인 aggregate를 상속한다.** 그래서 `:44`에서 **slicing 대입**(`static_cast<tictactoe_match_t &>(*this) = tictactoe_match_t(room_id)`)으로 도메인을 다시 심고, `:56-57`에서 admission을 미리 재 보려고 **aggregate를 통째로 복사**한다 ⇒ **actor마다 `join()`이 두 번 돈다**(복사본에 한 번, `:81`에서 진짜로 한 번) |
| **SMP-CP-24** | [supportchat:210-258,269-270](../../common/sample/supportchat/README.ko.md): `AgentAvailabilityDirectory`가 `SetAgentAvailableHandler`의 대상이고 §7.2/§7.4가 use case와 의존 방향을 정의한다 | `Application/ConversationAssignment/agent_assignment_service.hpp`의 `agent_availability_directory_t`·`agent_assignment_service_t`가 생성되는 곳은 **딱 하나** — `Server/Support/main.cpp:739-740`, 즉 **위조 self-check(SMP-CP-01) 안**이다. 진짜 serving 경로는 Infrastructure가 소유한 단일 `optional`(`main.cpp:130-145`)에서 상담원을 고른다. ⇒ **Application 레이어가 릴리스 게이트에서만 살아 있는 dead code**다 |
| **SMP-CP-25** | [supportchat:837](../../common/sample/supportchat/README.ko.md): "timer handler는 **시간 신호만 전달**하고, **idle/close 판정은 domain method가 수행한다**" | SupportChat: `Server/Support/main.cpp:276-296`의 `on_idle_tick`이 **Spot에서 두 판정을 다 내린다**(`now >= state.idle_deadline_unix_ms`, `now >= _close_deadline_unix_ms`). grace 상수도 Spot에 있다(`:475`). domain의 `mark_idle()`은 **setter로 전락**했다. Bingo: `bingo_room_spot.hpp:119`가 domain이 이미 반환한 `player_joined_notify_t`를 **버리고** `state.players`를 직접 훑어 **같은 notify를 다시 만들고**(`:122-132`), domain이 이미 `can_start`/`status = running`을 세팅했는데도(`bingo_room_game.hpp:41-44`) **"게임 시작 가능"을 `state.players.size() == 2`로 다시 판정한다**(`:133`) |
| **SMP-CP-26** | [runner 템플릿:9-11](../../common/sample/runner-templates/run_sample.template.sh): "No application setting is passed through environment variables. **Exporting a variable for a child process to read is the same violation.**" [공통 샘플:196-197](../../common/sample/README.ko.md): "환경 변수는 0개다" | `Bingo/run_sample.sh:160`·`GameQuest/run_sample.sh:136`·`DeliveryDispatch/run_sample.sh:105` — `export ZLINK_CPP_AUTO_CONNECT_TRACE="${ZLINK_CPP_AUTO_CONNECT_TRACE-1}"`. **framework runtime이 이걸 `std::getenv`로 4곳에서 읽는다**(`runtime/locations/location_auto_connect_host_service.hpp:498`, `runtime/spots/spot_node_host_service.cpp:133`, `runtime/host/actor_gateway_spot_bridge.cpp:72`, `runtime/spots/spot_route_internal_dispatcher.cpp:27`). 그리고 **Bingo의 self-check가 그 변수 덕에 생긴 trace 줄을 grep한다**(`run_sample.sh:330-345,372-374`) ⇒ **샘플의 pass/fail이 export한 환경변수에 달려 있다.** (앞서 "샘플 앱 코드 env 0건"이라 한 판정과 모순되지 않는다 — 이건 **runner + framework runtime** 축이고 그 판정은 앱 코드만 봤다) |
| **SMP-CP-27** | [공통 샘플:222-223](../../common/sample/README.ko.md)·[템플릿:20-26](../../common/sample/runner-templates/run_sample.template.sh): 순서는 **build → 로그 디렉토리 → Redis** → 서버 → readiness → self-check → 정리 | `SupportChat/run_sample.sh`(Redis `:102`, build `:195-200`)·`ShoppingMall`(Redis `:90`, build `:154-157`)·`GameQuest`(Redis `:115`, build `:177-180`)·`DeliveryDispatch`(Redis `:100`, build `:243-250`) — **네 runner가 컴파일 전에 Redis container를 띄운다.** 컴파일 내내 container를 붙들고 있고, 빌드 실패가 Docker cleanup을 거쳐 풀린다. Bingo(`:14-22`)·TicTacToe(`:13-20`)는 빌드가 먼저다 |
| **SMP-CP-28** | [공통 샘플:275-276](../../common/sample/README.ko.md)·[템플릿:13](../../common/sample/runner-templates/run_samples.template.sh): transient bind 실패 토큰은 **넷** — `Address already in use`\|`EADDRINUSE`\|**`already bound`**\|`errno=98` | `samples/run_samples.sh:6` — `BIND_RETRY_PATTERN="Address already in use\|EADDRINUSE\|errno=98"`. **`already bound`가 빠졌다** — 다른 언어 core가 내는 토큰이라, 그걸 내는 peer는 **재시도되지 않고 그대로 실패**한다 |
| **SMP-CP-29** | 각 샘플 문서의 **메시지 계약 표**가 wire 메시지를 고정한다 | 계약에 없는 wire 메시지 5종: TicTacToe **`GameEndedNotify`**(SMP-CP-05), DeliveryDispatch **`OfferDeliveryNotify`**(`Shared/Contracts/messages.hpp:203-210`), GameQuest **`GameQuestProjectionAdminReq/Res`·`GameQuestUnpublishedKillReq/Res`**(`Shared/Contracts/messages.hpp:266-292` — client가 일반 connector로 보낸다, `gamequest_client_scenario.hpp:152-180,211-220`), ShoppingMall **`ContinueOrderWorkflowMsg`**(`Shared/Contracts/messages.hpp:176-191` — 계약엔 `ContinueOrderWorkflowReq/Res`만 있다). GameQuest 둘은 **harness 전용**으로 분리하거나 계약에 정식 추가해야 한다 |
| **SMP-CP-30** | [gamequest:323-362,374-377](../../common/sample/event/gamequest.ko.md): `QuestEventStore`가 **durable source of truth**이고 replay 기반 복구를 요구한다 | `Server/QuestMission/main.cpp:240-249` — quest stream과 projection이 **프로세스 로컬 맵**이다. `Server/GameApi/main.cpp:171-177`의 gameplay event/fact도 마찬가지다. ⇒ deactivate/reactivate 테스트가 증명하는 건 **같은 프로세스 안의 replay**뿐이고, **노드 재시작 복구는 증명되지 않는다** |
| **SMP-CP-31** | [bingo:572-573](../../common/sample/bingo/README.ko.md): "**두 player client는** connector wait API로 `BingoGameStartedNotify`를 기다리고, **push state가 `Running`인지 확인한다**" | `Bingo/Client/bingo_client_scenario.hpp:105` — start 대기를 **client1에만** 건다. **client2는 아예 기다리지 않는다.** 그리고 유일한 단언이 `client1_started.state.room_id == room_id`(`:139`) — **`status == running`을 보지 않는다.** `.NET`은 **양쪽 connector에 걸고 둘 다 `Running`을 단언한다**(`BingoClientScenario.cs:79-86`). ⇒ **게이트가 SMP-CP-02(2번째 player가 start notify를 못 받는 버그)를 관측할 수단 자체를 갖고 있지 않다.** 버그와 약한 게이트가 **정확히 같은 자리에서 만난다** |
| **SMP-CP-32** | [gamequest:601-602](../../common/sample/event/gamequest.ko.md): replay 후 "진행 **중복 증가 없음**", reward **중복 append 없음** | `GameQuest/Client/gamequest_client_scenario.hpp:302-311` — `has_progress()`가 `progress.current_count >= current_count`로 비교한다. 그래서 `has_progress(after_replay, first_hunt, 3)`(`:206-209`)은 **replay가 4·5·50으로 이중 계산해도 통과한다.** 같은 `>=`가 reconcile(`:223-226`)과 rehydrate(`:269-272`) 검사도 무력화한다. `.NET`은 문제되는 자리를 **정확값으로 고정한다**(`GameQuestClientScenario.cs:91` — `CurrentCount: 1`). 게다가 첫 중복 재전송(`:88-94`)은 `event_id` 동일성만 보고 **progress를 다시 읽지도 않는다** ⇒ **멱등성 시나리오가 원리적으로 실패할 수 없다** |
| **SMP-CP-33** | [tictactoe:547-549](../../common/sample/tictactoe/README.ko.md): "각 `PlaceMarkReq` response는 **board, next turn**, last move actor, last move cell을 확인한다. 상대 client는 connector wait API로 **같은 state를 담은** `GameStateNotify`를 기다려 확인한다" | `TicTacToe/Client/tictactoe_client_scenario.hpp:271-273,291-293,311-313,331-333` — 4번의 비-최종 move 전부 `room_id`/`last_move_actor_id`/`last_move_cell`만 본다. **`board`는 승리 move에서 딱 한 번**(`:359`), **`next_turn`은 게임 시작 후 어디서도 단언되지 않는다.** 미러 push도 `room_id`+`last_move_cell`만 보고(`:276-277,296-297,316-317,336-337`) **response state와 대조하지 않는다.** `.NET`은 move마다 board와 NextTurn을 정확값으로 고정하고(`TicTacToeClientScenario.cs:112-114,126-129,140-143,154-157`) 미러 push마다 `Payload.State.Board == <response>.State.Board`를 비교한다(`:123,137,151,165`) |
| **SMP-CP-34** | [bingo:569-587](../../common/sample/bingo/README.ko.md) 5·7·8·9·11단계 | **5단계**: join push의 `State`를 아예 안 본다 — `client1_joined.actor_id`만 본다(`:136`). player 목록 단언(`:123-132`)은 문서가 지목한 `PlayerJoinedNotify` payload가 아니라 **response** `client2_match.state`에 걸려 있다. **7단계**(문서 `:574-575` "**두 player card가 모두** 9칸"): `client2_card`는 자기 카드만(`:149-153`), `client1_card`는 `status == running`만 본다(`:193`) — **카드를 아예 안 본다.** **8단계**(`:576-577` "`DrawSeq`·`Number`·**state**가 서로 같은지"): `draw_seq`·`number`만 비교하고(`:201-203`) **state는 비교하지 않는다.** **9단계**: `drawn_numbers`·`winners`만 대조하고(`:211-212`) **player 목록을 대조하지 않는다**(`.NET`은 actorId 순서까지 본다, `:152-154`). **11단계**(`:585-587` observer가 방을 떠나 Entry Spot으로 복귀): **서버가 세팅한 bool `stopped.stopped`를 읽을 뿐**(`:243-244`) client가 관측 가능한 증거가 없다 |
| **SMP-CP-35** | [tictactoe:526,530-533,542-544,555](../../common/sample/tictactoe/README.ko.md) 1·3·7·11단계 | **3단계**: 문서가 "host와 guest의 level은 **room 입장 조건 이상**"을 요구하는데, C++은 `room.required_level == 3`만 확인하고(`:165`) **어떤 player의 `level`도 그것과 비교하지 않는다.** guest `level`과 양쪽 `display_name`은 읽지도 않는다(`.NET`은 host·guest 둘 다 `Player.Level >= room.RequiredLevel`, `:58,:81`). **7단계**: join notify의 6개 필드 중 `room_id`·`mark` **2개만** 본다(`:251-252`) — `display_name`·`level`·`state.status`는 **필드가 존재하는데도**(`messages.hpp:178-179`) 미단언. **11단계**: 5개 중 `display_name` 누락(`:368-371`). **1단계**: `PlayNodes`가 각 Play endpoint와 SpotNode rid를 **모두** 담아야 하는데 비-owner 노드 rid 하나만 비어 있지 않은지 본다(`:62-64`) — `.NET`은 `PlayNodes.Count == PlayEndpoints.Count`를 단언한다(`:33`) |
| **SMP-CP-36** | [gamequest:604-605,608](../../common/sample/event/gamequest.ko.md): "**연결 끊고 binding 해제** → 다른 노드로 재접속 → **조회로 복원** → 이후 notify가 새 노드로", "2 노드에서 PlayerA/B가 **다른 owner에서** 동시 처리" | `GameQuest/Client/gamequest_client_scenario.hpp:230-235` — alice의 `api_a` connector를 **닫지도 disconnect하지도 않고** 두 번째 connector를 열어 재-join한다. 원래 세션은 **bind된 채 살아 있다.** unbind도, 재접속 후 `GetQuestProgressReq` **복원 조회**도, notify가 **옛 노드에 안 갔다는 negative**도 없다 — 새 노드의 positive push만 본다(`:238-253`). scale-out의 **"다른 owner"**도 어디서도 단언되지 않는다(owner 신원을 응답·push 어디서도 안 읽는다). rehydrate 준비는 **500ms 고정 sleep**이다(`:264`) — reactivation을 관측하지 않고 자고 넘어간다 |
| **SMP-CP-37** | [deliverydispatch:681-685](../../common/sample/deliverydispatch/README.ko.md): `Assigned → Accepted → PickedUp → Delivered`(재배정은 `Assigned → Reassigned → Accepted → Delivered`)가 **순서대로 도착**한다 | `DeliveryDispatch/Client/delivery_dispatch_client_scenario.hpp:107-110,147-150` — **독립적인 `wait_for` future 4개**를 걸고 선언 순서로 `.get()`한다(`:123-126,164-167`). **고정 순서로 `.get()`하는 건 각 상태가 "도착했다"만 증명하지 순서를 증명하지 않는다** — 어떤 interleaving도 통과한다. 순서 판정은 **서버가 계산한 `assertion.passed` bool**에 전부 위임돼 있다(`:176`). 덤으로 wait 등록과 `/deliveries` POST 사이에 **200ms 고정 sleep**이 경합 방지용으로 들어 있다(`:111,:151`) — `.NET`엔 없다. (**순서 미단언은 `.NET`도 동일**하므로 계약/기준선 공통 갭이다) |

### 샘플 서버 인프라 — 생명주기 · 알림 타게팅 · 상태 기계

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-CP-51** | [tictactoe:813-814](../../common/sample/tictactoe/README.ko.md): "조건을 만족하지 못하면 join을 **거부하거나 오류 response를 반환**해야 한다" | `typed_actor_join_result_t<T>`는 `variant<actor_join_accepted_t<T>, actor_join_rejected_t<T>>`인데 **양쪽 대안이 모두 `.reply`를 노출한다**(`contracts/actors/actor.hpp:167-192`). 그래서 `Bingo/.../EntrySpot/Handlers/match_bingo_actor_handler.hpp:27-28`의 `std::visit([](const auto &v){ return v.reply.state; }, joined)`와 `TicTacToe/.../EntrySpot/Handlers/play_actor_join_game_handler.hpp:18-19`의 `std::visit([](const auto &v){ return v.reply; }, joined)`가 **rejected 분기에도 컴파일되어 거절 payload를 정상 성공 응답으로 돌려준다.** TicTacToe의 level gate는 **진짜로 거절하는데**(`tictactoe_game_spot.hpp:52-55`) client는 평범한 `JoinGameRes`를 받는다. 묶음 재검토에서 SupportChat의 conversation join 두 곳도 같은 평탄화를 사용하고, agent entry join 결과는 무시한다는 사실을 추가로 확인했다. `Bingo/.../ensure_player_actor_handler.hpp:27-31`(`get_if` → throw), `observe_bingo_events_handler.hpp:27-28`(`std::get`), `TicTacToe/.../authenticate_play_session_handler.hpp:59-63`(`holds_alternative`)는 처음부터 올바르게 분기했다. ⇒ **SMP-CP-35(client 게이트가 level 조건을 평가하지 않는다)와 맞물려 아무도 관측할 수 없다** |
| **SMP-CP-52** | [bingo](../../common/sample/bingo/README.ko.md) §카드 제출: 카드는 한 번 낸다 | `Domain/Bingo/bingo_game.hpp:19-26` — `submit_card()`가 `player.card`/`marks`/`completed_lines`를 **중복 가드 없이 덮어쓴다.** 유일한 게이트가 `bingo_room_game.hpp:53-55`의 `status == running`인데, status는 **마지막 draw까지 계속 `running`**이다. handler(`Handlers/submit_bingo_card_handler.hpp:17`)도 무조건 부른다. 그리고 다음 tick의 `draw_next()`(`bingo_game.hpp:48-60`)가 **이미 뽑힌 번호 전부를 새 카드에 재적용**한다. ⇒ **1..N을 보고 나서 카드를 다시 내면 즉시 마킹돼 이긴다.** `.NET`은 둘 다 막는다(`BingoGame.cs:35` `if (IsFinished) throw`, `:40` `if (_cards.ContainsKey(actorId)) throw "already been submitted"`) |
| **SMP-CP-53** | [bingo:1053-1055](../../common/sample/bingo/README.ko.md): "이 정리는 **observer 구독 수명만** 끝내며, game room의 player cleanup이나 winner 판정 상태를 **바꾸지 않는다**" | `Spots/BingoRoomSpot/Handlers/stop_observing_bingo_events_handler.hpp:9-19` — `observers`에서 지우고 **무조건** `_context.leave_actor(...)`를 부른 뒤 `{true, node_rid}`를 돌려준다. **`_is_observer`도, 호출자가 등록된 observer인지도, `request.room_id`도 확인하지 않는다.** 그런데 이 handler는 `bingo_room_spot_t`에 등록돼 있어(`bingo_room_spot.hpp:42`) **game room에도 붙는다.** ⇒ **진행 중인 방의 player가 이 packet을 보내면 방에서 제거되고 `stopped = true`를 받는다.** `.NET`은 네 조건을 다 본다(`BingoRoom.cs:249`) |
| **SMP-CP-54** | [tictactoe:909-915](../../common/sample/tictactoe/README.ko.md): "room Spot이 승리 또는 draw를 감지해 **최종 `GameStateNotify`를 전송한 뒤** client는 `LeaveGameReq`를 보낸다" | `Spots/TicTacToeGameSpot/Handlers/play_actor_leave_game_handler.hpp:13-19` — 유일한 검사가 `request.room_id == snapshot().room_id`다. **status도 소속도 안 본다.** ⇒ 게임 도중 나가면 `on_leave_actor`(`tictactoe_game_spot.hpp:100-108`)가 상대에게 **winner가 빈 문자열이고 `draw == false`인 `game_ended_notify_t`**를 밀어 넣는다. `.NET`은 `if (!IsTerminal(state)) throw`(`TicTacToeGame.cs:169`)와 중복 leave no-op(`:171`)을 둔다 |
| **SMP-CP-55** | [deliverydispatch:478-479](../../common/sample/deliverydispatch/README.ko.md): Tracking이 **해당 배송의 고객**에게 `DeliveryStatusUpdatedMsg`를 보낸다 | `Tracking/Handlers/tracking_handlers.hpp:57,63` — actor 조회에도, 나가는 `DeliveryStatusUpdatedMsg.customer_id`에도 **컴파일 타임 상수 `sample_names_t::customer_id = "customer-1"`**(`Configuration/sample_names.hpp:31`)를 쓴다. 근본 원인은 계약이다 — `delivery_status_changed_req_t`(`Shared/Contracts/messages.hpp:224-231`)에 **`customer_id`가 없어서** Tracking이 누구 배송인지 **알 방법이 없다**(자기가 관리하는 `delivery_spot_directory_t`도 고객을 기록하지 않는다). ⇒ **두 번째 고객의 상태 push가 전부 엉뚱한 client로 간다.** `.NET`은 계약에 `CustomerId`를 싣고 끝까지 전달한다(`Messages.cs:124-129`, `DispatchZLinkAdapters.cs:56`, `Tracking/Handlers.cs:25,29`) — **SMP-CP-47이 기록한 ".NET에만 있는 `CustomerId`"가 사실은 `.NET`이 맞고 C++이 틀린 것이었다** |
| **SMP-CP-56** | [bingo:1242](../../common/sample/bingo/README.ko.md) §17.3: "진행 중 룸은 **자연 종료될 때까지** 유지되며". Play factory가 `use_drain_policy(drain_natural)`을 선언한다(`play_server_host_factory.hpp:66-67`) | `Spots/BingoRoomSpot/bingo_room_spot.hpp:139-144` — `on_leave_actor`가 actor를 지우고 `_game.leave()`를 부르고 **끝난다.** `spot_context_t::close()`(`contracts/spots/spot.hpp:533`)는 **샘플 전체에서 호출 0건**이다. `.NET`은 닫는다(`BingoRoom.cs:78-79`). ⇒ ① **observer 방**도 같은 200ms `bingo-draw` timer를 등록하는데(`:57-62`) 거기선 `should_draw()`가 영원히 false라 `cancel()`도 `on_closing()`도 **절대 안 돈다 — timer가 프로세스 수명 내내 틱한다**([IMP-CP-12](#상세)와 정확히 겹친다). ② **방이 안 닫히니 `drain_natural`이 영원히 완료되지 않는다** |
| **SMP-CP-57** | [tictactoe:199,481,489,493,495](../../common/sample/tictactoe/README.ko.md): `TicTacToeGameTimerHandler`를 두고, `TicTacToeMatch`가 "turn, **timeout**"을, `TicTacToeGame`이 "**timer 등록**"을, handler가 "**timer callback**"을 맡는다 | `TicTacToe/Server/` 전체에서 **`add_timer` grep 0건.** `tictactoe_game_spot.hpp`에 `on_initialize`도 `on_closing`도 없고, `Domain/TicTacToe/tictactoe_match.hpp`에 tick도 deadline도 없으며, `TicTacToeGameTimerHandler` **파일이 없다.** `.NET`엔 전부 있다(`TicTacToeGame.cs:18-19` — `GameTickPeriod=1s`·`TurnTimeout=15s`, `:105-108` `AddTimer<...>`, `:111-113` `OnClosingAsync` cancel, + `Handlers/TicTacToeGameTimerHandler.cs`). ⇒ **멈춘 턴이 C++에선 영원히 만료되지 않는다** |
| **SMP-CP-58** | [공통 샘플:336-343](../../common/sample/README.ko.md): 샘플 routing id는 애플리케이션이 **명시적으로 정한 문자열**에서 만든다 | `Application/RoomAllocation/bingo_room_allocator.hpp:23,34` — `mode + "-room-" + std::to_string(++_next)`이고 `_next`가 **프로세스별 멤버**다. `allocate_bingo_room_handler.hpp:43`가 그걸 그대로 `spot_rid_t::from_string(...)`로 만든다. ⇒ **`play-a`와 `play-b`가 둘 다 `two-player-room-2`를 만들어 공유 spot mesh에 같은 spot rid를 등록한다.** SMP-CP-56 때문에 옛 방이 **아직 살아 있다.** `.NET`은 `Guid.NewGuid()`를 쓴다(`BingoRoomAllocator.cs:22`). (3-client self-check에선 방이 하나뿐이라 안 드러난다) |
| **SMP-CP-59** | actor disconnect 시 구독을 정리한다 | `Spots/EntrySpot/tictactoe_entry_spot.hpp:75` — `on_disconnect_actor`가 `mark_disconnected()`만 부른다. `observers` 맵(`:93`)은 **`on_leave_actor`에서만** 정리된다(`:72`). `.NET`은 양쪽에서 지운다(`PlayEntrySpot.cs:61,70`). ⇒ **disconnect 뒤에도 `on_player_win_milestone`(`Handlers/player_win_milestone_event_handler.hpp:12-17`)이 죽은 actor의 unbound session에 `WinMilestoneNotify`를 계속 밀어 넣는다** |
| **SMP-CP-60** | [deliverydispatch:328-329,478-479](../../common/sample/deliverydispatch/README.ko.md): `FindCustomerActorReq`는 **CustomerGateway → directory** 경로에만 있다. Tracking의 시퀀스는 조회 hop 없이 `Tracking → CustomerEntry: DeliveryStatusUpdatedMsg`다 | `Tracking/Handlers/tracking_handlers.hpp:48-58` — 상태 변경마다 `resolve_spot_handle(...)` 뒤 **blocking `request_to_spot(FindCustomerActorReq).async<...>()`**를 하고 나서야 `send_to_actor`를 한다. framework가 **같은 표면을 갖고 있는데**(`contracts/actors/actor.hpp:169-177`의 `actor_directory_t::find/ensure`) **C++ 샘플 트리 전체에서 `actor_directory_t` grep 0건**이다. `.NET`은 `actorDirectory.FindAsync(...)`를 쓴다(`Tracking/Handlers.cs:29`). SMP-CP-20(GameQuest)과 같은 모양이지만 **다른 샘플·다른 표면**이다 |
| **SMP-CP-61** | [deliverydispatch:196,215](../../common/sample/deliverydispatch/README.ko.md): Tracking = "**tracking channel server, evidence store**" — spot도 actor도 없다 | `Tracking/Spots/DeliveryTrackingSpot/delivery_tracking_spot.hpp:12-108`은 **평범한 클래스**이고 `_history`를 **읽는 곳이 없다.** `delivery_spot_directory.hpp:14-79`는 DI 싱글턴으로 등록돼(`Tracking/main.cpp:112`) **한 번 쓰이고 한 번도 안 읽힌다**(`require()` 호출 0건). `Tracking/Spots/EntrySpot/customer_entry_spot.hpp:9-12`와 `Tracking/Actors/customer_actor.hpp:11-25`는 **어디서도 인스턴스화되지 않는다** — 진짜는 `CustomerGateway/main.cpp:111,70`에 있다. `customer_actor_t::snapshot()`(`:18-21`)은 node rid `"tracking-spot"`·generation `1`짜리 **가짜 `actor_ref_snapshot_t`를 지어낸다.** 게다가 `Tracking/main.cpp:119-122`가 `add_spot_mesh(customer_actor_discovery)`를 `enable_router`+`enable_pub_sub`로 등록하는데 **spot도 entry spot도 actor factory도 0개**다 |
| **SMP-CP-62** | [bingo:1140](../../common/sample/bingo/README.ko.md) §14: "`BingoRewardAcquiredEvent`는 **game state를 바꾸는 경로가 아니다**". 문서의 subscribe 경로는 **observer push 전용**이다 | reward subscription handler는 observer room의 `BingoRewardAnnouncedNotify` 전달만 담당한다. game 종료와 actor cleanup은 draw timer handler 한 곳에서 수행한다. |
| **SMP-CP-63** | 상태값은 계약이 고정한다 | 도달 불가능한 `"playing"`·`"ended"` 술어를 가진 snapshot wrapper, 항등 contract mapper, 미등록 created handler, EntrySpot의 미사용 match를 제거했다. 실제 상태 판단은 `tictactoe_match_t`와 `tictactoe_status_t` 한 경로에만 남는다. |
| **SMP-CP-64** | — | `bingo_game_t`의 player raw pointer와 `attach_players()`를 제거했다. 카드 제출은 room aggregate가 소유한 player vector를 호출 인자로 전달하므로 `bingo_room_game_t`의 기본 복사·이동이 독립된 state를 유지한다. |

### 메시지 계약 3자 대조 (문서 ↔ C++ ↔ `.NET`)

**전제 — 이 절의 wire 파손 다수가 여기서 나온다.** `.NET`은 `JsonSerializerDefaults.Web`을
`DefaultIgnoreCondition` 없이 쓴다(`dotnet/src/Zlink.Framework/Runtime/Messaging/ZLinkJsonSerializerOptions.cs:10-15`)
⇒ **`null`이 wire에 실리고**, `JsonStringEnumConverter`가 트리 어디에도 없어 **enum이 정수로 나간다.**
C++는 nlohmann `json.value(k, default)`로 읽는데, 이건 **키가 없을 때만** default를 준다 —
키가 `null`로 존재하면 `get<T>()`가 **`type_error.302`를 던진다.** 샘플별 null 가드 개수:
**Bingo 0 · TicTacToe 0 · GameQuest 0** · SupportChat 2 · DeliveryDispatch 2 · ShoppingMall 1.

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-CP-38** | [deliverydispatch:334-342](../../common/sample/deliverydispatch/README.ko.md): `DeliveryStatus`의 값을 **문자열로** 고정한다(`Created`·`Assigned`·…) | C++ `Shared/Contracts/messages.hpp:47-55`가 문자열 상수를 두고 `delivery_status_notify_t`(`:250-257`)가 `"status":"Assigned"`를 내보낸다(`:612-618`). `.NET` `Shared/Contracts/Messages.cs:5-14`는 **C# `enum`**이고 string 컨버터가 없어 **`"status":1`**을 내보낸다 — `DeliveryStatusNotify`(`:135-139`)·`DeliveryStatusChangedReq`(`:124-129`)·`DeliveryStatusUpdatedMsg`(`:141-146`) 전부. ⇒ **client-facing push에서 하드 wire 파손.** 문서도 C++ 편이다 |
| **SMP-CP-39** | [gamequest:535-539](../../common/sample/event/gamequest.ko.md): `QuestProgress`는 `…, LastSourceEventId: string?, Version: int64, UpdatedAtUnixMs` | C++ `Shared/Contracts/messages.hpp:128-139`는 **문서와 정확히 일치**한다(`lastSourceEventId`+`version` 방출, `:437-444`). `.NET` `Shared/Messages.cs:61-68`은 이름이 **`LastEventId`**이고 **`Version` 필드가 아예 없다.** C++의 `from_json`(`:445+`)이 `json.at(...)`을 쓰므로 `.NET` payload를 받으면 **즉시 throw**한다. ⇒ **조회 모델 = 이 샘플의 client-facing 산출물인데 그게 깨진다** |
| **SMP-CP-40** | [tictactoe:716-726](../../common/sample/tictactoe/README.ko.md): `GameState`의 `Winner?`·`XActorId?`·`OActorId?`·`LastMoveActorId?`·`LastMoveCell: int?` **다섯이 nullable** | C++ `Shared/Contracts/messages.hpp:141-153` — **전부 non-nullable**, `last_move_cell = -1` sentinel. `from_json`(`:361-373`)에 **null 가드 0개**. `.NET` `Messages.cs:99-108`은 전부 nullable. ⇒ C++는 `"winner":""`/`"lastMoveCell":-1`을 내보내고 **문서·`.NET`은 `null`**을 내보낸다. **양방향 파손**이고, 하필 **최상위 client-facing state 객체**다. 덤으로 C++에만 `draw` 필드가 있다(`:148`, 방출 `:354`) — 문서·`.NET`은 `Status == "Draw"`로 표현한다 |
| **SMP-CP-41** | [supportchat:475-481](../../common/sample/supportchat/README.ko.md): `JoinConversationReq` = `ParticipantId`·`Role`·`DisplayName`. client는 비워 보내지만 **Support 서버가 conversation-actor join 때 채워 보낸다** | C++ `Shared/Contracts/messages.hpp:172-175` — **필드가 하나도 없는 빈 구조체**이고 `{}`로 직렬화된다(`:526-529`). `.NET` `Messages.cs:48-51`엔 셋 다 있다. ⇒ **C++ Support 서버는 문서가 그 hop에서 요구하는 참가자 신원을 구조적으로 보낼 수 없다** |
| **SMP-CP-42** | [supportchat:397-402](../../common/sample/supportchat/README.ko.md): `ActorRefSnapshot`은 framework가 주는 wire 모델이며 "**샘플이 정의하지 않는다**". `NodeRid: bytes` | C++ `messages.hpp:108-113`이 **자기 `support_actor_ref_snapshot_t`**(`node_rid: string`, …)를 정의해 `EnsureSupportUserActorRes`(`:124-128`)·`EnsureAgentConversationRes`(`:138-143`)에 쓴다. `.NET`은 framework 타입을 쓴다(`Server/Configuration/SupportServerContracts.cs:12,22`). **C++ Bingo·DeliveryDispatch도 framework 타입을 쓴다 — SupportChat만 포크했다** |
| **SMP-CP-43** | [shoppingmall:737,762,794-797,805](../../common/sample/event/shoppingmall.ko.md): `Amount: decimal`, `OrderState.Amount: decimal?` | C++가 `double`을 쓴다 — `order_state_t`(`Shared/Contracts/messages.hpp:58`)·`start_order_workflow_req_t`(`:166`)·`authorize_payment_command_t`(`:125`)·`cart_seed_t`(`:140`). `.NET`은 `decimal`(`Messages.cs:24,35,54`). ⇒ **통화를 이진 부동소수로 다루고**, `decimal`→`double`은 **round-trip하지 않는다.** 게다가 `order_state_t::from_json`이 nullable **문자열**만 `json_nullable_string`으로 가드하고(`:260-270`, 사용처 `:411-413`) **`amount`는 안 해서** `"amount": null`을 받으면 **throw**한다 |
| **SMP-CP-44** | [deliverydispatch:312](../../common/sample/deliverydispatch/README.ko.md): `BindCourierSessionRes` = `CourierId`·`Actor`·`SessionRoute` | C++ `messages.hpp:109-115`는 **문서대로** `actor: actor_ref_snapshot_t{nodeRid, actorId, generation}`를 싣는다. `.NET` `Messages.cs:44-53`은 `NodeRid` 하나만 갖고 `Actor => new CourierActorBindingSnapshot(NodeRid)`로 만든다 — **`actorId`도 `generation`도 없는 actor 객체**다. ⇒ 하필 **DeliveryDispatch 릴리스 게이트가 노드 배치를 단언하는 바로 그 필드**다. 덤으로 `BindCourierSessionReq`는 문서상 client가 `CourierId`만 보내는데(`:311`) C++는 non-optional이라 **항상 `actor:{nodeRid:"",actorId:"",generation:0}`을 실어 보낸다**(`:101-107`; `.NET`은 `?= null`, `Messages.cs:39-42`) |
| **SMP-CP-45** | [gamequest §11.2:503-509](../../common/sample/event/gamequest.ko.md): entry→owner는 **flat one-way `GameplayMsg`** = `{EventId, PlayerId, Type, Payload: bytes, OccurredAtUnixMs}` | C++ `messages.hpp:222-226` = `gameplay_msg_t{ gameplay_event_envelope_t event; }` — **8필드 envelope를 감싼 wrapper**(`:209-219`). `Payload: bytes`가 없고, `Type`→`event_type`, `OccurredAtUnixMs`→`created_at_unix_ms`이며 **문서에 없는 필드가 4개** 더 붙는다. `.NET`엔 **`GameplayMsg` 자체가 없고** 같은 hop을 **`ApplyGameplayEventReq`(request!)**로 보낸다(`Server/Configuration/SampleConfiguration.cs:25`). C++는 one-way `send`다(`Server/GameApi/main.cpp:469`, 수신 `Server/QuestMission/main.cpp:264,282`). ⇒ **같은 hop인데 packet 이름도 호출 방식(one-way vs request)도 다르다** |
| **SMP-CP-46** | [tictactoe:672-674](../../common/sample/tictactoe/README.ko.md): `TicTacToeGameJoinRes` | C++엔 **Req만 있고**(`messages.hpp:135-139`) **Res가 없다** — actor join 응답을 `join_game_res_t`로 대신한다(`.../TicTacToeGameSpot/tictactoe_game_spot.hpp:51-61`, `.../EntrySpot/Handlers/play_actor_join_game_handler.hpp:18`). `.NET`엔 있다(`Messages.cs:60`, 사용처 `TicTacToeGame.cs:116,137`) |
| **SMP-CP-47** | 각 문서의 §메시지 계약 표가 필드를 고정한다 | **TicTacToe**: `GameStateNotify`가 문서(`:689-691`)·`.NET`(`Messages.cs:84`)엔 `{State}`뿐인데 C++엔 `room_id`+`next_turn`이 더 있다(`messages.hpp:220-226`, 방출 `:526-529`). `AuthenticatePlayerRes`도 문서(`:620-622`)엔 `{Player}`인데 C++엔 `accepted`+`reason`이 더 있다(`:85-91`). **GameQuest**: `SyncQuestProgressReq`가 문서(`:492`)·`.NET`(`Messages.cs:31`)엔 `{PlayerId}`(보정 트리거)인데 C++엔 `snapshot_kill_count`가 더 있다(`messages.hpp:159-166`) ⇒ **서버가 재계산해야 할 값을 client가 넘긴다.** **DeliveryDispatch**: `DeliveryStatusChangedReq`가 문서(`:326`)·C++(`messages.hpp:224-231`)엔 4필드인데 **`.NET`에만 `CustomerId`**가 더 있다(`Messages.cs:124-129`) |
| **SMP-CP-48** | SMP-CP-29에 이어, 계약에 없는 wire 타입이 더 있다 | **GameQuest**: `ApplyGameplayEventReq/Res`(`messages.hpp:252-264`)·`NotifyQuestProgressMsg`(`:230-236`)·`PlayerQuestSpotCreateReq`(`:186-190`)·`EnsurePlayerQuestSpotReq/Res`(`:174-184`). **ShoppingMall**: `EnsureOrderWorkflowSpotReq`(`messages.hpp:207-211`)·`PendingMappingReq`(`:213-219`, `owner_instance_id`를 흘린다)·`DeleteProjectionReq`(`:221-225`)·`OkRes`(`:227-231`)·`ServerAssertionReq/Res`(`:233-250`). 문서 §11 grep **전부 0건** |
| **SMP-CP-49** | 언어 간 wire 호환 | 위 "전제" 참조 — `.NET`이 `null`을 싣는데 C++ **Bingo·TicTacToe·GameQuest의 null 가드가 0개**다. ⇒ `.NET` client/peer가 보낸 payload에서 **C++가 `type_error.302`로 던진다.** SMP-CP-40·43이 구체 사례이고, **이건 그 셋을 넘어 계약 표면 전반에 깔린 체계적 위험**이다. 가드 패턴 자체는 이미 트리에 있다(`ShoppingMall/.../messages.hpp:260-270`의 `json_nullable_string`) — **쓰지 않았을 뿐이다** |
| **SMP-CP-50** | [bingo §11:596-844](../../common/sample/bingo/README.ko.md)가 wire를 고정한다 | `Shared/Contracts/bingo_messages.proto:150-154`의 `BingoActorEntrySpotNotify`(`actor_id`·`room_id`·**`target_node_rid`**) — `.NET`이 보낸다(`Server/Play/.../EntrySpot/BingoEntrySpot.cs:61`). **C++ Bingo 샘플엔 타입도 handler도 0건**이고 **문서 grep도 0건**이다. ⇒ `.NET`만 쏘는 유령 packet이고 **transport 신원(`target_node_rid`)을 client 계약에 흘린다** |

**Bingo가 6개 중 가장 깨끗하다** — 실제 wire가 전 hop protobuf이고(`Server/common_codecs.hpp:30-32`,
`bingo_session.hpp:61`, `.NET` `Client/Program.cs:48`), `.NET`엔 **C# 메시지 record가 아예 없어**
`.proto`가 유일한 계약이다. 두 `bingo_messages.proto`는 **바이트 단위로 동일하다**(`diff` 확인).
`optional int32 last_drawn_number`·`optional string observed_room_id`도 C++가 sentinel과 정확히
매핑한다(`protobuf_conversions.hpp:67-68,88`·`:270-271,283`). **wire 파손이 몰린 나머지 5개와
정확히 대비된다 — 공유된 스키마 파일 하나가 3자 대조를 대신한다.**

**DeliveryDispatch 게이트의 나머지는 충실하다** — subscribe→`DeliveryId`, create→동일 `DeliveryId`,
`courier-a`→node-1 / `courier-b`→node-2 bind를 응답의 actor node rid로 확인, offer가 각 courier
자기 connector로 push, 상태별 courier 귀속, 서버 evidence까지 `.NET` 기준선과 점 대 점으로 맞는다.

**push 대기 규칙은 4개 샘플 전부 깨끗하다** — 모든 push 대기가 connector의 `wait_for<T>()` 공개
API를 쓴다. 샘플 로컬 inbox 루프는 없다. (GameQuest `wait_for_progress()`(`:363-381`)는 100ms
폴링이지만 **push가 아니라 request/response**를 폴링하므로 규칙 위반이 아니다.)

### 상세 — E2E

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-CP-01** | [E2E README §3](../../common/e2e/README.ko.md): config는 **11개**다. [§8](../../common/e2e/README.ko.md): 각 config의 P0는 모두 구현돼야 한다 | `e2e/run_e2e_all.sh:15-27`의 `CONFIGS`에 **`SpotActorTransfer`(Config 10)가 없다.** 대신 공통 e2e 문서 grep **0건**이고 `.NET` e2e에도 없는 **`DeliveryDispatch`가 들어 있다.** ⇒ ST 20개 시나리오가 forward/reverse/shuffle **3변형 어디에서도 실행되지 않는다** |
| **E2E-CP-02** | [config-9:38,69-76](../../common/e2e/config-9-to-actor-messaging.ko.md): `session gateway 2`가 **실제 stream session을 받고 actor bind를 만든다.** TA-A1은 stream bind·bound-session push·"caller가 bind를 만들지 않았다"는 negative evidence를 요구한다 | `ToActorMessaging/Server/`에 **session gateway 프로세스가 없고**(Actor·Caller뿐), `Client/main.cpp`에 **stream connector 사용이 0건**이다. TA-A1은 HTTP `/ensure`+`/send`+`/request`만 친다(`Client/main.cpp:231`). TA-A3의 "late bind"는 실제로는 **"없는 actor 호출 → 만들고 호출"**이고(`:244`), TA-A4의 "disconnect/destroy"는 **라벨 문자열**일 뿐 그 lifecycle 전이를 만들지 않는다(`:253`). 그런데 feature-map은 TA-A1~A4를 전부 `구현`으로 적는다 |
| **E2E-CP-03** | [config-11:31,33](../../common/e2e/config-11-observability-ops.ko.md): `Session` 1 + `Play` 2 + **`OrderWorkflow` 2**. [§2.4](../../common/e2e/README.ko.md): 하나의 서버 프로젝트를 role 옵션으로 바꾸지 않는다. [§2.5](../../common/e2e/README.ko.md): 시나리오 ID 하나 = client scenario 파일 하나 | `ObservabilityOps/`에 **`Client/`가 없다** — 소스가 `Server/main.cpp`·`Trigger/main.cpp` 둘뿐이고 단언은 `run_e2e.sh`의 인라인 python이 한다. 서버는 **환경변수 role 스위치** 하나로 갈리고(`Server/main.cpp:251,469` — `ZLINK_CPP_E2E_ROLE`), **`OrderWorkflow` 역할이 0건**이다(`play-a`가 Session을 겸한다). runner 셀렉터도 시나리오 ID가 아니라 **phase**(`flow\|metrics\|drain\|…`)다 |
| **E2E-CP-04** | [§2.3](../../common/e2e/README.ko.md): Pub/Sub은 **subscriber 역할 server의 bounded evidence wait**를 성공 기준으로 쓴다. client는 publish를 트리거한 뒤 **각 subscriber의 marker를 확인한다** | `PubSub/Client/Scenarios/fanout_basic_delivery_scenario.hpp:11-20` — publish를 25번 하고 `"scenario PS-A1 passed"`를 **출력하고 끝난다.** 단언이 **0개**다. `/evidence/wait`는 **`run_e2e.sh`에서만** 불린다(client에서 0건). 7개 PS 시나리오 파일이 전부 같은 모양이다 |
| **E2E-CP-05** | [config-2:558,568,581](../../common/e2e/config-2-spot-service.ko.md): SM-F3·SM-F4·SM-F5가 정의돼 있다 | `SpotService/run_e2e.sh`의 `all` 목록에 **SM-F3·SM-F4·SM-F5가 없다.** 대신 문서 grep 0건인 **`SM-Q9`**가 들어 있다. `SM-A1-A2-A4-F1-F2`처럼 5개를 한 모드로 묶은 것도 §2.5 위반이다 |
| **E2E-CP-06** | [§3.1 축과 별개로 모든 config가 지켜야 하는 검증 요구](../../common/e2e/README.ko.md): 응답에 실린 actor ref는 concrete해야 한다 — **node rid 비어 있지 않음, `generation > 0`** | `SpotService/Server/Play/Spots/play_actor_model.hpp:188,200,218` — `.generation = 0`을 **리터럴로 박는다.** client에서 `generation`을 단언하는 코드는 **0건**이다 |
| **E2E-CP-07** | [config-10:73](../../common/e2e/config-10-spot-actor-transfer.ko.md): ST-A1은 **정확히** `admission → leave → joined → location_committed → success_reply` 순서를 요구한다 | `SpotActorTransfer/Client/main.cpp:355` — `wait_evidence`로 **순서 없는 포함 여부**만 본다(`:183`). **순서 helper는 있는데(`:203`) 쓰지 않는다.** `location_committed` 마커는 요구조차 안 한다. ST-A2도 요구된 negative side effect 6개 중 **1개만** 본다(`:378`) |
| **E2E-CP-08** | [§2.6](../../common/e2e/README.ko.md): framework host에는 **설정 파일 경로만** 넘긴다. **server와 client 앱 코드가 직접 쓸 수 있는 환경 변수는 0개다.** 어긋나면 **feature-map에 configuration migration gap을 기록한다** | `ToActorMessaging` **하나를 빼고 전 config**가 endpoint·Redis·key prefix·log dir·role을 `getenv`로 직접 읽는다(예: `RegistrationCodec/Server/Configuration/server_options.hpp:12`, `SpotService/Server/Play/Handlers/play_actor_handlers.hpp` — **handler 안에서** 읽는다). **11개 feature-map 중 이 갭을 기록한 곳이 0개다** |
| **E2E-CP-09** | [§2.1](../../common/e2e/README.ko.md): readiness 3초 / poll 0.1초 / route settle 5초 / scenario settle 3초 / HTTP probe 3초를 **명시적인 config 상수**로 둔다 | `SpotActorTransfer/run_e2e.sh:22` — `LOCAL_READINESS_TIMEOUT_SECONDS=30`(**기준의 10배**), ROUTE/SCENARIO settle 상수 없이 맨 `sleep 5`/`sleep 1`. `ObservabilityOps/run_e2e.sh` — **대기 상수가 하나도 없다.** `for _ in $(seq 1 300); sleep 0.1`(=30초 readiness)과 맨 `sleep 2`/`sleep 1`이 흩어져 있다 |
| **E2E-CP-10** | [§2.5](../../common/e2e/README.ko.md): 시나리오 ID 하나 = client scenario 파일 하나 | `ToActorMessaging`(302줄)·`SpotActorTransfer`(1120줄)·`DiscoveryRegistryHa`(339줄)·`ObservabilityOps`는 `Client/Scenarios/`가 **없고** 단일 `main.cpp`에 전 시나리오가 들어 있다. Config 1·2·3·4·5·7은 per-scenario 파일이 제대로 있다 |
| **E2E-CP-11** | [§2.8](../../common/e2e/README.ko.md): 상태는 `implemented`/`not-supported`/`blocked`/`deferred`로 **명확히** 쓴다. **버그를 피해 시나리오를 약하게 만들지 않는다** | `ObservabilityOps/feature-map.ko.md` — OBS-B1(connector `reconnects`)·OBS-B3(lease lateness)·OBS-C2(bound-session 연속성)를 본문에서 "pending/대체"라 **인정하면서** 상태 열은 `구현`/`구현(부분)`으로 적는다. `run_e2e.sh` 헤더 주석은 "remaining OBS ids are reported as PENDING"이라고 **정반대로** 말한다 |
| **E2E-CP-12** | [§2.7](../../common/e2e/README.ko.md): **기본 실행은 그 config의 구현된 시나리오를 순차 실행한다** | `DiscoveryRegistryHa/run_e2e.sh:8` — 인자가 없으면 **`SF-A1` 하나만** 돈다. 프레임워크 knob(heartbeat·lease·polling·grace)도 `ZLINK_CPP_SF_*` 환경변수로 뚫려 있다(`:9-12`) |
| **E2E-CP-13** | [§2.2·§2.8](../../common/e2e/README.ko.md): config마다 `feature-map.ko.md`를 둔다 | `e2e/SpotActorTransfer/`에 **`feature-map.ko.md`가 없다.** 11개 config 중 유일하다 |
| **E2E-CP-14** | [§3.1:489-497,546-547](../../common/e2e/README.ko.md): **"route mesh 없음 × 분리 배치" 조합을 Config 2의 P0 시나리오에 `P0`으로 적용한다.** 발굴 결함의 **대다수가 이 조합**에서 나왔다 | **두 topology가 교차하지 않아 그 조합이 아예 만들어지지 않는다.** ① route mesh는 "분리 배치"를 이루는 두 역할에서 **무조건** 켜진다 — `Server/Play/play_host_factory.hpp:134`·`Server/Session/session_host_factory.hpp:45`가 `add_route_mesh`를 조건 없이 부르고 **opt-out knob이 없다.** ② route-mesh 없는 유일한 실행은 `run_e2e.sh:1215-1228`(`ZLINK_CPP_E2E_DISABLE_ROUTE_MESH=1`)인데 **`multi-a`/`multi-b`만 띄운다** — session 노드도 stream 노드도 gateway도 없다. ⇒ **route-mesh-free 실행에는 session/spot 분리가 전혀 없고, session이 분리된 P0(D1·D2·D4·D5·D6·D7·D12·D15·B1·B2…)은 전부 route mesh를 깔고 돈다.** (Config 2 자체의 start-order 축은 `ordered_roles()`(`:682-702`)로 세 경로 전부에 제대로 걸려 있다. **단 다른 config는 아니다 — E2E-CP-22 참조**) |
| **E2E-CP-15** | [§2.5](../../common/e2e/README.ko.md): config 문서의 시나리오 ID 하나는 **client scenario 파일 하나와 대응**한다 | `RegistrationCodec/Client/Scenarios/`에 파일이 10개인데 config-4는 ID가 11개다. **`RC-A6`(P0, [config-4:103-107](../../common/e2e/config-4-registration-codec.ko.md) "잘못된 등록은 시작 단계에서 차단")만 client 파일이 없고** `run_e2e.sh`가 대신 단언한다. **경미 항목이다** — "기동 자체가 실패"는 client HTTP 호출로 표현할 수 없는 성질이라, §2.5 문구가 이 유형을 예외로 인정할지 판단이 필요하다 |
| **E2E-CP-16** | [config-2:324-332](../../common/e2e/config-2-spot-service.ko.md): **`SM-D2`는 `P0`**다 — client가 `session-a`에 붙고 bind 대상 actor는 **비선호 원격 play 노드(`play-b`)**에 있으며, 교차 노드 양방향 relay가 돌아야 한다. §3.1이 쓰인 바로 그 실패 유형이다 | `run_e2e.sh:3736-3745`의 `all` 목록에 **`SM-D2`가 없다.** scenario 파일(`Client/Scenarios/sm_d2_scenario.hpp`)도 runner 블록(`run_e2e.sh:2568`)도 **멀쩡히 있는데 기본 실행이 부르지 않는다.** feature-map(`:69`)은 SM-D2를 커버했다고 보고한다. ⇒ E2E-CP-05(Track F·SM-Q9)와 **별개**이며, 이쪽은 **Track D의 P0**다 |
| **E2E-CP-17** | [config-2:588-593](../../common/e2e/config-2-spot-service.ko.md): "public Spot manager로 **target user Spot을 닫는다.** 이후 **닫힌 Spot 경로의 실패를 확인**하고 같은 channel로 일반 channel request를 다시 보낸다" | `Client/Scenarios/sm_f5_scenario.hpp:16-56` — **spot을 닫는 코드가 없다.** 대신 `/spot/direct`를 보내고 **성공하지 않으면 throw한다**(`:49-53`, `value != "route-survived-f5:reply"`). runner도 `route-survived-f5`가 도착했음을 단언한다(`run_e2e.sh:2404-2406`). ⇒ **시나리오가 자기 계약의 정반대("Spot이 여전히 살아 있다")를 증명한다.** 덤으로 `play_b` client가 `play_b_http_endpoint`가 아니라 `play_http_endpoint`로 만들어져(`:34-36`) 인자가 死문자다 |
| **E2E-CP-18** | [config-2:473](../../common/e2e/config-2-spot-service.ko.md): "검증: error reply + **message-flow error evidence**(`Surface`=`SpotRoute`, `Reason`=`HandlerMissing`, `Action`=`ReplyError`/`Drop`)". **그 evidence가 곧 이 시나리오다** | `Client/Scenarios/sm_e1_scenario.hpp:69,84-87,100-104,117` — 서버 HTTP helper가 돌려준 bool(`request_failed`·`failed`·`sent`)만 본다. `sent`는 one-way가 **제출됐다**는 뜻일 뿐 drop을 말하지 않는다. runner 블록(`run_e2e.sh:3390-3427`)도 앱 marker만 보고 **`-flow.log` grep이 없다** — SM-C1(`:2144-2147`)·SM-C2(`:2202-2205`)는 `surface=spot_route…reason=handler_missing…action=drop`을 제대로 grep하는데 E1만 안 한다 |
| **E2E-CP-19** | [config-2:574-579](../../common/e2e/config-2-spot-service.ko.md): `SM-F4`(P0)는 넷을 요구한다 — ① request → error reply, ② **`send`(command) → reply 없이 drop되고 failure counter가 오른다**, ③ **message-flow error 분류가 남는다**, ④ 같은 channel의 다른 routing은 무영향 | `Client/Scenarios/sm_f4_scenario.hpp:23-52` — 없는 spot에 `/spot/direct` **request 한 번**(HTTP ≥ 400 기대) + 정상 request 한 번, 그리고 `"scenario SM-F4 passed"` 출력. **`send`/command가 아예 없고**, counter도 flow 로그도 안 본다. runner 단언도 **긍정 경로 하나뿐**이다(`run_e2e.sh:2402-2403`) |
| **E2E-CP-20** | [config-2:73](../../common/e2e/config-2-spot-service.ko.md): `SM-A1`은 "생성된 user spot의 location row가 `IZLinkLocationRuntimeQuery.ListSpotLocationsAsync(filter)`로 **조회된다**(spot lifecycle의 자동 row 등록 확인)"를 요구한다 | `Client/Scenarios/sm_a1_scenario.hpp:46-82`는 reply 필드만 본다. `run_e2e.sh:1367-1381`은 앱 marker만 본다. **`SpotService/` 트리 전체에서 location runtime query 표면(`list_spot_locations` 등) grep 0건**이다(`Server/Shared/Support/location_store.hpp:26`의 store *등록*만 있다). ⇒ 하필 **[IMP-CP-10](#3-라운드-3)이 "spot topology 조회는 항상 빈 페이지를 반환한다"고 지목한 그 표면**이고, 그걸 잡아야 할 e2e가 없다 |
| **E2E-CP-21** | [§3.1:527-529](../../common/e2e/README.ko.md): "location 발견·dial 수렴 직후 settle 지연 없이 즉시 첫 요청을 보낸다. **재시도나 sleep으로 가리지 않는다** — 첫 요청이 바로 성공하거나 fail-fast로 분류되는 것 자체가 검증 대상이다" | **세 config가 같은 방식으로 가린다.** Config 2: `Server/Play/Handlers/play_spot_route_handlers.hpp:876-898`의 `request_with_retry()`가 **10초/100ms retry**를 돌고 그 위에 `sm_c5_scenario.hpp:42-69`가 **또 10초 retry**를 감싼다. Config 1: `Server/Provider/Endpoints/provider_endpoints.hpp:17-38,40-60`이 **30초/100ms retry**를 돌고, 모든 시나리오가 `sleep "$ROUTE_SETTLE_SECONDS"` 뒤에 실행된다(`run_e2e.sh:378,389,408,…`). Config 5: **P0인 `RL-B4`**의 복구 트래픽 루프가 30초간 **모든 예외를 삼킨다**(`rl_b4_runtime_drain_scenario.hpp:162-183`). ⇒ **첫 요청 실패가 원리적으로 관측 불가능**하다 |
| **E2E-CP-22** | [§3.1 기동 순서 축:493,504-506](../../common/e2e/README.ko.md): config 러너가 기동 순서를 **인자로 받고**, 역방향 1회 + 고정 seed shuffle 1회를 최소로 돈다. Config **1·2·9**가 이 축의 대상이다 | `run_e2e_all.sh:29-32,121`이 `forward`/`reverse`/`shuffle:20260709` **3변형을 11개 config 전부에 `E2E_START_ORDER`로 export**한다. 그런데 **그 변수를 읽는 러너는 `SpotService`·`ToActorMessaging` 둘뿐**이다. ⇒ **나머지 9개 config는 완전히 동일한 실행을 3번 반복**한다 — 커버리지는 0인데 게이트 시간만 3배다. 특히 **문서가 이 축의 대상으로 지목한 Config 1(`RegistryMessaging`)이 변수를 읽지 않는다** |
| **E2E-CP-23** | [config-1:121-123](../../common/e2e/config-1-location-messaging.ko.md): `RM-A4`(P0)는 rid `api-a`가 endpoint p2로 바뀔 때까지 기다려 **`ListPeerLocations`가 살아 있는 row 하나(endpoint=p2)만** 반환함을 단언하고, **consumer 재시작 없이** 재요청해 **stale p1으로 반복 timeout하지 않음**을 본다 | `run_e2e.sh:405-406` — v1을 죽이고 **새 프로세스**를 띄운다. `rm_a4_same_rid_failover_scenario.hpp:25-31`은 그 **새 프로세스의 HTTP endpoint에 직접** 20번 요청한다. resolve를 하는 channel client가 **v1이 이미 죽은 뒤에 생성**되므로 **peer handover도, 피해야 할 stale endpoint도 존재하지 않는다.** `/locations/peers` 호출 **0건**. 유일한 단언 `instance_id == "api-a-v2"`(`:29`)는 v2가 유일한 생존자라 **자명하게 참**이다. `.NET`은 둘 다 한다(`RmA4SameRidFailoverScenario.cs:39,69-80` — row 1개·endpoint v2, `:56-66` — `v1Count == 0 && v2Count == 20`) |
| **E2E-CP-24** | [config-1:158](../../common/e2e/config-1-location-messaging.ko.md): `RM-B2`(P0) — "consumer가 죽은 endpoint로 timeout을 **반복하지 않음**" + "**지속 request 중** scale-in이 나도 완료된 요청은 정상 reply 또는 정해진 public error로 끝난다" | `rm_b2_scale_in_scenario.hpp:52-64` — settle 루프가 요청을 `catch (...) { settled = 0; sleep 200ms; }`로 감싸 **40 × 1.5초** 재시도한다. **문서가 금지한 바로 그 동작을 단언 대상이 아니라 삼킴 대상으로 만든다.** 게다가 client는 `wait_for_file`에 파킹돼 있고(`:30-31`) 그동안 러너가 api-b를 죽이고 5초를 잔다(`run_e2e.sh:458-461`) ⇒ **scale-in 순간에 흐르는 트래픽이 0**이다 |
| **E2E-CP-25** | [config-1:109-110](../../common/e2e/config-1-location-messaging.ko.md): `RM-A2`(P0) — consumer가 location store 자동 연결 **없이** 붙고, "**auto reconcile은 manual endpoint를 끊지 않는다(manual 연결 우선)**" | manual 경로가 **discovery에 참여하지 않는 별도 channel**이고(`Server/Provider/main.cpp:105-106`), 그 프로세스는 Redis store를 등록해 api channel을 **자동 연결한다**(`:90-95`). ⇒ **manual endpoint와 auto reconcile 루프를 동시에 가진 channel이 하나도 없다.** RM-A2 러너는 provider 하나만 띄워(`run_e2e.sh:375-382`) reconcile churn 자체가 안 난다. `rm_a2_manual_endpoint_scenario.hpp:17-20`은 value·rid·evidence만 본다 |
| **E2E-CP-26** | [config-1:234-235](../../common/e2e/config-1-location-messaging.ko.md)·[config-5:36-42](../../common/e2e/config-5-resilience-lifecycle.ko.md): 시나리오는 정확한 `ZLinkFrameworkErrorKind`(`RouteNotConnected`/`RequestTargetNotFound`/`RequestRejected`/`RequestFailed`) 또는 `TimeoutException`과 retriable 여부를 **이름으로** 단언한다 | consumer가 `error_type`을 내려 주는데(`Config 1 consumer_endpoints.hpp:214-215`·`Config 5 :301-302`) **`Client/Scenarios/` 전체에서 `error_type` grep 0건**이다. 대신 `status >= 400 \|\| body.find("failed")`(`rl_b1:15-16`), `std::exception` 카운트(`rl_b6:29-31`), bool(`rl_d4:24`)을 본다. **RM-C8**은 초과 payload 거절을 `oversized.failed`로만 보는데(`:44`) handler 자체 1500ms timeout(`consumer_endpoints.hpp:208`)이 **똑같이 통과**시킨다 ⇒ 한도 초과와 timeout이 **구분되지 않는다.** feature-map도 이걸 인정한다(`RegistryMessaging/feature-map.ko.md:21`) |
| **E2E-CP-27** | [config-4:33-34,52-80](../../common/e2e/config-4-registration-codec.ko.md): config의 논지 자체가 "**등록 방식이 달라도 같은 reply·evidence가 나오는가**"다. `RC-A1`=자동 스캔, `RC-A2`=attribute, `RC-A3`=수동(대조군) | `Server/Support/server_host.hpp:182-188` — **셋을 완전히 같은 호출로 등록한다**(`handlers.group(...).add<auto_request_handler_t>().add<attribute_request_handler_t>().add<manual_channel_request_handler_t>()…`). 세 handler는 **packet 타입만 다르다**(`Server/Handlers/registration_handlers.hpp:11,47,88`). attribute 쌍이 든 `topic_name`(`:53,75`)은 SPOT topic 필드라 client-server channel에서 **읽히지도 않는다.** ⇒ C++이 자동 스캔 축에서 면제인 건 맞지만, 그 면제가 **변주 축을 0개로** 만들었다. feature-map(`:12-13`)은 여전히 A1/A2를 별개 표면으로 `구현` 처리한다 |
| **E2E-CP-28** | [config-4:162](../../common/e2e/config-4-registration-codec.ko.md): `RC-B5` — "JSON fallback 또는 **정해진 public decode error**로 끝나며, **어느 쪽이든 결과가 관측으로 고정된다**" | mismatched peer 자체는 **진짜다**(`Server/JsonOnlyPeer/main.cpp:17` + `server_host.hpp:149-152`가 protobuf/msgpack serializer를 건너뛴다) — 위조가 아니다. 그런데 `Server/Endpoints/operational_endpoints.hpp:296-304`가 **"성공하지 않았다"만 단언한다.** 5초 timeout이든 dial 실패든 **뭐든 통과**한다. error kind도, json-only peer 쪽 `payload_decode_failed` marker도, content-type도 없다. client 파일은 **server가 계산한 bool**을 읽을 뿐이다(`codec_mismatch_scenario.hpp:13-16`). 게다가 feature-map(`:22`)은 "**JSON fallback 규칙으로 처리**"라고 적는데 **코드는 정반대(거절)를 단언한다** |
| **E2E-CP-29** | [config-4:152](../../common/e2e/config-4-registration-codec.ko.md): `RC-B4`(P0) — "**미지원 타입은 JSON fallback**이라는 정해진 규칙을 따른다" | `rc_b4_codec_coexistence_scenario.hpp:15-25` — json/protobuf/msgpack content-type과 `custom` 값을 본다. 그런데 **`custom_roundtrip_req_t`는 미지원 타입이 아니다** — serializer가 명시 등록돼 있다(`server_host.hpp:64-99`). **등록된 codec 어디에도 안 맞는 메시지를 한 번도 보내지 않고**, `custom` reply의 content-type은 아예 단언하지 않는다 ⇒ **fallback 규칙이 검증되지 않는다** |
| **E2E-CP-30** | [§2.7](../../common/e2e/README.ko.md): 개별 config runner는 **단일 시나리오 실행을 지원한다**(`./run_e2e.sh RC-A6`) | `RegistrationCodec/run_e2e.sh:166`의 glob `rc-a*`가 **`rc-a6`도 잡아** client를 `ZLINK_CPP_E2E_SCENARIO=rc-a6`로 띄운다. 그런데 `Client/main.cpp`에 **`rc-a6` branch가 0건**이라 `"unknown RegistrationCodec scenario"`를 던지고 exit 1 → `set -euo pipefail`이 **RC-A6 블록(`:184-189`)에 닿기도 전에 스크립트를 중단**시킨다. ⇒ **`./run_e2e.sh RC-A6`는 항상 실패한다.** `all`로만 돈다 |
| **E2E-CP-31** | [§2.5](../../common/e2e/README.ko.md): 시나리오 ID 하나 = client scenario 파일 하나 | `rl_d1_high_fanout_scenario.hpp:14`의 `run_resilience_stress_scenario()`가 **`Client/main.cpp`에서 참조되지 않는다**(branch 없음). 그러면서 40회 순차 루프 하나로 `"scenario RL-D1 passed"`·`"RL-D4 passed"`·`"RL-D5 passed"`를 **한꺼번에 출력**한다(`:24-26`). 진짜 RL-D1은 `run_e2e.sh:1117-1118`의 120회 curl burst다. `rl_c2_topology_recovery_scenario.hpp:14`도 마찬가지로 dead code이며(`run_quick_resilience_scenario()`를 부를 뿐) RL-C2 검증은 shell(`run_e2e.sh:935-950`)에 있다. `.NET`엔 진짜 `RlC2TopologyRecoveryScenario.cs`(83줄, crash → `topology/wait Ready 0` → api-a만 단언 → restart → 복구)가 있다 |
| **E2E-CP-32** | [config-5:121](../../common/e2e/config-5-resilience-lifecycle.ko.md): `RL-B2` — in-flight request가 provider crash 시 "**정해진 public error**로 끝난다" | `rl_b2_crash_during_inflight_scenario.hpp:99-101` — pending request를 **500ms per-request HTTP timeout**으로 쏜다. 서버가 일부러 느리게 만든 handler라 `response.has_value()`가 **무조건 false**다. 그래서 `ensure(!pending.get(), …)`(`:135-136`)는 **provider B를 죽이지 않아도 통과**한다. 셋업(in-flight 진입·`kill_pid`)은 옳은데 **단언이 실패할 수 없다** |
| **E2E-CP-33** | [config-5:244-262](../../common/e2e/config-5-resilience-lifecycle.ko.md): `RL-D4`는 **error-reply wire 직렬화**(`message-kind Error=5` vs `Response=2`, header `errorCode`/`errorMessage` round-trip, raw camelCase, `status` 필드 없음)를 본다. `RL-D5`는 "**단건 burst가 아닌 soak**" — 동시 N client·request+send 혼합·**수 분 지속**·latency drift 관찰 | `rl_d4_missing_request_handler_scenario.hpp:21-26` — `failure.failed == true`와 `handler_missing:reply_error` marker만 본다. **RL-D3의 중복**이다. `errorCode\|errorMessage\|Response=2\|Error=5` grep **0건** ⇒ **wire 호환을 소유한 config에서 wire 호환이 미검증**이다. `rl_d5_mixed_burst_scenario.hpp:23-36` — http client **하나로** 60회 순차 request + 60회 순차 command. 동시성도 지속도 latency 관찰도 없다. 파일 이름부터 `mixed_burst`다 |
| **E2E-CP-34** | [config-7:73-74](../../common/e2e/config-7-monitoring.ko.md): `MON-A2`(P0) — "service 노드(`svc-b`)를 **추가/종료**해 store의 peer location row를 바꾼다… 그 **payload가 실제 변화를 반영**한다" | `mon_a2_location_events_scenario.hpp:25-33` — client가 **trigger를 아무것도 하지 않는다.** `svc-a` evidence를 폴링해 `TopologyChanged`(`topology != 0`)와 `ServiceSummaryChanged`(`summary != 0`)가 한 줄씩 있는지만 본다. 러너도 mon-a2 실행 전후로 **`svc-b`를 띄우거나 죽이지 않는다**(`run_e2e.sh:211-227`). 그런데 C++ monitoring runtime은 **diff 없이 매 tick 세 이벤트를 무조건 발행**한다([IMP-CP-13](#라운드-2-2026-07-14--stage--관측--connector--http-client)) ⇒ **peer location이 하나도 안 바뀌어도 통과하는 P0**다. 즉 **e2e가 IMP-CP-13을 잡기는커녕 그것 덕분에 통과한다** |
| **E2E-CP-35** | [config-7:64,93-94,149-150](../../common/e2e/config-7-monitoring.ko.md): `MON-A1`은 payload에 `RemoteAddr`와 **있으면 `RoutingId`**를 포함한다. `MON-A4`는 **같은 rid·다른 endpoint로 provider 교체 → socket `Disconnected`/`Connected`/`ConnectionReady`** + drain/restore → `PeerAdmissionChanged`. `MON-D1`은 crash+restart를 **여러 번** 흔들며 **각 down/up 전이**를 관측한다 | `record_socket_event`(`service_event_recorders.hpp:65-71`)가 `source\|kind\|remote`만 남겨 **`RoutingId`를 아예 기록하지 않는다** ⇒ 관측 자체가 불가능. `record_location_event`(`:86-96`)도 `topology=<size>` **크기만** 남겨 "payload가 실제 변화를 반영"을 **원리적으로 단언할 수 없다.** `MON-A4`는 failover 절반(rid 교체·endpoint 변경·`Disconnected`/`Connected`)이 **통째로 없고** drain/restore만 한다(`mon_a4:35-57`). `MON-D1`은 graceful `/shutdown` **1회**뿐이고(`run_e2e.sh:230-247`) 단언은 `TopologyChanged` **카운트가 1 이상 늘었나**인데(`mon_d1:18-40`) 매 tick 발행이라 **1초면 무조건 충족**된다 ⇒ **전이가 아니라 카운터를 센다** |
| **E2E-CP-36** | [config-7:39](../../common/e2e/config-7-monitoring.ko.md): `TimerHandlerFailed`·`TimerStoppedAfterUnhandledException`은 **spot source**의 event다(`add_spot_events`가 등록한다) | `Server/Service/Support/service_host.hpp:66`이 **`add_spot_timer_events(...)`**를 등록한다 — [IMP-CP-15](#라운드-2-2026-07-14--stage--관측--connector--http-client)가 "C++ 전용"이라고 지목한 그 API다. `MON-A3`의 `timer=failing`(`:43`)과 `MON-A5`의 `timer=stopping`(`:52`)은 **그 비-스펙 source를 e2e가 따로 켜 줬기 때문에만** 통과한다. ⇒ **스펙대로 `add_spot_events`만 등록한 앱은 아무 이벤트도 못 받는데, e2e는 IMP-CP-15를 드러내기는커녕 우회해서 가린다** |
| **E2E-CP-37** | [config-6:61](../../common/e2e/config-6-store-failure-recovery.ko.md): "장애 시나리오는 harness가 Redis process를 **정지(stop)**했다가 **재기동(restart)**한다" | `Client/main.cpp:86,96,99,109,117,120,161,163,174,178,202,213,216` — **전부 `docker pause`/`unpause`뿐이다.** `docker stop`·`restart`는 **0건**. SIGSTOP은 **TCP 연결과 데이터셋을 그대로 보존**하므로 store가 **"재연결됐거나 비어 있는" 상태로 돌아오는 일이 없다** — 그런데 그게 [IMP-CP-06](#라운드-2-2026-07-14--stage--관측--connector--http-client)("Redis가 빈 데이터로 재시작하면 다음 100ms tick에 mesh 연결을 전부 끊는다")이 터지는 **바로 그 조건**이다. ⇒ **config 전체에서 Redis를 한 번도 재시작하지 않아, 이 config가 존재하는 이유인 결함이 원리적으로 재현되지 않는다** |
| **E2E-CP-38** | [config-6:116](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-B2` — "**새 outbound connect는 중단된다** — 장애 중 재시작한 provider가 있어도 store 복구 전에는 연결 대상에 추가되지 않음" | e2e가 `locations.store_failure_grace`를 세팅하는데(`Server/Shared/location_store.hpp:292-293`), **리포 전체(build 제외)에서 이 이름의 등장은 딱 둘** — 필드 선언(`framework/include/.../contracts/locations/options.hpp:17`)과 **이 세팅 한 줄**이다. **읽는 곳이 0개**(IMP-CP-06). 그리고 시나리오(`Client/main.cpp:107-126`)가 단언하는 건 **`SF-B1`과 똑같은 둘** — `grace + 2×heartbeat` 동안 요청이 계속 성공하고, status가 unhealthy라는 것. **문서가 요구한 "장애 중 provider 재시작"을 코드가 하지 않는다**(프로세스를 하나도 안 띄운다). ⇒ **`store_failure_grace`가 어떤 값이든, 심지어 미구현이어도 `SF-B2`는 통과한다** |
| **E2E-CP-39** | [40 §1·§5.1](../server/40-location-runtime.ko.md): **store는 저장만 하고, owner lease join은 framework runtime의 책임**이다([IMP-CP-35](#상세-1)) | Config 6에 등록된 유일한 store가 **공식 Redis 확장**이다(`Server/Shared/location_store.hpp:275-284`; consumer의 `delayable_location_store_t`는 pass-through, `:74-81`). 죽은 owner row는 **store 안에서** 걸러진다(`extensions/framework-locations-redis/.../redis.hpp:1411-1425`의 `owner_is_live()` → lease key에 `PTTL`). ⇒ `SF-C1`의 "lease TTL 경과 후 성공 결과에서 제외"(config-6:128)는 **Redis 확장이 거른다는 것만 증명**하고 framework runtime에 대해선 아무 말도 안 한다. **스펙대로("store는 저장만") 쓴 사용자 store는 계약을 위반하는데 이 e2e는 그대로 초록이다.** `SF-C2`·`SF-D2`도 같은 구조다 |
| **E2E-CP-40** | [config-6:157,164-168](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-D1`(P0)은 "각 provider evidence에 **불필요한 disconnect/reconnect marker가 없음**" + "request는 **전 구간에서** 성공"을 요구하고, `SF-D2`(P0)는 **재등록 → heartbeat 1회 유예 → diff** 순서와 "live link를 끊지 않음"을 요구한다 | `Client/main.cpp:159-170`(D1)·`:172-194`(D2) — **둘 다 store가 healthy로 복귀한 *뒤에야* 요청을 보낸다. 장애·복구 구간에 흐르는 트래픽이 0이다.** grace를 단언하는 곳도, disconnect evidence를 조회하는 곳도 없다. 게다가 provider evidence store는 **처리한 profile 요청만 기록**해(`DiscoveryRegistryHa/Server/Provider/Infrastructure/provider_evidence_store.hpp:22-26`, 유일한 writer가 `DiscoveryRegistryHa/Server/Provider/Handlers/provider_handlers.hpp:30`) **connection/disconnect marker라는 표면이 config에 아예 없다** ⇒ 문서가 요구한 negative를 **단언할 대상조차 없다.** `.NET`은 pause **전에** 동시 트래픽 루프를 띄우고 max-success-gap을 단언한다(`SfD2LongOutageRecoveryScenario.cs:19-23,41-44,110-117`) |
| **E2E-CP-41** | [config-6:106,157](../../common/e2e/config-6-store-failure-recovery.ko.md): "**기존 연결이 유지된다**"(fail-static), "request는 **전 구간에서** 성공한다" | `Server/Consumer/Endpoints/consumer_endpoints.hpp:18-37`의 `request_profile_with_retry()` — **30초 deadline · attempt당 5초 timeout · 100ms backoff · 모든 오류를 삼킨다.** ⇒ `SF-B1`·`SF-C1`·`SF-D1`·`SF-D2`의 "요청 성공" 단언이 전부 **"client의 10초 HTTP timeout(`Client/Support/client_support.hpp:181`) 안에 라우팅이 복구됐다"로 전락**한다. **fail-static과 recovery grace가 막으라고 존재하는 바로 그 disconnect→reconnect storm이 조용히 통과한다.** E2E-CP-21과 같은 계열이지만 다른 config·파일·시나리오다 |
| **E2E-CP-42** | [config-6:93-94](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-A2` — "provider 하나를 **추가로 띄웠다가** 정상 종료한다", "추가 후 **polling interval 몇 tick 안에 새 provider가 routing 대상이 된다**", "watch를 지원하는 배포와 결과 의미가 같다" | `watch_enabled`가 framework에서 **하드코딩 `false`**다(`src/runtime/locations/store_location_resolvers.hpp:224`) ⇒ `ensure(!status.watch_enabled)`(`Client/main.cpp:68`)는 **절대 실패할 수 없는 항진명제**이고, 비교 대상인 watch-capable 배포가 C++에 **존재하지 않아** 문서의 "결과 의미가 같다"는 **검증 불가능**하다. 그리고 코드는 **provider를 한 번도 추가로 띄우지 않고** 기존 `api-b`를 종료할 뿐이라 ⇒ **`SF-A2`가 `SF-C2`의 사실상 복제본**이고, "몇 tick 안에 routing 대상이 된다"는 **어디서도 단언되지 않는다.** `.NET`엔 전용 `PollingOnlyLocationStore.cs`·별도 consumer 프로세스·`StartProviderCAsync()`가 있다(`SfA2PollingFallbackScenario.cs:15-36`) |
| **E2E-CP-43** | [config-6:138-144](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-C2` — `Draining=true` 게시, drain 중 신규 배정 없음, 30초 drain deadline 안 종료, **row/lease가 lease 만료를 기다리지 않고 제거**된다. "**SF-C1과 달리 강제 종료나 lease 만료만으로 통과시키지 않는다**" | `Client/main.cpp:144-157` — `/shutdown`을 POST하는데 그건 **50ms 뒤 SIGTERM을 올릴 뿐**이다(`DiscoveryRegistryHa/Server/Provider/Handlers/provider_handlers.hpp:124` — **drain 요청 표면이 없다**). 그리고 row가 사라지길 기다리는 **timeout이 `options.lease_ttl`**이다(`:150`) ⇒ **순전히 lease 만료로 사라진 row도 통과한다 — 문서가 이름을 짚어 금지한 바로 그것.** `peer_location_t.draining`은 **public contract에 있는데**(`framework/include/.../contracts/locations/rows.hpp:36`) e2e의 peer projection이 **그 필드를 버린다**(`Server/Consumer/Endpoints/consumer_endpoints.hpp:154-156` — rid/endpoint/owner_id만). `.NET`은 `row.Draining`과 drain 중 lease-healthy를 단언한다(`SfC2GracefulRemovalScenario.cs:24-35`) |
| **E2E-CP-44** | [config-6:177](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-D3` — "healthy → **unhealthy + last error + lease 갱신 실패** → healthy + **last refresh**" 전이를 관측한다 | `src/runtime/locations/store_location_resolvers.hpp:221-241` — `get_status()`가 `list_owner_leases()`를 **즉석 probe로 던지고**, 성공하면 `store_healthy = true`**와 동시에** `last_refresh_at = now()`를 찍고(`:231-232`), 실패하면 `store_healthy = false`**와 동시에** `owner_lease_healthy = false`를 **강제로 덮어쓴다**(`:236-237`) — runtime이 `:226`에서 읽어 온 진짜 flag(`location_runtime.hpp:87-90`)를 **버린다.** ⇒ `has_last_refresh_at`(`Client/main.cpp:58,220`)은 **`store_healthy`와 문자 그대로 같은 bit**이고(refresh 시각이 전진하는지는 아무도 안 본다), 장애 중 `!owner_lease_healthy`(`:207`)는 **probe가 실패했다는 것만** 증명한다. **세 필드의 전이가 flag 하나가 두 번 뒤집히는 것으로 붕괴한다** |
| **E2E-CP-45** | [config-6:189-190,195-199](../../common/e2e/config-6-store-failure-recovery.ko.md): harness가 **Redis 응답을 지연**시킨다(proxy 또는 느린 Lua script). 검증 대상은 **store client가 진짜 non-blocking I/O를 하고 core I/O 스레드를 점유하지 않는가** | `Server/Shared/location_store.hpp:250-258` — 앱 쪽 데코레이터가 `std::this_thread::sleep_for(delay)`를 하고 **`.result()`로 블로킹**한다. consumer에만 물려 있고(`Server/Consumer/main.cpp:24-33`) **Redis는 손도 안 댄다.** ⇒ `ensure(delayed_store_read_ms >= delay_ms * 0.75)`(`Client/main.cpp:291`)는 **e2e 자기 `sleep_for`가 돌았다는 것만** 증명하고, 문서의 진짜 주장은 **측정되지 않는다** |
| **E2E-CP-46** | [config-3:48-49](../../common/e2e/config-3-pubsub.ko.md): warm-up은 **각 subscriber가 첫 event를 받을 때까지** 재발행한다(구독 준비 barrier). 오라클은 **공통 연속 구간(common contiguous sequence)이지 "N개 무손실"이 아니다** — `Publish(...).Async()`가 원격 수신을 보장하지 않기 때문이다 | `Client/Scenarios/fanout_basic_delivery_scenario.hpp:13-16` — warm-up 5개를 쏘고 **500ms 잔다. 수신을 관측하는 코드가 없다**(barrier 부재). 그리고 `run_e2e.sh:409-411`은 **세 subscriber 전부에서 `measure-*` 20개 전량**을 요구한다 ⇒ **계약이 허용하는 것보다 강한 오라클**이고, 관측되지 않은 barrier 위에 얹혀 있어 **설계상 flaky**하다. "같은 순서로 수신"도 **검사되지 않는다** — `contains_all_line_groups`가 집합 포함이다(`evidence_store.hpp:171-175,133-149`). 같은 무손실-전량 오라클이 `PS-B2`(23/23, `run_e2e.sh:448-451`)·`PS-A3`·`PS-A4`에 **재사용**된다 |
| **E2E-CP-47** | [config-3:88-92](../../common/e2e/config-3-pubsub.ko.md): `PS-B1` — 느린 subscriber가 **막혀 있는 동안** 빠른 subscriber들이 **계속 받는다**(fanout 격리) | 지연 주입은 진짜다(`run_e2e.sh:559` → `Server/Subscriber/Handlers/event_msg_handler.hpp:23-25`의 250ms). 그런데 오라클(`run_e2e.sh:444-447`)이 요구하는 건 sub-2/sub-3가 **결국** 16줄을 다 갖는 것뿐이고, bounded wait 기본값이 **10,000ms**다(`:387`). **느린 peer 뒤로 완전히 직렬화돼도 16 × 250ms = 4초**라 창 안에 넉넉히 들어온다. ⇒ **시간 제한이 없어 "격리"와 "head-of-line 블로킹"을 구분하지 못한다.** publisher가 막히든 말든 통과한다 |
| **E2E-CP-48** | [config-3:113](../../common/e2e/config-3-pubsub.ko.md): `PS-C1`(P0) — subscriber 쪽 drop marker에 더해 **publisher 쪽엔 dispatch marker가 없어야 한다**(`Publish(...).Async()`는 submit-only) | positive 절반은 튼튼하다(subscriber의 `reason=handlerMissing`/`action=drop`/`packet=MissingEventMsg`, `run_e2e.sh:397-399,452-455`, 실제 dispatch observer가 채운다 — `Server/Subscriber/main.cpp:29-32`). 그런데 **publisher evidence는 스냅샷만 뜨고(`run_e2e.sh:608`) 단언에 쓰이지 않는다** ⇒ 문서가 요구한 negative가 미검증 |

| **E2E-CP-49** | [config-10:277-286](../../common/e2e/config-10-spot-actor-transfer.ko.md): `ST-E2`(P0) = "**실패한** transfer는 bound session route를 바꾸지 않음". bind → transfer 시작 → **source-down-before-commit 또는 adapter 실패 주입** → target bound-session route가 안 생겼음을 확인 | `SpotActorTransfer/Client/main.cpp:703-731`의 `bound_session_rebind_isolation()` — **성공한 transfer를 돌린다**(`require(join.accepted)`, `:716`). 그리고 node B에 **새 bound session을 열어**(`:718`) 옛 세션이 push를 안 받는지 500ms negative로 본다(`:728-730`). **실패를 어디에도 주입하지 않는다.** ⇒ "실패한 transfer의 bound session 비오염"이라는 **P0 계약이 통째로 미검증**이다. E2E-CP-17(SM-F5)과 **같은 종류 — 시나리오가 자기 계약의 반대를 돈다** |
| **E2E-CP-50** | [config-10:45-46,305,318-320,362-366](../../common/e2e/config-10-spot-actor-transfer.ko.md): marker 집합을 고정하고, `ST-F1`·`ST-F2`("**이 순서가 뒤집히면 실패다**")·`ST-F5`가 그걸 **합격 기준**으로 삼는다 | `run_e2e.sh:196-201` — `require_runtime_marker()`가 marker가 없으면 **`Note: ... (timing-dependent)`를 찍고 `return 0`한다. 실행을 실패시킬 수 없다.** 앞의 주석(`:190-195`)이 그걸 **대놓고 정당화한다.** client는 넷 중 아무것도 단언하지 않는다 — `handoff_backlog`·`backlog_enqueued`·`mapping_evicted`·`stale_fail_fast`가 `Client/main.cpp`에 **0건**. ⇒ **문서가 "뒤집히면 실패"라고 못박은 순서 계약이 경고 한 줄로 강등됐다** |
| **E2E-CP-51** | [config-10:43-44](../../common/e2e/config-10-spot-actor-transfer.ko.md): order marker 9종을 고정하고 `ST-A1`(:83)·`ST-B1`(:130-131)·`ST-B3`(:160-161)이 그 순서를 단언한다 | `location_committed`는 join 완료 직후 기록되고 ST-A1이 순서를 단언한다. 그러나 `Server/ActorNode/main.cpp`에는 여전히 **`commit_ack` 0건, `source_cleanup` 0건**이고, ST-B1·ST-B3는 9개 marker의 엄격한 부분집합만 기다린다. `commit_request`는 join 호출 전에 기록되지만 remote commit ack와 source cleanup 경계가 없으므로 전체 transfer 순서는 아직 검증할 수 없다. |
| **E2E-CP-52** | config-10 각 시나리오의 절차·검증 | **`ST-D2`**(P1, `:251-254` — cleanup retry를 **지연시켰다가 실행**): `Client/main.cpp:654-675`가 평범한 transfer + `sleep 2s` + 재조회일 뿐 — **지연도 트리거도 주입도 없다.** source-cleanup 경로가 아예 없는 framework에서도 통과한다. 그걸 구현할 수 있는 유일한 서버 표면(`transfer_gate_store_t` + `/transfer-gates/{actorId}/release`, `Server/ActorNode/main.cpp:180-189,698-713`)은 **호출부가 0건**이다. **`ST-B2`**(P0, `:143-146` — `commit_ack`와 `source_cleanup` **사이에서** source를 붙잡았다 죽인다): gate를 안 걸고 transfer를 끝낸 뒤 `shutdown_node()`를 부른다(`:466-492`) — **`ST-C2`와 같은 모양**이고 target generation도 안 본다. **`ST-C1`**(P0, `:196` — target이 **pending admission timeout cleanup evidence를 남긴다**): 서버에 `pending_admission` **0건**, negative만 단언한다(`:548-586`). **`ST-C3`**(P1, `:225`): `require_no_contains(..., "packet_handler|after-joined-failure")`(`:1003-1005`)인데 **그 marker를 실은 packet을 아무도 안 보낸다** — 어떤 동작에서도 존재할 수 없다. **`ST-F5`**(P1, `:358-366`): `mapping_evicted`를 단언하지 않고(client 0건) entry snapshot도 안 읽으며 `sleep 3300ms` 뒤 stale probe로 **추론**한다(`:816-844`) |
| **E2E-CP-53** | [config-10:315-320](../../common/e2e/config-10-spot-actor-transfer.ko.md): `ST-F2`(P0)는 `D1`을 "transfer commit과 location publish가 끝난 **직후**" 보내 **backlog 뒤에 떨어지는지** 본다 | `Client/main.cpp:774-778` — gate를 풀고 **`join_task.get()`으로 블록한 뒤** `get_actor_ref(...)` HTTP 왕복까지 하고 나서 `D1`을 보낸다. **backlog는 이미 오래전에 비었다.** `B1→B2→D1` 순서는 publish-before-replay가 아니라 **벽시계 간격**으로 충족된다. 문서의 진짜 판별자(`backlog_enqueued`가 `location_committed`보다 먼저)는 **발행되지도 단언되지도 않는다.** `ST-F3`도 같은 모양이다(S3/S4를 `join_task.get()` 뒤에 보낸다, `:797-800`) ⇒ rebind 경계를 **한 번도 경합시키지 않는다** |
| **E2E-CP-54** | [config-10:344-349](../../common/e2e/config-10-spot-actor-transfer.ko.md): `G1`·`G2`를 **둘 다 packet으로** 옛 ref에 보낸다. `G1`은 `straggler_forward`로 도착, `G2`는 `stale_fail_fast`. "framework가 `G2`를 자동 저장·재전송한 evidence가 있으면 **실패다**" | `Server/ActorNode/main.cpp:935` — `G1`은 `send_to_actor`, `:904` — `G2`는 **`request_to_actor`**. **kind가 다르다.** `run_e2e.sh:207-212`가 이유를 적어 놨다 — "cpp handles stragglers client-side — sends **re-resolve**". 그 re-resolve가 `actor_client.cpp:103-121`(stale → `resolve_actor` → 재-`submit_send`)다. ⇒ **옛 ref에 `send`를 하면 조용히 re-resolve돼 배달된다 — 문서가 "실패"라고 부른 바로 그 동작인데, e2e가 `G2`를 request로 보내서 관측하지 않는다** |
| **E2E-CP-55** | [config-10:237-241](../../common/e2e/config-10-spot-actor-transfer.ko.md): `ST-D1`(P0)은 commit 전후로 location이 **바뀌지 않다가 바뀜**을 보고, "pending 상태가 evidence에 표시"되며 지연 중 **actor packet route**를 관측한다 | `Client/main.cpp:1028` — `during.generation == before.generation`, `:1035` — 해제 뒤 **`after.generation >= before.generation`**. local join은 node rid가 안 바뀌어 **generation이 유일한 신호인데 `>=`는 "변하지 않음"으로도 충족된다** ⇒ **commit 후 단언이 항진명제**다. pending evidence도, 지연 중 packet route 관측도 **미구현**이다. (remote 절반은 진짜다 — `get_actor_ref`가 public `actor_directory_t::find`를 지나므로 `:1059-1070`의 node-rid 검사는 문다) |
| **E2E-CP-56** | [config-10:35-38](../../common/e2e/config-10-spot-actor-transfer.ko.md): actor 노드 2 + **session gateway 2** + **transfer controller 1** | `run_e2e.sh:148-150` — **actor 노드 3개**를 띄우고 **session gateway도 transfer controller도 없다.** actor 노드가 HTTP 제어 endpoint와 stream endpoint를 **자기가 겸한다** ⇒ E2E-CP-02·03의 Config 10판이다 |
| **E2E-CP-57** | [config-10:395-396](../../common/e2e/config-10-spot-actor-transfer.ko.md): "callback order는 **단순 로그 문자열 grep이 아니라** 역할 server evidence와 message flow correlation id로 검증한다" | Track F의 framework marker 4종이 `emit_actor_handoff_marker`(`framework/src/runtime/spots/spot_runtime.cpp:2142-2153`)에서 나오는데, **stderr로 `fprintf`**하고 **`getenv("ZLINK_FRAMEWORK_CPP_ACTOR_HANDOFF_MARKERS")`로 게이트**된다(`:2133-2140`). `run_e2e.sh:97`이 그걸 export한다. ⇒ 문서가 **이름 짚어 금지한 로그 grep**이고, **SMP-CP-26과 같은 패턴**(runner가 framework 동작 knob을 export하고 runtime이 `getenv`로 읽는다)의 e2e판이다 |
| **E2E-CP-58** | [config-9:118,148-149](../../common/e2e/config-9-to-actor-messaging.ko.md): caller의 **to-actor 호출이 낸 public error kind**를 단언한다. 그리고 "**reply가 없는 send의 submit은 원격 actor의 존재 여부를 확인하는 수단으로 사용하지 않는다**" | `Server/Caller/main.cpp:127-132`(send)·`:166-171`(request) — **먼저 `_directory.find(actor_id)`를 하고 `nullopt`이면 자기가 `framework_exception_t(actor_route_not_found)`를 던진다.** framework의 분류 경로(`framework/src/runtime/actors/actor_client.cpp:210-214`)에 **도달하지 않는다.** `Client/main.cpp:263-266`은 caller가 하드코딩한 그 문자열을 단언한다 ⇒ **framework 분류를 통째로 지워도 통과한다.** 게다가 `:263-264`가 **send로** 실패 kind를 단언한다 — **문서가 이름 짚어 금지한 것**이고, 위의 precheck 덕에만 성립한다. 덤으로 `Caller/main.cpp:133-134`가 `send_to_actor(...).submit()`(void)이라 **send 결과가 client에 도달할 수 없다** — 모든 `"sent"`는 "directory.find가 성공했다"는 뜻일 뿐이다. ⇒ **config-9:23-24의 핵심 계약("send 완료 = actor owner의 로컬 mailbox 인계 성공")이 모든 TA 시나리오에서 미관측**이다 |
| **E2E-CP-59** | [config-9:34-35,127-128,137-138](../../common/e2e/config-9-to-actor-messaging.ko.md): "actor location row와 owner lease는 **framework lifecycle이 관리한다**". `TA-B2`는 "**actor owner를 교체하거나** generation을 바꾸어" stale을 만든다. `TA-B3`는 **live actor의 route를 끊었다가 복구 뒤 같은 ref로 다시** request한다. 서버는 **actor 노드 2개** | `Server/Caller/main.cpp:40-84`의 `write_fault_actor_row()` — 앱 코드가 `store->renew_owner_lease(...)`와 `store->update_actor(..., takeover)`를 **Redis에 직접 쓴다**(config-9:14가 내부 조작을 금지한다). `TA-B2`는 owner를 **옮기지 않고** generation **99**(`:69`)를 쓴다 — 문서가 말한 "이전 generation"이 아니라 **미래 generation**이다. actor 노드가 **`actor-a` 하나뿐**이라(`run_e2e.sh:60`) **owner 교체가 구성 불가능**하고, B2의 evidence 요구 2개(old/new owner 대조, re-resolve → 별도 성공 경로)가 **둘 다 없다.** `TA-B3`는 존재한 적 없는 **`ghost-node`/`ghost-spot`**을 row에 쓸 뿐(`:46-50`) **route를 끊지도 복구하지도 않는다** — 유일한 route가 정적 `connect_router`다(`:238-241`). "복구" 단계(`Client/main.cpp:280-282`)는 **다른 actor id**(`ta-b3` vs `ta-b3-disconnected`)를 부른다 ⇒ 실제로 검증되는 건 "모르는 node rid → route_not_connected"라는 **다른 메커니즘**이다 |
| **E2E-CP-60** | [config-9:118,148-149](../../common/e2e/config-9-to-actor-messaging.ko.md): "send 뒤 actor 노드에는 해당 actor id의 handler evidence가 **없고** actor location row도 새로 만들어지지 않는다", "public error kind **와 역할 서버 evidence를 함께** 확인한다" | `Client/main.cpp`의 유일한 evidence helper가 `require_evidence`(**positive 전용**, `:162-172`)이고 `/evidence` 조회(`:285`)는 TA-A1/A2/A3/A4에서만 쓴다. **location row를 조회하는 코드가 config 전체에 0건.** ⇒ Track B **세 시나리오 전부** 문서 요구의 절반만 구현했다 |
| **E2E-CP-61** | [config-11:177-179](../../common/e2e/config-11-observability-ops.ko.md): `OBS-C1`(P0) — typed `Draining=true` row를 게시하되 "**그러나 peer row는 삭제되지 않아** 기존 연결이 유지되고", 전파 지연 창에 기존 연결로 온 request가 **오류율 0**으로 처리되며, owner lease는 draining 동안 **계속 갱신된다** | `run_e2e.sh:303-309` — `cleaned_up = not play_b_rows` 뒤 **`assert draining_rows or cleaned_up`**. ⇒ **"peer row가 아예 없음"이 통과 조건으로 받아들여진다 — 문서가 금지한 바로 그것이다.** 앞 주석(`:300-302`)이 그걸 합리화한다. 그리고 create-rejection(`:314-322`)은 `if curl -fsS ...; then <assert>; fi` 뒤에 **무조건 `echo "OBS-C1 create-rejection PASS"`**를 찍는다 ⇒ **non-2xx·connection-refused·timeout이 전부 assert를 건너뛰고 PASS를 출력한다.** `zlink.drain.state` gauge 전이(runner에 `drain.state` **0건**), 전파 창의 오류율 0, draining 중 lease 갱신 — **셋 다 미단언**이다 |
| **E2E-CP-62** | [config-11:81-83,93,103,113](../../common/e2e/config-11-observability-ops.ko.md): `OBS-A1`은 connector 발원 flow가 **시간순으로** STREAM→**actor relay**→spot까지 잇는지, `OBS-A2`는 "**`grep flow=<id>`로 성공 라인과 실패 라인이 함께 잡힌다**", `OBS-A3`은 off 노드 **이후 노드에서 같은 flow가 다시 나타난다**, `OBS-A4`는 **구독자 N개** 라인이 같은 flow_id | **A1**(`:148-171`): 두 로그의 id 집합이 **교차하는지**만 본다. 서버가 새로 만든 flow도 `origin=application`이라 **"connector가 생성"이 구분되지 않고**, actor-relay hop을 거르지도 시간순을 보지도 않는다. **A2**(`:176-186`): error 라인에 flow id가 **있다**까지만 보고 **같은 id가 성공 라인에도 나오는지를 안 본다** — 그게 요구의 전부인데. 게다가 **`outcome=error`로 매칭한다**(`:180`) — [IMP-CP-18](#라운드-2-2026-07-14--stage--관측--connector--http-client)이 "타 언어는 `phase=`인데 C++만 `outcome=`"이라 지목한 그 토큰이다 ⇒ **e2e가 그 결함을 못박는다.** **A3**(`:536-547`): play-a가 아무것도 안 남기므로 **대조할 상류 id가 없다.** create-if-absent 하에선 flow를 못 받은 하류 노드도 자기 `origin=application` flow를 만들므로 **전파가 완전히 깨져도 같은 evidence가 나온다.** **A4**(`:257-258`): `origin=timer`가 **로그 아무 데나** 있으면 통과 — flow id에 묶이지도, action flow와 구분되지도 않는다. 구독 룸을 **하나만** 만들어(`:232-233`) "N 구독자"와 owner-skip 라인이 미검증 |
| **E2E-CP-63** | [config-11:136-138,151,164,202-203](../../common/e2e/config-11-observability-ops.ko.md) | **`OBS-B2`**(P0): "룸에 **부하를 주고**", `actor.transfers`가 이동 완료 **1회당 1회**, `pending_requests.count`가 transfer당 **한 번**. → 부하를 **안 준다**(`:215-220`은 계기 존재·`kind`·합계 0만 본다). transfer 계기는 `sum(...) >= 1`(`:429-436`) — **"최소 1회"지 "이동당 1회"가 아니다.** `transfer.duration`의 구간과 `pending_requests`의 **값**은 아무것과도 비교되지 않는다. **`OBS-B3`**(P1): `fanout.published`/`received`가 **1:N로 계수**되는지 — `:263-275`가 둘의 **존재와 topic만** 보고 **값을 비교하지 않는다.** feature-map은 "**1:N 실측**"이라 적는다. **`OBS-B4`**(P1): metrics **off**로 띄운 노드에서 `body["metrics"] == []`를 단언한다(`:550-554`) — metrics off면 e2e 자기 collector가 **애초에 등록되지 않는다**(`Server/main.cpp:492-493`) ⇒ **설치한 적 없는 collector가 아무것도 못 모았다는 항진명제**이고, framework 내부 적재에 대해선 아무 말도 안 한다. **`OBS-C3`**(P0): (a) `drain-natural`과 (b) `release-and-recreate` 둘인데 **(b)만 돈다**(`:500-524`). (b)의 재생성 검사도 `state in ("created","existing")`(`:521`)로 **평범한 create(`:139`)와 똑같은 단언**이라 "row가 해제됐다 재구성됐다"와 "애초에 해제 안 됐다"를 **구분하지 못한다.** C3는 feature-map의 pending 목록에도 **없다** |
| **E2E-CP-64** | [E2E README §3](../../common/e2e/README.ko.md): config는 11개다. [§2.3:258](../../common/e2e/README.ko.md): evidence endpoint는 **marker를 노출**한다(판정이 아니라). [§2.5:324-326](../../common/e2e/README.ko.md): client scenario가 **검증 흐름을 직접** 보여야 한다 | `e2e/DeliveryDispatch/`는 config 문서가 없을 뿐 아니라 **샘플의 갈라진 fork**다 — feature-map이 스스로 계약을 *샘플*이라 선언하고("기준 구현: `dotnet/samples/DeliveryDispatch`"), 역할 분리가 **더 넓으며**(e2e에만 `DispatchApi`·`DispatchCenter`·`CourierGateway`; 샘플은 `Dispatch` 하나) **`Shared/Contracts/messages.hpp`가 서로 다르다** ⇒ **같은 wire에 계약 헤더가 둘이고**, 통합 게이트는 **fork 쪽**을 돌린다(진짜 Config 10은 빼놓고 — E2E-CP-01). `DD-A4`는 client가 `/self-check/assert`를 POST하고 **서버가 bool 판정을 돌려준다**(`Client/delivery_dispatch_client_scenario.hpp:166` → `Server/DispatchApi/main.cpp:45-66`) — 판정 자체는 진짜지만(SMP-CP-01 같은 위조가 **아니다**) **evidence가 아니라 verdict를 노출**한다. 그리고 이 config가 **[IMP-CP-39](#라운드-4-2026-07-14--근거-없는-공개-표면-샘플e2e-감사에서-역으로-발굴)의 disown된 `actor_gateway_t`에 가장 크게 기댄다**(feature-map이 그렇게 적어 놨다) |

**Config 10·11에서 진짜인 것**: `ST-B4`(custom empty-state adapter + `domain_state_loaded`), `ST-F1`(유일하게
순서 helper `assert_evidence_order`를 쓰는 진짜 순서 검사), `ST-C2`, `ST-E1`, `ST-F6`의 correlation 절반.
`OBS-C4`(connector의 public close reason으로 `closeReason=server_drain` 확인)와 `OBS-C5`(a: `"force_stopping"
not in states`, b: `drain.forced{kind=actor|session}` 카운터)는 **깨끗하다**.

**Config 3에서 확인한 깨끗한 축**: 모든 PS 단언이 **subscriber 역할 서버의 bounded `/evidence/wait`**에
걸려 있다(shell 로그 grep이 아니다) — `run_e2e.sh:380-389`가 subscriber endpoint로 POST하고
`Server/Subscriber/Infrastructure/evidence_store.hpp:90-104`가 받는다. `wait()`가 **전체 스냅샷**을
돌려주므로(`:96,111-115`) negative 검사(`run_e2e.sh:404-406`)도 **공허하지 않다.** `PS-A3`의 late
subscriber는 **진짜로 발행이 시작된 뒤에** 붙고(`run_e2e.sh:522-523`이 client의 `READY` marker를
gate로 쓴다), `PS-A2`·`PS-A3`·`PS-A4`는 문서의 negative 단언을 갖고 있다.

### 이 라운드에서 확인한 깨끗한 축

**연결 축은 위반 0이다.** `enable_client(endpoint)`·`connect_router`·`connect_peer_pub`는
**TicTacToe에만** 있고(규약이 허용하는 유일한 샘플), 나머지 정본 샘플은 전부 인자 없는
`enable_client()` + location store 자동 연결이다. [§13.2](../90-implementation-gap.ko.md)의 판정과 일치한다.

**샘플 앱 코드의 환경변수 사용은 0건**이다(커밋 `b83807bed`가 config 파일로 전환). **단 runner 축은
아니다** — SMP-CP-26이 그걸 뒤집는다. runner 3개가 framework 동작 knob을 export하고 framework
runtime이 그걸 `getenv`로 읽는다. "앱 코드 0건"은 **앱 코드만** 본 판정이었다.

Redis 격리 자체(`docker create --tmpfs /data` → `start` → `inspect` → `rm -fv`, 자기 container id만
정리, 고정 host port 없음)는 6개 샘플 runner 전부 규약대로다. 통합 runner도 개별 runner를 호출만
하고 절차를 재구현하지 않는다. 다만 **순서**(SMP-CP-27)와 **retry 토큰**(SMP-CP-28)이 어긋난다.

**e2e client가 framework client API를 직접 쓰는 사례도 0건**이다(HTTP wrapper + stream connector만).
`Server/Driver`·`TestRunner`·`ScenarioRunner`나 `/run`·`/scenario/all`·`/execute` endpoint도 트리 전체에
**0건**이다. `run_e2e_all.sh`의 요약 출력·중단 정리·start_order 3변형은 §2.1.1 규약대로다.

### 이 라운드가 드러낸 방법론 문제

**feature-map을 신뢰 기준으로 쓸 수 없다.** Config 9 Track A는 전부 `구현`으로 적혀 있으나
session gateway 프로세스도 stream connector 사용도 0건이고(E2E-CP-02), §2.6 위반은 11개 중
0개가 기록돼 있다(E2E-CP-08). 이번 감사에서 한 리뷰어가 Config 1~7을 feature-map 자기신고 기준으로
"적합"이라 판정했는데, 같은 범위를 **코드로 다시 읽자 P0 구멍이 쏟아졌다** — E2E-CP-04·05·06,
그리고 2차 스윕에서 E2E-CP-16~36. **feature-map은 갱신 대상이지 근거가 아니다.**

**"구조가 맞으니 통과"도 근거가 아니다.** 1차 스윕은 Config 1·2·3·4·5·7을 "시나리오 파일이 ID마다
하나씩 있으니 깨끗하다"고 봤다. 2차 스윕이 **같은 config를 시나리오별로 읽자** `SM-D2`(P0)가 `all`에서
빠져 있고(E2E-CP-16), `SM-F5`가 계약의 **정반대**를 단언하고(E2E-CP-17), `RL-D1`·`RL-C2` client
시나리오가 **dead code**이며(E2E-CP-31), `MON-A2`(P0)가 **원리적으로 실패할 수 없고**(E2E-CP-34),
`RC-A1`·`RC-A2`가 `RC-A3`와 **완전히 같은 호출**(E2E-CP-27)이라는 게 나왔다.

**감사자 자신의 "깨끗함" 판정도 검증 대상이다.** 이 문서의 1차 기록은 start-order 축을 "규약대로"라고
적었는데, 2차에서 **11개 config 중 9개가 그 변수를 읽지 않는다**는 게 드러났다(E2E-CP-22). 같은 식으로
"샘플 환경변수 0건"도 앱 코드만 본 판정이었고 runner 축에서 뒤집혔다(SMP-CP-26).

### 가장 중요한 것 — **e2e가 갭을 못 잡는 게 아니라, 잡을 수 없게 배치돼 있다**

Config 6은 [IMP-CP-06](#라운드-2-2026-07-14--stage--관측--connector--http-client)(store failure
grace 미구현·reconcile 100ms 하드코딩)과 [IMP-CP-35](#상세-1)(framework가 owner lease를 join하지
않음)를 **검증하라고 존재하는 config**다. 그런데 실제로는:

- 장애를 **`docker pause`로만** 만든다 → store가 "재연결됐거나 빈" 상태로 **돌아오지 않는다** →
  IMP-CP-06이 터지는 **조건 자체가 안 생긴다**(E2E-CP-37).
- stale row 제외를 **공식 Redis 확장이** 해 준다 → framework가 lease를 join하든 말든 **초록**이다
  (E2E-CP-39). **스펙대로 쓴 사용자 store는 계약을 위반하는데 e2e는 통과한다.**
- `store_failure_grace`를 **세팅만** 하고 읽는 곳이 0인데, 단언이 `SF-B1`과 같아서
  **미구현이어도 통과**한다(E2E-CP-38).
- 모든 "요청 성공" 단언이 consumer의 **30초 retry 루프**를 지난다(E2E-CP-41).

**그래서 Config 6이 실제로 증명하는 것은** (a) Redis 확장의 `owner_is_live()` lease 필터가 돈다,
(b) Redis를 SIGSTOP하면 즉석 probe가 실패하고 풀면 성공한다, (c) 30초 retry가 결국 살아 있는
provider를 찾는다 — **셋 다 framework location-runtime 계약이 아니다.**

**이 패턴을 다른 config에서도 확인했다** — `MON-A2`(P0)는 IMP-CP-13(diff 없이 매 tick 발행) **덕분에**
통과하고(E2E-CP-34), `MON-A3`·`MON-A5`는 IMP-CP-15가 지목한 **비-스펙 source를 e2e가 따로 켜서**
가린다(E2E-CP-36). `OBS-A2`는 IMP-CP-18이 지목한 **C++ 전용 `outcome=` 토큰을 오히려 못박는다**
(E2E-CP-62). **갭을 닫을 때 e2e를 함께 고치지 않으면, 고친 뒤에도 게이트는 여전히 초록이다.**

### 지배적인 실패 유형 — **"실패할 수 없는 단언"**

이 라운드에서 가장 많이 나온 건 미구현이 아니라 **원리적으로 실패할 수 없게 쓰인 단언**이다.
같은 병이 여러 형태로 나타난다.

| 형태 | 사례 |
|---|---|
| **비교 연산자를 느슨하게** | GameQuest 멱등성 `>=`(SMP-CP-32), `ST-D1` local generation `>=`(E2E-CP-55) |
| **존재하지 않는 것의 부재를 단언** | `ST-C3`가 **아무도 안 보내는** packet의 marker 부재를 본다(E2E-CP-52) |
| **설치한 적 없는 것이 비었음을 단언** | `OBS-B4`가 등록하지 않은 collector가 빈 걸 확인한다(E2E-CP-63) |
| **하드코딩 상수를 단언** | `SF-A2`의 `!watch_enabled`(E2E-CP-42), `RL-B2`의 500ms timeout(E2E-CP-32) |
| **금지된 결과를 통과로 수용** | `OBS-C1`이 **row 삭제**를 받아들인다(E2E-CP-61) |
| **실패를 경고로 강등** | Track F `require_runtime_marker()`가 `return 0`(E2E-CP-50) |
| **무조건 PASS 출력** | `OBS-C1` create-rejection(E2E-CP-61) |
| **자기가 만든 오류를 자기가 단언** | `TA-B1`이 caller가 던진 kind를 확인한다(E2E-CP-58) |
| **재시도·sleep으로 세탁** | consumer 30초 루프(E2E-CP-41), spot route 10초 루프(E2E-CP-21) |
| **경합 창을 스스로 닫음** | `ST-F2`·`ST-F3`가 `join_task.get()` 뒤에 보낸다(E2E-CP-53) |
| **계약의 반대를 단언** | `SM-F5`(E2E-CP-17), `ST-E2`(E2E-CP-49) |

**이 목록을 갭을 닫을 때 체크리스트로 쓴다.** 구현을 고친 뒤 해당 e2e가 **여전히 통과한다면
그 e2e가 틀린 것**이다 — 고쳤는지 확인하려면 **먼저 e2e가 실패하는지부터 봐야 한다.**

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X1** | pending actor row를 resolve 성공으로 반환 | IMP-CP-07 |
| **IMP-X2** | location event source 결측 + `StoreFailure`/`StoreRecovered` 부재 | IMP-CP-09 |
| **IMP-X3** | startup validation이 아예 없다 | IMP-CP-04 |
| **IMP-X5** | message-flow 관측자가 로그 모드에 묶여 침묵 | **이 언어 전용 ID 없음** — `message_flow_tracer.hpp:69-105`의 `trace()`가 `enabled_for(outcome)`와 샘플 게이트에서 `emit()`(=`deliver_observer`) **앞에** 반환한다. [52 §3](../server/52-message-flow-tracing.ko.md)대로 관측자는 모드와 무관하게 발화해야 한다 |
| **IMP-X6** | `origin=lifecycle`을 생성하지 않는다 | **이 언어 전용 ID 없음** — `flow_origin_t::lifecycle`이 로그 이름 switch(`message_flow_tracer.hpp:157`)에만 있다. `flow_context_t::enter(..., lifecycle)`을 부르는 곳이 없다 |
| **IMP-X7** | connector send payload 한도를 압축 전에 적용 | IMP-CP-27 |
| **IMP-X14** | `listPageSize`가 죽어 있다 | **이 언어 전용 ID 없음** — `contracts/locations/options.hpp:16`이 트리 내 유일한 등장이고, `store_location_resolvers.hpp:180`이 `page_size == 0`(무제한) 요청을 만든다 |
| **IMP-X15** | `storeFailureGrace`가 죽어 있다 | IMP-CP-06 |
| **IMP-X16** | `include_native_diagnostics`가 죽어 있다 | IMP-CP-33 |
| **IMP-X18** | Redis fixture 바이트 단위 일치 주장이 거짓 | IMP-CP-37 |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

C++ public header와 package는 이 문서가 추적하던 계약 차이를 해소했다. 아래는 각 항목의
해소 결과이며, 상세 근거는 C++ 계약 ledger와 구현 로그에 있다.

| 항목 | 해소 결과 |
|------|-----------|
| coroutine blocking bridge | 공개 계약층의 `.result()` bridge 제거(lifecycle/transfer adapter는 coroutine). runtime 내부의 동기 소비 경로는 실행 줄 소유자가 관리한다 |
| 오류 kind | 공통 집합 밖 여섯 enumerator를 public enum에서 제거하고 `detail::boundary_error_t` 내부 상태로 강등. 경계 의미는 `framework_exception_t::code()`(`std::error_code`) 파셋으로 노출 |
| callback 이름 | `on_create_actor`/`on_actor_join(ed)`/`on_leave_actor`/`on_disconnect_actor`/`destroy_actor` snake_case 통일(camelCase 탐지 경로 삭제) |
| typed session handler와 route-mesh options | `typed_session_packet_handler_for` concept과 serializer 경유 typed invoker 추가, route-mesh runtime options 정렬 |
| one-way, location watch와 message-flow control | 일반 one-way와 actor send 모두 `void submit()`, relay/disconnect는 `task_t<void>`. location watch와 message-flow 계약 표면 반영 |
| actor membership와 join 결과 | `is_joined()` 제거 후 `std::optional<spot_rid_t> spot_rid()` 단일 상태, join 결과는 승인/거절 `std::variant` |
| 관측·운영(metrics/flow/drain) | flow correlation, 계기 카탈로그, graceful drain(핸드오프·liveness·session-closing)을 구현하고 Config 1~11 E2E와 sample로 검증 |

dispatch 실패의 로그 수준([channel 메시징 §3.1](../server/11-channel-messaging.ko.md))도 2026-07-13에
대조하고 정렬했다. 이전 C++ reporter는 **모든 dispatch 오류를 Error로 기록**해 원인별 구분이
없었다. 지금은 application 코드가 던진 handler 예외를 one-way라도 Error로 남기고, handler 없음·
payload decode 실패·invalid frame은 send(및 actor send)를 Warning, publish를 Debug로 낮춘다.
request는 error reply로 끝나므로 Error를 유지한다(`.NET`의 `SendLogLevel`/`PublishLogLevel`
기본값과 같은 의미). 검증은 `test_cpp_framework_message_flow`의 수준 매핑 케이스다.

### 5.1 C++ 비동기 실행 정책 — 해소

**해소(2026-07-14).** 당시의 turn 계약(자동 turn dispatch)을 검증하는 Config 8
`AutomaticTurnDispatch`가 전 시나리오 통과했다(ATD-C3B·ATD-D2 포함).

> **이후 계약이 바뀌었다.** 자동 turn dispatch는 폐기됐고 세 terminator(`submit`/`async`/`yield`)가
> 정본이다([04 §1.1](../04-async-execution-policy.ko.md)). 아래 서술은 당시 계약 기준의 기록이며,
> 현재 갭은 [§12.21](#1221-yield-terminator-부재-전-언어)이 소유한다. Config 8도
> [실행 turn과 terminator](../../common/e2e/config-8-execution-turn.ko.md)로 재작성했다.

간헐 실패의 원인은 turn 배선이 아니라 **stream connector의 heartbeat 응답 경로**였다. connector는
server liveness ping의 pong을 `dispatch()` 경로에서만 썼는데, ATD client는 응답을 기다리는 동안
`dispatch()`를 부르지 않는다. 그래서 수신 pump가 ping을 읽어 표시만 해 두고 pong은 나가지 않았고,
응답이 heartbeat 창보다 오래 걸리는 정상 요청에서 서버가 세션을 heartbeat timeout으로 끊었다.
client에는 그것이 `End of file`로 보였다. 지금은 수신 pump가 pong을 write 큐에 싣고, 동기 request
루프도 자기 문맥에서 바로 답한다. 추적 기록은 C++ 구현 로그의 `CPP-ATD-TIMER-RESUME-001`에 있다.

STREAM 압축 wire는 다른 언어와 같은 LZ4 pickle 프레이밍으로 정렬했다(이전 raw
`[u32][block]` 프레이밍은 언어 경계를 넘지 못했다). 남은 wire 항목은 SPOT fan-out의
단일 프레임 인코딩이며, 원인(프레임워크 부착 SPOT의 multipart publish가 첫 파트만 전달)이
core 소유라 C++ 계약 ledger에 열린 항목으로 남겨 두었다.
