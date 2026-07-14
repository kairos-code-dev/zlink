# C++ — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> **framework의 레퍼런스 구현이다.** 스펙은 이쪽이 완전할 것을 기대한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 20건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-CP-01** (결함) — 20 §3.3
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
