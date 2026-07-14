# Node.js — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> NestJS host. connector 계약은 TypeScript가 따로 소유한다.

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 20건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-ND-01** (결함) — 54 §4·§5
- [ ] **IMP-ND-02** (미구현) — 54 §3.1·§4-2
- [ ] **IMP-ND-03** (결함) — 03 §5.3
- [ ] **IMP-ND-04** (미구현) — 54 §7.1
- [ ] **IMP-ND-05** (결함) — 54 §3.3-4
- [ ] **IMP-ND-06** (결함) — 54 §3.4
- [ ] **IMP-ND-07** (결함) — 05 §2.6·22 §2
- [ ] **IMP-ND-08** (미구현) — 22 §6·§6.1
- [ ] **IMP-ND-09** (미구현) — 51·05 §2.4.3
- [ ] **IMP-ND-10** (결함) — 30 §7.2

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X2** — location event source(`location-peer/spot/actor/route`, `StoreFailure`/`StoreRecovered`)가 없다
- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.5** — spot 메시징 표면 누락 (Node)
- [ ] **§12.6** — session handler registry 키 (Node)
- [ ] **§12.11** — location event kind 이름 (Node)

### 전 언어 공통 계약 갭 (모든 언어가 함께 닫는다)

- [ ] **§12.20** (결함) — 응답에 packet name을 싣는다
- [ ] **§12.21** (결함+미구현) — `yield` terminator 부재 + `async`가 자동으로 turn을 반납
- [ ] **§12.22** (결함+미구현) — HTTP client가 framework 계약 밖에 있다
- [ ] **§12.23** (미구현) — worker 축 분리와 `yield` 부재
- [ ] **§12.24** (결함) — actor join의 orchestration이 뒤집혀 있다

본문은 [갭 인덱스](../90-implementation-gap.ko.md)가 소유한다. **§12.21과 §12.24는 한 묶음이다** — join orchestration을 먼저 바로잡지 않고 자동 turn dispatch만 걷어내면 user Spot → user Spot join이 즉시 막힌다.

## 2. 구현 감사 상세

