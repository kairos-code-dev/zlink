# `.NET` — 구현 갭 체크리스트

[갭 인덱스](../90-implementation-gap.ko.md) | [스펙 목차](../README.ko.md)

> **기준선이다.** 다른 언어가 여기에 맞춘다. 그래서 여기 남은 갭은 **다른 언어로 전파된다.**

**이 문서는 계약이 아니라 작업 목록이다.** 계약은 spec이 소유한다. 여기서는 **스펙과 코드가 어긋난 자리**와 그것을 닫았는지만 추적한다.

**두 종류를 구분한다** — **미구현**(없다 → 만든다) / **결함**(있는데 계약과 다르게 돈다 → 동작을 바꾼다). 결함이 더 위험하다: 없는 것은 컴파일이 막아 주지만, 있는데 다르게 도는 것은 **부하가 걸릴 때만 드물게 깨진다.**

## 1. 진행 체크리스트

**전체 15건. 완료 0건.**

### 구현 감사에서 발굴 (2026-07-14, 스펙↔코드 직접 대조)

- [ ] **IMP-DN-01** (결함) — 20 §5
- [ ] **IMP-DN-02** (결함) — 22 §5·20 §8
- [ ] **IMP-DN-03** (결함) — 05 §3.3·31 §15
- [ ] **IMP-DN-04** (결함) — 51
- [ ] **IMP-DN-05** (결함) — 05 §2.4.3
- [ ] **IMP-DN-06** (결함) — 40 §3·§8.2
- [ ] **IMP-DN-07** (결함) — 20 §8

### 교차 언어 결함 (여러 구현에 같은 문제)

- [ ] **IMP-X3** — startup validation이 스펙의 설정 오류를 통과시킨다
- [ ] **IMP-X4** — location store read에 5초 취소 상한이 없다

### 언어별 표면 차이 (기준선 대조)

- [ ] **§12.7** — metric drop reason 라벨 도달 불가 (`.NET`)

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
| **IMP-DN-01** | 결함 | [20 §5](../server/20-spot-messaging.ko.md): SPOT route one-way의 decode 실패는 **`Drop`** + 경고 + metric | `Runtime/Spots/ZLinkSpotRouteDispatcher.cs:105-108` — `Request` 분기는 `TryDecode`로 감싸는데 **one-way 분기만 `DecodeBody`를 무방비 호출**한다. 예외가 dispatcher를 뚫고 나가 **drain 루프까지 중단**시키고, 뒤에 큐잉된 route 메시지가 처리되지 않는다 |
| **IMP-DN-02** | 결함 | [22 §5](../server/22-actor-model.ko.md)·[20 §8](../server/20-spot-messaging.ko.md): handler 중복 등록은 **startup 오류** | 중복 검사가 spot **활성화 시점**의 `Bind()`에만 있다(`ZLinkSpotPacketRegistry.cs:25-38`). host는 정상 기동하고 **첫 방 생성에서** 터지며, 그것도 `SpotCreateFailed`로 감싸여 설정 오류로 보이지 않는다. Entry Spot 중복은 startup에서 잡히므로 **두 표면이 비대칭** |
| **IMP-DN-03** | 결함 | [05 §3.3](../05-framework-api.ko.md)·[31 §15](../server/31-session-actor-dispatch.ko.md): send는 nonblocking 시도 → **pending queue + ready 알림** | `Runtime/Streams/ZLinkBoundSessionService.cs:82-90` — bound-session push만 `SendFlags.DontWait` 실패 시 **즉시 `RouteNotConnected` 예외**. 클라이언트 소켓 하나가 잠깐 차면 브로드캐스트 타이머 턴이 죽는다 |
| **IMP-DN-04** | 결함 | [51](../server/51-runtime-metrics.ko.md): `zlink.spot.count`는 현재 유지 중인 SPOT 수 | `ZLinkSpotNodeCatalog.cs` — `RecordSpotCreated`가 `GetOrCreateAsync`(:354)에만 있고 **`CreateAsync`에는 없다.** 종료는 양쪽 다 기록(:548, :581). `CreateAsync`만 쓰는 앱은 5회 만들고 닫으면 게이지가 **-5** |
| **IMP-DN-05** | 결함 | [05 §2.4.3](../05-framework-api.ko.md): reason 닫힌 집합에 `PayloadDecodeFailed` | `ZLinkSpotActorPacketDispatcher.cs:35-51` — decode가 handler 호출 **안에서** 일어나 모두 `HandlerException`으로 보고된다. actor 표면에서 `PayloadDecodeFailed`가 **한 번도 발생하지 않는다** |
| **IMP-DN-06** | 결함 | [40 §3·§8.2](../server/40-location-runtime.ko.md): 목록 조회는 `list page size` option을 따른다 | `ZLinkStoreLocationResolvers.cs:90-98` — `new ZLinkPageRequest(1000, …)` **하드코딩**. drain 대상 탐색 경로라 option이 무시된다 |
| **IMP-DN-07** | 결함 | [20 §8](../server/20-spot-messaging.ko.md): **같은 Entry Spot 타입 중복**은 설정 오류 | `ZLinkSpotRegistrationValidator.cs:55-63` — spot factory는 노드 간 중복을 검사하는데 **Entry Spot 타입은 안 한다.** 두 SpotNode가 같은 Entry Spot 타입을 등록해도 기동된다 |

