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

## 라운드 2 (2026-07-14) — Stage · 관측 · connector · HTTP client

**C++은 레퍼런스 구현인데 이번 라운드에서도 가장 많이 나왔다.**

### 체크리스트

- [ ] **IMP-CP-12** (결함) — **spot을 닫아도 그 spot의 timer가 멈추지 않는다.** 닫힌 spot에 tick이 계속 들어가고 컨텍스트가 누수된다
- [ ] **IMP-CP-13** (결함) — location 모니터링이 **diff 없이 매 tick 이벤트를 발행**한다
- [ ] **IMP-CP-14** (결함) — 등록한 location polling 간격에 **숨은 1초 상한**이 걸린다
- [ ] **IMP-CP-15** (결함) — 스펙이 정한 **spot source가 timer 실패 이벤트를 내지 못한다**
- [ ] **IMP-CP-16** (미구현) — 계기 8개 결측
- [ ] **IMP-CP-17** (결함) — `add_spot_events(name, interval)`의 **interval을 읽는 곳이 없다**
- [ ] **IMP-CP-18** (결함) — 폴백 로그가 `phase=` 대신 **`outcome=`**을 쓴다
- [ ] **IMP-CP-19** (미구현) — **connector에 자동 재연결이 없다.** 수립된 연결이 끊기면 영원히 `Disconnected`
- [ ] **IMP-CP-20** (결함) — connector `connect()`가 **현재 상태를 무시**한다(`Closed`도 되살아나고, `Connected`면 소켓이 하나 더 열린다)
- [ ] **IMP-CP-21** (미구현) — connector `connect_timeout`(기본 5초)을 **적용하지 않는다**
- [ ] **IMP-CP-22** (결함) — connector heartbeat이 **`dispatch()` 안에서만** 돈다
- [ ] **IMP-CP-23** (결함) — `Manual` 모드인데 error/state/disconnected callback을 **IO 스레드에서 인라인 호출**한다
- [ ] **IMP-CP-24** (결함) — connector metadata 디코더가 **빈 값을 거부**한다 (교차 언어 wire 파손)
- [ ] **IMP-CP-25** (결함) — HTTP: streaming body가 **redirect에서 빈 채로 재전송**된다
- [ ] **IMP-CP-26** (결함) — HTTP: 압축 해제 후 **`content-length`를 제거하지 않는다**
- [ ] **IMP-CP-27** (결함) — connector send payload 한도를 **압축 전** payload에 적용한다

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

- [ ] **IMP-CP-28** (결함) — `extension_boundaries.hpp` — **설치되는 공개 헤더인데 스펙 근거도 구현도 0**
- [ ] **IMP-CP-29** (결함) — `unhandled_dispatch_options_t` 5개 필드가 **검증만 되고 읽히지 않는다**
- [ ] **IMP-CP-30** (결함) — `on_retry`/`on_dead_letter` — **C++에만 있는 메시지 신뢰성 계약**, 스펙 근거 0
- [ ] **IMP-CP-31** (결함) — send backpressure 기한이 **30초** — 스펙은 1000ms이고, request timeout을 재사용한다
- [ ] **IMP-CP-32** (결함) — `zlink_builder_t`·`message_bus_t`가 **C++ 스펙 스스로 비계약이라 선언한 내부 타입**을 노출한다
- [ ] **IMP-CP-33** (결함) — `include_native_diagnostics`를 **읽는 곳이 없다**
- [ ] **IMP-CP-34** (결함) — **`close_erased()`가 `callback_depth`/`close_requested`를 잘못된 mutex로 읽고 쓴다**
- [ ] **IMP-CP-35** (결함) — framework runtime에 **owner-lease join이 아예 없다.** store에 떠넘겼다
- [ ] **IMP-CP-36** (결함) — Redis 페이징이 SSCAN 커서가 아니라 **SMEMBERS + 정수 오프셋**이다
- [ ] **IMP-CP-37** (결함) — actor row에 **다른 셋에는 없는 `mesh` hash 필드**를 쓴다
- [ ] **IMP-CP-38** (결함) — lease remove/list가 **자기 Lua script를 쓰지 않는다**(그 script는 dead code)

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

## 라운드 4 (2026-07-14) — 샘플 · E2E

**여기서 처음으로 진짜 기능 버그가 나왔다.** 지금까지는 계약 위반이었지 게임이 깨지진 않았다.

### 샘플 체크리스트

