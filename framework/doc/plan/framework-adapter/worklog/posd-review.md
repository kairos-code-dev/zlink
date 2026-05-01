# Framework POSD Review

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

- `pending`: framework send/publish/request submit queue는 아직 plan의 bounded pending queue + ready drain runtime으로 통합되지 않았다.
- `verified`: public builder 이름은 draft의 `WithPacketName(...)`과 일치한다.

## Iteration 2

상태: `implemented`

### Red Flags

- `verified`: stream connector public resolver와 attribute가 message-name 용어를
  노출했다.
- `verified`: handler attribute와 context가 public property로 `MessageName`을
  노출했다.
- `pending`: framework send/publish/request submit queue는 아직 plan의 bounded
  pending queue + ready drain runtime으로 통합되지 않았다.

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

- `pending`: bounded pending queue + ready drain submit runtime.

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
- `pending`: session gateway와 actor relay envelope의 body 전달 경로는 아직 실제
  smoke로 검증되지 않았다.

### Alternatives

- 대안 1: 각 call builder가 dotnet zlink public `Send(..., DontWait)` /
  `Request(..., DontWait)` 호출과 retry queue를 직접 가진다.
- 대안 2: framework 내부에 `ZLinkAsyncSubmitter`를 두고 call builder는 packet 생성과
  submit operation만 넘긴다.

선택: 대안 2를 선택했다. ready callback, pending deadline, cancellation, stop cleanup
정책을 한 모듈에 숨기면 routed channel과 기존 channel/SPOT이 같은 의미를 공유할 수
있다. framework는 native 함수를 직접 호출하지 않고 dotnet zlink public API만 사용한다.

수정 결과:

- actor/session stream builder의 실행 구현은 protected helper로 숨기고 public 실행
  함수는 `Async(...)`만 남겼다.
- `ZLinkAsyncSubmitter`가 bounded pending queue, `OnSendReady(...)` 기반 drain,
  `SendTimeout` pending deadline을 담당한다.
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
