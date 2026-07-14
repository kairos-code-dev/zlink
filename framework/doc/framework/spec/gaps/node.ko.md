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

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

**export 집합 자체는 깨끗하다** — framework 268/268, nestjs 66/66이 카탈로그와 일치한다.
**문제는 다른 데 있다.**

### 체크리스트

- [ ] **IMP-ND-21** (결함) — connector 패키지가 **raw header bytes API를 root export**한다
- [ ] **IMP-ND-22** (결함) — nestjs의 배포된 `.d.ts`가 **선언되지 않은 subpath를 import**해 내부 등록 레코드를 앱 타입 그래프로 끌고 온다
- [ ] **IMP-ND-23** (결함) — payload decode 실패를 **조용히 문자열로 바꾼다.** actor 경로에서 `PayloadDecodeFailed`가 **도달 불가**
- [ ] **IMP-ND-24** (결함) — `ZLinkWorkerOptions.minThreads`/`idleTimeoutMs`가 **조용한 no-op**
- [ ] **IMP-ND-25** (결함) — `includeNativeDiagnostics`를 **읽는 곳이 없다**
- [ ] **IMP-ND-26** (결함) — **actor가 든 spot을 닫을 수 있다** (`.NET` IMP-DN-17과 동형)
- [ ] **IMP-ND-27** (결함) — 중복 `destroyActor`가 **파괴되기 전에 성공을 반환**하고, 실패한 destroy는 **영구히 재시도 불가**
- [ ] **IMP-ND-28** (결함) — 첫 `GetOrCreate` 호출자의 취소가 **다른 호출자 전부를 실패**시킨다
- [ ] **IMP-ND-29** (결함) — 서버가 `correlation_id`를 `request_seq`로 **날조한다**
- [ ] **IMP-ND-30** (결함) — `listPageSize`를 **읽는 곳이 없다.** 내부 기본값이 **무한**이다
- [ ] **IMP-ND-31** (미구현) — `storeFailureGrace`를 **읽는 곳이 없다**

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-ND-21** | [32 §5·§4.2](../stream-connector/32-stream-connector.ko.md): **임의 header bytes를 다루는 API를 공개 표면에 두지 않는다.** application code는 이 header를 직접 만들거나 수정하지 않는다 | `stream-connector/src/index.ts:2-3`이 `ZlinkStreamFrameCodec`·`ZlinkStreamHeaderCodec`을 export한다. `ZlinkStreamFrameCodec.ts:6`의 `encode(header, payload, maxPayloadSize = 64*1024)`는 한도가 **호출자가 덮어쓸 수 있는 기본 인자**라, 이 경로는 connector에 설정된 `maxSendPayloadSize`를 **통째로 우회한다.** 앱이 `kind=Response`·`requestSeq`를 **위조**하거나 압축하지 않고 압축 플래그만 켤 수 있다 |
| **IMP-ND-22** | [00 §5](../00-public-contract-governance.ko.md)·[node 01](../server/languages/node/01-system-structure.ko.md): 내부 등록 레코드는 공개 표면이 아니다 | `nestjs/dist/contracts.d.ts:2`(외 `.d.ts` 10개)가 `'@zlink-systems/framework/nest-integration'`에서 `ZLinkFrameworkRegistrationOptions` 등을 import하는데, `framework/package.json`의 `exports`는 **`"."` 하나뿐**이다. 그 이름들은 268개 카탈로그에 **없다.** `framework-loader.ts:7-13`이 `createRequire`+`path.join`으로 **exports map을 의도적으로 우회**하고, `nestjs/package.json`엔 `exports`도 `files`도 없어 `dist/` 전체가 deep-import 가능하다. ⇒ `node16`/`bundler` 해석에선 소비자가 **타입 체크조차 못 하고**, 레거시 해석에선 **framework 내부 선언을 조용히 흡수**한다. **갭 문서 §4.2가 이 항목을 "해소"로 적고 있는데, 아니다** |
| **IMP-ND-23** | [05 §2.4.3](../05-framework-api.ko.md): decode 실패는 `PayloadDecodeFailed` + drop + metric | `runtime/messaging/payload-codec.ts:61-66` — `try { JSON.parse(text) } catch { return text as T; }`. serializer registry가 비어 있는 **기본 경로**가 이거다. `spot-actor-packet-dispatch.ts:211-227`이 `PayloadDecodeFailed`를 보고하려고 `try/catch`로 감싸는데, **호출된 쪽이 이미 파싱 오류를 삼켰으므로 그 catch는 영영 안 튄다.** channel/route 경로는 제대로 던진다(`channel-envelope.ts:199-207`) — **두 dispatch 표면이 서로 어긋난다.** ⇒ 손상되거나 codec이 틀린 actor packet이 **JS `string`을 DTO 타입으로 캐스팅한 채 handler에 넘어간다** |
| **IMP-ND-24** | — | `RegistrationTypes.ts:64-69`에 선언, `RegistrationValidators.ts:59,61`에서 검증, `runtime/workers/index.ts:20,22`에서 resolve — 그런데 스케줄러는 `maxQueueLength`와 `maxThreads`만 읽는다 |
| **IMP-ND-25** | — | 쓰는 곳 3, 읽는 곳 0. **갭 문서 §4.1이 이 행을 닫힌 것으로 적고 있는데, 아니다** |
| **IMP-ND-26** | [21 §close](../server/21-spot-node.ko.md) | `spot-activation-registry.ts:118-121`이 `canClose()`를 확인하고 close를 등록하는데, **close 본문은 spot의 직렬 큐에 post**된다(`spot-activation.ts:244-255`). 그 줄에 **이미 큐잉된 join이 먼저 실행**되어 `joinedActors.size === 1`이 되고, 뒤이어 도는 close 작업은 **`canClose()`를 다시 확인하지 않는다.** ⇒ timer·native spot·location row를 **무조건 정리한다** |
| **IMP-ND-27** | [22 §destroy](../server/22-actor-model.ko.md): 같은 actor instance에 대한 **중복 destroy는 성공으로 끝난다** — 파괴가 **끝난 뒤** 성공이지, 그 전이 아니다 | `runtime/actors/index.ts:255-258` — `beginDestroy()`가 `undefined`를 반환하면(=이미 destroying) **즉시 성공으로 resolve**한다. ⇒ A가 native destroy를 await하는 동안 B가 destroy를 부르면 **B는 곧바로 성공**을 받는다. B의 호출자가 "없어졌구나" 하고 같은 id로 `createActor`를 하면 **같은 state 객체를 돌려받고**, 뒤늦게 끝난 A가 `states.delete()`를 실행해 **방금 만든 actor의 상태를 지운다.** 게다가 `releaseActor`가 던지면 `resetDestroying()`만 하고 `nativeActorRef`는 **남겨 두어** 재시도가 이미 파괴된 native ref로 또 부른다 → **destroy가 영영 성공할 수 없고** location row가 lease 만료까지 샌다. `.NET`은 두 번째 호출자가 **공유 teardown에 합류해 실제 완료를 기다린다** |
| **IMP-ND-28** | [21](../server/21-spot-node.ko.md)·[54 §6](../server/54-graceful-drain-handoff.ko.md) | `spot-activation-registry.ts:183-207` — 소유자의 `create` 클로저가 **첫 호출자의 `signal`**을 캡처하고, 대기자들은 `pending.ready`의 거부를 물려받는다. `.NET` IMP-DN-18과 **같은 경합** |
| **IMP-ND-29** | [52 §9](../server/52-message-flow-tracing.ko.md) | `stream-session-runtime.ts:200,236,281` — `?? decodedHeader.requestSeq?.toString()` |
| **IMP-ND-30** | [40 §3·§8.2](../server/40-location-runtime.ko.md) | `contracts/Locations/Options.ts:5,13` — 읽는 곳 0. `framework-locations-redis/src/store.ts:369-373`이 `pageSize <= 0`이면 `SMEMBERS`로 **전체**를 읽는다 |
| **IMP-ND-31** | [40 §6.1](../server/40-location-runtime.ko.md) | `contracts/Locations/Options.ts:6,14` — **어느 패키지에도** 읽는 곳이 없다 |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X2** | location event source 결측 | §12.11 |
| **IMP-X3** | startup validation이 설정 오류를 통과 | IMP-ND-10 · IMP-ND-16 · IMP-ND-17 · IMP-ND-19 |
| **IMP-X5** | message-flow 관측자가 로그 모드에 묶여 침묵 | **이 언어 전용 ID 없음** — `runtime/diagnostics/message-flow.ts:116-142`. `flowIfEnabled()`(:93-98) 때문에 호출부가 **이벤트를 만들지도 않는다**. [52 §3](../server/52-message-flow-tracing.ko.md)대로 관측자는 모드와 무관하게 발화해야 한다 |
| **IMP-X6** | `origin=lifecycle`을 생성하지 않는다 | **이 언어 전용 ID 없음** — `flow-context.ts:12`의 `currentOrCreateFlow()`가 항상 `Application`으로 만든다. enum은 `contracts/Eventing/Contracts.ts:52`에 있고 디코더만 쓴다 |
| **IMP-X8** | 수동 endpoint가 auto-reconcile을 끄지 않는다 | IMP-ND-18 |
| **IMP-X10** | SPOT timer 등록 검증이 startup이 아니다 | IMP-ND-19 |
| **IMP-X11** | `fanout.received` 미등록 topic 라벨 | IMP-ND-20 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-ND-26 |
| **IMP-X13** | `correlation_id` 날조 | IMP-ND-29 |
| **IMP-X14** | `listPageSize`가 죽어 있다 | IMP-ND-30 |
| **IMP-X15** | `storeFailureGrace`가 죽어 있다 | IMP-ND-31 |
| **IMP-X16** | `includeNativeDiagnostics`가 죽어 있다 | IMP-ND-25 |
| **IMP-X17** | `GetOrCreate` 취소가 다른 호출자를 실패시킨다 | IMP-ND-28 |
| **IMP-X18** | Redis fixture 불일치 | 빈 컬렉션을 `{}`/`[]`로 낸다 |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

### 4.1 dispatch options

언어별 스펙은 dispatch 최적화 전략을 runtime 내부에 두고 message kind별 unhandled
policy, diagnostics와 message-flow observer만 정의한다. 현재 `ZLinkDispatchOptions`는
단일 `mode`, 단일 `unhandled.action`과 제한된 diagnostics를 제공한다.