## 3. 언어별 표면 차이 상세

### §12.7 metric drop reason 라벨 도달 불가 (`.NET`)

**미충족(`.NET`).** [51 §4.4](../server/51-runtime-metrics.ko.md)의 `zlink.channel.messages.dropped`는
`no_handler`, `decode_error`, `backpressure`, `stale_route` 네 라벨을 규정한다. 현재 `.NET`
런타임에서 실제로 방출되는 값은 `no_handler` 하나뿐이다 — decode 실패 경로가 drop metric을
기록하지 않고, `backpressure`와 `stale_route` 사유를 넘기는 호출부가 없다.

## 라운드 2 (2026-07-14) — 관측 · Stage · companion 패키지

라운드 1이 대조하지 않은 축(`00`·`10`·`11`·`25`·`50~53`·`12`·`32`)을 스펙과 코드로 직접 대조했다.

### 체크리스트

- [ ] **IMP-DN-08** (결함) — `zlink.fanout.received`가 **등록되지 않은 topic까지 라벨로 단다**
- [ ] **IMP-DN-09** (결함) — STREAM ingress가 client가 안 보낸 `correlation_id`를 `request_seq`로 **날조한다**
- [ ] **IMP-DN-10** (결함) — attribute로 선언한 SPOT timer 검증이 **startup이 아니라 spot 활성화 시점**
- [ ] **IMP-DN-11** (결함) — connector가 **짝 없는 `Response`/`Error`를 수신 큐에 적재**한다
- [ ] **IMP-DN-12** (결함) — HTTP client가 **proxy 자격증명을 대상 서버로 흘리고**, CONNECT는 인증 없이 나간다
- [ ] **IMP-DN-13** (결함) — connector send payload 한도를 **압축 전** payload에 적용한다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-DN-08** | [51 §5·§4.4b](../server/51-runtime-metrics.ko.md): 라벨은 **등록 시점의 닫힌 집합**만 붙인다. 앱이 room id를 인코딩한 동적 topic은 **금지 대상**이며, 미등록 topic은 라벨을 생략하고 합계만 기록한다 | `ZLinkSpotSubscriptionRegistry.cs:143` — `RecordFanoutReceived(message.Topic)`이 `_descriptorsByTopic.TryGetValue`(:152) **앞에** 있다. ⇒ 소켓에 도달하는 **모든 topic 값이 수집기에 새 시계열을 만든다.** ZoneWorld가 실제로 `zone.border.<from>.<to>` 같은 topic을 쓴다. 발행 측(`ZLinkSpotPublishCalls.cs:43,105`)은 올바르게 `null`을 넘긴다 |
| **IMP-DN-09** | [52 §9](../server/52-message-flow-tracing.ko.md): `correlation_id`는 **보내는 client가 생성**하고 **server는 echo만** 한다. **서버는 ingress에서 생성하지 않는다** | `ZLinkStreamSessionRuntime.cs:387,408` — `decoded.CorrelationId ?? decoded.RequestSeq?.ToString()`. `request_seq`는 **연결마다 도는 카운터**라, corr을 안 넣은 서로 다른 세션들의 로그가 전부 `corr=1`, `corr=2`…를 단다. ⇒ **corr이 join 키이기를 그만둔다** |
| **IMP-DN-10** | [25 §4.1](../server/25-stage-wrapper-on-spot.ko.md): 빈 이름·`period ≤ 0`·`catch-up ≤ 0`은 **host 시작 또는 등록 시점**의 설정 오류 | `ZLinkScannedSpotHandlers.cs:86-90` — scanner가 `[ZLinkSpotTimerHandler(name, 0)]`을 **검증 없이** descriptor로 만든다. 검사는 `ZLinkSpotTimerRegistry.cs:48-60`(**활성화 시점**)에만 있다. ⇒ host는 healthy로 기동하고 **첫 방 생성부터 전부 실패**한다. Java는 startup scan에서 잡는다 |
| **IMP-DN-11** | [32 §10.1·§9](../stream-connector/32-stream-connector.ko.md): response·error·heartbeat는 **수신 한도에 넣지 않는다**. request id가 부합하지 않는 error는 `RemoteError` | `Runtime/ZlinkStreamReceiveDispatcher.cs:18-37` — `pending.TryComplete()`가 실패하면 `RequestSeq is null`인 `Error`만 오류 표면으로 가고 **나머지는 전부 수신 메시지 큐로 떨어진다.** ⇒ 30초에 timeout된 request의 응답이 31초에 도착하면 **읽지 않은 메시지 예산(1024)을 갉아먹고**, 짝 없는 `Error`가 `RemoteError`로 **영영 보고되지 않는다** |
| **IMP-DN-12** | [http 07 §7.3](../http-client/07-auth-tls-proxy.ko.md): proxy 인증 정보는 **대상 서버로 새지 않아야 한다**(**CONNECT tunnel 요청에만** 실림) | `Zlink.HttpClient/Runtime/RequestPerformer.cs:132-133` — `Proxy-Authorization`을 **매 요청 메시지 헤더**에 붙인다. `https://` 대상이면 그 헤더는 **CONNECT 터널 안쪽을 타고 원본 서버까지 간다.** 정작 `HttpTransportFactory.cs:27-31`의 `new WebProxy(...)`에는 **credential이 없어서 CONNECT 자체는 인증 없이** 나간다. ⇒ proxy 인증은 407로 실패하고, **그 자격증명은 엉뚱한 서버 손에 들어간다.** C++·Node는 올바르다 |
| **IMP-DN-13** | [32 §4.7](../stream-connector/32-stream-connector.ko.md): 한도는 payload 바이트에만 적용하며 **압축을 쓰면 압축된 payload 기준**이다 | `ZlinkStreamFrameSender.cs:20-26` — 한도 검사가 **압축 전에** 돈다. ⇒ 80KB JSON을 `.compress()`하면(wire 6KB) **browser connector는 받고 .NET/Java/C++은 거부한다.** 압축이 존재하는 바로 그 이유가 막힌다 |