| ID | 종류 | 계약 | 구현이 하는 일 |
|----|------|------|----------------|
| **IMP-ND-01** | 결함 | [54 §4·§5](../server/54-graceful-drain-handoff.ko.md): 순서는 marker → 신규 수용 차단 → **handoff** → in-flight 대기 → owner 정리. 기존 세션은 actor handoff를 거친다 | `runtime/host/index.ts:485-487` — `notifyServerDrain()`이 **handoff 앞에** 있다. 모든 세션을 먼저 끊는다. ⇒ SIGTERM 한 번에 **모든 클라이언트가 먼저 끊기고**, bound actor는 세션을 잃은 뒤에야 handoff된다. DRAIN-003·DRAIN-013이 통과할 수 없다 |
| **IMP-ND-02** | 미구현 | [54 §3.1·§4-2](../server/54-graceful-drain-handoff.ko.md): drain 중 신규 수용을 거부한다(`RequestRejected`/`ActorCreateRejected`) | **admission gate가 없다.** `ready` 플래그를 읽는 곳이 `isReady()` 하나뿐이고, spot create·actor create·신규 STREAM 연결·draining 노드로의 join 어디에도 검사가 없다. `RequestRejected`는 **런타임 코드에서 한 번도 생성되지 않는다** |
| **IMP-ND-03** | 결함 | [03 §5.3](../03-message-model.ko.md): `Error`는 **비어 있지 않은 `error-code`**를 갖고, 실패를 `RequestFailed`로 뭉개지 않는다 | `runtime/channels/channel-envelope.ts:134-150` — 오류 reply 인코더가 `message: string`만 받아 **kind를 버리고** `errorCode:'ZLinkRouteHandlerError'`를 **하드코딩**한다. 디코더(:157-159)는 `errorCode`를 **읽지도 않고** `ZLinkConfigurationException`을 던진다. ⇒ 호출자에게 `kind`도 `isRetriable`도 없다. **오류 분류가 wire에서 통째로 소실**된다 |
| **IMP-ND-04** | 미구현 | [54 §7.1](../server/54-graceful-drain-handoff.ko.md): 서버는 1초 ping / 5초 pong timeout / 30초 idle timeout | **liveness 루프가 없다.** ping도 pong 추적도 idle 타이머도 없다. `protocol.ts:82-97`은 reason 바이트를 **`4`(server_drain)로 하드코딩**한다. ⇒ 전원이 뽑힌 클라이언트(half-open TCP)를 **영원히 감지 못 한다.** 세션·bound actor·binding·location row가 전부 남는다 |
| **IMP-ND-05** | 결함 | [54 §3.3-4](../server/54-graceful-drain-handoff.ko.md): row/lease 정리가 **성공한 뒤에만** `Drained`로 전이한다 | `runtime/locations/runtime.ts:156-173` — 정리 실패를 **삼킨다.** 그래서 Redis가 죽어 있어도 drain이 `Drained`를 반환한다. `OwnerCleanupFailed`는 **생성되는 곳이 없다.** ⇒ 죽은 노드의 lease와 row가 TTL까지 남아 peer들이 계속 dial한다 |
| **IMP-ND-06** | 결함 | [54 §3.4](../server/54-graceful-drain-handoff.ko.md): 마커 게시는 **deadline까지 재시도**한다 | `runtime/host/index.ts:476-484` — **한 번만** 게시하고 실패하면 즉시 force-stop. 전파 대기도 없다. ⇒ SIGTERM 순간의 일시적 store 오류 하나가 **모든 방과 actor를 강제 종료**시킨다 |
| **IMP-ND-07** | 결함 | [05 §2.6](../05-framework-api.ko.md)·[22 §2](../server/22-actor-model.ko.md): **filter는 그 dispatch의 DI scope에서 resolve한다. handler와 같은 scope다** | `runtime/channels/channel-dispatch-services.ts:67-81` — filter를 **싱글턴으로 한 번** resolve해 프로세스 수명 내내 재사용한다. handler는 dispatch마다 새 scope다. ⇒ request-scoped filter가 **resolve되지 않고**, 필드에 상태를 두는 filter는 동시 dispatch 간에 **조용히 공유**된다 |
| **IMP-ND-08** | 미구현 | [22 §6·§6.1](../server/22-actor-model.ko.md): 호출자는 resolver나 actor manager로 **remote `ActorRef`**를 얻는다 | `runtime/actors/index.ts:114-121` — `find()`가 **로컬 in-process map만** 본다. `ensure()`는 placement 인자를 무시하고 로컬 생성으로 간다. ⇒ **다른 노드의 actor에 메시지를 보낼 public 경로가 Node에 없다** |
| **IMP-ND-09** | 미구현 | [51](../server/51-runtime-metrics.ko.md)·[05 §2.4.3](../05-framework-api.ko.md): trace가 off여도 **metric/counter는 남는다** | `zlink.channel.messages.dropped` 등 **7개 계기가 아예 없다.** ⇒ trace를 끄면 drop이 **완전히 보이지 않는다** |
| **IMP-ND-10** | 결함 | [30 §7.2](../server/30-stream-session.ko.md): stream node 이름 빈 값·중복은 설정 오류 | `RegistrationBuilders.ts:198-201` — `streamNodes[name] ??= {}`. 두 모듈이 같은 이름을 등록하면 **조용히 덮어쓴다** |

## 3. 언어별 표면 차이 상세

### §12.5 spot 메시징 표면 누락 (Node)

**미충족(Node).** 두 항목이다.

- route client에 `sendToSpot` / `requestToSpot`가 없다. spot node가 아닌 외부 client가 spot handle로
  spot에 메시지를 보낼 수 없다([20 §6](../server/20-spot-messaging.ko.md), [24 §3](../server/24-spot-address-messaging.ko.md)).
- spot 전송이 handle을 한 번 resolve한 뒤 그대로 보내고 끝난다. [24 §4](../server/24-spot-address-messaging.ko.md)가
  요구하는 **stale 실패 감지 → handle 갱신 → request 1회 재전송**이 없다.

### §12.6 session handler registry 키 (Node)

**미충족(Node).** [31 §10.2](../server/31-session-actor-dispatch.ko.md)의 session handler registry는 packet
name을 키로 dispatch해야 한다. Node 구현은 **handler 클래스 이름**을 키로 저장하므로 wire의 packet
name과 우연히 일치하지 않으면 영구 미매치가 된다. 중복 등록 검출과 `Configure()` 등록 창 강제도
없다.

### §12.11 location event kind 이름 (Node)

