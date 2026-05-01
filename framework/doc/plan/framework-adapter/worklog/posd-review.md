# Framework POSD Review

## 2026-05-01 Final Iteration

상태: `verified`

### Red Flags

- `verified`: framework red flag 없음.

### Alternatives

- 대안 1: 기존 routed handler와 raw relay handler를 유지하고 sample helper만 바꾼다.
- 대안 2: public 표면을 session actor dispatch helper와 resolver 기반
  `SessionProxy`로 바꾸고, raw relay 이름은 내부 packet으로만 남긴다.

선택: 대안 2를 적용했다. request sequence 보존, binding token 검증, session route
해석은 framework 아래로 숨기고 sample은 actor 생성과 dispatch 선택만 드러낸다.

### 최종 재점검

- metadata 전달 정책은 기본 deny이며, `ForwardApplicationKey(...)`로 허용한
  application key만 actor context snapshot에 들어간다. raw stream header metadata와
  session-local metadata는 handler에 그대로 노출하지 않는다.
- session 위치는 `IZLinkActorSessionLocationWriter`와
  `IZLinkActorSessionRouteResolver`로 제한했다. sample registry metadata adapter는
  writer/resolver 내부 구현이며 public route resolver 입력을 넓히지 않는다.
- `session-gateway.ko.md`는 superseded 보관본으로 축소했다. old API 이름은 제거
  이력 표에만 남고, 새 구현 기준은 `session-gateway-usability.ko.md`다.
- 최종 검색에서 새 code와 새 sample 표면의 old public API, fake transport, retry,
  warmup sleep, 별도 serializer helper 항목은 발견되지 않았다.

## Iteration 1

상태: `implemented`

### Red Flags

- `implemented`: send/publish public submit API가 sync/no-wait 값을 노출했다.
- `implemented`: stream connector send builder가 sync `Exec()` 경로를 통해 fire-and-forget `Task.Run` queue를 사용했다.
- `verified`: public surface의 packet name override는 `WithPacketName(...)`,
  `PacketName`, `ZLinkPacketAttribute`, `ZlinkStreamPacketNameAttribute`,
  `IZlinkStreamPacketNameResolver`로 정리했다. 내부 envelope와 protocol header의
  `MessageName` 필드는 wire 호환을 위한 구현 세부로 남겼다.

### Alternatives

- 대안 1: 공통 submit runtime을 `Zlink.Framework` runtime에 두고 call builder가 operation만 넘긴다.
- 대안 2: 각 builder에서 직접 native socket을 호출하되 public API를 먼저 정리한다.

선택: 이번 변경은 대안 2를 적용했다. public API에서 오래된 실행 경로를 제거하는 범위를 먼저 닫고, 더 큰 submit runtime 통합은 다음 반복 항목으로 남겼다.

수정 결과:

- `IZLinkSendCall`, `IZLinkPublishCall`은 `ValueTask Async(...)`만 실행 함수로 가진다.
- stream connector core와 codec wrapper는 `Async(...)`만 실행 함수로 가진다.
- send builder의 sync `Exec()`와 그 내부 `Task.Run` queue를 제거했다.

남은 red flag:

- `verified`: framework send/publish/request submit queue는 아직 plan의 bounded verified queue + ready drain runtime으로 통합되지 않았다.
- `verified`: public builder 이름은 draft의 `WithPacketName(...)`과 일치한다.

## Iteration 2

상태: `implemented`

### Red Flags

- `verified`: stream connector public resolver와 attribute가 message-name 용어를
  노출했다.
- `verified`: handler attribute와 context가 public property로 `MessageName`을
  노출했다.
- `verified`: framework send/publish/request submit queue는 아직 plan의 bounded
  verified queue + ready drain runtime으로 통합되지 않았다.

### Alternatives

- 대안 1: 내부 wire 필드까지 모두 `PacketName`으로 rename한다.
- 대안 2: public API만 `PacketName`으로 정리하고, envelope/header 내부 필드는
  compatibility와 구현 안정성을 위해 유지한다.

선택: 대안 2를 선택했다. 사용자가 호환성은 고려하지 말라고 했지만, 내부 wire 필드명은
사용자 인터페이스 복잡도를 만들지 않는다. public surface를 먼저 정리하는 쪽이 변경
범위 대비 효과가 크다.

수정 결과:

