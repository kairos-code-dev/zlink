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

- [ ] **SMP-CP-01** (결함) — SupportChat의 **릴리스 게이트가 위조**돼 있다. framework를 거치지 않는 in-memory 도메인 단언이 `verified`를 찍는다
- [ ] **SMP-CP-02** (결함) — Bingo에서 **2번째 참가자가 `GameStartedNotify`를 영원히 못 받는다**
- [ ] **SMP-CP-03** (결함) — TicTacToe가 **스펙 근거 0인 public API**(`add_spot_resolver`)를 샘플에서 쓴다
- [ ] **SMP-CP-04** (결함) — TicTacToe 발행 메시지에 `Msg` 접미어(`PlayerWinMilestoneMsg`)
- [ ] **SMP-CP-05** (결함) — TicTacToe가 문서에 없는 push(`GameEndedNotify`)를 추가로 쏜다
- [ ] **SMP-CP-06** (결함) — Bingo wire 이름이 **샘플 안에서 갈라져 있다**(`...Msg` vs proto `...Event`)
- [ ] **SMP-CP-07** (결함) — TicTacToe `NextTurn`이 mark가 아니라 **actor id**를 싣는다
- [ ] **SMP-CP-08** (미구현) — SupportChat·DeliveryDispatch에 **문서에 없는 `Probe` 프로세스**
- [ ] **SMP-CP-09** (미구현) — ShoppingMall에 **`ClientScenario`가 없다**
- [ ] **SMP-CP-10** (결함) — SupportChat이 typed connector wait 대신 **raw packet + 수동 JSON 파싱**
- [ ] **SMP-CP-11** (미구현) — ShoppingMall·GameQuest의 **"동시" 시나리오가 실제로는 순차**다
- [ ] **SMP-CP-12** (결함) — Bingo·GameQuest runner가 **고정 sleep을 readiness로 쓴다**(문서가 명시적으로 금지)
- [ ] **SMP-CP-13** (미구현) — ShoppingMall·GameQuest에 **Domain/Application/Infrastructure 레이어가 없다.** SupportChat엔 **Infrastructure가 없다**

**아래는 흐름·레이어·runner 축을 따로 훑어 나온 것이다. 여기서 진짜 기능 버그가 나왔다.**

- [ ] **SMP-CP-14** (**버그**) — **SupportChat idle timer가 메시지 순번을 wall-clock으로 착각**해 첫 tick에 방을 닫는다
- [ ] **SMP-CP-15** (**버그**) — **ShoppingMall owner spot이 자기 재개를 예약하지 않는다.** HTTP edge가 대신 한다
- [ ] **SMP-CP-16** (결함) — **ShoppingMall이 전역 Redis 키 하나 + 전역 락 하나**로 모든 주문을 직렬화한다. owner-spot 모델이 무효화된다
- [ ] **SMP-CP-17** (결함) — **SupportChat customer가 자기를 상담원으로 등록할 수 있다**(role 검사 없음)
- [ ] **SMP-CP-18** (결함) — **TicTacToe notification publisher가 아무것도 발행하지 않는다.** vector에 쌓기만 하고 읽는 곳이 0
- [ ] **SMP-CP-19** (결함) — **SupportChat이 Api hop을 Support actor에서 Session으로 옮기고 payload를 고쳐 쓴다**
- [ ] **SMP-CP-20** (결함) — **GameQuest가 event마다 문서에 없는 blocking ensure 왕복**을 하고, owner를 샘플이 직접 해시한다
- [ ] **SMP-CP-21** (결함) — **ShoppingMall이 owner routing 대신 named mesh 2개 + 샘플 해시 + node 지정 request**를 쓴다
- [ ] **SMP-CP-22** (결함) — **Domain이 framework 헤더를 include하고 wire DTO를 직접 만든다**(Bingo·TicTacToe)
- [ ] **SMP-CP-23** (결함) — **TicTacToe Spot이 도메인 aggregate를 상속**한다. slicing 대입 + actor마다 `join()` 2회
- [ ] **SMP-CP-24** (결함) — **SupportChat Application 레이어가 serving 경로에서 dead code**다(위조 self-check에서만 쓰인다)
- [ ] **SMP-CP-25** (결함) — **Domain이 이미 내린 판정을 Infrastructure가 다시 내린다**(SupportChat idle/close, Bingo join notify)
- [ ] **SMP-CP-26** (결함) — **runner 3개가 framework 동작 knob(`ZLINK_CPP_AUTO_CONNECT_TRACE`)을 export**하고, Bingo는 그 덕에 생긴 로그로 self-check한다
- [ ] **SMP-CP-27** (결함) — **runner 4개가 빌드 전에 Redis container를 띄운다**
- [ ] **SMP-CP-28** (결함) — **통합 runner의 transient-bind 패턴이 `already bound` 토큰을 빠뜨린다**
- [ ] **SMP-CP-29** (결함) — **계약에 없는 wire 메시지 5종**(TicTacToe·DeliveryDispatch·GameQuest·ShoppingMall)
- [ ] **SMP-CP-30** (결함) — **GameQuest event store가 프로세스 로컬 맵**이라 재시작 복구를 증명하지 못한다