2026-07-13 구현에서 다음 항목을 정식 계약에 맞췄다.

```text
public dispatch mode 제거 완료
request/send/publish별 unhandled policy
ReplyError
LogAndDrop
Drop
includeNativeDiagnostics
localRid
peerRid
socketRole
```

현재 계약과 구현 위치는
`packages/framework/src/contracts/Dispatch/ZLinkDispatchOptions.ts`다. Config 8
`AutomaticTurnDispatch`의 전체 Node.js runner도 통과했다 — **구 계약 기준 기록**이며, 그 config는
[config-8 실행 turn과 terminator](../../common/e2e/config-8-execution-turn.ko.md)(`TD-*`)로 대체됐다.

### 4.2 public export 경계

2026-07-13 구현에서 package root와 공개 `contracts/Configuration` export가 framework
내부 등록 record, normalize/validate helper와 default builder를 더 이상 내보내지 않도록
정리했다. 다음 종류의 이름은 package root에서 제거했다.

```text
createFrameworkRegistration
createFrameworkOptions
RouteChannelInternalState
MutableCodecRegistryOptions
DefaultDispatchOptionsBuilder
내부 registration record
내부 normalize/validate helper
```

공개 options, builder와 사용자가 구현하는 extension point만 package root에 남겼다. NestJS adapter는
framework package 내부의 integration bridge를 빌드 시점에 사용하지만, 이 bridge는 package export에
등록된 public subpath가 아니다. 따라서 application public surface에는 내부 등록 record와 구현 타입이
나타나지 않는다. source export test와 실제 `.tgz` consumer test가 이 경계를 검증한다.

### 4.3 typed session handler

typed payload handler와 serializer registry 연결을 구현했다. application handler에서 raw
`ZLinkMessage`를 받는 escape hatch는 제거했으며, bound session도 packet 타입으로 routing한다.

### 4.4 one-way actor와 bound session

actor와 bound session을 포함한 one-way submit을 `void submit()`으로 통일했다. 취소 신호는
actor 이동이나 session bind처럼 완료를 기다리는 장기 작업에만 남겼다.

### 4.5 interface catalog와 export 목록

언어별 interface catalog는 application public 타입의 목표 시그니처를 모두 고정한다.
location interface의 `I` prefix를 제거했다. package root의 내부 registration 타입도 제거했고,
companion NestJS package의 참조는 application export와 분리된 integration subpath로 옮겼다.

### 4.6 Actor membership와 join 결과

`isJoined`와 중복 join call을 제거하고 `spotRid`를 membership 상태 기준으로 고정했다. join
결과는 `status` discriminated union이며 승인 variant만 필수 actor ref를 가진다.

### 4.7 관측과 종료

OpenTelemetry meter `zlink.framework`, UUIDv7 flow correlation, typed graceful drain과
`session-closing` 제어 프레임을 구현했다. Node.js Config 11 `ObservabilityOps` runner는
OBS-A1~C5 evidence와 함께 통과했다. `Bingo.Ts`도 flow, metrics, drain 설정을 사용하는
sample smoke를 통과했다.

### 4.8 typed packet identity와 최종 상태

channel, route, Spot과 fanout packet identity는 `@ZLinkPacket`이 해당 class에 직접 기록한
metadata를 우선 사용하고, metadata가 없으면 생성자 이름을 사용한다. payload의
`packetName()` method와 call builder의 packet name override는 제거했다. decorator가 없는
subclass는 부모 class의 metadata를 상속하지 않는다. Stream Connector frame의 명시적 packet
name은 별도 connector 계약이므로 이 규칙의 제거 대상이 아니다.

### 4.9 stream disconnect routing id

SupportChat의 즉시 재연결 검증에서 기존 연결의 disconnect 처리와 새 actor binding이 겹치는
경합을 발견했다. Node.js framework는 같은 actor의 disconnect와 새 binding을 직렬화하고, 이전
binding token이 새 binding을 지우지 못하도록 수정했다. Stream Connector도 `close()`가 TCP 종료를
완료한 뒤 반환하도록 수정했다.

**충족.** core STREAM session은 disconnect monitor event에 peer routing id를 기록한다. Node addon은
이 값을 public `MonitorEvent.routingId`로 전달하고, framework adapter는 같은 값을 session runtime에
넘긴다. 따라서 같은 endpoint에 여러 session이 있어도 종료된 session 하나만 선택해 disconnect
callback과 binding 정리를 실행한다. routing id가 없는 이전 event를 endpoint만으로 추측하지 않는
방어 동작은 유지한다.

검증은 실제 STREAM peer를 연결·종료해 addon event의 routing id가 비어 있지 않은지 확인하고,
framework의 다중 session 회귀 검사에서 지정된 session만 종료되는지 확인했다. sample 재검토는
별도 G5 gate에서 계속 추적한다.

### 4.10 Stream Connector browser-only package와 검증

`@zlink-systems/stream-connector` package root를 플랫폼 `WebSocket` 기반 browser ESM으로 교체했다.
Node TCP/TLS, 직접 WebSocket 구현, Node flow context와 `/browser` subpath를 제거했다. public
transport는 `WebSocket`과 `WebSocketSecure`만 남으며 `tcp://`와 `tls://`는 connector를 만들 때
`ConfigurationError`로 거부한다.

브라우저 비동기 flow는 [flow correlation §4.4](../server/53-flow-correlation.ko.md)의 명시적 계약을 따른다.
connector instance에는 현재 inbound flow를 저장하지 않는다. 관련 outbound는 call builder의
`flowFrom(message)`로 flow 쌍을 전달하고, 표시하지 않은 outbound는 새 application flow를 만든다.
fake WebSocket contract test에서 관련 outbound의 보존과 관련 없는 callback의 격리를 확인했다.

MessagePack과 Protobuf package root도 browser-safe payload codec만 내보내고 server serializer 등록은
`./framework` subpath로 분리했다. `stream-wire`는 같은 source의 ESM/CommonJS 산출물을 제공한다.
Bingo는 생성된 정적 encode/decode와 결정성 검사를 사용하며 runtime filesystem lookup과 `protoPath`
option을 사용하지 않는다.

실제 Chromium은 `ws`와 `wss` request/reply·push, 명시적 flow 전달과 관련 없는 callback 격리,
reconnect, drain, close reason을 검증한다. 브라우저 기본 신뢰 설정에서는 자체 서명 인증서를
거부하며, 테스트가 이를 우회하는 connector option은 없다. `close()`는 WebSocket의 실제 close
event가 올 때까지 완료되지 않는지 fake WebSocket 회귀 검사에서도 확인한다.

Node ambient type 없는 browser declaration/build, browser bundle의 Node module 부재, codec graph
분리, Bingo 생성 codec 결정성, npm tarball browser/CommonJS consumer도 통과했다. 다섯 STREAM
sample client와 네 framework E2E client를 Chromium으로 실행했고, Browser TypeScript connector에서
`.NET`과 C++ STREAM server로 보내는 cross-language smoke도 통과했다. 따라서 이 항목에 남은
public contract gap은 없다.

### 4.11 dispatch 실패 수준과 `FailCaller`

2026-07-13 재대조에서 두 가지 구현 차이를 추가로 확인하고 해소했다.

첫째, channel dispatch error reporter가 원인과 message kind에 관계없이 모든 실패를 Error로
기록했다. publish handler가 없으면 unhandled policy가 Warning을 한 번 더 기록해 중복 로그도
남았다. reporter가 handler 예외는 Error, handler 없음·decode 실패·invalid frame은 send는
Warning, publish는 Debug로 내부 결정하도록 수정했다. 공개 `ZLinkUnhandledDispatchOptions`에서
호출자가 이 계약을 바꿀 수 있던 `sendLogLevel`과 `publishLogLevel`도 제거했다.

둘째, 공통 framework API가 요구하는 `FailCaller`가 Node.js enum과 local dispatch 경로에
없었다. local Spot request와 같은 reply frame 없는 호출은 이제 caller의 Promise를 실패시키고
observer event에 `FailCaller`를 기록한다. transport reply frame을 만들 수 있는 request는
기존처럼 `ReplyError`를 사용한다.

두 항목은 contract test에서 로그 호출 횟수와 수준, local caller의 Promise 실패 및 observer
event를 함께 검증한다.

### 4.12 actor 소유권 변경 중 session relay

2026-07-13 sample 반복 검증에서 actor가 다른 Spot node로 이동하는 동안 session binding의
`ActorRef`를 갱신하는 짧은 구간에 다음 client request가 들어오면 `ActorSessionNotBound`로
실패하는 경합을 확인했다. binding 갱신은 actor별 lifecycle coordinator를 사용했지만 session
relay는 같은 직렬화 경로에 참여하지 않아, 이전 route를 제거한 뒤 새 route를 등록하기 전의
중간 상태를 관찰할 수 있었다.

session relay도 같은 actor별 lifecycle coordinator에서 실행하도록 수정했다. 이제 소유권 갱신
중 들어온 relay는 갱신 완료 뒤 새 `ActorRef`와 binding route를 사용한다. contract test는 binding
갱신을 의도적으로 중단한 동안 relay가 실패하거나 먼저 실행되지 않는지 검증한다. Bingo sample은
서로 다른 play node 사이 actor 이동 직후 client request를 반복 실행해 이 경합의 실제 경로도
검증한다.

### 4.13 startup validation 누락 (해소)

2026-07-13에 [channel 메시징 §4](../server/11-channel-messaging.ko.md)와
[SPOT 메시징 §8](../server/20-spot-messaging.ko.md)의 각 행을 Node.js registration validator에 직접
대입해 다음 누락을 확인했고, 같은 날 구현과 회귀 검사를 추가해 모두 해소했다.

- server에 request/send handler가 하나도 없어도 startup이 성공한다.
- subscriber에 publish handler가 하나도 없어도 startup이 성공한다.
- router와 pub/sub 역할을 모두 사용하지 않는 SpotNode가 허용된다.
- actor factory를 등록한 SpotNode에 router 역할이 없어도 허용된다.
- router 또는 pub/sub 역할을 사용하면서 bind endpoint를 지정하지 않아도 허용된다.
- location store의 자동 연결과 같은 SPOT 수신 역할의 수동 peer endpoint를 함께 지정하면
  역할별 연결 정책이 필요하다.

