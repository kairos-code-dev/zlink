# .NET framework public contract POSD 검토 리스트

## 문서 목적

`framework/languages/dotnet/src/Zlink.Framework/Contracts/` 아래의 공개 계약 전체(46파일, 약 2,500줄)를
POSD 관점에서 전수 검토한 결과다. location 계약(`Contracts/Locations`)은
`framework-dotnet-location-contract-posd-redesign-plan.ko.md`가 재설계 정본이므로 범위에서 제외하고,
그 문서의 "전체 framework public surface 분류 재검토 항목" 절과 겹치는 항목은 참조만 남긴다.

이 문서는 수정 실행 계획이 아니라 검토 리스트다. 항목이 채택되면 parity 정책에 따라 공통 draft에
먼저 반영하고, 실행 계획은 항목 규모에 따라 별도 plan으로 분리하거나 location plan에 편입한다.
심각도 순서: A(정합성 결함) > B(같은 지식의 중복) > C(형태·의미론) > D(규약 일괄 통일).

검토 기준은 location plan과 같다: 같은 지식은 한 곳에만, 능력은 런타임 예외가 아니라 타입으로,
불완전 상태는 표현 불가능하게, 저장·전송 형식은 public에 노출하지 않고, 계층·독자가 다르면 표면을
섞지 않는다.

## A. 정합성 결함 — 즉시 수정 가치

### A1. `IZLinkSpotContext.leaveActor` — 소문자 메서드명

`Contracts/Spots/ZLinkSpot.cs:234`의 `ValueTask leaveActor(...)`는 공개 계약에서 유일한 소문자
camelCase 메서드다. 이미 샘플까지 전파됐다(Bingo `BingoRoom.cs:177,244`, TicTacToe
`TicTacToeGame.cs:171`). 또한 짝이 되는 표면과 이름 규약도 어긋난다 — entry spot 쪽은
`DestroyActorAsync`인데 spot 쪽은 `leaveActor`(Async 접미 없음).

권장: `LeaveActorAsync`로 개명. 샘플·구현체(`ZLinkSpotActivationActors.cs:5`)·다른 언어 이식분 동시
수정. 순수 개명이라 draft 반영 부담이 작고 가장 먼저 처리할 항목이다.

### A2. `ZLinkAutoConnectType` ↔ `ZLinkLocationAutoConnectType` 이중 정의

같은 닫힌 값 집합이 public enum 두 벌로 존재한다: `Contracts/Channels/ZLinkAutoConnectType.cs`와
`Contracts/Locations`의 `ZLinkLocationAutoConnectType`. 이미 drift가 발생했다 — Channels 판은
`DealerMesh = 3`이 없다(값 0,1,2,4,5로 3이 결번인데 이유가 어디에도 없다). 둘 다 런타임에서 활발히
사용 중이다(registration validator, channel options, auto-connect host).

권장: 하나로 통합하거나(위치는 auto-connect의 주인인 쪽), 의도적으로 다른 집합이라면(채널 등록이
지원하는 부분집합) 그 관계와 결번 이유를 두 타입의 주석에 상호 참조로 박는다. location plan의
"닫힌 값 집합은 단일 매핑 테이블" 원칙과 같은 결이며, canonical 문자열 codec 통합 시 함께 처리해야
두 enum이 서로 다른 문자열로 저장되는 사고를 막는다.

### A3. `ZLinkFrameworkErrorKind` — 암묵 ordinal + 재시도 지식 분산

`Contracts/Errors/ZLinkFrameworkException.cs`의 오류 kind enum은 명시 값이 없어 첫 항목이 암묵 0이고,
중간 삽입 한 번이면 전체 ordinal이 밀린다. 오류 kind는 언어 간 로그·문서·E2E 검증에서 대조되는
값이라 cross-language parity 위험이 크다. 또한 `IsRetriable`이 kind에서 파생되지 않고 throw 지점의
ctor 인자라서, 같은 kind가 곳에 따라 retriable이 다를 수 있다 — 재시도 판단이라는 한 가지 지식이
모든 throw site에 흩어져 있다.

권장: (1) 명시 값 + `Invalid = 0` 규약 적용. (2) kind → retriable 기본 매핑을 한 곳(정적 테이블)에
두고, ctor 플래그는 제거하거나 예외적 override로만 남긴다.