- `ZLinkRequestAttribute`, `ZLinkSendAttribute`, `ZLinkEventAttribute`는
  `PacketName` property를 사용한다.
- `IZLinkHandlerContext`와 `ZLinkHandlerInvocation`은 `PacketName` property를
  노출한다.
- stream connector resolver와 attribute는 `ZlinkStreamPacketName*` 이름을 사용한다.

남은 red flag:

- `verified`: bounded verified queue + ready drain submit runtime.

## Iteration 3

상태: `implemented`

### Red Flags

- `verified`: actor/session stream builder base class가 public `SendAsync(...)` 실행
  함수를 노출했다. public 실행 함수가 `Async(...)`와 `SendAsync(...)` 두 이름으로
  갈라지면 호출자가 같은 의미를 두 번 배워야 하므로 얕은 public surface가 된다.
- `implemented`: channel/SPOT send, publish, request call builder가 각자 native
  submit을 직접 호출했다. backpressure, timeout, cancellation, runtime stop cleanup
  정책이 여러 call builder에 흩어질 위험이 있었다.
- `verified`: routed channel이 없어 session gateway가 target node를 명시하는 relay를
  표현할 수 없었다.
- `verified`: session gateway와 actor relay envelope의 body 전달 경로는 아직 실제
  smoke로 검증되지 않았다.

### Alternatives

- 대안 1: 각 call builder가 dotnet zlink public `Send(..., DontWait)` /
  `Request(..., DontWait)` 호출과 retry queue를 직접 가진다.
- 대안 2: framework 내부에 `ZLinkAsyncSubmitter`를 두고 call builder는 packet 생성과
  submit operation만 넘긴다.

선택: 대안 2를 선택했다. ready callback, verified deadline, cancellation, stop cleanup
정책을 한 모듈에 숨기면 routed channel과 기존 channel/SPOT이 같은 의미를 공유할 수
있다. framework는 native 함수를 직접 호출하지 않고 dotnet zlink public API만 사용한다.

수정 결과:

- actor/session stream builder의 실행 구현은 protected helper로 숨기고 public 실행
  함수는 `Async(...)`만 남겼다.
- `ZLinkAsyncSubmitter`가 bounded verified queue, `OnSendReady(...)` 기반 drain,
  `SendTimeout` verified deadline을 담당한다.
- channel/SPOT send, publish, request submit은 `SendFlags.DontWait` 경로로 submitter를
  사용한다.
- routed channel public API와 runtime을 추가하고 request reply는 transport request
  sequence로 매칭된다.

남은 red flag:

- `verified`: session gateway/actor relay의 internal envelope body 전달은 원본
  stream body bytes와 header snapshot을 함께 전달하도록 고쳤고, 실제 TCP stream과
  framework routed channel을 함께 쓰는 integration test로 확인했다.
- `verified`: 같은 `actorId`가 다시 bind되면 새 session context가 이전 binding을
  덮어쓴다. 이전 session의 unbind는 session id가 같을 때만 제거하므로 새 binding을
  지우지 않는다.

## Iteration 4

상태: `verified`

### Red Flags

- `verified`: session gateway internal packet이 public `ZlinkStreamHeader`를 그대로
  JSON serialize하려 했다. `ZlinkStreamMetadata`는 생성자가 닫혀 있어 wire DTO로
  쓰기 어렵고, public 계약 타입을 내부 transport serialization 형식으로 고정하는
  얕은 모듈 위험이 있었다.
- `verified`: actor relay reply가 `Reply(byte[])`를 거치면 raw body가 아니라 JSON
  `byte[]` 값으로 client stream에 전달될 수 있었다.
- `verified`: sample이 marker나 fake transport로 끝나면 framework routed channel과
  stream connector의 실제 결합을 검증하지 못한다.

### Alternatives

- 대안 1: public `ZLinkActorRelayEnvelope`와 `ZlinkStreamHeader`를 그대로 routed
  envelope body에 싣는다.
- 대안 2: internal packet DTO를 두고 stream header는 serialize 가능한 snapshot으로
  바꾸며, 원본 body bytes는 별도 `byte[]`로 싣는다.

선택: 대안 2를 선택했다. public API 타입과 internal transport DTO를 분리하면 metadata
생성 방식과 body bytes 보존 규칙이 framework 내부에 숨겨진다. 호출자는
`OpenActorRelay(...).DispatchAsync(...)`와 `SessionGateway.SendToActor(...).Async(...)`
만 알면 된다.