## 라운드 3 (2026-07-14) — 근거 없는 표면 · 조용한 no-op · 경합

라운드 3은 질문을 뒤집었다 — **"코드가 스펙이 허용하지 않는 걸 하는가?"**

**public 타입 이름은 깨끗하다.** `Zlink.Framework.Contracts`의 interface 106개 + 타입 144개가 전부
카탈로그에 있고 `Runtime/**`은 아무것도 export하지 않는다. **문제는 전부 "받아서 검증하고 버리는
옵션"과 경합이다.**

### 체크리스트

- [ ] **IMP-DN-14** (결함) — `IZLinkSocketConfig` 14개 중 **9개를 적용하지 않고**, `Linger`는 **앱 몰래 0으로 강제**한다
- [ ] **IMP-DN-15** (결함) — `IZLinkRouteConfig`/`IZLinkOutboundRouteConfig`가 **설정만 되고 읽히지 않는다**
- [ ] **IMP-DN-16** (결함) — SpotNode의 role config 표면이 **완전한 no-op**이다
- [ ] **IMP-DN-17** (결함) — **actor가 든 spot을 닫을 수 있다** (check-then-act 경합)
- [ ] **IMP-DN-18** (결함) — 첫 `GetOrCreate` 호출자의 취소가 **같은 spot을 기다리는 다른 호출자 전부를 실패**시킨다

### 상세

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **IMP-DN-14** | `IZLinkSocketConfig`의 각 항목은 소켓에 적용된다 | 적용 경로(`ZLinkChannelBundleFactory.cs:167-176`)가 다루는 건 `MaxMessageSize`·`SendHighWaterMark`·`ReceiveHighWaterMark` **셋뿐**이다. `Linger`·`TcpNoDelay`·`IPv6`·`Immediate`·`ConnectTimeout`·`HandshakeInterval`·`SendBufferSize`·`ReceiveBufferSize`·`ReceiveTimeout`은 **읽는 곳이 없다.** 더 나쁜 건 `ZLinkDotNetBackendAdapters.cs:23,31,39,47,75`가 DEALER/ROUTER/PUB/SUB에 **`Linger = TimeSpan.Zero`를 하드코딩**한다는 것이다. ⇒ `Linger = 1s`를 설정하면 **수락되고 getter로 1초로 읽히는데** 소켓은 Linger 0으로 돈다. 종료 시 큐에 남은 메시지가 **전부 버려진다** — 그 설정이 막으려던 바로 그 일이 |
| **IMP-DN-15** | `RequireKnownPeer`/`AllowPeerHandover`/`EnablePeerProbe` 등은 route 동작을 정한다 | 읽는 곳이 **없다.** 대신 `ZLinkRouteChannelInitializer.cs:50-51`이 `SetMandatory(true); SetHandover(true);`를, `ZLinkRouteConnectionSet.cs:118-119`가 `SetProbe(true)`를 **상수로** 박아 둔다. client-server의 **server ROUTER는 셋 중 어느 것도 부르지 않아** `ConfigureServerRouting()`이 **자기가 이름 붙인 바로 그 소켓에서 무효**다 |
| **IMP-DN-16** | `ConfigurePubSubPublisher()` 등으로 SpotNode 소켓을 설정한다 | 살아 있는 config 객체를 돌려주는데 **읽는 곳이 0개**다. 애초에 불가능하다 — `IZLinkBackendSpotNode`에 **소켓 옵션 setter가 아예 없다.** ⇒ SPOT fan-out이 backpressure에서 조용히 드롭하는 걸 막으려고 `SendHighWaterMark = 100_000; NoDrop = true`를 걸면 **오류 없이 수락되고 버려진다** |
| **IMP-DN-17** | [21 §close](../server/21-spot-node.ko.md): **actor가 남아 있는 user Spot은 종료하지 않고 실패를 반환한다** | `ZLinkSpotNodeCatalog.cs:429-437` — `if (activation.JoinedActorCount > 0) return false;`로 **검사한 뒤 닫는다.** `JoinedActorCount`는 자기 `_gate`가 지키는데, join commit(`ZLinkSpotActivationActors.cs:339`)은 **spot의 직렬 줄**에서 돌며 그 락을 잡지 않는다. ⇒ "0명 확인 → close 등록" 사이에 join commit이 끼면 **actor가 든 방이 파괴된다.** `OnLeaveActor`가 안 돌아 앱 장부엔 그 actor가 남고, actor의 location row는 **해제된 spot을 가리킨다.** C++만 이걸 제대로 한다(`node->mutex`로 검사와 close를 함께 감싼다) |
| **IMP-DN-18** | [21](../server/21-spot-node.ko.md): `GetOrCreate`는 하나의 activation을 모든 호출자가 공유한다. [54 §6](../server/54-graceful-drain-handoff.ko.md): **호출자의 취소는 그 호출자의 대기만 중단한다** | `ZLinkSpotNodeCatalog.cs:340-375` — 소유자가 **자기 token**으로 생성을 돌리고, 실패하면 `pending.Fail(...)`로 **공유 TCS를 그 실패로 완료**한다. ⇒ 1초 deadline인 A와 30초 deadline인 B가 같은 방을 요청하면, **A가 1초에 취소될 때 B도 함께 죽는다** — B에겐 29초가 남아 있었는데. 부하 상황에서 **성질 급한 클라이언트 하나가 그 방에 몰린 모두를 날린다** |