추가로 location plan이 신설하려는 `ActorIdConflict`/`ActorCreateRejected`는 기존
`ActorTypeMismatch`/`ActorAlreadyExists`/`ActorCreateFailed`/`RequestRejected`와 의미가 겹친다.
신설 전에 기존 kind 재사용 가능 여부를 먼저 판정해야 한다 — location plan P0의 오류 분류 항목에
"기존 `ZLinkFrameworkErrorKind`와의 대조"를 전제 조건으로 추가할 것.

### A4. per-role `Add*LocationStore<TStore>()` 5종 — location 계약과 모순

`IZLinkFrameworkOptions`(Contracts/Configuration/Builders.cs:186-199)에 peer/spot/actor/route/lease
store를 role별로 따로 등록하는 메서드 5개가 남아 있다. location 계약은 "다섯 role은 한 backend에서
all-or-nothing"(NewClaim의 lease 원자 판정 요구)이라 role별 등록은 그 결정과 정면 모순이다. 현재
실사용은 테스트뿐이고 application·샘플 사용처가 없다.

권장: 5종 삭제, `AddLocationStore(instance)` + `UseInMemoryLocationStores()`만 남긴다. location plan
P2에 편입.

## B. 같은 지식의 중복

### B1. `IZLinkActorJoinSpotCall` ≡ `IZLinkActorJoinEntrySpotCall`

`Contracts/Actors/IZLinkActorContext.cs:49-117` — 두 인터페이스가 멤버 목록(Timeout/Async/Yield/
`Async<TReply>`/`Yield<TReply>`)은 물론 기본 구현 본문까지 복사-붙여넣기로 동일하다. 반환 타입
self-reference(`Timeout`)만 다르다.

권장: 하나의 call 타입으로 합치거나, self-returning이 필요하면 공통 기반에 기본 구현을 두고 두
파생은 `Timeout`만 갖게 한다.

### B2. `ZLinkSpotActorJoinResult` ≡ `ZLinkSpotCreateResponse`

`Contracts/Spots/ZLinkSpot.cs:3-24`와 `Contracts/Spots/Contracts.cs:10-31` — `(bool Accepted,
ZLinkMessage? Reply)` 형태와 Accept/Reject 팩토리 4개가 그대로 두 벌이다.

권장: 공용 accept/reject 결과 타입 하나로 수렴하거나, 도메인상 구분을 유지하려면 팩토리 구현만이라도
공유한다. 우선순위는 낮지만 언어 이식 시 4언어 × 2타입 × 4팩토리로 복제가 증폭되는 지점이다.

### B3. `IZLinkSpot<TActor>` ↔ `IZLinkEntrySpot<TActor>` / `IZLinkSpotContext` ↔ `IZLinkEntrySpotContext`

actor 멤버십 콜백 4개(OnActorJoin/OnJoinedActor/OnLeaveActor/OnDisconnectActor)가 spot·entry spot
인터페이스에 중복 선언되고, context 두 인터페이스도 6멤버(SpotRid/NodeRid/Handlers/Outbound/AddTimer/
RunWorker)가 중복이다.

권장: 멤버 공유용 기반 인터페이스(예: actor 멤버십 콜백, 공통 context 멤버) 추출을 검토한다. 단
java/kotlin의 "단일 제너릭, 2-인터페이스로 쪼개지 말 것" 결정과 충돌하지 않는 형태여야 하므로
(사용자가 구현하는 표면의 개수는 유지, 선언 공유만) 공통 draft에서 이식성 판정 후 진행한다.

### B4. `AddActorSend`/`AddActorRequest` — 이름 둘, 행동 하나

`IZLinkActorHandlerRegistry`(ZLinkSpot.cs:166-178)의 두 메서드는 둘 다 `AddActorPacket`에 위임하는
기본 구현이라 send/request 구분이 계약상 가짜다. 호출자는 구분이 의미 있다고 믿게 된다.

권장: 런타임이 실제로 send/request를 등록 시점에 구분하지 않는다면 `AddActorPacket` 하나만 남긴다.
구분이 필요해질 예정이면 기본 구현 위임을 제거하고 실제 구분을 구현한다.

### B5. `IZLinkRequestCall` ↔ `IZLinkRouteRequestCall`