수정 결과:

- `ZLinkActorRelayPacket`과 `ZLinkStreamHeaderSnapshot`을 추가했다.
- session context에 raw send/request/reply helper를 추가해 request sequence와 body
  bytes를 보존했다.
- routed runtime은 `ZLink.ActorRelay`와 `ZLink.SessionGateway` internal packet을
  framework가 직접 dispatch한다.
- session-gateway sample은 실제 TCP stream connector와 framework routed channel을
  사용한다.

남은 red flag:

- `verified`: queue 7-10 범위에서 새 POSD red flag는 남지 않았다.

## Iteration 5

상태: `verified`

### Red Flags

- `verified`: `IZLinkActorRelay.Send(...)`와 `Request(...)`가 stream dispatch 중 호출될
  때 현재 stream header를 새 actor relay packet에 재사용했다. 이 때문에
  `.WithPacketName(...)`으로 지정한 packet name이 무시되고, nested request가 현재
  client packet name으로 전달되는 정보 누출이 있었다.

### Alternatives

- 대안 1: sample에서 nested API lookup을 피하고 모든 packet을 `DispatchAsync(...)`로만
  전달한다.
- 대안 2: framework public builder 의미를 바로잡아 `DispatchAsync(...)`만 원본 stream
  header를 보존하고, `Send(...)`/`Request(...)`는 builder의 packet name으로 새 header를
  만든다.

선택: 대안 2를 선택했다. public builder는 호출자가 지정한 message와 packet name을
보내는 깊은 인터페이스여야 한다. 현재 dispatch header를 암묵적으로 끌고 가는 동작은
호출자가 예측하기 어렵고, session gateway sample의 API lookup 같은 정상 흐름을 깨뜨린다.

수정 결과:

- `ZLinkActorRelaySendCall`과 `ZLinkActorRelayRequestCall`이 현재 stream dispatch header를
  재사용하지 않도록 고쳤다.
- 기존 raw stream packet 보존 경로는 `DispatchAsync(...)`에만 남겼다.
- `StreamIntegrationTests`에 nested actor relay request가 `.WithPacketName("api.resolve")`
  그대로 proxy에 도착하는 회귀 테스트를 추가했다.

검증:

```bash
dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug --no-restore --filter SessionGateway_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence
```

결과: 통과. 새 red flag는 남지 않았다.

## Iteration 6

상태: `verified`

### Red Flags

- `verified`: framework routed channel runtime이 `UseDiscovery(...)` 설정을
  `RouterSocket`에 attach하지 않았다. 일반 channel discovery는 동작하지만
  `AddRoutedChannel(...)`은 manual connection 없이는 peer를 자동 연결하지 못했다.
- `verified`: session-gateway sample에 retry를 넣어 discovery 문제를 숨기는 방향은
  얕은 sample이 된다. sample은 연결 정책을 보정하지 않고, framework가 discovery
  계약을 지켜야 한다.

### Alternatives

- 대안 1: sample에서 create/join request를 retry한다.
- 대안 2: dotnet zlink/core와 framework를 분리한 회귀 테스트로 원인을 확인하고,
  framework routed channel runtime에서 discovery attach를 구현한다.

선택: 대안 2를 선택했다. dotnet zlink/core의 `Discovery + RouterSocket`는 정상이며,
framework가 discovery를 붙이지 않은 것이 원인이었다. sample에 retry를 넣으면
framework 버그를 가리고 사용자가 배워야 할 핵심 흐름도 흐려진다.

수정 결과:

- `ZLinkRoutedChannelRuntime`이 discovery handle을 소유하고 dispose한다.
- `ZLinkFrameworkRuntime.InitializeRoutedChannels(...)`가 manual connection이 없고
  global `UseDiscovery(...)`가 있으면 `RouterSocket.AttachDiscovery(...)`를 호출한다.
- routed channel validation은 discovery와 manual connection 혼용을 막는다.
- session-gateway sample은 embedded registry와 `UseDiscovery(...)`만 사용하고
  `UseManualConnections(...)`, retry helper, 고정 discovery wait를 제거했다.

검증:

```bash
dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Debug --no-restore --filter discovery_attached_routers_exchange_routed_request
dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug --no-restore --filter RoutedRequest_WorksAcrossDiscoveryAttachedRouters
dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug --no-build
```