해소한 항목은 설정 오류를 첫 message 호출이나 연결 timeout까지 늦추므로 application 개발자가
runtime 내부 연결 조건과 구동 순서를 알아야 하는 문제로 이어진다. Node.js는 registration과
NestJS handler discovery가 끝난 뒤, socket을 만들기 전에 위 구성을
`ZLinkConfigurationException`으로 거부한다. 회귀 검사는 잘못된 구성이 startup 전에 실패하는지
검증한다.

마지막 항목은 공통 channel topology §5.2의 역할별 manual 연결 규칙을 runtime에 적용해 해소했다.
router에 manual peer가 있으면 router auto reconcile만 수행하지 않고, pub/sub에 manual endpoint가
있으면 pub/sub auto reconcile만 수행하지 않는다. location store와 actor 위치 조회는 그대로
유지한다. 따라서 TicTacToe는 sample 전용 wrapper 없이 수동 SPOT peer와 원격 actor 위치 조회를
함께 사용할 수 있다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

**여기서 "가짜 통과"가 나왔다.** 실패할 수 없는 검증이다.

### 체크리스트

- [ ] **E2E-ND-01** (**가짜 통과**) — Config 11에 **e2e 앱이 없고 시나리오를 `echo`로 통과시킨다**
- [ ] **E2E-ND-02** (**가짜 통과**) — probe 서버가 **클라이언트가 검사할 값을 리터럴로 만들어 낸다**
- [ ] **SMP-ND-01** (미구현) — Bingo의 정본 `yield` 왕복이 **계약·서버·클라이언트 게이트 어디에도 없다**
- [ ] **SMP-ND-02** (결함) — **6개 샘플 전부가 framework session handler registry를 우회**한다
- [ ] **SMP-ND-03** (결함) — DeliveryDispatch가 **문서가 명시적으로 금지한** route-mesh + node rid로 offer를 보낸다
- [ ] **SMP-ND-04** (결함) — TicTacToe가 **자체 Redis room-route 스키마**를 들고 있다
- [ ] **SMP-ND-05** (결함) — TicTacToe의 "self-join notify 없음" 검사가 **25ms 창**이다
- [ ] **E2E-ND-03** (결함) — Config 9·10에 **`Client/Scenarios/`가 없다**
- [ ] **E2E-ND-04** (결함) — `§2.1` settle 상수가 **어느 runner에도 없고** readiness가 최대 **60초**
- [ ] **E2E-ND-05** (결함) — Redis 격리에 **탈출구**가 있다(`ZLINK_REDIS_E2E_ENDPOINT`)
- [ ] **E2E-ND-06** (결함) — e2e 앱 코드가 **환경변수를 읽고 쓴다**(`§2.6`: 0개)
- [ ] **E2E-ND-07** (결함) — e2e 클라이언트가 **HTTP client wrapper를 안 쓴다** — 전부 raw `fetch`
- [ ] **E2E-ND-08** (결함) — 시나리오 파일 **138개 중 0개**에 머리말 주석이 없다
- [ ] **E2E-ND-09** (결함) — 낡은 디렉토리 이름과 죽은 `dist/`
- [ ] **E2E-ND-10** (결함) — `START_ORDER` 축이 config 2개에만 있다
- [ ] **E2E-ND-11** (결함) — SpotService `all`이 **문서에 없는 `SM-Q9`를 기본 게이트에 넣는다**
- [ ] **E2E-ND-18** (결함) — `RM-B2`가 scale-in 동안 트래픽을 끊고 **남은 provider를 직접 호출한다**
- [ ] **E2E-ND-19** (결함) — Config 1 negative가 **public error kind를 전혀 분류하지 않는다**
- [ ] **E2E-ND-20** (미구현) — `RC-A6`가 세 startup-invalid 축 중 **duplicate 하나만** 검증한다
- [ ] **E2E-ND-21** (결함) — `RL-D1`은 fanout이 아니라 **평범한 request 120개**다
- [ ] **E2E-ND-22** (미구현) — `RL-D4`가 `Error=5`와 `errorCode`/`errorMessage` wire를 검증하지 않는다
- [ ] **E2E-ND-23** (결함) — `RL-D5`가 수 분 soak가 아니라 **단발 Promise burst**다
- [ ] **E2E-ND-24** (**가짜 통과**) — `SF-B2`가 신규 outbound connect를 만들지 않아 **죽은 `storeFailureGrace`를 잡지 못한다**
- [ ] **E2E-ND-25** (결함) — `SF-D1`·`SF-D2`가 store stop/restart 대신 **pause/unpause**만 한다
- [ ] **E2E-ND-26** (미구현) — `SF-C2`가 draining marker·drain deadline·정상 종료를 검증하지 않는다
- [ ] **E2E-ND-27** (미구현) — `MON-A1`이 socket event의 **RemoteAddr·RoutingId를 단언하지 않는다**
- [ ] **E2E-ND-28** (**가짜 통과**) — `MON-A2`가 provider 추가·종료를 일으키지 않고 **기존 startup event만 기다린다**
- [ ] **E2E-ND-29** (미구현) — `MON-A4`가 failover 절반을 실행하지 않고 topology **payload 변화도 대조하지 않는다**
- [ ] **E2E-ND-30** (미구현) — Config 2·9의 P0에 **route-mesh-absent × separated-deployment** 조합이 없다
- [ ] **E2E-ND-31** (**가짜 통과**) — actor ref의 `generation > 0`을 **어느 config도 단언하지 않는다**
- [ ] **E2E-ND-32** (미구현) — `TA-A1`~`A3`가 bound-session snapshot과 **no-bind 비오염 negative**를 검증하지 않는다
- [ ] **E2E-ND-33** (미구현) — `TA-A4`가 actor destroy 뒤 `ActorRouteNotFound` 절반을 실행하지 않는다
- [ ] **E2E-ND-34** (결함) — `TA-B2`·`TA-B3`가 실제 owner 교체·route 단절 대신 **ActorRef 필드를 위조한다**
- [ ] **E2E-ND-35** (결함) — Config 10이 spot 생성마다 500ms sleep해 **수렴 직후 첫 요청 결함을 가린다**