**아래는 §Client 검증 흐름(릴리스 게이트)을 번호 단계별로 걸어 나온 것이다.
게이트가 약해서 위의 버그들이 통과해 왔다.**

- [ ] **SMP-CP-31** (결함) — **Bingo 게이트가 SMP-CP-02를 구조적으로 잡을 수 없다.** client1만 start를 기다리고 `status`도 안 본다
- [ ] **SMP-CP-32** (결함) — **GameQuest 멱등성 단언이 `>=`라 실패할 수 없다.** 중복 증가해도 통과한다
- [ ] **SMP-CP-33** (결함) — **TicTacToe가 모든 move에서 `board`·`next_turn`을 버린다.** 미러 push는 존재 여부만 본다
- [ ] **SMP-CP-34** (결함) — **Bingo 게이트 5·7·8·9·11단계가 문서보다 약하다**
- [ ] **SMP-CP-35** (결함) — **TicTacToe 게이트 1·3·7·11단계가 필드를 빠뜨린다.** level 입장 조건은 아예 평가되지 않는다
- [ ] **SMP-CP-36** (결함) — **GameQuest reconnect가 정의하는 두 반쪽(unbind·복원 조회) 없이 돈다.** "다른 owner"도 미단언
- [ ] **SMP-CP-37** (결함) — **DeliveryDispatch가 상태 "순서"를 단언하지 않는다.** 독립 future를 선언 순서로 `.get()`할 뿐이다

### 체크리스트 — E2E

