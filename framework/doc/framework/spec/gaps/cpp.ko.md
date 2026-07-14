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