### 가장 무거운 둘

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-01** | [e2e §2·§2.2·§2.4·§2.5](../../common/e2e/README.ko.md): 역할 서버 + 시나리오 ID당 클라이언트 파일 하나. **test-runner로 대체하지 않는다**. §5: e2e는 in-process contract test가 **아니다** | `e2e/ObservabilityOps/`에 **`run_e2e.sh`와 `feature-map.ko.md` 둘뿐이다.** `Server/`도 `Client/`도 `Shared/`도 없다. runner는 **폐기된 config-8(ATD)을 재실행**해 로그를 grep하고, **in-process contract test**를 돌린 뒤, 이렇게 통과시킨다 — `for scenario in OBS-A1 … ; do echo "$scenario … PASS"; done`. **`echo`가 검증이다.** 그리고 feature-map은 13개를 전부 "구현"으로 적는다. **[gaps §4.7]이 "OBS-A1~C5 evidence와 함께 통과했다"고 기록하고 있는데 — 거짓이다** |
| **E2E-ND-02** | [config-1 RM-A1(**P0**)](../../common/e2e/config-1-location-messaging.ko.md): live-owner peer row와 **두 provider로의 연결 상태**를 확인한다 | `RegistryMessaging/Server/LocationProbe/Endpoints/location-probe-endpoints.ts:24-28` — 모든 row를 `serviceRole: Router`, `state: Ready` **리터럴로** 매핑한다. 클라이언트는 `serviceRole === Router && state === Ready`를 단언한다. ⇒ **절대 실패할 수 없는 단언이다.** 살아 있는 검증은 `rows >= 2` 하나뿐. `ResilienceLifecycle/Server/TopologyProbe`도 `state: Ready`를 하드코딩한다. 게다가 이 probe 서버들은 **application 역할이 없어** §2.4가 금지하는 형태다 |
| **E2E-ND-11** | [e2e README §2.7:371-375](../../common/e2e/README.ko.md): 기본 실행은 **해당 config가 정의한 구현 시나리오**를 순차 실행한다. [config-2 §5:654-658](../../common/e2e/config-2-spot-service.ko.md): Track A~G가 계약 범위다 | `SpotService/Client/main.ts:110,112`가 문서에 없는 `SM-Q9`를 등록하고, `run_e2e.sh:158-166`이 `all`의 child group으로 **항상 실행**한다. `config-2` 문서 전체에 `SM-Q9`는 0건이다 |
| **E2E-ND-18** | [config-1 RM-B2:151-159](../../common/e2e/config-1-location-messaging.ko.md): 지속 request 중 B를 정상 종료하고, in-flight가 reply/public error로 끝나며 consumer가 A로 수렴해야 한다 | `RegistryMessaging/Client/Scenarios/rm-b2-scale-in-scenario.ts:16-21,34-48` — scale-in 전후 요청을 consumer가 아니라 **provider A HTTP endpoint에 직접** 보낸다. B 종료 뒤 1초 sleep 동안 요청은 0개다. 따라서 stale endpoint 반복 timeout과 pending 정리를 관측할 수 없다 |
| **E2E-ND-19** | [config-1 RM-C5:204-212](../../common/e2e/config-1-location-messaging.ko.md): request error reply와 observer `HandlerMissing`/`ReplyError`, send의 `HandlerMissing`/`Drop`을 구분한다. [RM-C8:230-238](../../common/e2e/config-1-location-messaging.ko.md): 한도 초과를 정해진 public error로 분류한다 | `RegistryMessaging/Client/Scenarios/rm-c5-missing-packet-scenario.ts:6-14`은 `failed` bool과 packet-name 포함만 보고 reason/action을 검사하지 않는다. `rm-c8-payload-round-trip-scenario.ts:23-27`도 oversized 결과의 `failed === true`만 본다. timeout·decode 오류·앱이 만든 bool도 모두 같은 성공이다 |
| **E2E-ND-20** | [config-4 RC-A6:103-111](../../common/e2e/config-4-registration-codec.ko.md): duplicate kind+packet, 잘못된 handler group, 미지원 channel kind 조합을 각각 startup에서 거부한다 | `RegistrationCodec/Client/Scenarios/InvalidRegistrationScenario.ts:5-13`과 `feature-map.ko.md:10`은 **duplicate registration 하나만** 만들고 검사한 뒤 RC-A6 전체를 `구현`으로 표시한다 |
| **E2E-ND-21** | [config-5 RL-D1:214-222](../../common/e2e/config-5-resilience-lifecycle.ko.md): 많은 subscriber/consumer에 높은 **fanout** 부하를 주고 누락·붕괴를 본다 | `ResilienceLifecycle/Client/Scenarios/rl-d1-high-fanout-scenario.ts:7-17`은 한 consumer HTTP endpoint에 channel request 120개를 병렬 전송할 뿐 publish/subscriber/fanout이 0건이다. evidence도 `:20-26`에서 두 provider 중 하나의 marker 하나만 요구한다 |
| **E2E-ND-22** | [config-5 RL-D4:244-252](../../common/e2e/config-5-resilience-lifecycle.ko.md): wire `message-kind=Error(5)`와 camelCase `errorCode`/`errorMessage`를 확인하고 성공 `Response(2)`와 구분한다 | `ResilienceLifecycle/Client/Scenarios/rl-d4-missing-request-handler-scenario.ts:7-19`은 앱 DTO의 `failed` bool과 server marker의 packet name만 본다. raw/error header·message kind·error code/message 검사가 모두 없다 |
| **E2E-ND-23** | [config-5 RL-D5:254-262](../../common/e2e/config-5-resilience-lifecycle.ko.md): 동시 N client가 request/send를 **수 분간 지속**하고 latency drift와 종료 후 pending/정리를 관측한다 | `ResilienceLifecycle/Client/Scenarios/rl-d5-mixed-burst-scenario.ts:7-32`은 request 60개와 send 60개를 한 번 `Promise.all`하고 marker 하나씩만 기다린다. 지속 시간·latency·drift·pending·resource 검사가 없다 |
| **E2E-ND-24** | [config-6 SF-B2:109-117](../../common/e2e/config-6-store-failure-recovery.ko.md): grace 초과 중 기존 연결은 유지하되, **장애 중 재시작한 provider 같은 새 outbound connect는 중단**돼야 한다 | `DiscoveryRegistryHa/run_e2e.sh:214-219`은 topology를 전부 띄운 뒤 Redis만 제거한다. `Client/Scenarios/SfB2StoreFailureGraceScenario.ts:12-24`도 이미 연결된 A/B로 request를 반복할 뿐 새 provider를 시작하지 않는다. 따라서 `IMP-ND-31`처럼 `storeFailureGrace`를 아예 읽지 않아도 결과가 같다 |
| **E2E-ND-25** | [config-6 SF-D1:150-158](../../common/e2e/config-6-store-failure-recovery.ko.md)·[SF-D2:160-168](../../common/e2e/config-6-store-failure-recovery.ko.md): store를 정지했다 **재기동**하고 재등록→heartbeat 유예→빠진 target disconnect 순서를 검증한다 | `DiscoveryRegistryHa/run_e2e.sh:236-264`은 D1·D2 모두 같은 Redis container를 `docker pause`/`unpause`할 뿐 stop/restart하지 않는다. D2 client는 `SfD2LongOutageRecoveryScenario.ts:35-58`에서 실패를 삼키고 성공 간격을 **6초까지 허용**한다. 빈 store 재시작과 전 구간 성공·연결 보존을 검증하지 못한다. 별도 D3만 `run_e2e.sh:267-279`에서 container를 재생성한다 |
| **E2E-ND-26** | [config-6 SF-C2:131-146](../../common/e2e/config-6-store-failure-recovery.ko.md): `Draining=true` 게시, 신규 배정 제외, 30초 deadline 안 정상 종료, terminal 직후 row 제거를 crash/lease 만료와 구분한다 | `DiscoveryRegistryHa/run_e2e.sh:229-233`은 `/shutdown` 호출 직후 client를 실행하고, `Client/Scenarios/SfC2GracefulShutdownScenario.ts:11-23`은 row 부재와 A reply만 본다. draining row·신규 배정·process exit/deadline·강제 종료 여부를 한 번도 읽지 않는다 |
| **E2E-ND-27** | [config-7 MON-A1:57-65](../../common/e2e/config-7-monitoring.ko.md): 연결/해제 kind뿐 아니라 source name과 payload `RemoteAddr`, 있으면 `RoutingId`까지 확인한다 | `RuntimeMonitoring/Client/Scenarios/mon-a1-socket-events-scenario.ts:12-29`은 source와 connected/disconnected kind만 찾는다. `RemoteAddr`와 `RoutingId` 문자열은 시나리오 전체에 0건이다 |
| **E2E-ND-28** | [config-7 MON-A2:67-75](../../common/e2e/config-7-monitoring.ko.md): `svc-b`를 **추가/종료**해 peer row를 바꾸고, 살아 있는 `svc-a`의 projection payload가 실제 diff를 반영하는지 본다 | `RuntimeMonitoring/Client/Scenarios/mon-a2-location-runtime-events-scenario.ts:6-26`은 상태 변경 동작 없이 기존 evidence에서 `TopologyChanged`/`ServiceSummaryChanged`와 count nonzero를 기다린다. startup 때 event 하나만 있어도 통과하며 add/stop diff·before/after payload가 없다 |
| **E2E-ND-29** | [config-7 MON-A4:87-95](../../common/e2e/config-7-monitoring.ko.md): (a) 같은 rid·다른 endpoint failover와 (b) drain/restore를 모두 일으키고 socket/location projection 전이를 확인한다 | `RuntimeMonitoring/Client/Scenarios/mon-a4-availability-transition-scenario.ts:7-44`은 drain/restore만 실행한다. failover는 0건이고, topology 단언도 `TopologyChanged` line 수가 `>= 2`인지 볼 뿐 각 line의 peer endpoint/상태가 전후 동작과 일치하는지 비교하지 않는다 |
| **E2E-ND-30** | [e2e README §3.1:487-497,546-547](../../common/e2e/README.ko.md): Config 2·9 P0은 **route mesh 없음 × session/spot 분리 배치**를 우선 적용한다 | Config 2 remote session relay는 `SpotService/Server/Session/session-host-factory.ts:35-45`와 `Server/Play/play-host-factory.ts:69-85`에서 route mesh를 항상 등록한다. route-mesh 없는 `SM-F6`은 `SpotService/Client/Scenarios/sm-f6-scenario.ts:15-56`처럼 session relay 자체가 없다. Config 9도 Actor/Caller/Session 모두 `ToActorMessaging/run_e2e.sh:123-155`의 router endpoint를 받아 route mesh로만 기동한다 |
| **E2E-ND-31** | [e2e README §3.1:514-519](../../common/e2e/README.ko.md): 응답 actor ref는 node rid가 비어 있지 않고 **generation > 0**인 concrete snapshot이어야 한다 | Node E2E client의 generation 비교는 값 보존 또는 존재만 본다. 대표적으로 `SpotService/Client/Scenarios/sm-d15-scenario.ts:30-55`는 `generation !== undefined`, `ToActorMessaging/Client/main.ts:143-160`은 bind reply와 입력의 동등성만 확인한다. `generation > 0` 단언은 전체 `e2e/*/Client`에 0건이라 양쪽이 0이어도 통과한다 |
| **E2E-ND-32** | [config-9 TA-A1:69-77](../../common/e2e/config-9-to-actor-messaging.ko.md)·[TA-A2:79-87](../../common/e2e/config-9-to-actor-messaging.ko.md)·[TA-A3:89-97](../../common/e2e/config-9-to-actor-messaging.ko.md): no-bind 전후 bound-session snapshot과 session gateway/client의 bind·push **부재 evidence**까지 대조한다 | `ToActorMessaging/Client/main.ts:54-101`은 send/request/push positive와 actor handler evidence만 본다. bound-session snapshot을 조회하는 호출이 없고, A2의 session/client negative와 A3의 bind 전 snapshot·no-bind-created marker 부재도 단언하지 않는다 |
| **E2E-ND-33** | [config-9 TA-A4:99-107](../../common/e2e/config-9-to-actor-messaging.ko.md): disconnect 뒤 생존 actor 성공에 이어 actor를 destroy하고 같은 ref가 `ActorRouteNotFound`, 새 handler evidence 0인지 확인한다 | `ToActorMessaging/Client/main.ts:104-113`은 connector close 뒤 send/request 성공까지만 실행하고 끝난다. destroy 호출·destroy 뒤 request·부재 error·negative evidence가 모두 없다 |
| **E2E-ND-34** | [config-9 TA-B2:121-129](../../common/e2e/config-9-to-actor-messaging.ko.md): owner 교체/generation 변경 뒤 old ref 실패와 새 live ref 성공을 본다. [TA-B3:131-139](../../common/e2e/config-9-to-actor-messaging.ko.md): 실제 route 단절 뒤 `RouteNotConnected`, 복구 뒤 같은 ref 성공을 본다 | `ToActorMessaging/Client/main.ts:122-140`은 owner를 교체하지 않고 generation에 `+1`, route를 끊지 않고 `nodeRid='to-actor-missing-route'`로 **입력 DTO를 위조**한다. 새 live ref 성공과 route 복구 follow-up도 없다 |
| **E2E-ND-35** | [e2e README §3.1:527-529](../../common/e2e/README.ko.md): location 발견·dial 수렴 직후 settle delay 없이 첫 요청을 보내며 retry/sleep으로 가리지 않는다 | `SpotActorTransfer/Client/main.ts:685-690`의 공용 `createSpot()`은 모든 spot 생성 뒤 **무조건 500ms sleep**하고, 그 뒤 remote join/request를 보낸다. 첫 요청 수렴 race가 재현될 창을 스스로 닫는다 |