- [ ] **E2E-CP-01** (결함) — **Config 10이 통합 게이트에서 아예 안 돈다.** 대신 문서에 없는 config가 돈다
- [ ] **E2E-CP-02** (결함) — **Config 9 Track A(P0 전부)가 이름만 그렇다.** session gateway 역할도 stream connector도 없다
- [ ] **E2E-CP-03** (결함) — **Config 11이 구조 자체가 규약 밖**이다(Client 없음·env role 스위치·`OrderWorkflow` 역할 0건)
- [ ] **E2E-CP-04** (결함) — **PubSub client 시나리오가 아무것도 단언하지 않는다**
- [ ] **E2E-CP-05** (결함) — **SpotService `all`이 문서 시나리오를 빼먹고**(SM-F3·F4·F5) **문서에 없는 걸 돌린다**(SM-Q9)
- [ ] **E2E-CP-06** (미구현) — actor ref **`generation`이 리터럴 0**이고 검증하는 곳이 없다
- [ ] **E2E-CP-07** (결함) — **Config 10의 순서 계약이 검증되지 않는다**(포함 여부만 본다)
- [ ] **E2E-CP-08** (결함) — **§2.6 설정 정책을 `ToActorMessaging` 빼고 전 config가 위반**하고, feature-map에 기록한 곳이 0개다
- [ ] **E2E-CP-09** (결함) — **§2.1 대기 기준 위반**(SpotActorTransfer readiness 30초, ObservabilityOps 상수 0개)
- [ ] **E2E-CP-10** (결함) — **§2.5 시나리오 파일 분리 위반** — 4개 config가 단일 `main.cpp`
- [ ] **E2E-CP-11** (결함) — **Config 11 feature-map이 자기 모순**이다(pending을 `구현`으로 적는다)
- [ ] **E2E-CP-12** (결함) — **Config 6 기본 실행이 단일 시나리오**(`SF-A1`)이고 프레임워크 knob이 env로 뚫려 있다
- [ ] **E2E-CP-13** (결함) — **Config 10에 `feature-map.ko.md`가 없다**
- [ ] **E2E-CP-14** (미구현) — **§3.1 "route mesh 없음 × 분리 배치" 조합이 아예 만들어지지 않는다**
- [ ] **E2E-CP-15** (결함) — Config 4의 **`RC-A6`(P0)에 client scenario 파일이 없다**(shell runner가 대신 단언)
- [ ] **E2E-CP-16** (결함) — **`SM-D2`(P0, 원격 bind·relay)가 `all` 목록에 없어 게이트에서 안 돈다**
- [ ] **E2E-CP-17** (결함) — **`SM-F5`가 자기 계약의 정반대를 단언한다.** Spot을 닫지 않고 "살아 있음"을 확인한다
- [ ] **E2E-CP-18** (결함) — **`SM-E1`(P0)이 자기 존재 이유인 message-flow error evidence를 단언하지 않는다**
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
- [ ] **E2E-CP-30** (결함) — **`./run_e2e.sh RC-A6`가 항상 실패한다**(client에 branch가 없다)
- [ ] **E2E-CP-31** (결함) — **`RL-D1`·`RL-C2` client scenario가 dead code**다. `main.cpp`가 부르지 않는다
- [ ] **E2E-CP-32** (결함) — **`RL-B2`(P1) 단언이 실패할 수 없다.** 500ms timeout이라 crash와 무관하게 통과
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
- [ ] **E2E-CP-47** (결함) — **`PS-B1`의 격리 단언에 시간 제한이 없어** 격리와 head-of-line 블로킹을 구분하지 못한다
- [ ] **E2E-CP-48** (미구현) — **`PS-C1`의 publisher 쪽 negative**(dispatch marker 없음)가 단언되지 않는다

**Config 9·10·11 심층 — 여기서 "단언이 실패할 수 없다"가 가장 많이 나왔다.**

