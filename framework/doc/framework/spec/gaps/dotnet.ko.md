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