### e2e 구조·규약 이탈

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-03** | [e2e README §2.2:236](../../common/e2e/README.ko.md): `Client/Scenarios/<ScenarioId><Name>Scenario.*` — **시나리오 ID 하나마다 파일 하나**. [§2.5:310,328-332](../../common/e2e/README.ko.md): 여러 시나리오를 하나의 `AllScenario`·`ScenarioSet`으로 묶지 않는다 | `ToActorMessaging/Client/main.ts:29-48` — 7개 시나리오(`TA-A1`~`TA-B3`)가 한 파일의 `Record<string, fn>` 테이블과 `runTaA1`~`runTaB3` 함수로 들어 있다(276줄). `SpotActorTransfer/Client/main.ts`는 20개 시나리오가 든 **805줄 한 파일**이다. 두 config 모두 `Client/Scenarios/`도 `Client/Support/`도 없다 — 나머지 9개 config는 갖고 있어 **같은 언어 안에서 client 형태가 갈린다** |
| **E2E-ND-04** | [e2e README §2.1:158-169](../../common/e2e/README.ko.md): readiness **3초** / poll 0.1초 / route settle **5초** / scenario settle **3초** / HTTP probe 3초를 runner 상단의 명시적 상수로 둔다. "이 값 안에 준비되지 않는 로컬 e2e는 대기 시간을 늘려서 통과시키지 않는다" | route settle·scenario settle 상수는 **e2e 트리 전체에 0건**이다. `SpotService/run_e2e.sh:13-16`은 `LOCAL_READINESS_TIMEOUT_SECONDS=3`을 선언해 두고 실제 루프는 `ATTEMPTS=600 × POLL=0.1` = **60초**를 기다린다(`:461-469`). 그 3초 상수를 **읽는 곳은 어디에도 없다**(7개 runner에서 대입만 있다). `AutomaticTurnDispatch/run_e2e.sh:24-27`은 15초, `ToActorMessaging/run_e2e.sh:19-24`는 이름 있는 상수 없이 `seq 1 120` + `sleep 0.25` = 30초, `SpotActorTransfer/run_e2e.sh:22-32`는 40초에 더해 `:141`이 노드마다 **무조건 `sleep 2`**를 건다. ⇒ 긴 대기가 수렴 실패를 그대로 가린다 |
| **E2E-ND-05** | [e2e README §2.7:359-363](../../common/e2e/README.ko.md): **필수 격리 규칙** — 실행마다 전용 Docker Redis container를 새로 만든다. host Redis나 다른 실행의 endpoint를 공유·fallback으로 쓰면 안 된다. [:393-394](../../common/e2e/README.ko.md): Docker Redis를 만들지 못하면 **즉시 실패**한다 | `SpotService/run_e2e.sh:192-199` — `REDIS_ENDPOINT="${ZLINK_REDIS_E2E_ENDPOINT:-}"`가 비어 있을 때**만** container를 만든다. 이 환경변수가 있으면 docker 검사조차 건너뛰고 외부 endpoint를 그대로 쓴다. 오류 메시지(`:195`)가 `"Docker is required ... unless ZLINK_REDIS_E2E_ENDPOINT is set."`로 **탈출구를 안내**한다. ⇒ 이 config의 pause/flush/cleanup이 다른 실행과 같은 인스턴스를 때릴 수 있다 |
| **E2E-ND-06** | [e2e README §2.6:342-344](../../common/e2e/README.ko.md): endpoint·Redis·routing id·timeout·로그/evidence 경로를 환경 변수로 전달하지 않으며, **server와 client 애플리케이션 코드에서 직접 사용할 수 있는 환경 변수는 0개다** | role option parser 네 곳이 CLI로 받은 rid를 곧바로 프로세스 전역에 쓴다 — `SpotService/Server/MultiNode/Configuration/multi-node-options.ts:28`, `Server/Session/Configuration/session-options.ts:29`, `Server/Play/Configuration/play-options.ts:28`, `Server/Gateway/gateway-host-factory.ts:160`이 전부 `process.env.ZLINK_E2E_RID = rid`. evidence store가 그 전역값을 되읽는다(`e2e/evidence-store.js:7,10`). `SpotService/evidence-store.ts:7`은 `process.env.ZLINK_NODE_E2E_ROOT`를 읽고, `RuntimeMonitoring/Client/Scenarios/mon-d1-failure-recovery-scenario.ts:67`은 자식 프로세스에 `ZLINK_E2E_RID`를 심는다. ⇒ routing id가 검증된 CLI 계약이 아니라 **전역 상태로 흐른다** |
| **E2E-ND-07** | [e2e README §2.5:314-316](../../common/e2e/README.ko.md): client는 server app endpoint를 **언어별 HTTP client wrapper**로 호출한다 | `@zlink-systems/http-client` 참조가 `e2e/` 전체에 **0건**이다(같은 저장소의 samples는 6건 쓴다). 대신 config마다 `Client/Support/http-client.ts`를 손으로 만들어 전역 `fetch`를 감싼다 — `RegistryMessaging/Client/Support/http-client.ts:1-19`의 `getJson`/`postJson`에는 **timeout이 없다**. §2.1의 3초 HTTP probe 상한을 강제할 지점이 아예 없고, `DiscoveryRegistryHa/Client/Support/http-client.ts:9-19`만 별도 `getJsonWithin`으로 `AbortController`를 붙인다. `.NET`은 같은 자리에서 `ZLinkHttpClient`를 쓴다(`dotnet/e2e/LocationMessaging/Client/Support/DynamicClusterLauncher.cs:163`) |
| **E2E-ND-08** | [e2e README §2.5:311](../../common/e2e/README.ko.md): **각 scenario 파일 첫머리에** 그 시나리오가 무엇을 검증하는지 적는다. [§2.9:450-451](../../common/e2e/README.ko.md): 독자가 파일을 열었을 때 "이 시나리오가 왜 필요한가"를 바로 알 수 있어야 한다 | `e2e/*/Client/Scenarios/*.ts` **138개 중 첫 줄이 주석인 파일은 0개**다 — 전부 `import`로 시작한다(예: `SpotService/Client/Scenarios/sm-f6-scenario.ts:1`). 파일 어딘가에 주석이라도 있는 건 15개뿐이다. ⇒ 시나리오 ID를 계약 문서에 잇는 단서가 **파일 이름 하나뿐**이라, 시나리오가 약해져도 무엇이 빠졌는지 파일 안에서 드러나지 않는다 |
| **E2E-ND-09** | [e2e README §2.2:220-221,244-245](../../common/e2e/README.ko.md): config를 `.NET`과 같은 독립 실행 배포 묶음으로 옮기고 역할 경계와 파일 분류를 유지한다. [§3:468,473](../../common/e2e/README.ko.md): config 1 = Location messaging, config 6 = Store 장애·복구 | (a) `e2e/DiscoveryRegistryHa/Server/Probe/`에는 **`dist/`만 있고 source가 없다** — 삭제된 역할 서버의 컴파일 산출물이 그대로 남아 다음 빌드에서도 살아 있다. (b) `e2e/ToActorMessaging/Client/dist/`에는 `Client/`·`Shared/`·`ToActorMessaging/` **세 세대의 출력이 겹쳐** 있다. (c) 디렉토리 이름이 config 문서·`.NET`과 어긋난다 — `RegistryMessaging`(config-1, `.NET`은 `LocationMessaging`), `DiscoveryRegistryHa`(config-6, `.NET`·Java는 `StoreFailure`). (d) 로그 디렉토리도 `SpotActorTransfer/run_e2e.sh:18`만 `log/`, 나머지 10개는 `logs/`다 |
| **E2E-ND-10** | [e2e README §3.1:487-493](../../common/e2e/README.ko.md): 기동 순서 축은 **Config 1·2·9**에 적용한다. [:504-506](../../common/e2e/README.ko.md): config 러너는 기동 순서를 인자로 받고, 축 변형은 **역방향 1회 + 고정 seed shuffle 1회**를 최소로 돌린다 | 기동 순서를 읽는 runner는 둘뿐이고 전달 방식도 다르다 — `SpotService/run_e2e.sh:12,103-126`은 환경변수 `E2E_START_ORDER`, `ToActorMessaging/run_e2e.sh:10,101,171`은 위치 인자 `$2`다. **config-1인 `RegistryMessaging`에는 없다.** 그리고 통합 게이트 `run_e2e_all.sh:83`은 `./run_e2e.sh "${scenario}"`만 호출하고 `E2E_START_ORDER`를 설정하지도, 두 번째 인자를 넘기지도 않는다. ⇒ 축을 구현한 두 config조차 기본 실행에서는 **forward만 돈다.** reverse/shuffle을 호출하는 곳은 트리 전체에 0건이다 |