**미충족(Node).** [40 §9](../server/40-location-runtime.ko.md)와 [50 §3.1](../server/50-runtime-monitoring.ko.md)이 고정한
location runtime event kind의 닫힌 집합은 `StatusChanged`, `TopologyChanged`,
`ServiceSummaryChanged`, **`StoreFailure`**, `StoreRecovered`다. `.NET`, Java, C++은 이 이름을
쓰는데 Node 구현만 `StoreUnavailable`을 쓴다. 닫힌 enum의 멤버 이름은 관측 데이터의 안정 키이므로
언어마다 다를 수 없다.

## 라운드 2 (2026-07-14) — 관측 · channel topology · TypeScript connector

### 체크리스트

- [ ] **IMP-ND-11** (결함) — `flow_id`를 **홉마다 새로 만든다.** wire에 실린 id와 로그의 id가 **다르다**
- [ ] **IMP-ND-12** (결함) — tracing이 `off`인데도 **wire에 flow id를 생성한다**
- [ ] **IMP-ND-13** (결함) — 모니터링 dispatcher가 예외 시 `continue`가 아니라 **`return`**한다
- [ ] **IMP-ND-14** (결함) — 샘플링이 **flow 단위가 아니라 이벤트 단위**다
- [ ] **IMP-ND-15** (미구현) — Entry Spot이 `spot.count`/`created`/`closed`에 **잡히지 않는다**
- [ ] **IMP-ND-16** (결함) — handler 없는 `server`/`subscriber` 역할이 startup을 통과하고 **소켓을 아예 bind하지 않는다**
- [ ] **IMP-ND-17** (결함) — channel 종류가 **배타적이지 않고**, 같은 이름을 두 번 등록하면 **조용히 병합**된다
- [ ] **IMP-ND-18** (결함) — 수동 endpoint가 그 역할의 자동 연결 reconcile을 **끄지 않는다**
- [ ] **IMP-ND-19** (결함) — SPOT timer 등록 검증이 **startup이 아니라 spot 활성화 시점**
- [ ] **IMP-ND-20** (결함) — `fanout.received`가 등록되지 않은 topic까지 라벨로 단다(`.NET` IMP-DN-08과 동형)
- [ ] **IMP-TS-01** (결함) — **TypeScript connector**: 안 읽은 backlog가 쌓이면 `FrameTooLarge`로 **세션을 끊는다**
- [ ] **IMP-TS-02** (결함) — **TypeScript connector**: handler 없는 수신 메시지를 **버려서** `waitFor`가 이미 도착한 메시지를 못 받는다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-ND-11** | [53 §2.1·§6](../server/53-flow-correlation.ko.md): **진입점에서 한 번만 생성**한다. 홉마다 재생성하지 않는다 | `flow-context.ts:12-14` — ambient flow가 없으면 **매번 새 id를 만들고 저장하지 않는다.** 그래서 `channel-envelope.ts:69`이 header에 **A**를 싣고, `message-flow.ts:127`은 `sent` 로그에 **B**를 찍고, `reply_received`는 **C**를 찍는다. ⇒ 서버는 `flow=A`로 남기는데 Node 클라이언트는 `flow=B`로 남긴다. **`grep flow=A`가 호출자 자신의 로그를 못 찾는다** — 이게 `53`이 존재하는 이유인데 |
| **IMP-ND-12** | [53 §2.2·§6](../server/53-flow-correlation.ko.md): tracing이 `Off`면 **새 id를 만들지 않는다** | `channel-envelope.ts:69,94`·`stream-frame-factory.ts:74` — 모드 검사 **없이** 항상 생성한다. `.NET`·C++은 게이트한다 |
| **IMP-ND-13** | [50 §3.2](../server/50-runtime-monitoring.ko.md): **한 handler가 예외를 던져도 다음 handler를 계속 실행한다.** monitoring loop를 깨지 않는다 | `runtime/diagnostics/index.ts:63-71` — `catch { console.error(...); return; }`. 게다가 handler를 **타입 구분 없이 한 리스트**에 등록해 모든 이벤트를 모든 handler에 보낸다. ⇒ 예상 못 한 이벤트 타입에 **처음 터지는 handler 하나가 그 뒤 전부를 영구히 굶긴다** |
| **IMP-ND-14** | [53 §5](../server/53-flow-correlation.ko.md): **flow 단위 일관 샘플링** — `flow_id` 해시로 결정해 한 흐름은 전부 남거나 전부 빠진다 | `message-flow.ts:172-183` — `sampleCounter % stride`. ⇒ `sample_rate=0.1`이면 한 흐름의 **10%만 무작위로** 남아 **어떤 흐름도 끝까지 추적되지 않는다.** `53 §5`가 막으려던 바로 그 실패다 |
| **IMP-ND-15** | [51 §4.2](../server/51-runtime-metrics.ko.md): `kind` 라벨은 `entry`/`user`로 나뉜다 | `spot-activation-registry.ts:104-105,132-133` — `{kind:'user'}` **하드코딩**. Entry Spot emit 지점이 없다. ⇒ `spot.count{kind="entry"}`가 **존재하지 않아** Entry Spot 큐 적체(매치메이킹 병목 신호)를 볼 수 없다 |
| **IMP-ND-16** | [11 §4](../server/11-channel-messaging.ko.md): server에 request/send handler 없음, subscriber에 publish handler 없음은 **설정 오류**. **모든 설정 오류는 host 시작 전에 실패한다** | `RegistrationValidators.ts:205-216` — **반대 방향만** 검사한다(handler ⇒ capability). 그리고 `channel-runtime-lifecycle.ts:174-181`이 handler 0개면 `continue`해서 **ROUTER를 아예 만들지도 bind하지도 않는다.** ⇒ handler를 다른 이름으로 묶는 오타 하나에 host가 **healthy로 기동하고 `:5001`에 아무것도 안 붙는다.** 로그도 metric도 없다. **갭 문서 §4.13이 이 행을 해소했다고 적고 있는데, 아니다** |
| **IMP-ND-17** | [10 §4](../server/10-channel-topology.ko.md): channel 종류는 **배타적**이다. 같은 이름을 두 번 등록하는 것도 **설정 오류** | `RegistrationBuilders.ts:154-162,192-195` — `channels[name] ??= {}`. 두 번 등록하면 **병합**되고, client/server + fanout을 같은 이름에 걸면 **네 역할이 다 켜진 채** 검증을 통과한다. `addSpotMesh`는 중복을 거부하므로 **패턴은 이미 알고 있었다** |
| **IMP-ND-18** | [10 §5.2](../server/10-channel-topology.ko.md) | `channel-autoconnect.ts:95-165` — 수동 endpoint가 있어도 reconcile 루프를 만든다. SPOT 역할은 이 규칙을 **지키므로**(`spot-node-autoconnect.ts:109-150`) **두 표면이 서로 어긋난다.** Java(IMP-JV-16)와 같은 결함 |
| **IMP-ND-19** | [25 §4.1](../server/25-stage-wrapper-on-spot.ko.md) | `spot-timer.ts:299-320` — 검증이 활성화 시점의 `add()`에만 있다. ⇒ `periodMs: 0`이 healthy로 기동하고 **모든 방 생성이 실패**한다 |
| **IMP-ND-20** | [51 §5](../server/51-runtime-metrics.ko.md) | `channel-dispatchers.ts:239` — handler 조회(:249) **앞에서** topic 라벨을 붙인다. `.NET` IMP-DN-08과 동형 |
| **IMP-TS-01** | [32 §4.7·§10.1](../stream-connector/32-stream-connector.ko.md): 한도는 **payload 바이트에만**. 큐가 가득 차면 새 메시지를 **버리고 `ReceivedMessageDropped`를 보고**한다(한도 = 1024 **메시지**) | `BrowserWebSocketConnection.ts:119-130` — **누적 미읽음 바이트**를 per-payload 한도와 비교해 `FrameTooLarge`를 던지고 **WebSocket을 끊는다.** ⇒ 기본 `Manual` 모드에서 게임 루프가 pump 사이에 있는 동안 서버가 2KB 프레임 40개를 밀면 backlog 80KB > 64KB → **연결이 끊긴다** |
| **IMP-TS-02** | [32 §10.1](../stream-connector/32-stream-connector.ko.md): `Send` packet은 handler나 **대기 표면(`waitFor`)으로 넘어가기 전까지 수신 큐에 머문다** | `ZlinkStreamReceivedMessages.ts:275-294` — `enqueue`마다 즉시 `drain()`하고 `if (handlers === undefined) continue;`로 **버린다.** `waitFor`는 호출 시점에야 handler를 등록한다. ⇒ 세션 bind 직후 서버가 민 `GameStarted`를 client가 `waitFor`로 기다리면 **이미 버려져서 5초 뒤 timeout**된다. `.NET`은 독립 unread history를 유지해 만족한다 |