결과: 모두 통과. 새 red flag는 남지 않았다.

## Iteration 7

상태: `implemented`

### Red Flags

- `implemented`: session 표면에 자동 relay builder를 두면 framework가 actor 생성 여부,
  actor type 선택, session binding 갱신 시점을 모두 정해야 한다.
  이는 application 정책을 framework builder로 끌어올리는 얕은 모듈 위험이다.
- `implemented`: session과 session proxy에 handler registry를 두면 actor/node/spot
  실행 문맥의 handler registry와 경쟁한다. 사용자는 같은 message handler를 어디에
  등록해야 하는지 다시 판단해야 한다.

### Alternatives

- 대안 1: 자동 session relay builder를 추가하고 authenticator, route resolver,
  location writer를 모두 builder에 연결한다.
- 대안 2: session은 기존 callback을 유지하고, framework는
  `CreateActorAsync(...)`, `CreateRemoteActorAsync(...)`,
  `DispatchToActorAsync(...)`처럼 actor create와 request sequence를 보존하는 helper만
  제공한다.

선택: 대안 2를 선택했다. session ingress는 인증, actor 배치, actor 재사용, remote actor
생성 시점 같은 domain 정책을 가장 많이 아는 위치다. framework가 자동 builder로 이 결정을
대신하면 설정 표면이 커지고 예외 규칙이 늘어난다. 대신 반복되면 위험한 transport
mechanics만 helper 아래에 숨기는 쪽이 깊은 모듈에 가깝다.

수정 결과:

- `session-gateway-usability.ko.md`에서 자동 session relay builder와
  authenticator/forward builder 예시를 제거했다.
- session과 session proxy에는 handler registry를 두지 않고, handler registry는
  actor/node/spot 실행 문맥에만 둔다는 기준을 명시했다.
- session callback 예시는 인증 뒤 사용자가 actor node를 고르고
  `CreateActorAsync(...)` 또는 `CreateRemoteActorAsync(...)`로 actor를 만든 뒤 동일한
  `DispatchToActorAsync(...)`를 쓰는 형태로 바꿨다.
- actor handler 등록 예시는 전역 registry가 아니라 actor 객체의 `Configure()` 안에서
  `Context.AddPacket<THandler>()`를 호출하는 형태로 정정했다. spot handler도 spot 객체
  안에서 등록한다.
- actor/spot route resolver 입력은 request 객체나 metadata를 받지 않고 `actorId` 또는
  `spotId` 하나만 받도록 정리했다. metadata가 resolver로 들어가면 route 조회가 작은
  dispatcher가 되어 정보 은닉을 깨뜨리기 때문이다.
- `IZLinkActorSessionLocationWriter`는 framework 기본 registry 구현을 두지 않는 대신,
  registry discovery metadata를 사용하는 sample adapter를 draft에 추가했다. writer와
  resolver가 같은 key를 공유하되 resolver 입력은 `actorId` 하나로 유지한다.
- registry metadata sample은 reconnect 경쟁을 숨기지 않도록 `BindingToken` 조건부 삭제를
  명시했다. atomic compare/delete가 없으면 read 후 delete로 흉내 내지 말고 registry
  기능을 추가하거나 별도 store를 써야 한다고 기록했다.
- `SessionGateway`, `AddSessionProxyHandler(...)`, direct target send/request 이름은
  compatibility layer 없이 제거하고, 새 public 이름으로 한 번에 교체하는 breaking
  change로 기록했다.

검증:

결과: 자동 session relay builder 흔적은 draft에 남지 않았다. 구현 전 draft 갱신이므로
build 검증은 수행하지 않았다.

## Iteration 8

상태: `implemented`

### Red Flags

- `implemented`: draft가 application actor 객체와 session dispatch handle을 모두
  `IZLinkActor`로 표현했다. 이 상태로 구현하면 remote actor handle에 `Configure()`나
  actor context를 억지로 붙이거나, actor 객체 생성자 주입 계약을 흐리게 된다.
- `implemented`: metadata 전달 정책이 개념 설명에 머물러 있어 구현자가 raw stream
  header, framework routing metadata, application metadata의 경계를 임의로 정해야 했다.