- [ ] **SMP-CP-01** (**버그**) — Bingo에서 **두 번째 player가 `BingoGameStartedNotify`를 못 받는다**
- [ ] **SMP-CP-02** (미구현) — Bingo의 **정본 `yield` 사용처가 코드에 없다**
- [ ] **SMP-CP-03** (결함) — SupportChat의 **릴리즈 게이트가 날조**다 — framework를 거치지 않는다
- [ ] **SMP-CP-04** (결함) — TicTacToe가 **자체 Redis 스키마 + 근거 없는 `add_spot_resolver`**를 쓴다
- [ ] **SMP-CP-05** (결함) — TicTacToe `NextTurn`이 mark가 아니라 **actor id**를 담는다
- [ ] **SMP-CP-06** (결함) — Bingo reward packet 이름이 **자기 proto와 모순**된다(`Msg` vs `Event`)
- [ ] **SMP-CP-07** (결함) — 문서에 없는 **`Probe` 프로세스**가 SupportChat·DeliveryDispatch에 있다
- [ ] **SMP-CP-08** (결함) — 후반 샘플 4개가 **Domain/Application/Infrastructure 계층을 뭉갠다**
- [ ] **SMP-CP-09** (결함) — ShoppingMall이 **전역 Redis 키 하나 + 전역 락 하나**로 모든 주문을 직렬화한다
- [ ] **SMP-CP-10** (결함) — ShoppingMall에 **`ClientScenario`가 없다**
- [ ] **SMP-CP-11** (결함) — "동시" 시나리오가 **순차 실행**된다
- [ ] **SMP-CP-12** (결함) — GameQuest event store가 **프로세스 로컬 맵**이라 재시작 복구를 증명하지 못한다
- [ ] **SMP-CP-13** (결함) — 클라이언트 self-check가 문서보다 약하다(릴리즈 게이트)
- [ ] **SMP-CP-14** (결함) — 계약에 없는 wire 메시지 5종

### 샘플 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-CP-01** | [Bingo §10 step 6](../../common/sample/bingo/README.ko.md): **두 player client**가 `BingoGameStartedNotify`를 기다린다 | `bingo_room_spot.hpp:135` — `send_to_players(started, actor.actor.actor_id)`. **두 번째 인자는 제외 필터다**(`:175` `if (actor_id == excluded_actor_id) continue;`). 그런데 방금 join해서 **게임을 시작시킨 바로 그 player를 제외하고** 시작 notify를 보낸다. ⇒ **player-2는 시작 notify를 영영 못 받는다.** `.NET`은 이걸 명시적으로 보정한다(`BingoRoom.cs:59-63` — join 시 게임이 `Running`이면 그 actor의 bound session으로 따로 보낸다). **SMP-CP-13(약한 게이트)이 이 버그를 가려 왔다** |
| **SMP-CP-02** | [sample README](../../common/sample/README.ko.md)가 **Bingo §7.1을 `yield`의 정본 사용처**로 지목한다 | 문서에 10건, **C++ 샘플 전체에 0건.** Api 서버는 handler 2개뿐이고(`api_server_framework.hpp:36-39`), `BingoPlayerState`는 `completed_lines`에서 끝나며, `on_actor_joined`/`on_leave_actor`는 **채널 왕복이 없는 동기 void**다. (전 언어 공통 — 문서가 모든 구현보다 앞서 있다) |
| **SMP-CP-03** | 샘플의 **클라이언트 검증 흐름은 릴리즈 게이트**다 | `Server/Support/main.cpp:733-810` — "self-check"가 **서버 프로세스 안에서 평범한 도메인 객체를 만들어** 반환값을 단언한다. **Spot도 actor도 session도 framework 경로도 없다.** `Probe/main.cpp:60-73`이 그 문자열을 HTTP로 가져와 `server-invariants=verified`를 찍는다. **샘플의 릴리즈 게이트를 뒤집어쓴 유닛 테스트다** |
| **SMP-CP-04** | [TicTacToe §6.2](../../common/sample/tictactoe/README.ko.md): **샘플이 자체 Redis 스키마를 만들지 않는다** | `play_server_host_factory.hpp:77-86`이 `add_spot_resolver("redis-room-route", …)`를 등록하고 자체 해시 스키마(`redis_room_route_store.hpp`)로 뒷받침한다. 게다가 **`add_spot_resolver`는 스펙 트리 grep 0건** — 이 샘플이 유일한 소비자인 **근거 없는 공개 표면**이다 |
| **SMP-CP-09** | [ShoppingMall §2.2·§4·§16](../../common/sample/event/shoppingmall.ko.md): owner-spot 모델의 존재 이유가 **단일 병목 제거**다 | `Server/Common/store.hpp:163-166` — 모든 주문의 event stream·read model·commerce state를 **`shoppingmall:commerce-state` 단일 블롭**에 넣고, `redis_lock_t`가 읽기/갱신마다 **전역 락 하나**를 잡는다. ⇒ 두 `OrderWorkflow` 인스턴스가 **락 하나 뒤에 직렬화된다.** 위층의 spot-mesh 라우팅은 맞는데 **아래층 저장소가 그걸 무효화한다** |