`Contracts/Channels/Calls.cs` — 두 request call은 Yield 유무만 다른 복제다. 게다가 Yield는 기본
구현이 throw라서(→ C1) "지원할 수도 있는" 멤버를 가진 base와 그것을 뺀 사본이 공존하는 뒤집힌
구조다.

권장: C1(Yield 능력의 타입화)과 함께 해소한다 — yield 미지원 call이 기본형이 되고, yield 지원이
확장 타입이 되면 RouteRequestCall 사본이 필요 없어진다.

### B6. `ZLinkHandlerInvocation`의 중복 보관

`ChannelName`/`PacketName`이 `Context`(IZLinkHandlerContext)에도, invocation 자체에도 있다
(ZLinkHandlerInvocation.cs). `ZLinkRouteSendContext.RouterChannelId`도 base의 `ChannelName`과 같은
값의 다른 이름이다(RouteCalls.cs:62-66).

권장: invocation은 `Message` + `Context`만 갖고 이름들은 Context에서 읽게 한다. RouterChannelId는
유지하더라도 "ChannelName과 동일 값"임을 주석으로 고정하거나 한쪽을 제거한다.

## C. 형태·의미론

### C1. Yield 기본 구현이 `NotSupportedException` throw — 3곳

`IZLinkRequestCall.Yield`(Calls.cs:18), `IZLinkActorJoinSpotCall`/`IZLinkActorJoinEntrySpotCall.Yield`
(IZLinkActorContext.cs), `IZLinkWorkerCall.Yield`(ZLinkWorkers.cs:9). 인터페이스가 "지원할 수도 있는"
멤버를 선언하고 지원 여부는 런타임 예외로만 드러난다 — 능력이 타입에 없고 문서에도 없다.

권장: yield 가능한 call을 별도 타입(예: `IZLinkYieldRequestCall : IZLinkRequestCall`)으로 분리해
"yield를 쓸 수 있는 자리"가 컴파일 타임에 드러나게 한다. 최소한 어떤 표면이 Yield를 지원하는지를
계약 주석과 spec에 닫힌 목록으로 박는다. B5와 함께 처리.

### C2. `IZLinkSpotManager` overload 사다리와 `object` 판

Contracts/Spots/Contracts.cs:41-106 — Create/GetOrCreate 각각 (무인자 / `ZLinkMessage` / `object` /
`TRequest` 제너릭) 판이 겹친다. `object request` 판은 `TRequest`판과 기능이 같고(제너릭이 object로도
동작) 타입 안전성만 잃는다. overload 해소 모호성의 원천이기도 하다.

권장: `object` 판 삭제. `ZLinkMessage` 판과 `TRequest` 판 2벌로 정리(actor manager도 같은 2벌 규약
사용 중이므로 규약이 맞춰진다).

### C3. `IZLinkActorManager`의 raw `string actorType` — location 재설계와 정렬 필요

location plan은 actor id 전역 unique + "type은 생성·등록·진단 정보"로 방향을 정했고, 신설
`IZLinkActorDirectory`는 id 단독 lookup이다. 로컬 `IZLinkActorManager.CreateAsync(actorId,
actorType, ...)`의 raw string actorType은 이 방향과 어긋나지는 않지만(생성이므로 type이 필요),
directory와의 역할 경계·시그니처 규약(문자열 vs 등록 타입 유도)이 문서화되어 있지 않다.

권장: location plan P0의 actor id/type 계약 확정 시 manager 표면도 같은 문구로 묶는다 — 생성 계열만
type을 받고 lookup 계열은 id 단독, type 문자열의 유일한 정의는 actor factory 등록
(`AddActorFactory<TFactory>(string actorType)`)이라는 규칙.

### C4. `ZLinkActorJoinResult(bool Accepted, ActorRef Actor, ...)` — 거부 시 불완전 상태

Accepted가 false여도 `Actor`(non-nullable)와 `Reply`가 채워져 있는 것처럼 보인다. 거부 결과에서
Actor를 읽는 코드가 컴파일된다 — 불완전 상태가 표현 가능하다.

권장: `ActorRef?`로 바꾸거나 Accepted/Rejected를 팩토리로 구분해 거부 시 Actor 접근이 의미 없음을
계약 문구로 박는다.

### C5. set-only 옵션 인터페이스