## 교차 언어 결함 — 이 언어에서 무엇을 고치나

**교차 언어 결함이라도 고치는 일은 이 언어에서 한다.** [갭 인덱스](../90-implementation-gap.ko.md) §15.3이
**왜**(계약과 결정)를 소유하고, 아래 표가 **무엇을**(이 언어의 작업)을 소유한다.

| 교차 결함 | 무엇이 깨지나 | 이 언어의 작업 |
|---|---|---|
| **IMP-X3** | startup validation이 스펙의 설정 오류를 통과시킨다 | IMP-DN-02 · IMP-DN-07 · IMP-DN-10 |
| **IMP-X4** | location store read에 5초 취소 상한이 없다 | **이 언어 전용 ID 없음** — `Runtime/Locations/`에 `StoreReadTimeout` 개념 자체가 없다. [54 §3.4](../server/54-graceful-drain-handoff.ko.md)가 요구하는 5초 상한을 store read 경계마다 적용한다 |
| **IMP-X7** | connector send payload 한도를 압축 *전* payload에 적용 | IMP-DN-13 |
| **IMP-X9** | HTTP client가 proxy 자격증명을 대상 서버로 흘린다 | IMP-DN-12 |
| **IMP-X10** | SPOT timer 등록 검증이 startup이 아니다 | IMP-DN-10 |
| **IMP-X11** | `fanout.received`가 미등록 topic까지 라벨로 단다 | IMP-DN-08 |
| **IMP-X12** | actor가 든 spot을 닫을 수 있다 (경합) | IMP-DN-17 |
| **IMP-X13** | 서버가 `correlation_id`를 `request_seq`로 날조 | IMP-DN-09 |
| **IMP-X14** | `listPageSize`가 무시된다 | IMP-DN-06 (1000을 하드코딩) |
| **IMP-X17** | `GetOrCreate` 취소가 다른 호출자 전부를 실패시킨다 | IMP-DN-18 |
| **IMP-X18** | Redis fixture 바이트 단위 일치 주장이 거짓 | 빈 컬렉션 표현이 fixture와 다르다 |

## 이전 기록 — 기준선 대조 (2026-07-13 이전)

> **이 절은 과거 기록이다.** 당시 계약 기준으로 확인한 내용이며, 그 뒤 계약이 바뀐 항목이 있다
> (특히 실행 terminator — [갭 인덱스 §12.21](../90-implementation-gap.ko.md) 참조).
> **현재 작업 목록은 이 문서 위쪽의 체크리스트다.**

`.NET` public declaration과 package는 이 문서에서 추적하던 계약 차이를 해소했다.
actor membership은 nullable `SpotRid`만 상태 기준으로 사용하고, join 결과는 승인/거절
sealed record로 유효한 상태만 표현한다.

다음 타입은 기존 interface catalog에서 이름이나 전체 시그니처를 찾기 어려웠다.
현재 `.NET` interface 문서의 전체 inventory, 보완 시그니처와 공통 기능 커버리지 표에
반영했다.

