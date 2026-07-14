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

### 가장 무거운 둘

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-ND-01** | [e2e §2·§2.2·§2.4·§2.5](../../common/e2e/README.ko.md): 역할 서버 + 시나리오 ID당 클라이언트 파일 하나. **test-runner로 대체하지 않는다**. §5: e2e는 in-process contract test가 **아니다** | `e2e/ObservabilityOps/`에 **`run_e2e.sh`와 `feature-map.ko.md` 둘뿐이다.** `Server/`도 `Client/`도 `Shared/`도 없다. runner는 **폐기된 config-8(ATD)을 재실행**해 로그를 grep하고, **in-process contract test**를 돌린 뒤, 이렇게 통과시킨다 — `for scenario in OBS-A1 … ; do echo "$scenario … PASS"; done`. **`echo`가 검증이다.** 그리고 feature-map은 13개를 전부 "구현"으로 적는다. **[gaps §4.7]이 "OBS-A1~C5 evidence와 함께 통과했다"고 기록하고 있는데 — 거짓이다** |
| **E2E-ND-02** | [config-1 RM-A1(**P0**)](../../common/e2e/config-1-location-messaging.ko.md): live-owner peer row와 **두 provider로의 연결 상태**를 확인한다 | `RegistryMessaging/Server/LocationProbe/Endpoints/location-probe-endpoints.ts:24-28` — 모든 row를 `serviceRole: Router`, `state: Ready` **리터럴로** 매핑한다. 클라이언트는 `serviceRole === Router && state === Ready`를 단언한다. ⇒ **절대 실패할 수 없는 단언이다.** 살아 있는 검증은 `rows >= 2` 하나뿐. `ResilienceLifecycle/Server/TopologyProbe`도 `state: Ready`를 하드코딩한다. 게다가 이 probe 서버들은 **application 역할이 없어** §2.4가 금지하는 형태다 |

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