`IZLinkSpotPublisherConfig`/`IZLinkSpotSubscriberConfig`(Configs.cs:58-76)의 속성이 전부
`{ set; }`이다. 읽을 수 없는 옵션은 진단·테스트에서 검증 불가능하고, 같은 파일의
`IZLinkSocketConfig`(get/set)와 규약이 어긋난다.

권장: get/set으로 통일.

### C6. `ZLinkEncodedPayload` — 복사 의미의 이중성

`From(...)` 3종은 전부 방어적 복사를 하는데 public 생성자 `new ZLinkEncodedPayload(memory)`는 복사
없이 caller 메모리를 aliasing한다. 두 생성 경로의 소유권 의미가 다르고 문서화되어 있지 않다
(bindings의 ownership 원칙과 같은 부류의 문제).

권장: 생성자를 막고(From만 공개) 복사 규칙을 주석으로 고정하거나, no-copy 경로가 필요하면
`FromOwned` 같은 이름으로 의도를 드러낸다.

### C7. send call 터미널·옵션 불일치

- `IZLinkSessionSendCall.Submit()` / `IZLinkSessionReplyCall.Submit()`은 CancellationToken이 없고,
  `IZLinkBoundSessionSendCall.Submit(ct)` / `IZLinkSendCall.Submit(ct)`는 있다.
- `Compress()`가 session send에는 있고 bound session send에는 없다. 같은 "클라이언트 세션으로 보내는"
  연산의 능력이 표면마다 다르다.

권장: 터미널 시그니처와 옵션 집합을 표면 간 대조표로 만들고 의도된 차이만 주석으로 남기고 나머지는
통일. (actor client의 `.Async(ct)` 단독 터미널 결정과 함께 "send 계열 터미널 규약"으로 draft에 한
번에 정리하는 것이 좋다.)

### C8. publish/send 표면 명명 드리프트

- publish 3벌: `IZLinkFanoutClient.Publish(channel, topic, msg)` /
  `IZLinkSpotPublisherClient.PublishSpot(channel, topic, msg)` / `IZLinkSpotOutbound.Publish(topic,
  msg)` — 이름·파라미터 규약이 제각각이다.
- `IZLinkRouteClient.Send/Request`만 bare 동사다 — 다른 client는 `SendToChannel`/`RequestToChannel`/
  `SendToSpot`/`RequestToSpot`(+ 신설 `SendToActor`/`RequestToActor`) 문법.
- 빌더 `AddRouteMesh(name)`만 "Channel" 접미가 없다(`AddClientServerChannel`/`AddFanoutChannel`,
  runtime 접근자는 `RouteMeshChannel(name)`).

권장: "동사 + To + 대상" 문법으로 통일(`SendToNode`/`RequestToNode` 등 대상 명시), publish는
`Publish(channelName, topic, ...)` 한 형태로. 개명은 4언어 파급이 크므로 변경 대비표를 만들어 draft
승인 후 일괄 처리.

## D. 규약 일괄 통일

### D1. public enum의 암묵 ordinal

명시 값 없는 public enum이 다수다: `ZLinkFrameworkErrorKind`(A3), Dispatch 계열 전부
(`ZLinkMessageFlowOutcome`/`ZLinkDispatchErrorSurface`/`ZLinkDispatchMessageKind`/
`ZLinkDispatchErrorReason`/`ZLinkDispatchErrorAction`/`ZLinkUnhandledDispatchAction`/
`ZLinkMessageFlowLogMode`), `ZLinkSpotCreateState`, `ZLinkStreamSessionError`. message-flow는 4언어
parity가 완료된 기능이라 ordinal 어긋남이 곧 관측 데이터 어긋남이 된다.

권장: location plan Values.cs와 같은 규약(명시 값, 상태류는 `Invalid = 0`)을 공개 enum 전체에
적용하고, 예외(플래그 비트값 등)는 주석으로 근거를 박는다.

### D2. 시간·오류 원시값 표현 혼재

monitoring 모델(`ZLinkSpotNodeStatus`/`ZLinkSpotNodePeerEntry`/`ZLinkSpotNodeSubjectEntry`)은
`ulong LastChangedMs`/`ConnectedSinceMs`와 `int LastError`(raw errno)를 쓰고, eventing 모델은
`DateTimeOffset`을 쓴다. 같은 monitoring public surface 안에서 시간 표현이 두 가지고, raw errno는
backend 세부의 노출이다(location plan 재검토 절의 socket native diagnostics와 같은 부류).