```text
IZLinkActorClient
IZLinkActorDirectory
IZLinkActorJoinCall
IZLinkActorLocationStore
IZLinkActorRequestCall
IZLinkActorSendCall
IZLinkChannelRuntimeOptions
IZLinkClientServerChannelOptions
IZLinkCodecExtension
IZLinkCodecRegistrar
IZLinkLocationReadiness
IZLinkOwnerLeaseStore
IZLinkPeerLocationStore
IZLinkRouteLocationStore
IZLinkRouteMeshChannelOptions
IZLinkSpotActorLifecycle
IZLinkSpotCommonContext
IZLinkSpotLocationStore
IZLinkStreamCompressionBuilder
IZLinkUnhandledDispatchOptions
IZLinkWorkerCall
IZLinkWorkerOptions
```

`IZLinkActorSendCall`은 다른 one-way call과 같은 `void Submit(CancellationToken)` 계약을
제공한다. `SpotHandle`, capability별 `IZLinkEndpointConnections`, sealed monitoring event와
typed packet identity 단일 소유도 contract/unit/E2E 및 실제 package consumer로 검증한다.

runtime metrics, flow correlation, graceful drain과 session closing도 정식 계약, package와
Bingo 공개 예제, Config 1~11의 공통 E2E 181개로 검증했다.