### E2E 체크리스트

- [ ] **E2E-CP-01** (결함) — **Config 10이 게이트에서 보이지 않는다** — `run_e2e_all.sh`에 없고 feature-map도 없다
- [ ] **E2E-CP-02** (결함) — 게이트가 **config 문서에 없는 `DeliveryDispatch` e2e를 대신 돌린다**
- [ ] **E2E-CP-03** (결함) — Config 11이 **`Client/`가 없고** 단일 바이너리를 env로 역할 전환하며 검증이 **인라인 파이썬**이다
- [ ] **E2E-CP-04** (결함) — Config 9의 **P0 트랙이 대역**이다 — session gateway도 stream connector도 없다
- [ ] **E2E-CP-05** (결함) — Config 2의 `all` 실행이 **문서 시나리오 3개를 빠뜨리고 문서에 없는 `SM-Q9`를 돌린다**
- [ ] **E2E-CP-06** (결함) — **Config 3의 클라이언트 시나리오가 아무것도 단언하지 않는다**
- [ ] **E2E-CP-07** (결함) — actor ref `generation`이 **리터럴 `0`**이고 아무도 단언하지 않는다
- [ ] **E2E-CP-08** (결함) — Config 10의 순서 단언이 **포함 검사**로 약화돼 있다
- [ ] **E2E-CP-09** (결함) — `§2.6` 설정 정책 위반이 **전 config에 걸쳐 있는데 feature-map에 기록이 0**
- [ ] **E2E-CP-10** (결함) — `§2.1` 대기 기준 위반
- [ ] **E2E-CP-11** (결함) — config 4개에 **`Client/Scenarios/`가 없다**
- [ ] **E2E-CP-12** (결함) — Config 11의 **P0 3개가 대체품**인데 "구현"으로 표시돼 있다

### E2E 상세 — 가장 무거운 것

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-CP-06** | [e2e §2.5](../../common/e2e/README.ko.md): **시나리오 파일이 검증의 단위**다 | `PubSub/Client/Scenarios/fanout_basic_delivery_scenario.hpp` — 메시지 25개를 publish하고 **`scenario PS-A1 passed`를 출력한다. 단언이 파일에 하나도 없다.** `/evidence/wait`는 **`run_e2e.sh`에서만** 부른다. ⇒ **클라이언트 시나리오 파일이 아무것도 검증하지 않는다** |
| **E2E-CP-01** | [e2e §2.8](../../common/e2e/README.ko.md) | `SpotActorTransfer`가 `run_e2e_all.sh`의 `CONFIGS`에 **0번 등장**하고, **feature-map이 없는 유일한 config**다. ⇒ 집계 게이트가 **Config 10을 한 번도 돌리지 않는다** |
| **E2E-CP-02** | — | `e2e/DeliveryDispatch/`는 **config 문서가 없는데**(common/e2e grep 0건) `CONFIGS`에 들어 있다. ⇒ 게이트가 **계약이 정의하지 않은 것을 돌리면서 정의한 것은 건너뛴다** |
| **E2E-CP-05** | [config-2](../../common/e2e/config-2-spot-service.ko.md) | `SpotService/run_e2e.sh:3737-3745`의 `all` 목록이 **`SM-F3`·`SM-F4`·`SM-F5`를 빠뜨리고**, **문서에 없는 `SM-Q9`를 포함**한다 |

### feature-map을 증거로 쓰면 안 된다

**C++ feature-map은 신뢰할 수 없다.** `ObservabilityOps` map은 **자기 runner가 PENDING이라고 찍는 행을 "구현"으로** 적고, `ToActorMessaging` map은 대역인 TA-A1~A4를 `implemented`로 적는다.

**그래서 Config 1·4·5·6·7은 이번 라운드에서 시나리오별로 검증되지 않았다.** feature-map과 구조만 보고 "깨끗하다"고 판정했는데, 같은 방식으로 판정한 Config 2·3에서 **직접 읽어 보니 P0 구멍이 나왔다.** **잔여 리스크로 남긴다 — 통과로 취급하지 않는다.**