- `implemented`: actor/session location writer sample은 있었지만 writer와 resolver를
  어떻게 같은 registry metadata store에 등록하는지, stale unbind를 어떤 atomic 조건으로
  처리하는지 구현 단서가 부족했다.
- `implemented`: `IZLinkActorPlayRouteResolver`가 어느 public API에서 호출되는지
  명확하지 않아 session helper가 actor ref를 가진 뒤에도 resolver를 다시 호출하는
  shallow path가 생길 수 있었다.

### Alternatives

- 대안 1: 기존 이름을 유지하고 설명 문장으로 local actor와 remote actor의 차이를
  보완한다.
- 대안 2: actor 실행 객체와 dispatch handle을 타입으로 분리하고, lifecycle과 metadata
  policy를 계약 형태로 문서화한다.

선택: 대안 2를 선택했다. 타입 이름이 책임 경계를 표현해야 구현자가 예외 규칙을 덜
만든다. `IZLinkActor`는 actor node에서 생성되는 객체로, `IZLinkActorRef`는 session이
dispatch에 사용하는 handle로 분리하는 쪽이 깊은 모듈에 맞다.

수정 결과:

- session helper 반환 타입과 `DispatchToActorAsync(...)` 인자를 `IZLinkActorRef`로
  정리하고, `IZLinkActor`는 `ActorId`와 `Configure()`를 가진 실행 객체로 정의했다.
- remote actor marker는 제거했다. local/remote 차이는 create 함수와 internal
  handle 구현에 숨기고, dispatch caller는 같은 `IZLinkActorRef`만 본다.
- `CreateActorAsync(...)`와 `CreateRemoteActorAsync(...)` lifecycle을 writer 호출,
  local binding table 갱신, writer 실패 시 rollback, disconnect unbind 순서까지
  구현 가능한 단계로 작성했다.
- `ZLinkMessageMetadata`, `IZLinkMessageMetadataPolicy`,
  `ConfigureMetadata(...).ForwardApplicationKey(...)` 형태의 metadata 전달 계약을
  추가했다. resolver 입력 metadata는 되살리지 않았다.
- `IZLinkSessionProxy`와 `IZLinkActorClient` call builder 계약을 `Async(...)` 실행 함수
  기준으로 추가했다.
- registry discovery metadata sample은 같은 store instance를 writer와
  `IZLinkActorSessionRouteResolver`로 등록하는 예시를 추가했다.
- `IZLinkActorPlayRouteResolver`는 `IZLinkActorClient`에서만 호출하고,
  session `DispatchToActorAsync(...)`는 `IZLinkActorRef`의 resolved target을 사용한다고
  명시했다.
- old raw actor relay public registration은 제거 대상이며, custom adapter는 framework
  internal path로만 둔다고 정리했다.

검증:

결과: actor 객체와 actor handle의 타입 혼선, resolver metadata 입력, 오래된 route key
관련 표현은 draft에서 제거했다. 구현 전 draft 갱신이므로 build 검증은 수행하지 않았다.

## Iteration 9

상태: `implemented`

### Red Flags

- `implemented`: `session-gateway-usability.ko.md`의 테스트 기준이 happy path 중심이라
  writer rollback, stale unbind, missing resolver validation, metadata deny, registry
  conditional delete 같은 실패 회귀를 구현자가 놓칠 수 있었다.
- `implemented`: draft 내용을 정식 spec에 반영한 뒤 전체 spec/draft를 다시 대조하는
  단계가 plan에 없어, 이전 `SessionGateway` 문서와 새 session actor dispatch 문서가
  동시에 서로 다른 기준처럼 남을 위험이 있었다.

### Alternatives

- 대안 1: 기존 테스트 표는 유지하고 구현자가 필요한 실패 테스트를 추론하게 한다.
- 대안 2: draft의 테스트 기준을 실패와 validation까지 포함한 회귀 항목으로 나누고,
  plan에 정식 spec 반영 및 전체 spec/draft 충돌 리뷰 phase를 추가한다.

선택: 대안 2를 선택했다. 회귀 테스트는 구현자의 추론에 맡기기보다 깨지면 안 되는
동작을 문서에 고정해야 한다. 또한 draft가 정식 spec으로 반영된 뒤에는 이전 문서와
새 문서의 개념 충돌을 별도 단계로 닫아야 한다.

수정 결과:

- `session-gateway-usability.ko.md`의 테스트 기준을 typed dispatch, resolver/direct target,
  actor create/session location lifecycle, metadata/codec/timeout/error, discovery/registry
  sample 항목으로 나누어 보강했다.
- `session-actor-dispatch-implementation-plan.ko.md`에 draft 내용을 정식 spec에 반영하는
  phase를 추가했다.
- 같은 plan에 전체 spec/draft 개념 충돌 리뷰 phase를 추가하고, 현재 확인된 충돌 후보
  문서를 기록했다.

검증:

결과: 추가된 테스트 기준과 plan 단계는 문서 변경이다. 금지 표현 검색과 표 형식 확인을
수행했고, build 검증은 수행하지 않았다.

## Spec/Draft Cross Review 1

상태: `implemented`

### Findings

- `verified`: `policy/session-gateway.ko.md`가 기존 `EnableSessionGateway()`,
  `AddSessionProxyHandler<THandler>()`, `BindActorAsync(...)`, `OpenActorRelay(...)`,
  `IZLinkSessionGateway.SendToActor(...)` 중심의 public API를 현재 기준처럼 설명한다.
  새 `session-gateway-usability.ko.md`는 이 표면을 제거하고 `SessionProxy`와
  `CreateActorAsync(...)`/`CreateRemoteActorAsync(...)`/`DispatchToActorAsync(...)`로
  바꾸므로 두 draft가 동시에 읽히면 기준이 충돌한다.
- `verified`: `.NET` interface 문서와 stream 문서가 `AttachActorAsync(...)`,
  `DisconnectActorAsync(...)` 기반 session actor bridge를 public 표면으로 설명한다.
  새 draft는 actor 실행 객체와 dispatch handle을 분리하고 session은 `IZLinkActorRef`를
  저장해야 하므로 같은 개념의 API가 일치하지 않는다.
- `verified`: `spec/sample/tictactoe/session-gateway.ko.md`가 기존
  `OpenActorRelay(...)`, `BindActorAsync(...)`, `SendToActor(...)` 흐름을 sample 완료
  기준으로 적고 있다. 새 sample 기준은 session actor dispatch helper와 `SessionProxy`다.
- `verified`: `regression-test-matrix.ko.md`에는 새 resolver, writer, binding token,
  metadata policy, `SessionProxy` 회귀 항목이 아직 반영되지 않았고, 기존
  `AttachActorAsync(...)` 회귀 항목이 남아 있다.
- `verified`: `policy/README.ko.md`는 `session-gateway.ko.md`와
  `session-gateway-usability.ko.md`를 같은 단계의 연속 문서처럼 나열한다. 지금 상태에서는
  어떤 문서가 새 기준인지 명시하지 않아 reader가 old API를 현재 목표로 오해할 수 있다.

### Search Used

```bash
grep -RIn "EnableSessionGateway\\|AddSessionProxyHandler\\|IZLinkSessionGateway\\|SendToActor\\|RequestActor\\|OpenActorRelay\\|IZLinkSessionProxyHandler\\|BindActorAsync\\|AttachActorAsync\\|DisconnectActorAsync" framework/doc/spec
grep -RIn "RouteKey\\|route key\\|ZLinkSpotRouteRequest\\|ZLinkActor.*RouteRequest\\|metadata.*resolver\\|resolver.*metadata\\|SpotNodeId\\|direct target" framework/doc/spec
grep -RIn "InMemoryRoutedChannel\\|UseManualConnections\\|Warmup\\|RouteWarmup\\|SampleJson\\|System.Text.Json\\|ExecAsync\\|WithDontWait\\|\\.Sync\\|\\.SendAsync(" framework/doc/spec
```

### Next Action

이 리뷰는 충돌 식별 단계다. 다음 문서 정리 단계에서는 기존
`session-gateway.ko.md`와 sample 문서를 새 session actor dispatch 모델로 갱신하거나,
명확히 superseded 문서로 표시해야 한다. `.NET` binding 문서의 session actor bridge도
새 `IZLinkActorRef` 기반 표면으로 맞춘다.

## Spec/Draft Cross Review 2

상태: `implemented`

### 수정 결과

- `policy/session-gateway.ko.md`를 superseded 초안으로 표시하고, 현재 구현 기준은
  `session-gateway-usability.ko.md`라고 명시했다. 이 문서에 남은 old API 이름은 이전
  설계 배경을 설명하는 문맥으로만 분류한다.