> **실행 terminator는 예외다.** 위 목록이 만들어질 당시에는 "request·actor join·worker의 yield
> 전용 타입을 제거하고 단일 완료 terminator가 자동으로 turn을 관리한다"가 계약이었고, 그 기준으로
> 갭이 닫힌 것으로 기록했다. **그 계약은 폐기됐다.** 현재 정본은 세 terminator
> (`submit`/`async`/`yield`)이며([04 §1.1](../04-async-execution-policy.ko.md)), `.NET`은 이를
> 충족하지 않는다. 따라서 **`.NET`에 남은 구현 차이는 [§12.20](#1220-응답에-packet-name을-싣는다-전-언어),
> [§12.21](#1221-yield-terminator-부재-전-언어), [§12.22](#1222-http-client가-framework-계약-밖에-있다-전-언어),
> [§12.23](#1223-worker-축-분리와-yield-부재-전-언어)이다.** 그 밖에 이 문서가 추적하는 `.NET`
> 차이는 없다.

## 라운드 4 (2026-07-14) — 샘플 · E2E

### 체크리스트

- [ ] **SMP-DN-01** (미구현) — Bingo의 **player-record / `yield` 축이 코드에 통째로 없다**
- [ ] **SMP-DN-02** (결함) — 정본 샘플 6개 중 **4개가 여전히 환경변수로 endpoint·Redis를 받는다**
- [ ] **SMP-DN-03** (결함) — TicTacToe가 **위치 인자로 역할을 전환하는 단일 실행 파일**이다
- [ ] **SMP-DN-04** (결함) — DeliveryDispatch 메시지 계약 drift
- [ ] **SMP-DN-05** (결함) — GameQuest 메시지 계약 drift
- [ ] **SMP-DN-06** (결함) — SupportChat의 **"반드시 오류로 검증한다" 5개 중 3개를 안 본다**
- [ ] **SMP-DN-07** (결함) — ZoneWorld에 **`.NET` 전용 두 번째 클라이언트**가 있다(문서: TypeScript 하나만)
- [ ] **SMP-DN-08** (결함) — 클라이언트 단언이 문서보다 약하다(Bingo 7·8단계, DD 순서)
- [ ] **E2E-DN-01** (결함) — `ObservabilityOps`가 **e2e 앱이 아니다** — 샘플 바이너리를 셸로 구동한다
- [ ] **E2E-DN-02** (결함) — Config 9·10에 **`Client/Scenarios/`가 없다**(Program.cs 954줄·519줄)
- [ ] **E2E-DN-03** (결함) — Config 10이 **세 역할을 한 프로젝트로 뭉갰다**
- [ ] **E2E-DN-04** (결함) — readiness 기본값이 **30초**(SpotService **60초**) — 문서는 3초
- [ ] **E2E-DN-05** (결함) — `RuntimeMonitoring`에 **시나리오 실행 전용 `Trigger` 역할**이 있고 **다른 서버의 로그 파일을 읽어** 검증한다
- [ ] **E2E-DN-06** (결함) — `RM-C9`(backpressure)가 **이름뿐**이고 `RM-A4`(P0)가 주장하는 것을 검증하지 않는다
- [ ] **E2E-DN-07** (결함) — 역할 서버가 **30초 재시도 루프로 route 수렴 실패를 가린다**
- [ ] **E2E-DN-08** (결함) — 클라이언트가 bounded wait endpoint 대신 **GET 폴링 루프**를 돈다(24개 파일)
- [ ] **E2E-DN-09** (결함) — 시나리오 파일 명명·커버리지 장부

### 가장 무거운 것

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-01** | [e2e §2.2·§2.4·§2.5](../../common/e2e/README.ko.md) | `e2e/ObservabilityOps/`에 **`Client/`가 없다.** `Server/Program.cs`가 **Bingo·ShoppingMall 샘플 서버를 import**해 `options.Role`로 스위치하고, 클라이언트로 **`samples/Bingo/Client`를 쓴다.** 시나리오 로직은 `run_e2e.sh` 안의 **인라인 파이썬 783줄**에 있다. 그런데 feature-map은 OBS 13개를 전부 "구현"으로 적는다 |
| **E2E-DN-07** | [e2e §2](../../common/e2e/README.ko.md): **수렴 직후 첫 요청**은 **재시도나 sleep으로 가리지 않는다** — 첫 요청이 바로 성공하는 것 **자체가 검증 대상**이다. *"workaround를 넣은 테스트는 완료로 보지 않는다"* | `LocationMessaging/Server/Provider/Endpoints/ProviderEndpoints.cs:120-141` — `RequestProfileWithRetryAsync`가 **30초 동안 100ms 간격으로 재시도**하며 `ZLinkFrameworkException`을 삼킨다. Config 1의 **모든** `/profile/request`가 이걸 통과한다 |
| **E2E-DN-06** | [config-1 RM-C9](../../common/e2e/config-1-location-messaging.ko.md): 처리 속도보다 빠르게 **다량** 보내 송신 큐를 **HWM까지 채운다** | `RmC9BackpressureScenario.cs:11` — `SlowSendCount = 8`. **one-way send 8번**으로는 어떤 HWM에도 못 닿는다. 그러고는 **10초 자고**(`:25`) 후속 request가 되는지 본다. **backpressure가 만들어지지 않는다** |
| **E2E-DN-04** | [e2e §2.1](../../common/e2e/README.ko.md): local readiness **3초**. *"긴 대기는 버그를 늦게 발견하게 만들기 때문에 완료 조건으로 인정하지 않는다"* | 모든 runner가 **기본 30초**(`SpotService` **60초**)이고 **환경변수로 덮어쓸 수 있다.** 문서는 환경변수를 "느린 CI나 진단용 override"로만 허용한다 |

## 라운드 5 (2026-07-14) — e2e Config 7·9 심층

**기준선의 e2e에도 "실패할 수 없는 단언"이 무더기로 있다.** 얕은 패스는 구조만 봤고, 시나리오
파일을 한 줄씩 읽으니 나왔다.

### 실패할 수 없는 단언

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-10** (**가짜 통과**) | [config-7 §2·§5](../../common/e2e/config-7-monitoring.ko.md): socket 이벤트 kind는 **닫힌 enum**(`Connected`·`ConnectionReady`·`Disconnected`·`HandshakeFailed`·`PeerAdmissionChanged`·`Closed`)에 속해야 한다 | `MonA5FixedKindsScenario.cs:18-21` — `kind=HandshakeFailed` **또는 `kind=Internal`**을 받아들인다. `Internal`은 **그 닫힌 집합에 없다.** ⇒ 이 시나리오의 존재 이유가 "framework가 잘못된 handshake를 `HandshakeFailed`로 분류한다"를 증명하는 건데, **그 분류를 지우고 catch-all로 떨어져도 통과한다.** 게다가 evidence store가 누적이라 앞선 시나리오가 낸 아무 `Internal` 이벤트가 **트리거 전에 이미 조건을 만족시킨다.** feature-map은 `구현`으로 적으면서 본문엔 fallback을 **자백한다** |
| **E2E-DN-11** (**가짜 통과**) | [config-7 MON-D1](../../common/e2e/config-7-monitoring.ko.md): svc-b가 **떠났다가 돌아오는 전이**를 관측한다 | `MonD1FailureRecoveryScenario.cs:84-86` — 누적 카운터에 대한 **`>= 3`**이다. MON-D1이 시작되기 전에 이미 부팅 수렴(≥1, MON-A2가 단언)과 MON-A4의 drain·restore(각 1)로 **문턱을 넘는다.** ⇒ **svc-b를 멈추기도 전에 첫 루프에서 통과한다.** 카운터를 세는 것으로는 remove/re-add 전이를 구분할 수 없다 |
| **E2E-DN-12** (**가짜 통과**) | [config-7 MON-A2·MON-A3](../../common/e2e/config-7-monitoring.ko.md): **트리거를 발생시킨다**(svc-b 추가/종료, spot subject 변경) | 두 시나리오 모두 **`/evidence/wait` 한 번이 전부다**(MON-A2는 34줄). **아무것도 추가하지 않고 아무것도 멈추지 않는다.** ⇒ **부팅 수렴과 100ms 폴링의 초기 diff만으로 통과한다** |
| **E2E-DN-13** (**가짜 통과**) | [e2e §2.3](../../common/e2e/README.ko.md): **시나리오 실행 전용 server가 만든 marker만으로 성공을 판정하지 않는다** | `MON-B2`의 evidence를 **`Server/Trigger`가 자기 안에서 임시 host를 만들어 단언하고 손으로 조립한 문자열**로 돌려준다(`TriggerValidation.cs:60` — **리터럴 상수**를 반환한다). 클라이언트는 그 문자열을 grep한다. **e2e 옷을 입은 in-process contract test다** |
| **E2E-DN-14** (**가짜 통과**) | [config-7 MON-A4](../../common/e2e/config-7-monitoring.ko.md): **failover** + drain/restore | failover 다리가 **아예 없다.** 그리고 "drain evidence" 단언이 **방금 클라이언트가 호출한 그 엔드포인트가 무조건 쓰는 marker**다(`ServiceHostFactory.cs:125`) — 모니터링에 대해 **아무것도 단언하지 않는다** |

### 구조 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **E2E-DN-15** (결함) | [e2e §2.3·§2.4](../../common/e2e/README.ko.md): 시나리오 실행만 위임받는 server는 **폴더 이름이 달라도 금지 대상**이다. evidence는 **실제로 처리한 역할 server**가 노출한다 | `Server/Trigger`가 제품 기능이 없다. 그리고 **MON-C1이 다른 프로세스의 stderr 파일을 디스크에서 읽어** dispatch 실패를 검증한다(`TriggerLogReader.cs:15`). 그 로그 줄은 runner가 `ZLINK_DEBUG_FRAMEWORK_TASKS=1`을 켜야만 나온다. **C++에서 본 결함과 같다** |
| **E2E-DN-16** (결함) | [e2e §2.4](../../common/e2e/README.ko.md): 하나의 서버 프로젝트를 mode로 역할 전환하지 않는다. **같은 `Program.cs`를 복사해 default role만 바꾸는 것도 금지** | `FilteredService`·`ThrowingService`가 **`ServiceHostFactory.Create(args, profile)` 하나에 enum으로 분기**한다. 결과: config-7이 요구하는 **동일한 두 service 노드** 중 `svc-b`에 **spot mesh가 아예 없다** |
| **E2E-DN-17** (결함) | [config-9 §5](../../common/e2e/config-9-to-actor-messaging.ko.md): 실패 분류는 **framework가 낸 public error kind**여야 한다 | `Server/Caller/Program.cs:43-48` — 역할 서버가 **시나리오 ID로 분기**하고(`request.Scenario.StartsWith("TA-B1")`), **`ZLinkFrameworkErrorKind.ActorRouteNotFound`를 직접 만들어 던진다.** ⇒ 앞으로 `/request`를 타는 시나리오는 **진짜와 구별되지 않는 가짜 분류**를 받는다 |
| **E2E-DN-18** (미구현) | [config-9 §2·§5](../../common/e2e/config-9-to-actor-messaging.ko.md): actor의 **bound-session snapshot marker**로 bind 비오염을 대조한다 | 그 marker가 **어디에도 없다.** TA-A2/A3는 대신 **push를 시도해 실패하는 것**을 bind 상태 프로브로 쓴다 — config-9이 존재 검사에 대해 **명시적으로 금지한 형태**다. TA-A1의 "새 bind가 생기지 않았다"는 negative는 **session gateway가 marker를 내는데도 읽지 않는다** |
| **E2E-DN-19** (결함) | [e2e §3.1](../../common/e2e/README.ko.md): **수렴 직후 첫 요청**을 재시도나 sleep으로 가리지 않는다 | TA-B3의 **복구 후 첫 요청**을 10초 재시도 루프로 감싼다(`AssertCallWithRetryAsync`). 실패 분류 단언도 마찬가지라, **10초 동안 틀린 kind가 나와도 통과한다** |
| **E2E-DN-20** (미구현) | [e2e §3.1](../../common/e2e/README.ko.md): **`route mesh 없음` 축이 Config 9의 P0**다 | `Server/Caller/Program.cs:27-28`이 `ConnectRouter`를 **하드와이어**한다. route mesh 없는 변형을 **실행할 수 없다.** README가 그 축이 잡는 버그 부류까지 명시했는데(원격 actor join relay가 route mesh 등록을 전제하던 구현) **feature-map에 기록도 없다** |
| **E2E-DN-21** (결함) | [e2e §2.5](../../common/e2e/README.ko.md) | Config 9에 **`Client/Scenarios/`가 없다** — 7개 시나리오가 `Program.cs`의 람다 딕셔너리다 |

## 라운드 5 — ZoneWorld (`shared_sample`, **작업 중**)

> **ZoneWorld dotnet은 아직 커밋되지 않은 작업 중 코드다.** 아래는 현재 워킹트리 기준이며,
> 완성 전에 반영하면 된다.

### 진짜 버그

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-09** (**버그**) | [zoneworld §2.4·§2.5](../../common/sample/zoneworld/README.ko.md): border snapshot은 **`Tick`이 보관 중인 값보다 작거나 같으면 무시**한다. zone spot 생성 시 **`Tick = 0`**이다 | `ZoneState.cs:19` — `_adjacentHighWater`를 별도로 들고, `ExpireStaleSnapshots`(`:60-68`)가 **`_adjacent`만 지우고 `_adjacentHighWater`는 영영 안 지운다.** ⇒ zone-node-2를 재시작하면 그 spot의 `Tick`이 **0부터 다시 시작**하는데, 살아남은 zone-node-1의 high-water는 **≈400**이다. 재시작된 노드가 보내는 `Tick=1,2,3…`이 전부 `tick <= newest`에 걸려 **영구히 버려진다. 그 순간부터 border sync가 죽는다.** 만료된 뒤엔 "보관 중인 값"이 없으므로 새 `Tick=1`은 **받아들여야** 한다. **runner가 실제로 zone-node-2를 재시작하는데**(ZW-B4·C2·C3·E5), **이걸 잡을 ZW-B1이 첫 재시작 앞에서 돌아** 스위트는 초록으로 남는다 |
| **SMP-DN-10** (**가짜 통과**) | [zoneworld §8.1·§11 ZW-C1](../../common/sample/zoneworld/README.ko.md): `Registered`는 **location event**에서, `Connected`는 **socket event**에서 온다. 문서가 위험을 직접 적어 뒀다 — *"각각 다른 출처에서 오므로, 하나만 보면 다른 하나의 배선이 죽어 있어도 통과한다"* | `NodeRegistry.cs:29-36` — 1초마다 오는 **report 메시지가 `Registered = true`를 찍는다.** ⇒ **두 플래그가 같은 배선(report channel)에서 나온다.** `LocationEventHandler`를 **통째로 지워도 ZW-C1이 통과한다.** 문서가 경고한 바로 그 실패다 |
| **SMP-DN-11** (결함) | [zoneworld §2.4](../../common/sample/zoneworld/README.ko.md): 같은 `PlayerId` 재입장 시 **좌표와 zone은 유지된다** | `ZoneEntrySpot.cs:70-74` — `JoinWorldReq` handler가 **무조건 고정 스폰(25,25)으로 재입장**시킨다. 게다가 그 handler는 **entry spot에 있을 때만** dispatch되므로, zone spot에 살아 있는 actor에겐 `JoinWorldReq` handler가 **아예 없다.** 시나리오는 매번 GUID 접미사를 붙여서 **같은 `PlayerId`로 재입장하는 경우가 한 번도 없다** |

### 규약 위반

| ID | 계약 | 구현이 하는 일 |
|----|------|----------------|
| **SMP-DN-12** (**절대 규칙 위반**) | [샘플 규약](../../common/sample/README.ko.md): **TicTacToe만** 수동 연결을 쓸 수 있다 | `ZoneNode/Program.cs:88-91,107` — `ConnectRouter`·`ConnectPeerPub`·`EnableClient(endpoint)`. peer endpoint가 `ZoneWorldSettings.cs`에 박혀 있고 **주석이 후속 에이전트에게 지우지 말라고 지시한다.** [갭 인덱스 §13.2] 참조 |
| **SMP-DN-13** (결함) | [샘플 규약](../../common/sample/README.ko.md): 앱 코드가 쓸 수 있는 **환경변수는 0개** | `ZoneWorldSettings.cs:20-35`에서 **21개**를 읽는다. 그중 둘은 설정이 아니라 **동작 스위치**다 — `ZONEWORLD_FAULT_TICK_ZONE`을 **모든 zone spot의 100ms tick마다 환경에서 다시 읽어** 예외를 던지고, `ZONEWORLD_DISABLE_BOTS`도 마찬가지다. **다른 5개 샘플은 최근 커밋에서 전부 config 파일로 옮겼다** |
| **SMP-DN-14** (결함) | [샘플 규약](../../common/sample/README.ko.md): 실행마다 **전용 Docker Redis**를 만든다. **host Redis 공유 금지, key prefix만 다르게 하는 것도 안 된다** | `run_sample.sh:12-13,43-44` — **host Redis 6379**를 쓰고 key prefix로만 격리한다. 게다가 시작할 때 `pkill -f "bin/Debug/net8.0/ZoneWorld.Server"`를 해서 **동시에 도는 다른 실행을 죽인다** |

**깨끗한 축(확인함):** actor cross-node transfer(상태 유실 없음, 좌표·zone 교차검증), bot이 bound
session 없이 도는 것, fanout topic에 동적 id 없음, 발행자가 노드 목록을 모름, border band·인접·병합
규칙, move 검증 순서, tick 순서, 자동 handler 등록, 계층 디렉토리.

**`.NET` `Client/`는 위반이 아니다** — [zoneworld §0.2](../../common/sample/zoneworld/README.ko.md)가
언어별 headless 시나리오 client를 명시적으로 허용한다. 상위 README의 "client는 TypeScript 하나만"은
**브라우저 client**를 말한다. (다만 그 TS client는 **아직 계약 파일 두 개뿐**이라 사실상 미착수다.)