### 샘플 — 정본 흐름 결손과 구조 이탈

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-ND-01** (미구현) | [bingo §7.1:452-464](../../common/sample/bingo/README.ko.md): `BingoRoom.OnJoinedActor`는 Api 서버에 `GetPlayerRecordReq`를, `OnLeaveActor`는 `ReportBingoResultReq`를 보내고 **`yield`로 기다린다**. [:846-847](../../common/sample/bingo/README.ko.md): `PlayerJoinedNotify`의 `Wins`·`Losses`는 그 `GetPlayerRecordRes` 값을 그대로 담는다. [:570-571](../../common/sample/bingo/README.ko.md): client 게이트가 그 값을 확인한다 | 세 층 모두 없다. **계약** — `Shared/Contracts/bingo-messages.generated.ts:283-294`의 `PlayerJoinedNotify`에 `wins`/`losses`가 없고 `GetPlayerRecordReq`/`ReportBingoResultReq` 타입 자체가 트리에 0건이다. **서버** — `Server/Api/Handlers/`에는 `authenticate-player-handler.ts`와 `match-bingo-handler.ts` 둘뿐이고, `bingo-room-spot.ts:131-157`의 `onJoinedActor`는 외부 왕복 없이 바로 notify를 만든다(`:160-163`의 `onLeaveActor`는 로그 한 줄). `yield` grep 0건. **클라이언트** — 전적 단언이 없다. ⇒ 이 샘플이 `yield` terminator를 보여 주려던 **유일한 정본 경로가 통째로 빠져 있다** |
| **SMP-ND-02** (결함) | [31 §10.2:484-489](../server/31-session-actor-dispatch.ko.md): **session context는 packet handler registry를 갖는다.** session은 `Configure()`에서 `Context.Handlers`에 handler를 등록하고, dispatch callback은 그 registry로 분기한다. 미등록 packet이면 그 호출이 실패를 반환한다 | 표면은 있다 — `packages/framework/src/contracts/Streams/IZLinkSession.ts:31`의 `handlers: ZLinkSessionHandlerRegistry`, `IZLinkSessionPacketHandler.ts:8-11`의 `addHandler`/`tryHandle`. **6개 샘플 중 이를 호출하는 곳은 0건**이다. 전부 `onDispatch`에서 `dispatch.packetName` 문자열을 손으로 비교한다(트리 전체 22건) — `Bingo.Ts/.../bingo-session.ts:24-46`, `SupportChat.Ts/.../supportchat-session.ts:43`, `DeliveryDispatch.Ts/.../customer-session.ts:33`·`courier-session.ts:30`, `GameQuest.Ts/.../game-api-session.ts:34`, `TicTacToe.Ts/.../play-session.ts:55`. `bingo-session.ts:33`은 `payload.decode<AuthenticateReq>(Object as never)`로 **typed 경계까지 우회**한다. ⇒ 중복 등록 검출도, 미등록 packet의 관측 가능한 실패도 없다 |
| **SMP-ND-03** (결함) | [deliverydispatch §6:225,231-236](../../common/sample/deliverydispatch/README.ko.md): `delivery-couriers`는 **Spot mesh**다. offer는 배치 정책이 고른 노드의 `CourierEntrySpot` **`SpotHandle`**로 보내며, "application이 route mesh channel에 **node rid를 찍어 보내는 표면은 이 샘플에서 쓰지 않는다**" | `Server/DispatchCenter/dispatch-center-module.ts:43`과 `Server/Courier/courier-module.ts:54`가 `delivery-couriers`를 **`addRouteMeshChannel(...)`**로 등록한다. `Server/DispatchCenter/dispatch-worker.ts:80-84`가 `routes.sendToNode(courierActorNodeRouteChannel, courierActorNodeRid(courierId), offer)`로 offer를 보내고, `:128-137`의 actor ensure도 같은 `requestToNode`다. `Shared/Configuration/sample-names.ts:20-28`의 `courierActorNodeRid`는 courier id를 `courier-node-1`/`courier-node-2`로 **하드코딩 매핑**한다. spot handle resolver는 같은 트리의 SupportChat이 이미 쓴다(`SupportChat.Ts/.../allocate-conversation-handler.ts:45-48`) — **표면이 없어서가 아니다** |
| **SMP-ND-04** (결함) | [tictactoe:18-21,33-34,129](../../common/sample/tictactoe/README.ko.md): Redis 위치 저장소는 **framework의 public spot remote address resolver 계약 뒤에 숨긴다.** actor가 `JoinSpot(roomId)`를 쓰면 Redis-backed resolver가 owner SpotNode route를 돌려주고, 다른 Play의 actor는 **location store에서 얻은 `SpotHandle`**로 room Spot을 가리킨다 | `Server/Configuration/redis-room-route-store.ts:9-15,22-30` — 앱이 직접 redis client를 열어 `<prefix>tictactoe:rooms:<roomId>` hash에 `RouteChannelId`·`OwnerNodeRid`·`SpotRid`·`SpotKind`를 쓴다. **framework redis store가 spot row에 쓰는 필드 이름과 같은데**(`packages/framework-locations-redis/src/redis-row-codec.ts:118-121,185`) `SpotKind`만 framework의 wire 정수(`zlinkSpotKindToWire`) 대신 문자열 `'User'`다. `tictactoe-game-room-provisioner.ts:18-38`은 spot을 만든 뒤 이 row를 쓰고 **자기가 쓴 값을 다시 읽어 비교**한다. **그 row를 라우팅에 읽는 곳은 0건**이고(실제 resolve는 `createTicTacToeLocationStore`의 framework store가 한다), `.NET` TicTacToe에는 이런 store가 아예 없다. ⇒ framework location row와 조용히 갈라질 수 있는 사본이 하나 더 있다 |
| **SMP-ND-05** (결함) | [tictactoe:814](../../common/sample/tictactoe/README.ko.md): **첫 actor가 join할 때는 self-join notify를 보내지 않는다** | `Client/tictactoe-client-scenario.ts:226-244`의 `expectNoMessage`가 `waitFor(...).timeout(25)` — **25ms만** 듣고 timeout이면 성공으로 친다. 게다가 호출 지점(`:101-106`, `:124-129`)이 `await client1.request(joinGameReq(...))`가 **이미 반환한 뒤**라, 감시 창이 join 왕복 자체를 덮지도 않는다. 같은 트리의 Bingo는 `bingo-client-scenario.ts:260-291`의 `whileNoMessage`로 작업 **전에** 감시를 걸고 작업이 끝날 때까지 유지한다. ⇒ self-join notify가 25ms보다 늦게(원격 hop·부하) 도착하면 **그대로 통과한다** |

## 라운드 5 — 샘플 · e2e 심층