- `policy/README.ko.md`에서 `session-gateway.ko.md`는 이전 초안,
  `session-gateway-usability.ko.md`는 현재 session actor dispatch 기준 초안으로 구분했다.
- `spec/sample/tictactoe/README.ko.md`, `direct.ko.md`,
  `session-gateway.ko.md`의 sample 설명을 `SessionProxy`와 session actor dispatch helper
  기준으로 갱신했다.
- `.NET` `handler-interfaces.ko.md`, `aspnet-core-stream.ko.md`,
  `stream-samples.ko.md`에서 `AttachActorAsync(...)`/`DisconnectActorAsync(...)` 표면을
  `CreateActorAsync(...)`, `CreateRemoteActorAsync(...)`,
  `DispatchToActorAsync(IZLinkActorRef, ...)` 기준으로 바꿨다.
- `spot-samples.ko.md`의 actor join 설명과 sample 흐름도 session actor create 기준으로
  맞췄다.
- `regression-test-matrix.ko.md`의 session actor bridge 회귀 항목을 새 API 기준으로
  갱신하고 writer rollback, stale binding token guard 항목을 추가했다.

### 재검색 결과

- sample 문서와 `.NET` binding 문서에는 `BindActorAsync(...)`, `AttachActorAsync(...)`,
  `DisconnectActorAsync(...)`, `OpenActorRelay(...)`, `SendToActor(...)` 기준의 현재 표면
  설명이 남지 않았다.
- 전체 spec 검색에서 남는 old session gateway API 이름은 superseded 처리된
  `policy/session-gateway.ko.md`와 새 `session-gateway-usability.ko.md`의 제거 설명
  문맥이다.
- `UseManualConnections(...)`와 `Warmup` 검색 결과는 일반 channel/spot/manual connection
  문서와 다른 언어 샘플의 service 이름이다. session actor dispatch sample 금지 항목과는
  직접 충돌하지 않는다.

### 남은 주의점

- `policy/session-gateway.ko.md` 전체를 완전히 삭제하거나 새 모델로 전면 재작성할지는
  구현 완료 뒤 정식 spec 반영 단계에서 결정한다. 현재는 superseded 표시로 reader 충돌을
  막는다.

## Spec/Draft Cross Review 3

상태: `implemented`

### 추가 발견

- `spot-samples.ko.md`에는 아직 `IZLinkActor.Context { get; set; }`와
  `actor.Context.GetSpot<SampleSpot>()` 예시가 남아 있었다. 이는 actor context
  constructor 주입 기준과 충돌하고, session/handler가 actor 내부 framework context에
  직접 접근하게 만드는 얕은 인터페이스다.
- `handler-interfaces.ko.md`의 `IZLinkActorContext`가 `SessionId`를 노출했다. 이 값은
  actor가 session 위치를 직접 저장해도 된다는 신호로 읽힐 수 있고, reconnect 뒤 stale
  route를 막기 위해 session route resolver/writer로 위치 정보를 제한한다는 새 기준과
  충돌한다.

### 수정 결과

- `spot-samples.ko.md`의 actor contract와 sample code를 constructor-injected
  `IZLinkActorContext` 기준으로 바꿨다. actor context는 actor 내부의 private field로
  숨기고, join/reply/spot 조회는 actor method로 감싸도록 정리했다.
- `handler-interfaces.ko.md`의 `IZLinkActorContext.SessionId`를 제거하고, session id,
  session router id, binding token은 runtime metadata와 session route resolver/writer의
  책임이라고 명시했다.

### 재검색 결과

- sample 문서와 `.NET` binding 문서에서 `IZLinkActor.Context`,
  `actor.Context`, `Actor.Context`, actor context setter 형태가 더 이상 검색되지 않는다.
- actor/session 위치 정보 검색 결과에서 actor context의 session id 노출은 제거되었고,
  session context와 stream 자체의 `SessionId`만 남았다.

### POSD 판단

- context 공개 속성을 없애 actor 객체가 자기 framework context를 감추게 했으므로 정보
  은닉이 좋아졌다.
- session 위치 정보는 resolver/writer와 runtime metadata로 한정되어 stale state
  위험 신호를 줄였다.
- 현재 sample과 `.NET` binding draft에서 서로 다른 actor/session 모델이 경쟁하는
  충돌은 발견되지 않았다.