권장: monitoring spec 개정 항목으로 분리 — 시간은 `DateTimeOffset`(또는 명시된 monotonic 규약)으로
통일하고, `LastError`는 안정된 오류 표현으로 감쌀지 판정한다. 즉시 바꾸지 않는다(cross-language
관측 계약이므로 draft 선행).

### D3. attribute 17종 규약

`Contracts/Handlers/Attributes.cs` — packetName이 어떤 것은 positional 필수, 어떤 것은 `init` 선택;
`ZLinkSpotTimerHandlerAttribute(name, double periodMilliseconds)`만 raw double ms(다른 곳은
TimeSpan); `ZLinkSpotSubscriptionAttribute(spotNodeName, topic)`와
`ZLinkSpotSubscriptionHandlerAttribute(topic)`의 파라미터 비대칭.

권장: attribute 표를 만들어 (대상, 필수 파라미터, 선택 파라미터) 규약을 통일한다. reflection
attribute는 언어 idiom 차이가 허용되는 영역이므로(.NET 전용 표면) draft 부담 없이 .NET에서 정리
가능. 단 이름·의미는 등록 API와 1:1이어야 한다.

## location plan 재검토 절과 겹치는 항목 (참조)

아래는 이미 `framework-dotnet-location-contract-posd-redesign-plan.ko.md`의 "전체 framework public
surface 분류 재검토 항목" 절이 다루므로 이 문서에서는 중복 서술하지 않는다.

- `IZLinkChannelRuntimeOptions` / `IZLinkSocketConfig.Weight`의 build-time·runtime 이중 의미
- `IZLinkSpotRemoteAddressResolver` extension의 계약 근거 확인
- monitoring event payload의 raw row 노출, `IZLinkRuntimeEventPublisher`의 독자 분리
- `ZLinkSocketNativeEventType`/`ZLinkSocketDiagnostic.NativeValue`
- `ZLinkHandlerInvocation`/`IZLinkHandlerFilter`의 middleware 분류 (이 문서 B6은 그중 중복 보관
  문제만 추가로 지적)

## 비목표

- per-kind 채널 빌더(ClientServer/RouteMesh/Fanout)의 self-returning fluent 멤버 중복
  (EnableServer/SetRoutingId/AddHandlerGroup/...)은 제너릭 기반 인터페이스로 통합하지 않는다 —
  반환 타입 곡예가 되고 java/cpp 이식성을 해친다. per-type 유지.
- `ZLinkMessage`, codec 계약(`IZLinkCodecExtension`/`IZLinkMessageSerializer`), timer/worker 옵션은
  깊이·은닉이 적절해 변경 후보가 아니다(C6의 payload 생성 경로만 예외).

## 적용 범위 — e2e·샘플 포함

이 문서의 모든 항목(A~D)은 계약 파일 수정만으로 완료되지 않는다. 완료 조건은 **소비자 이행 완료**다:
framework 구현, `e2e/`, `samples/`, 테스트가 전부 새 표면만 사용하고, 변경 전 이름·형태의 grep이
그 전체 범위에서 0이어야 한다. 특히 A1(`leaveActor`)처럼 이미 샘플에 전파된 항목과 C8(명명 문법
통일)처럼 호출부가 넓은 항목은 e2e·샘플 수정이 작업량의 본체다. 언어별 적용 문서의 S5 단계가 이
범위를 추적한다.

## 반영 순서 권장

1. **A1, A4** — 순수 개명·삭제라 파급 계산이 쉽고 지금 코드베이스가 작을수록 싸다. A4는 location
   plan P2에 편입.
2. **A3 + location plan 오류 분류 정합** — location plan P0가 오류 kind를 신설하기 전에 처리해야
   중복 kind가 생기지 않는다.
3. **A2, C1+B5, C7** — draft에 "닫힌 값 집합 단일화", "call 터미널·능력 규약"으로 묶어 한 번에 결정.
4. **B1~B4, B6, C2, C4~C6** — 계약 형태 정리. 언어별 porting draft 차분에 포함.
5. **C3, C8, D1~D3** — 명명·규약 일괄 변경이라 변경 대비표를 만들어 draft 승인 후 진행. D2는
   monitoring spec 개정으로 분리.