- [ ] **E2E-CP-49** (결함) — **`ST-E2`(P0)가 계약과 정반대 시나리오를 돈다.** 실패한 transfer를 검증해야 하는데 **성공한 transfer**를 돌린다
- [ ] **E2E-CP-50** (결함) — **Track F의 필수 marker가 단언이 아니라 경고로 강등**돼 있다. `require_runtime_marker()`가 **절대 실패하지 못한다**
- [ ] **E2E-CP-51** (미구현) — **필수 order marker 9개 중 3개가 서버에 존재하지 않고**(`location_committed`·`commit_ack`·`source_cleanup`), `commit_request`는 **`joined` 뒤에** 찍혀 문서 순서를 뒤집는다
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
- [ ] **E2E-CP-64** (결함) — **stray `e2e/DeliveryDispatch/`가 샘플의 갈라진 fork**다. **같은 wire에 계약 헤더가 둘**이고, 게이트는 fork 쪽을 돌린다

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
| **E2E-CP-40** | [config-6:157,164-168](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-D1`(P0)은 "각 provider evidence에 **불필요한 disconnect/reconnect marker가 없음**" + "request는 **전 구간에서** 성공"을 요구하고, `SF-D2`(P0)는 **재등록 → heartbeat 1회 유예 → diff** 순서와 "live link를 끊지 않음"을 요구한다 | `Client/main.cpp:159-170`(D1)·`:172-194`(D2) — **둘 다 store가 healthy로 복귀한 *뒤에야* 요청을 보낸다. 장애·복구 구간에 흐르는 트래픽이 0이다.** grace를 단언하는 곳도, disconnect evidence를 조회하는 곳도 없다. 게다가 provider evidence store는 **처리한 profile 요청만 기록**해(`Server/Provider/Infrastructure/provider_evidence_store.hpp:221-224`, 유일한 writer가 `Handlers/provider_handlers.hpp:80`) **connection/disconnect marker라는 표면이 config에 아예 없다** ⇒ 문서가 요구한 negative를 **단언할 대상조차 없다.** `.NET`은 pause **전에** 동시 트래픽 루프를 띄우고 max-success-gap을 단언한다(`SfD2LongOutageRecoveryScenario.cs:19-23,41-44,110-117`) |
| **E2E-CP-41** | [config-6:106,157](../../common/e2e/config-6-store-failure-recovery.ko.md): "**기존 연결이 유지된다**"(fail-static), "request는 **전 구간에서** 성공한다" | `Server/Consumer/Endpoints/consumer_endpoints.hpp:18-37`의 `request_profile_with_retry()` — **30초 deadline · attempt당 5초 timeout · 100ms backoff · 모든 오류를 삼킨다.** ⇒ `SF-B1`·`SF-C1`·`SF-D1`·`SF-D2`의 "요청 성공" 단언이 전부 **"client의 10초 HTTP timeout(`Client/Support/client_support.hpp:181`) 안에 라우팅이 복구됐다"로 전락**한다. **fail-static과 recovery grace가 막으라고 존재하는 바로 그 disconnect→reconnect storm이 조용히 통과한다.** E2E-CP-21과 같은 계열이지만 다른 config·파일·시나리오다 |
| **E2E-CP-42** | [config-6:93-94](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-A2` — "provider 하나를 **추가로 띄웠다가** 정상 종료한다", "추가 후 **polling interval 몇 tick 안에 새 provider가 routing 대상이 된다**", "watch를 지원하는 배포와 결과 의미가 같다" | `watch_enabled`가 framework에서 **하드코딩 `false`**다(`src/runtime/locations/store_location_resolvers.hpp:224`) ⇒ `ensure(!status.watch_enabled)`(`Client/main.cpp:68`)는 **절대 실패할 수 없는 항진명제**이고, 비교 대상인 watch-capable 배포가 C++에 **존재하지 않아** 문서의 "결과 의미가 같다"는 **검증 불가능**하다. 그리고 코드는 **provider를 한 번도 추가로 띄우지 않고** 기존 `api-b`를 종료할 뿐이라 ⇒ **`SF-A2`가 `SF-C2`의 사실상 복제본**이고, "몇 tick 안에 routing 대상이 된다"는 **어디서도 단언되지 않는다.** `.NET`엔 전용 `PollingOnlyLocationStore.cs`·별도 consumer 프로세스·`StartProviderCAsync()`가 있다(`SfA2PollingFallbackScenario.cs:15-36`) |
| **E2E-CP-43** | [config-6:138-144](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-C2` — `Draining=true` 게시, drain 중 신규 배정 없음, 30초 drain deadline 안 종료, **row/lease가 lease 만료를 기다리지 않고 제거**된다. "**SF-C1과 달리 강제 종료나 lease 만료만으로 통과시키지 않는다**" | `Client/main.cpp:144-157` — `/shutdown`을 POST하는데 그건 **50ms 뒤 SIGTERM을 올릴 뿐**이다(`Server/Provider/Handlers/provider_handlers.hpp:167-181` — **drain 요청 표면이 없다**). 그리고 row가 사라지길 기다리는 **timeout이 `options.lease_ttl`**이다(`:150`) ⇒ **순전히 lease 만료로 사라진 row도 통과한다 — 문서가 이름을 짚어 금지한 바로 그것.** `peer_location_t.draining`은 **public contract에 있는데**(`framework/include/.../contracts/locations/rows.hpp:36`) e2e의 peer projection이 **그 필드를 버린다**(`Server/Consumer/Endpoints/consumer_endpoints.hpp:154-156` — rid/endpoint/owner_id만). `.NET`은 `row.Draining`과 drain 중 lease-healthy를 단언한다(`SfC2GracefulRemovalScenario.cs:24-35`) |
| **E2E-CP-44** | [config-6:177](../../common/e2e/config-6-store-failure-recovery.ko.md): `SF-D3` — "healthy → **unhealthy + last error + lease 갱신 실패** → healthy + **last refresh**" 전이를 관측한다 | `src/runtime/locations/store_location_resolvers.hpp:221-241` — `get_status()`가 `list_owner_leases()`를 **즉석 probe로 던지고**, 성공하면 `store_healthy = true`**와 동시에** `last_refresh_at = now()`를 찍고(`:231-232`), 실패하면 `store_healthy = false`**와 동시에** `owner_lease_healthy = false`를 **강제로 덮어쓴다**(`:236-237`) — runtime이 `:226`에서 읽어 온 진짜 flag(`location_runtime.hpp:87-90`)를 **버린다.** ⇒ `has_last_refresh_at`(`Client/main.cpp:58,220`)은 **`store_healthy`와 문자 그대로 같은 bit**이고(refresh 시각이 전진하는지는 아무도 안 본다), 장애 중 `!owner_lease_healthy`(`:207`)는 **probe가 실패했다는 것만** 증명한다. **세 필드의 전이가 flag 하나가 두 번 뒤집히는 것으로 붕괴한다** |
| **E2E-CP-45** | [config-6:189-190,195-199](../../common/e2e/config-6-store-failure-recovery.ko.md): harness가 **Redis 응답을 지연**시킨다(proxy 또는 느린 Lua script). 검증 대상은 **store client가 진짜 non-blocking I/O를 하고 core I/O 스레드를 점유하지 않는가** | `Server/Shared/location_store.hpp:250-258` — 앱 쪽 데코레이터가 `std::this_thread::sleep_for(delay)`를 하고 **`.result()`로 블로킹**한다. consumer에만 물려 있고(`Server/Consumer/main.cpp:24-33`) **Redis는 손도 안 댄다.** ⇒ `ensure(delayed_store_read_ms >= delay_ms * 0.75)`(`Client/main.cpp:291`)는 **e2e 자기 `sleep_for`가 돌았다는 것만** 증명하고, 문서의 진짜 주장은 **측정되지 않는다** |
| **E2E-CP-46** | [config-3:48-49](../../common/e2e/config-3-pubsub.ko.md): warm-up은 **각 subscriber가 첫 event를 받을 때까지** 재발행한다(구독 준비 barrier). 오라클은 **공통 연속 구간(common contiguous sequence)이지 "N개 무손실"이 아니다** — `Publish(...).Async()`가 원격 수신을 보장하지 않기 때문이다 | `Client/Scenarios/fanout_basic_delivery_scenario.hpp:13-16` — warm-up 5개를 쏘고 **500ms 잔다. 수신을 관측하는 코드가 없다**(barrier 부재). 그리고 `run_e2e.sh:409-411`은 **세 subscriber 전부에서 `measure-*` 20개 전량**을 요구한다 ⇒ **계약이 허용하는 것보다 강한 오라클**이고, 관측되지 않은 barrier 위에 얹혀 있어 **설계상 flaky**하다. "같은 순서로 수신"도 **검사되지 않는다** — `contains_all_line_groups`가 집합 포함이다(`evidence_store.hpp:171-175,133-149`). 같은 무손실-전량 오라클이 `PS-B2`(23/23, `run_e2e.sh:448-451`)·`PS-A3`·`PS-A4`에 **재사용**된다 |
| **E2E-CP-47** | [config-3:88-92](../../common/e2e/config-3-pubsub.ko.md): `PS-B1` — 느린 subscriber가 **막혀 있는 동안** 빠른 subscriber들이 **계속 받는다**(fanout 격리) | 지연 주입은 진짜다(`run_e2e.sh:559` → `Server/Subscriber/Handlers/event_msg_handler.hpp:23-25`의 250ms). 그런데 오라클(`run_e2e.sh:444-447`)이 요구하는 건 sub-2/sub-3가 **결국** 16줄을 다 갖는 것뿐이고, bounded wait 기본값이 **10,000ms**다(`:387`). **느린 peer 뒤로 완전히 직렬화돼도 16 × 250ms = 4초**라 창 안에 넉넉히 들어온다. ⇒ **시간 제한이 없어 "격리"와 "head-of-line 블로킹"을 구분하지 못한다.** publisher가 막히든 말든 통과한다 |
| **E2E-CP-48** | [config-3:113](../../common/e2e/config-3-pubsub.ko.md): `PS-C1`(P0) — subscriber 쪽 drop marker에 더해 **publisher 쪽엔 dispatch marker가 없어야 한다**(`Publish(...).Async()`는 submit-only) | positive 절반은 튼튼하다(subscriber의 `reason=handlerMissing`/`action=drop`/`packet=MissingEventMsg`, `run_e2e.sh:397-399,452-455`, 실제 dispatch observer가 채운다 — `Server/Subscriber/main.cpp:29-32`). 그런데 **publisher evidence는 스냅샷만 뜨고(`run_e2e.sh:608`) 단언에 쓰이지 않는다** ⇒ 문서가 요구한 negative가 미검증 |

| **E2E-CP-49** | [config-10:277-286](../../common/e2e/config-10-spot-actor-transfer.ko.md): `ST-E2`(P0) = "**실패한** transfer는 bound session route를 바꾸지 않음". bind → transfer 시작 → **source-down-before-commit 또는 adapter 실패 주입** → target bound-session route가 안 생겼음을 확인 | `SpotActorTransfer/Client/main.cpp:703-731`의 `bound_session_rebind_isolation()` — **성공한 transfer를 돌린다**(`require(join.accepted)`, `:716`). 그리고 node B에 **새 bound session을 열어**(`:718`) 옛 세션이 push를 안 받는지 500ms negative로 본다(`:728-730`). **실패를 어디에도 주입하지 않는다.** ⇒ "실패한 transfer의 bound session 비오염"이라는 **P0 계약이 통째로 미검증**이다. E2E-CP-17(SM-F5)과 **같은 종류 — 시나리오가 자기 계약의 반대를 돈다** |
| **E2E-CP-50** | [config-10:45-46,305,318-320,362-366](../../common/e2e/config-10-spot-actor-transfer.ko.md): marker 집합을 고정하고, `ST-F1`·`ST-F2`("**이 순서가 뒤집히면 실패다**")·`ST-F5`가 그걸 **합격 기준**으로 삼는다 | `run_e2e.sh:196-201` — `require_runtime_marker()`가 marker가 없으면 **`Note: ... (timing-dependent)`를 찍고 `return 0`한다. 실행을 실패시킬 수 없다.** 앞의 주석(`:190-195`)이 그걸 **대놓고 정당화한다.** client는 넷 중 아무것도 단언하지 않는다 — `handoff_backlog`·`backlog_enqueued`·`mapping_evicted`·`stale_fail_fast`가 `Client/main.cpp`에 **0건**. ⇒ **문서가 "뒤집히면 실패"라고 못박은 순서 계약이 경고 한 줄로 강등됐다** |
| **E2E-CP-51** | [config-10:43-44](../../common/e2e/config-10-spot-actor-transfer.ko.md): order marker 9종을 고정하고 `ST-A1`(:83)·`ST-B1`(:130-131)·`ST-B3`(:160-161)이 그 순서를 단언한다 | `Server/ActorNode/main.cpp`에 **`location_committed` 0건, `commit_ack` 0건, `source_cleanup` 0건.** 그래서 세 시나리오가 **엄격한 부분집합만** 단언한다(ST-B1은 9개 중 5개만 기다린다, `Client/main.cpp:454-463`). 유일하게 존재하는 `commit_request`는 **`co_await context.join_spot(...)`가 반환한 *뒤에* 추가된다**(`:347-348`, `:471-472`) ⇒ **실제 위치가 `joined` 다음이라 문서 순서를 뒤집는다.** 그런데 **`commit_request`를 단언하는 client 코드가 0건**이라 아무도 못 잡는다 |
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