**Node에는 좋은 소식이 하나 있다.** **Bingo가 C++ 버그 전부에 대해 깨끗하다** — 시작 notify를
제외 필터 없이 **전원에게** 보내고, 카드 재제출을 거부하고, 관전 종료에 진짜 멤버십 가드가 있고,
방을 **실제로 닫고 타이머를 취소한다.** 그리고 **클라이언트 게이트가 두 player 모두** 시작 notify를
기다리고 둘 다 `Running`을 단언한다 — **C++ 버그를 가렸던 약한 게이트가 여기엔 없다.**
`DeliveryDispatch`는 배송 상태 **도착 순서를 진짜로 단언한다**(`.NET`·C++은 못 한다).

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-ND-06** (**버그**) | [supportchat §11](../../common/sample/supportchat/README.ko.md): `OpenConversationRes.State`는 8필드 `ConversationState`다 | `support-entry-handlers.ts:58-67` — **채널 응답에서 `conversationId`만 가져오고 나머지를 손으로 지어낸다**: `status: WaitingForAgent`(하드코딩), `subject`는 **요청자가 보낸 값을 에코**, `lastMessageSeq: 0`. 도메인이 낸 진짜 status는 **버린다** — 그 시점엔 이미 **상담원이 배정돼 `Active`**인데. ⇒ 상담원이 붙었는데 고객은 **"대기 중"이라고 듣는다.** 그리고 클라이언트 단언 **5개가 하드코딩된 리터럴을 검사한다 — 실패할 수 없다** |
| **SMP-ND-07** (**버그**) | [25 §8](../server/25-stage-wrapper-on-spot.ko.md): spot 종료 뒤 추가 callback을 만들지 않는다 | SupportChat이 대화마다 **50ms 타이머**를 걸고 `context.close()`를 **한 번도 부르지 않는다**(grep 0건). 대화가 `Closed`가 돼도 타이머는 **초당 20회 영원히 돈다.** ⇒ 대화 1,000건을 처리한 서버가 **초당 2만 번 헛돌고** 죽은 Spot 1,000개를 프로세스 수명 내내 붙든다. TicTacToe도 같다(1초 주기) |
| **SMP-ND-08** (**버그**) | [supportchat §13](../../common/sample/supportchat/README.ko.md): `WaitingForClose → Active`로 가는 **유일한 입력은 새 `SendChatMessageReq`**다 | `conversation.ts:74-81` — **상담원이 재접속해 re-join하면** 상태 가드 없이 `Active`로 되돌리고 **close 기한을 지운다.** ⇒ 대화가 idle로 넘어가 양쪽이 알림을 받은 뒤 상담원 스트림이 끊겼다 붙으면, 방이 **조용히 되살아나고 `ConversationClosedNotify`가 영영 안 온다.** 고객은 "곧 종료됩니다"에 **영원히 갇힌다** |
| **SMP-ND-09** (**버그**) | [tictactoe §9](../../common/sample/tictactoe/README.ko.md): 승리는 **라인 완성**이다. timeout은 승리 조건이 아니다 | `tictactoe-match.ts:99-103` — turn timeout 시 **상대를 승자로 만들고**, `lastMoveActorId`를 **수를 두지 않은 쪽**으로, `lastMoveCell`을 `null`로 세팅한다. 게다가 `publishWinMilestone`은 `status === Won`일 때만 도는데 **`TurnTimedOut`은 거기 도달할 수 없다.** ⇒ 99승인 host가 **timeout으로 100승을 채우면 milestone이 영영 발행되지 않고**, 두 클라이언트는 **수를 두지도 않은 player가 `null` 칸에 뒀다**고 렌더한다 |
| **SMP-ND-10** (**버그**) | [tictactoe §7](../../common/sample/tictactoe/README.ko.md): `PlayActorObserveMilestoneHandler`를 `EntrySpot/Handlers/`에 둔다 | **그 파일이 없다.** 대신 `play-session-factory.ts:30`이 **`new PlayEntrySpot(...)`** — framework lifecycle 밖에서 만든 Spot이라 `context`가 **영영 할당되지 않는다.** 세션이 **packet-name switch**로 그 고아 객체의 메서드를 부른다. ⇒ `observeMilestone`에 `this.context.*`를 **한 줄만 추가해도** 모든 `ObserveMilestoneReq`가 **TypeError로 죽는다** |
| **SMP-ND-11** (결함) | [공통 샘플 §공통 작성 원칙:313-325](../../common/sample/README.ko.md): 모든 wire payload는 **이름 있는 계약**으로 두고, 호출 지점의 inline object literal과 흩어진 packet-name 문자열을 금지한다 | 세 샘플이 호출 지점에서 응답 객체를 직접 만든다 — `SupportChat/.../supportchat-session.ts:95-100`, `DeliveryDispatch/.../customer-session.ts:62`, `GameQuest/.../game-api-session.ts:46`. packet 이름도 `SupportChat/.../conversation-actor-handlers.ts:28`과 `TicTacToe/.../play-actor-{join-game,leave-game,place-mark}-handler.ts:19-20`에 문자열로 흩어져 있다. 타입 검사는 일부 `satisfies`에만 걸리고 **wire 이름과 payload 계약을 한 선언에서 고정하지 못한다** |
| **SMP-ND-12** (결함) | [bingo client 12단계:588-594](../../common/sample/bingo/README.ko.md)·[tictactoe client 12단계:558-564](../../common/sample/tictactoe/README.ko.md): 세 client의 inbound observer marker와 필수 필드를 **release gate가 확인**한다 | Bingo는 `Client/main.ts:35-40`, TicTacToe는 `tictactoe-client-scenario.ts:261-267`에서 marker를 **출력만** 한다. 각 scenario는 각각 `bingo-client-scenario.ts:228-234`, `tictactoe-client-scenario.ts:205-211`에서 끝나며 marker 존재·필드 단언이 없다. 공용 runner도 browser 성공만 기다린다(`run-sample.mjs:139-146,227-231`) ⇒ observer를 제거하거나 필드를 비워도 샘플은 통과한다 |
| **SMP-ND-13** (미구현) | [bingo lifecycle gate:1116-1131](../../common/sample/bingo/README.ko.md)·[tictactoe lifecycle gate:945-957](../../common/sample/tictactoe/README.ko.md): room leave·Entry Spot destroy·추가 lifecycle callback 부재를 **server-side evidence로 검증**한다 | 공용 runner의 Bingo 경로(`run-sample.mjs:107-146`)와 TicTacToe 경로(`:177-231`)는 서버를 띄우고 browser client만 실행한다. `destroyActor`·room `onLeaveActor`·추가 callback 부재를 읽는 단언이 0건이다. actor destroy 연결을 끊어도 client가 `LeaveGameReq`를 submit한 직후 성공 종료하므로 release gate는 계속 초록이다 |
| **SMP-ND-14** (결함) | [shoppingmall scale-out:981-982](../../common/sample/event/shoppingmall.ko.md): 주문 A/B를 서로 다른 owner에서 **동시에 처리**하고 어느 API에서도 같은 조회 모델을 확인한다 | `shoppingmall-client-scenario.ts:124-129`이 A 시작 응답을 **await한 뒤** B 시작을 보낸다. `Promise.all`은 이미 시작이 끝난 두 주문의 상태 조회에만 쓴다(`:130-133`). owner 직렬화나 전역 락으로 두 주문을 순차 처리해도 통과한다 |
| **SMP-ND-15** (결함) | [gamequest scale-out:604-608](../../common/sample/event/gamequest.ko.md): 2노드에서 PlayerA/B가 **서로 다른 owner**에서 동시에 처리되는지 확인한다 | `gamequest-client-scenario.ts:73-90`은 두 stream request를 동시에 보내지만 owner identity를 읽거나 비교하지 않은 채 `gamequest-concurrent-owners=completed`를 출력한다. 최종 server assertion도 reward/source event와 marker 존재만 검사하고 owner 상이성은 보지 않는다(`quest-progress-store.ts:195-203`). 두 player가 같은 owner에 배치돼도 통과한다 |
| **SMP-ND-16** (결함) | [deliverydispatch 메시지 계약:324-328](../../common/sample/deliverydispatch/README.ko.md): `DeliveryStatusChangedReq`는 `DeliveryId`·`Status`·`CourierId`·`OccurredAt` 네 필드다. 고객 식별자는 다음 hop의 `DeliveryStatusUpdatedMsg`에만 있다 | `Shared/Contracts/messages.ts:127-134`가 `DeliveryStatusChangedReq`에 **`customerId`를 추가**하고, Tracking handler가 그 비계약 필드로 actor를 resolve한다(`tracking-handlers.ts:24-37`). 같은 packet을 계약대로 쓰는 peer는 고객 actor를 찾을 수 없고, Node가 보낸 payload에는 문서에 없는 필드가 실린다 |
| **SMP-ND-17** (미구현) | [tictactoe 내부 join 계약:664-675](../../common/sample/tictactoe/README.ko.md): room Spot join reply는 별도 `TicTacToeGameJoinRes { State }`다 | `Shared/Contracts/messages.ts:118-125`에는 client-facing `JoinGameRes`와 `TicTacToeGameJoinReq`만 있고 **`TicTacToeGameJoinRes`가 없다**. room Spot도 내부 join reply를 `JoinGameRes`로 반환한다(`tictactoe-game-spot.ts:182-203`). `.NET`은 정식 타입으로 encode/decode하므로(`dotnet/samples/TicTacToe/Shared/Contracts/Messages.cs:60`) packet 계약을 이름으로 맞추는 교차 언어 흐름이 갈라진다 |
| **SMP-ND-18** (**wire 파손**) | [tictactoe `GameState`:713-726](../../common/sample/tictactoe/README.ko.md): `NextTurn`은 non-null `string`이고 terminal nullable 필드 집합에 포함되지 않는다 | `Shared/Contracts/messages.ts:170-180`은 `nextTurn: string | null`, `tictactoe-match.ts:47-51,130-143`은 terminal state에서 실제로 `null`을 wire에 싣는다. `.NET`·C++ 계약 타입은 non-null string(`dotnet/.../Messages.cs:104`, `cpp/.../messages.hpp:146`)이라 terminal state의 표현이 Node에서만 갈라진다 |
| **SMP-ND-19** (**wire 파손**) | [shoppingmall 메시지 계약:735-763,794-797](../../common/sample/event/shoppingmall.ko.md): 주문·결제의 `Amount`와 조회 상태의 `Amount`는 **decimal**이다 | `Shared/Contracts/messages.ts:26-36,65-75`가 둘 다 JavaScript `number`로 선언한다. `number`는 이진 부동소수라 decimal 금액을 보존하지 못하며, 큰 값이나 소수 금액은 `.NET decimal` peer가 보낸 값을 decode→encode하는 순간 달라질 수 있다 |
| **SMP-ND-20** (결함) | [supportchat client 8~17단계:1051-1061](../../common/sample/supportchat/README.ko.md): participant join과 두 방의 message response/push를 **상태와 conversation별 의미 값으로 검증**한다 | `supportchat-client-scenario.ts:57-72`는 첫 `ParticipantJoinedNotify`에서 actor id만, chat response/push에서 `MessageSeq`만 본다. `ConversationId`·sender·text와 join push의 `Active` state를 단언하지 않는다. 두 번째 방도 `:82-91`에서 conversation id 또는 sequence 하나만 본다. 잘못된 방의 payload나 요청 text를 에코하지 않는 push도 같은 sequence만 맞으면 통과한다 |

### 실패할 수 없는 e2e 게이트

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-12** (**가짜 통과**) | [config-3 PS-A1(**P0**)](../../common/e2e/config-3-pubsub.ko.md): fanout 전달을 **순서대로** 확인한다 | warm-up이 **`seq 1..120`을 같은 `runId`·같은 topic**으로 발행한 뒤, 측정 구간이 **`seq 100..111`** — **warm-up 범위 안에 통째로 들어 있다.** 판정기는 `runId`·topic·seq 범위로만 거르고 **`value`를 읽지 않는다**(`warmup-100`과 `measure-100`을 구분하는 유일한 필드다). ⇒ **측정 발행을 전부 지워도, 그 시점에 fanout이 완전히 깨져도 통과한다.** 게다가 12개 중 `>= 3`이고 **순서 검사가 없다** |
| **E2E-ND-13** (**가짜 통과**) | [config-1 RM-C9](../../common/e2e/config-1-location-messaging.ko.md): 송신 큐를 **HWM까지 채운다** | `consumer-endpoints.ts:107-112` — `submitProfileUnderPressure`가 `.submit()`을 **await하지도 확인하지도 않고** `return 'Submitted'` 한다. 클라이언트는 `outcomes.every(o => o === 'Submitted')`를 단언한다. ⇒ **문자열 리터럴이 성공 판정기다. 전송이 전부 실패해도 통과한다** |
| **E2E-ND-14** (**미구현**) | [config-3 §2](../../common/e2e/config-3-pubsub.ko.md): **모든 노드에 Redis location store**를 두고 peer row를 framework가 관리한다 | `publisher-host-factory.ts:35`·`subscriber-host-factory.ts:48` — **`useInMemoryLocationStores()`**다. subscriber는 `--publisher-endpoint`로 **하드와이어**돼 있고 `run_e2e.sh`에 **"redis"가 0건**이다. ⇒ 이 config의 존재 이유인 **store 기반 fanout이 한 번도 실행되지 않는다.** feature-map에 기록 없음 |
| **E2E-ND-15** (**가짜 통과**) | [config-9 TA-B1(**P0**)](../../common/e2e/config-9-to-actor-messaging.ko.md): **형식은 맞지만 stale한 ref**를 넘긴다 | 클라이언트가 `actor`를 **아예 안 넘기고**, caller의 `requireActorRef`가 `request.actor === undefined`에 **자기가 예외를 던진다** — `sendToActor`에 **도달하지 않는다.** ⇒ framework의 actor-route 분류를 **통째로 지워도 통과한다** |
| **E2E-ND-16** (**가짜 통과**) | [config-10 ST-F1·ST-F3(**둘 다 P0**)](../../common/e2e/config-10-spot-actor-transfer.ko.md): `P1 → P2 → P3` 순서를 단언한다 | `assertOrder`가 **`entry.kind`만 비교하고 `entry.value`를 안 읽는다.** ST-F1은 `['packet_handler','packet_handler','packet_handler']` — **같은 kind 셋**이라 **어떤 순열이든 통과한다.** `P1/P2/P3`는 `value`에 있다. ⇒ **"3개가 도착했다"로 퇴화한다.** 필수 marker `source_cleanup`은 **트리 전체에 0건** |
| **E2E-ND-17** (**가짜 통과**) | [config-1 RM-A4(**P0**)](../../common/e2e/config-1-location-messaging.ko.md): consumer **재시작 없이** peer handover를 확인한다 | 교체 후 요청을 **replacement 프로세스 자신의 HTTP**로 보낸다. p1을 resolve했던 클라이언트가 **하나도 살아남지 않아** handover 경로가 **구조적으로 관측 불가능**하다. `v1Count === 0` 단언도 죽은 프로세스의 연결 실패를 삼키고 `[]`를 반환해 **항상 참**이다 |

**Config 8이 Node에 없다** — `TD-*` grep 0건. 대신 폐기된 `ATD-*` 앱이 **기본 스윕에 남아 있고**,
**Config 11의 P0 증거가 그 폐기된 앱과 in-process contract test에서 나온다**(e2e README §5가 e2e가
아니라고 명시한 것이다).

> **감사자 자신의 한계 보고:** samples는 수렴했으나 **e2e는 수렴하지 않았다.** config-2(SpotService,
> 시나리오 51개)와 config-11을 C++ 수준 깊이로 보지 못했다. **SpotService가 가장 크고 거의 확실히
> 더 있다.**

## 라운드 4 상세 — 샘플 · E2E (뒤늦게 채운 근거)

**이 절은 라운드 4 체크리스트(SMP-ND-01~05 · E2E-ND-03~10)의 근거를 채운 것이다.**
당시 체크리스트만 적고 계약↔구현 대조를 남기지 않아 작업자가 집어서 고칠 수 없었다.

### 샘플

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-ND-01** (미구현) | [bingo:452-507,833-847](../../common/sample/bingo/README.ko.md): room Spot의 actor join이 Api 서버에서 **`GetPlayerRecordReq/Res`**로 전적을 조회하고, leave가 **`ReportBingoResultReq/Res`**로 기록한다. 둘 다 **`yield`** 터미네이터를 쓴다. `BingoPlayerState`에 **`Wins`·`Losses`**가 있다. [공통 샘플](../../common/sample/README.ko.md)이 **이 자리를 `yield`의 기준 사용처로 지목한다** | `samples/Bingo.Ts/**` — `GetPlayerRecord`·`ReportBingoResult`·`wins`·`losses` grep **0건**이고, **`.yield()` 호출도 0건**이다. 계약(contracts)·서버 handler·클라이언트 게이트 **어디에도 없다.** ⇒ 문서가 `yield`를 보여 주겠다고 고른 유일한 샘플 지점이 **통째로 비어 있다.** (전 언어 공통 — [갭 인덱스 SMP-X1](../90-implementation-gap.ko.md) 참조. `yield` terminator 자체가 §12.21의 미구현 항목이라 **두 갭은 한 묶음**이다) |
| **SMP-ND-02** (결함) | [31](../server/31-session-actor-dispatch.ko.md): session context는 **packet handler registry**를 갖는다. **packet-name switch를 금지한다** | **6개 샘플 전부**가 framework의 session handler registry를 우회하고 packet을 손으로 분기한다. ⇒ 스펙이 이름을 짚어 금지한 형태이고, 같은 packet name 중복 등록이 startup에서 걸리지 않는다. C++ [IMP-CP-11](cpp.ko.md)과 같은 뿌리 — **다만 C++은 registry가 없어서 그렇고, Node는 있는데 안 쓴다** |
| **SMP-ND-03** (결함) | [deliverydispatch:328-329,478-479](../../common/sample/deliverydispatch/README.ko.md): Tracking/Dispatch의 offer 경로에 **node rid 지정 hop이 없다.** [31 §375-377](../server/31-session-actor-dispatch.ko.md): transport 위치값(`RoutingId`류)은 **사용자가 일반 handler에서 직접 다루는 값이 아니다** | `samples/DeliveryDispatch.Ts/Server/DispatchCenter/dispatch-worker.ts:80,129` — `this.routes.sendToNode(...)`와 `.requestToNode(...)`로 **route-mesh에 node rid를 직접 지정해** offer를 보낸다. ⇒ 스펙이 금지한 transport 신원을 application 코드가 들고 라우팅한다. framework의 owner routing을 쓰지 않는다 |
| **SMP-ND-04** (결함) | [공통 샘플 §공통 작성 원칙](../../common/sample/README.ko.md): 필요한 기능이 **공개 계약에 없으면 샘플에서 우회하지 않고** framework의 public contract를 먼저 보완한다 | `samples/TicTacToe.Ts/Server/Configuration/redis-room-route-store.ts` — 샘플이 **자체 Redis room-route 스키마**를 정의해 들고 있다. C++ [SMP-CP-03](cpp.ko.md)(`add_spot_resolver`)과 같은 계열이다 — **샘플이 스펙에 없는 저장소 계약을 만들어 쓴다.** TicTacToe가 "Redis room route store"를 보여 주는 샘플이라는 문서 서술과, 그 store가 **framework 공개 표면이 아니라는 사실**이 충돌한다 |
| **SMP-ND-05** (**실패할 수 없는 단언**) | [공통 샘플 §Client self-check:358](../../common/sample/README.ko.md): *"자기 자신에게 보내면 안 되는 join notify는 **받지 않았음을 확인**한다"* | `samples/TicTacToe.Ts/Client/tictactoe-client-scenario.ts:236` — `.timeout(25)`. **25밀리초** 기다려 보고 안 오면 통과다. ⇒ 네트워크·직렬화·dispatch를 거치는 push가 25ms 안에 도착할 리 없으므로 **self-join notify를 실제로 보내고 있어도 이 단언은 통과한다.** negative 단언에 창을 너무 좁게 주면 **아무것도 검증하지 않는 것과 같다** |

### E2E

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-06** (결함) | [e2e §2.6](../../common/e2e/README.ko.md): framework host에는 **설정 파일 경로만** 넘긴다. ***"server와 client 애플리케이션 코드에서 직접 사용할 수 있는 환경 변수는 0개다"***. 어긋나면 **feature-map에 configuration migration gap을 기록한다** | e2e 앱 코드가 endpoint·Redis·routing id·log 경로를 **`process.env`로 직접 읽는다**(15개 파일). **feature-map에 기록한 config는 0개다.** ⇒ 최근 커밋 `b741d64fd`("refactor(node): load sample and e2e role configuration")가 **샘플은 옮겼는데**(샘플 앱 코드 `process.env` **0건**) **e2e는 남겨 뒀다** |
| **E2E-ND-04** (결함) | [e2e §2.1](../../common/e2e/README.ko.md):158-164 — readiness **3초** / poll 0.1초 / **route settle 5초** / **scenario settle 3초** / HTTP probe 3초를 **명시적인 config 상수**로 둔다. *"이 값 안에 준비되지 않는 로컬 e2e는 대기 시간을 늘려서 통과시키지 않는다"* | **`ROUTE_SETTLE_SECONDS`·`SCENARIO_SETTLE_SECONDS`가 어느 runner에도 없다**(`RegistryMessaging`·`SpotService`·`PubSub` grep 0건). readiness는 최대 **60초**까지 늘어난다. ⇒ 긴 대기가 수렴 실패를 가린다 |
| **E2E-ND-05** (결함) | [e2e §2.7](../../common/e2e/README.ko.md): **필수 격리 규칙** — 실행마다 **전용 Docker Redis container**를 새로 만든다. *"Docker Redis를 만들지 못하면 runner는 즉시 실패한다. **host Redis나 다른 실행의 endpoint로 자동 전환해서 성공 처리하면 안 된다**"* | `e2e/SpotService/run_e2e.sh:192,195` — `REDIS_ENDPOINT="${ZLINK_REDIS_E2E_ENDPOINT:-}"`. 환경변수가 세팅돼 있으면 **Docker 없이 그 endpoint를 그대로 쓴다.** 문서가 이름을 짚어 금지한 **탈출구**다. ⇒ 다른 실행의 Redis를 빌려 쓰면 cleanup·장애 주입이 섞여 **테스트 간섭**이 난다 |
| **E2E-ND-07** (결함) | [e2e §2](../../common/e2e/README.ko.md):43-45 — *"client는 **언어별 HTTP client wrapper**를 사용한다"*. framework가 제공하는 client 표면을 쓰는 것 **자체가 검증 대상**이다 | e2e client **12개 파일이 raw `fetch(...)`를 쓰고, `ZLinkHttpClient`를 쓰는 파일은 0개**다. ⇒ Node framework가 HTTP client를 공개하는데 **자기 e2e가 그걸 한 번도 안 쓴다.** 그 표면의 redirect·압축·timeout 동작이 e2e에서 전혀 검증되지 않는다 |
| **E2E-ND-03** (결함) | [e2e §2.2·§2.5](../../common/e2e/README.ko.md):306-310 — 시나리오는 **`Client/Scenarios/<ScenarioId><Name>Scenario.*`로 파일마다 하나**씩 둔다. *"개별 scenario의 요청·검증 본문은 `Program.cs`에 두지 않는다"* | **Config 9·10에 `Client/Scenarios/`가 없다.** `e2e/SpotActorTransfer/Client/`는 `main.ts` 하나이고, `e2e/ToActorMessaging/Client/`도 마찬가지다. ⇒ 시나리오 ID ↔ 파일 대응이 없어 다른 언어가 같은 단위로 옮길 수 없다 |
| **E2E-ND-10** (미구현) | [e2e §3.1 기동 순서 축](../../common/e2e/README.ko.md):493,504-506 — config runner가 기동 순서를 **인자로 받고**, 역방향 1회 + 고정 seed shuffle 1회를 최소로 돈다. Config **1·2·9**가 대상이다 | `START_ORDER`를 읽는 runner가 **11개 중 2개**뿐이다. ⇒ 나머지 9개 config는 이 축이 **없다.** C++ [E2E-CP-22](cpp.ko.md)와 같은 결함이지만, C++은 통합 러너가 변수를 export까지 하면서 안 읽는 no-op이었고 **Node는 아예 축이 없다** |
| **E2E-ND-08** (결함) | [e2e §2.9 주석 작성 규칙](../../common/e2e/README.ko.md):450-451 — *"시나리오 파일 첫머리에는 이 파일이 **어떤 사용자 흐름과 어떤 framework 동작을 검증하는지** 적는다. 독자가 파일을 열었을 때 '이 시나리오가 왜 필요한가'를 바로 알 수 있어야 한다"* | 시나리오 파일 **138개 중 머리말 주석이 있는 파일이 0개**다. ⇒ 규약이 요구하는 유일한 필수 주석이 **전무**하다. 시나리오가 왜 존재하는지 코드만 보고는 알 수 없다 |
| **E2E-ND-09** (결함) | [e2e §7](../../common/e2e/README.ko.md): config 접두사와 디렉터리 이름은 config 문서와 대응한다. 빌드 산출물은 VCS에서 제외한다 | 낡은 디렉터리 이름(`RegistryMessaging`·`DiscoveryRegistryHa` — config 문서는 각각 *Location messaging*·*Store 장애·복구*다)과 **죽은 `dist/`**가 트리에 남아 있다. ⇒ 다른 언어와 대조할 때 어느 config인지 이름으로 알 수 없고, 빌드 산출물이 소스와 섞여 있다 |
