# Framework Adapter Implementation Checklist

이 파일은 `full-implementation-and-sample-plan.ko.md` Phase 0에서 만든 작업 로그다.
상태 값은 `pending`, `implemented`, `verified`, `not-applicable`만 사용한다.

## Current Direction Note

상태: `implemented`

이 파일의 이전 queue 기록에는 `SessionGateway`, `BindActorAsync(...)`,
`OpenActorRelay(...)`, `IZLinkSessionGateway.SendToActor(...)` 기준의 구현 이력이 남아
있다. 이 기록은 당시 작업 이력으로 보존하지만, 새 구현 기준으로 사용하지 않는다.

현재 기준은 아래 문서다.

- `framework/doc/spec/draft/framework-adapter/policy/session-gateway-usability.ko.md`
- `framework/doc/plan/framework-adapter/session-actor-dispatch-implementation-plan.ko.md`

새 기준에서는 session -> actor 방향은 `CreateActorAsync(...)`,
`CreateRemoteActorAsync(...)`, `DispatchToActorAsync(IZLinkActorRef, ...)`로 표현하고,
actor -> client 방향만 `SessionProxy`로 표현한다. `SessionGateway` 이름과 direct target
send/request API는 제거 대상이다.

## Phase 0 Inventory

### Project Paths

상태: `implemented`

- Framework solution: `framework/languages/dotnet/Zlink.Framework.sln`
- Framework tests:
  - `framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.RuntimeTests/Zlink.Framework.RuntimeTests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.MonitoringRuntimeTests/Zlink.Framework.MonitoringRuntimeTests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj`
  - `framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj`
- Current TicTacToe sample projects:
  - `framework/languages/dotnet/samples/TicTacToe/Shared/TicTacToe.Shared.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Server/TicTacToe.Server.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Client/TicTacToe.Client.csproj`
- Planned sample projects not yet present:
  - `framework/languages/dotnet/samples/TicTacToe(session-gateway)` 아래 gateway
    version sample. 기존 `TicTacToe/` sample과 파일을 공유하지 않는다.

`framework/samples` 디렉토리는 현재 없으므로 sample 범위는 실제 경로인
`framework/languages/dotnet/samples`를 기준으로 진행한다. 계획 문서의 검증 명령도
없는 경로에서 실패하지 않도록 조건부 검색으로 수정했다.

### Old Submit API Inventory

상태: `verified`

초기 발견 항목:

- `Zlink.Framework` runtime: `WithDontWait()`, `Sync()`가 channel, SPOT, actor channel send/publish call에 남아 있었다.
- Stream connector: `Exec()`, `ExecAsync()`가 core builder와 JSON, MessagePack, Protobuf, Auto codec wrapper에 남아 있었다.
- Tests and fixtures: framework tests, stream connector tests, doc fixtures, test host가 오래된 실행 이름을 사용했다.
- Sample: current TicTacToe client가 `ExecAsync<TReply>()`를 사용했다.

처리 결과:

- `WithDontWait()`, `Sync()`, `Exec()`, `ExecAsync()` 항목은 모두 `.Async(...)`로
  변경되었다.
- framework public builder 실행 함수와 draft sample 호출부의 `SendAsync(...)`도
  제거했다. `WebSocket.SendAsync(...)`처럼 외부 transport API 호출은 금지 대상이
  아니다.

현재 검증:

```bash
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/languages/dotnet framework/doc/spec/draft/framework-adapter
if [ -d framework/samples ]; then
  rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/samples
fi
```

검색 결과 중 외부 transport API 호출을 제외한 framework public builder 실행 함수와
draft sample `.SendAsync(...)` 호출은 queue 4번에서 제거한다.

## Reference Documents

### policy/README.ko.md

- API: `not-applicable` -- 묶음 목차 문서라 별도 API 계약을 갖지 않는다.
- Behavior: `not-applicable` -- 세부 동작은 하위 주제 문서에서 확인한다.
- Failure: `not-applicable` -- 실패 의미는 하위 주제 문서에서 확인한다.
- Test: `verified` -- queue 4-10 구현 뒤 관련 framework test와 sample game flow를 실행했다.
- Sample: `verified` -- 기존 TicTacToe sample과 별도 session-gateway sample 경로를 구분했다.

### policy/overview.ko.md

- API: `implemented` -- channel, routed channel, stream, SPOT, registry, monitoring 축이 코드에 존재한다.
- Behavior: `implemented` -- session gateway는 routed channel과 stream binding 위에서 동작한다.
- Failure: `implemented` -- runtime 실패는 framework exception과 routed error reply로 전달된다.
- Test: `verified` -- routed channel, stream, session gateway integration test를 통과했다.
- Sample: `verified` -- session-gateway sample이 실제 routed channel과 stream connector를 사용한다.

### policy/framework-api.ko.md

- API: `implemented` -- send/publish/request 실행 이름은 `Async(...)`로 맞췄다.
- Behavior: `implemented` -- 오래된 no-wait public option은 제거했다.
- Failure: `implemented` -- async submit timeout, cancellation, routed error reply 경로를 구현했다.
- Test: `verified` -- framework, runtime, multiprocess test 통과.
- Sample: `verified` -- 기존 sample과 session-gateway sample이 현재 public API를 사용한다.

### policy/interaction-model.ko.md

- API: `implemented` -- submit builder의 public 실행 함수는 `Async(...)`만 남았다.
- Behavior: `implemented` -- stream connector send는 transport async write를 직접 사용한다.
- Failure: `implemented` -- request reply는 request sequence 기준으로 매칭하고 timeout 시 pending을 정리한다.
- Test: `verified` -- connector request sequence test와 framework request tests 통과.
- Sample: `verified` -- sample은 `.Async(...)` 실행 표면만 사용한다.

### policy/message-model.ko.md

- API: `implemented` -- framework envelope와 stream helper header가 packet name, kind, correlation/request sequence를 보존한다.
- Behavior: `implemented` -- session gateway internal packet은 원본 stream body bytes와 header snapshot을 함께 전달한다.
- Failure: `implemented` -- error reply envelope와 stream request timeout 경로를 유지한다.
- Test: `verified` -- 같은 packet name의 동시 routed request가 sequence 기준으로 분리됨을 확인했다.
- Sample: `verified` -- session-gateway sample이 client request와 notify packet을 분리해서 사용한다.

### policy/channel-topology.ko.md

- API: `implemented` -- `AddChannel`, `AddRoutedChannel`, discovery, registry query surface가 코드에 있다.
- Behavior: `implemented` -- routed channel은 target `RoutingId`를 public call에서 받는다.
- Failure: `implemented` -- missing handler, send failure, routed error reply를 호출자에게 전달한다.
- Test: `verified` -- channel/routed/registry integration tests를 통과했다.
- Sample: `verified` -- session-gateway sample이 embedded registry와 `UseDiscovery(...)`로
  session/api/play routed mesh를 자동 구성한다.

### policy/session-gateway.ko.md

- API: `implemented` -- `EnableSessionGateway`, `AddSessionProxyHandler`,
  `IZLinkSessionGateway`, `IZLinkActorRelay`, `BindActorAsync`를 추가했다.
- Behavior: `verified` -- actor relay와 session gateway가 routed channel 위에서 stream body와 request sequence를 보존한다.
- Failure: `implemented` -- binding 누락과 handler 실패는 routed error reply 또는 exception으로 전달한다.
- Test: `verified` -- 실제 TCP stream과 routed channel을 함께 쓰는 session gateway integration test를 통과했다.
- Sample: `verified` -- 별도 `TicTacToe(session-gateway)` sample이 실제 framework API와
  discovery 기반 routed channel로 통과했다.

### bindings/dotnet/handler-interfaces.ko.md

- API: `implemented` -- `IZLinkSendCall`, `IZLinkPublishCall`에서 `WithDontWait()`와 `Sync()` 제거.
- Behavior: `implemented` -- in-repo caller는 send/publish에서 `.Async(...)`를 사용한다.
- Failure: `implemented` -- handler 실패는 caller-visible exception 또는 routed error reply로 모은다.
- Test: `verified` -- `Zlink.Framework.Tests`, `Zlink.Framework.RuntimeTests`, `Zlink.Framework.MultiProcessTests` 통과.
- Sample: `verified` -- sample handler와 call builder가 현재 interface를 사용한다.

### bindings/dotnet/streaming-client.ko.md

- API: `verified` -- stream connector core/JSON/MessagePack/Protobuf/Auto codec wrapper에서
  `Exec`, `ExecAsync`를 제거했다. public `ZlinkStreamConnectorOptions.SendTimeout`과
  stream send builder `WithTimeout(...)`도 제거했다.
- Behavior: `implemented` -- send builder는 `ValueTask Async(...)`, request builder는 `ValueTask<TReply> Async<TReply>(...)`를 사용한다.
- Failure: `implemented` -- request timeout과 disconnected failure는 connector error로
  전달되고 pending request를 정리한다.
- Test: `verified` -- `Systems.Zlink.Stream.Connector.Tests` 통과.
- Sample: `implemented` -- TicTacToe client가 connector `.Async<TReply>(...)`를 사용한다.

### TicTacToe sample specs

- API: `implemented` -- 기존 `Client/Server/Shared` sample 구조를 보존하고,
  connector 실행 이름만 현재 `Async(...)` API에 맞췄다.
- Behavior: `implemented` -- 기존 sample의 API server, Play server, stream client,
  actor/game room 흐름을 유지한다.
- Failure: `implemented` -- sample game flow에서 framework routed error 없이 정상 reply/notify를 확인했다.
- Test: `verified` -- `framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln`
  Debug build 통과.
- Sample: `verified` -- SessionGateway sample은 별도 project로 추가했고 실제 framework
  routed channel public API 위에서 동작한다.

## Known Gaps

- `verified`: 원래 TicTacToe sample은 `Client/Server/Shared` 구조로 복원했다.
  기존 sample을 `Direct/`로 재구성하지 않는다.
- `implemented`: 기존 sample client의 오래된 `ExecAsync(...)` 호출만 현재
  connector API인 `Async(...)`로 바꿨다. sample의 server/client/game 구조는
  바꾸지 않았다.
- `verified`: SessionGateway sample은 실제 framework `AddRoutedChannel(...)`,
  session gateway/actor relay public API를 사용하며 별도
  `TicTacToe(session-gateway)/` project로 추가했다. in-memory routed channel은
  사용하지 않는다.
- `verified`: solution은 원래 `TicTacToe.Shared`, `TicTacToe.Server`,
  `TicTacToe.Client` project를 다시 참조한다.

## Autonomous Resume Queue

이 저장소가 현재 상태에서 다시 열리면 아래 순서대로 사람에게 묻지 않고 계속
진행한다.

이 queue의 `pending` 항목이 재개 순서의 단일 기준이다. 위 Reference Documents 표에
남은 일반 `pending`은 queue 10번 문서 대조 단계에서 해소하며, 이 queue보다 먼저
잡지 않는다.

현재 새 컨텍스트에서 바로 시작할 작업은 4번이다. 1-3번은 이미 `verified` 상태이므로
되돌리거나 다시 설계하지 않는다.

1. `verified`: solution 파일의 duplicate project entry를 정리하고 기존 TicTacToe
   sample project를 유지한다. 정리 후 Debug/Release build를 다시 실행한다.
   - 유지할 entry:
     - `Systems.Zlink.Stream.Connector.Json` GUID `{930BD426-7750-4A83-9960-FB690DFEB1D8}`
     - `Systems.Zlink.Stream.Connector` GUID `{EF560B85-2ED8-43D1-B6C4-8B9A86EA5A99}`
   - 제거할 duplicate entry와 관련 `ProjectConfigurationPlatforms`, `NestedProjects` 행:
     - `Systems.Zlink.Stream.Connector.Json` GUID `{4ED534BB-0AB0-43C4-A08E-9CCD313F09F9}`
     - `Systems.Zlink.Stream.Connector` GUID `{A5301BBD-A51D-421D-A3E6-07EB5F2FB4E3}`
   - 정리 뒤 실행:
     ```bash
     /home/hep7/.dotnet/dotnet sln framework/languages/dotnet/Zlink.Framework.sln add \
       framework/languages/dotnet/samples/TicTacToe/Shared/TicTacToe.Shared.csproj \
       framework/languages/dotnet/samples/TicTacToe/Server/TicTacToe.Server.csproj \
       framework/languages/dotnet/samples/TicTacToe/Client/TicTacToe.Client.csproj
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
     ```
2. `verified`: `WithPacketName(...)` public builder 이름을 draft의
   `WithPacketName(...)`으로 맞춘다. 내부 envelope 필드명은 필요한 만큼만 유지하고,
   public surface에는 packet 용어를 사용한다.
3. `verified`: 기존 TicTacToe sample을 보존한다. `Client/Server/Shared` root
   project를 `Direct/` project로 바꾸지 않는다. 필요한 API 이름 변경은 현재
   framework public API에 맞추는 최소 수정으로 제한한다.
4. `verified`: 오래된 public `SendAsync(...)` 실행 표면과 draft sample 예시를
   제거한다.
   - framework public builder 실행 함수는 `Async(...)`만 남긴다.
   - actor/session stream send/reply builder의 public `SendAsync(...)`는 제거하거나
     private helper로 숨긴다.
   - draft sample의 `.SendAsync(...)` 예시는 `.Async(...)`로 맞춘다.
   - `WebSocket.SendAsync(...)` 같은 외부 transport API 호출은 유지한다.
   - 검증:
     ```bash
     rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/languages/dotnet framework/doc/spec/draft/framework-adapter
     ```
     `framework/languages/dotnet` 검색 결과는 `WebSocket.SendAsync(...)` 같은 외부
     transport API 호출만 남아야 한다. `framework/doc/spec/draft/framework-adapter`
     검색 결과에는 framework public builder 실행 함수나 draft sample의
     `.SendAsync(...)` 호출이 남으면 안 된다.
   - 검증 결과: 위 검색에서
     `Systems.Zlink.Stream.Connector/Transport/WebSocketConnection.cs`의
     `WebSocket.SendAsync(...)`만 남았다.
5. `implemented`: framework async submit runtime을 bounded pending queue + ready drain
   모델로 통합한다. 먼저 기존 channel/spot send, publish, request submit에 적용하고,
   routed send/request가 같은 runtime을 재사용할 수 있게 runtime interface를 숨긴다.
   - 근거 draft:
     [handler-interfaces.ko.md](../../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md),
     [lifecycle-and-failure-semantics.ko.md](../../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md),
     [interaction-model.ko.md](../../../spec/draft/framework-adapter/policy/interaction-model.ko.md),
     [framework-api.ko.md](../../../spec/draft/framework-adapter/policy/framework-api.ko.md)
   - framework `.NET` 구현의 ready drain 시작점은 socket `OnSendReady(...)`
     callback 하나로 고정한다. `zlink_send_ready_handler`와 `ZLINK_POLLOUT`은
     그 아래 native/core 계층의 구현 경로이며, framework가 세 신호를 각각
     감시하지 않는다.
   - `SendTimeout` 값은 core blocking send에 넘겨 timeout을 기다리는 용도가 아니라
     framework async pending deadline으로 사용한다. 값 해석은 현재 `.NET`
     binding/core option과 맞춘다. `.NET` `SendTimeout = null`은 core `-1`과 같은
     무한 대기, `TimeSpan.Zero`는 no-wait submit, 양수 값은 그 시간 안에 submit하지
     못하면 실패다. 음수 `TimeSpan`은 option setter에서 허용하지 않는다.
   - framework channel/socket option의 기본 `SendTimeout`은
     `TimeSpan.FromMilliseconds(200)`으로 설정한다. async submit runtime은 core socket
     기본값을 직접 사용하지 않고, framework가 socket/channel option에 설정한
     resolved `SendTimeout` 값을 읽어 사용한다. 사용자가 `SendTimeout = null`을
     명시한 경우에만 core `-1`과 같은 무한 대기로 본다.
   - stream connector public options에는 `SendTimeout`을 두지 않는다.
     `ZlinkStreamConnectorOptions.SendTimeout`과 stream send builder
     `WithTimeout(...)`은 제거했다. connector request reply 대기는 `RequestTimeout`만
     사용한다.
   - `Task.Run`으로 blocking send를 감싸지 않는다는 조건과 HWM/ready drain,
     timeout, cancellation, runtime stop cleanup을 test로 확인한다.
   - 구현 결과: `ZLinkAsyncSubmitter`를 추가하고 channel send/publish/request,
     SPOT send/publish/request submit이 dotnet zlink public
     `OnSendReady(...)`와 `SendFlags.DontWait` 경로를 사용하게 했다.
   - 검증 결과:
     `/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug --no-restore --filter ZLinkAsyncSubmitterTests`
     통과.
   - 남은 확인: queue 12에서 전체 build/test/smoke를 다시 실행한다.
6. `verified`: framework `.NET` routed channel public API를 구현한다.
   - 근거 draft:
     [session-gateway.ko.md](../../../spec/draft/framework-adapter/policy/session-gateway.ko.md),
     [handler-interfaces.ko.md](../../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md)
   - `AddRoutedChannel(...)`
   - `IZLinkRoutedClient`
   - `IZLinkRoutedSendCall`
   - `IZLinkRoutedRequestCall`
   - routed handler registry와 dispatch
   - request sequence 기준 reply matching
   - 같은 packet name의 동시 request가 sequence 기준으로 분리되는 test
   - routed send/request submit은 queue 5번의 공통 async submit runtime을 사용한다.
   - 구현 결과: `AddRoutedChannel(...)`, `IZLinkRoutedClient`,
     `IZLinkRoutedSendCall`, `IZLinkRoutedRequestCall`, routed send/request handler,
     별도 routed handler registry/runtime을 추가했다.
   - 검증 결과:
     `/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug --no-restore --filter RoutedChannelIntegrationTests`
     통과. 같은 packet name의 동시 request가 request sequence 기준으로 분리됨을
     확인했다.
7. `verified`: framework `.NET` session gateway와 actor relay API를 구현한다.
   - 근거 draft:
     [session-gateway.ko.md](../../../spec/draft/framework-adapter/policy/session-gateway.ko.md),
     [framework-api.ko.md](../../../spec/draft/framework-adapter/policy/framework-api.ko.md)
   - Session server는 client stream과 `actorId -> stream` binding을 소유한다.
   - API server는 application location store interface를 소유한다.
   - Play server는 actor와 game room을 소유한다.
   - ActorRelay와 SessionGateway는 framework routed channel 위에서 request sequence
     기준으로 reply를 돌려준다.
   - reconnect 시 같은 `actorId`가 새 stream으로 교체되는 동작을 test로 검증한다.
   - 구현 결과: `BindActorAsync`, `UnbindActorAsync`, `OpenActorRelay`,
     `IZLinkSessionGateway`, session proxy handler, internal `ZLink.ActorRelay`와
     `ZLink.SessionGateway` routed dispatch를 추가했다. internal packet은 원본 stream
     body bytes와 header snapshot을 함께 전달한다.
   - 검증 결과:
     `/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug --no-restore --filter SessionGateway_Relays`
     통과. 실제 TCP stream과 framework routed channel을 함께 사용했고 request
     sequence 기준 reply matching을 확인했다.
   - reconnect binding은 같은 `actorId`를 dictionary에서 새 session context로
     덮어쓰는 방식으로 구현했다. 기존 session의 unbind는 session id가 같을 때만
     제거하므로 새 binding을 지우지 않는다.
8. `verified`: 실제 Session Gateway sample을 기존 sample과 겹치지 않는 별도
   `framework/languages/dotnet/samples/TicTacToe(session-gateway)` project로 추가한다.
   이 항목은 5, 6, 7번 framework API가 실제로 동작해야 완료할 수 있다.
   - 근거 draft:
     [tictactoe-game-sample.ko.md](../../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md),
     [session-gateway.ko.md](../../../spec/draft/framework-adapter/policy/session-gateway.ko.md)
   - sample 내부 `InMemoryRoutedChannel` 같은 대체 transport를 만들지 않는다.
   - Session server는 client stream과 `actorId -> stream` binding을 사용한다.
   - API server는 application location store interface를 소유한다.
   - Play server는 actor와 game room을 소유한다.
   - ActorRelay와 SessionGateway는 framework routed channel API를 사용한다.
   - reconnect smoke는 같은 `actorId`가 새 Session server에 bind되고 Play server의
     notify target이 바뀌는 것을 log와 assertion으로 검증한다.
   - 구현 결과: `TicTacToe.SessionGateway.csproj`와 API, Session, Play, Client,
     Scenario, Contracts, Configuration 디렉토리를 추가했다. sample은 api host,
     session host, play host, stream client를 한 process에서 띄우지만 통신은 실제
     TCP stream connector와 framework routed channel을 사용한다.
   - sample은 embedded registry와 `UseDiscovery(...)`를 사용한다.
     `UseManualConnections(...)`나 sample 내부 대체 transport는 사용하지 않는다.
   - API server는 in-memory location store를 소유하고, Session server는 actor relay로
     `CreateGameReq`와 `ResolveGameReq`를 API server에 보낸다.
   - Play server는 실제 TicTacToe board, turn, winner state를 소유하고
     `IZLinkSessionGateway.SendToActor(...).Async(...)`로 client notify를 보낸다.
   - scenario는 같은 `actorId`가 첫 번째 Session server에서 인증한 뒤 두 번째
     Session server로 reconnect하는 흐름, 두 player join, `X0 O3 X1 O4 X2` move
     sequence, final board `XXXOO....`와 winner `player-x`를 검증한다.
   - 검증 결과:
     `/home/hep7/.dotnet/dotnet build "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug`
     통과.
     `/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug --no-build`
     통과. 출력에서 `created=sample-game, reconnect=player-x`,
     `final=XXXOO...., status=Won, winner=player-x`,
     `notifications=state:13, joined:1`을 확인했다.
     `rg -n "UseManualConnections|ExecuteWithRetry|DiscoveryReady|RouteWarmup|Task\\.Delay\\(SampleTimings" "framework/languages/dotnet/samples/TicTacToe(session-gateway)"`
     검색 결과 없음.
9. `verified`: monitoring, registry, stage wrapper, Unity connector 범위를 문서와
   코드로 대조한다. 구현 범위에 들어가면 이 queue에 구체 구현 항목을 이 항목 앞에
   추가하고 즉시 구현한다. scope/non-goal이면 `not-applicable` 이유를 남긴다.
   - monitoring: `AddZLinkMonitoring(...)`, socket monitor, registry/spot polling diff
     runtime과 integration tests가 이미 있다. 이번 session gateway 작업에서 추가
     구현이 필요하지 않다.
   - registry: embedded registry, registry query, remote query client, discovery
     연결이 이미 있다. 이번 session gateway 작업에서 actor/session location store는
     application 책임으로 남겼으므로 framework registry 구현을 추가하지 않는다.
   - stage wrapper: draft는 `SPOT` 위에 얹는 상위 application wrapper를 별도 범위로
     둔다. 현재 framework public API에는 `IStage*` 표면을 추가하지 않는 것이 맞으므로
     이번 queue에서는 `not-applicable`이다.
   - Unity connector: `Systems.Zlink.Stream.Connector.Unity`는 core stream connector를
     감싸며 `SendTimeout` public option이나 old `Exec*` API를 노출하지 않는다. 이번
     queue에서 추가 구현이 필요하지 않다.
10. `verified`: 문서 대조 리뷰를 다시 실행하고 이 파일의 모든 `pending`을
   `implemented`, `verified`, `not-applicable` 중 하나로 바꾼다. `not-applicable`은
   이유를 반드시 쓴다. 위 `Reference Documents`의 일반 `pending` 항목은 이 단계에서
   문서별로 해소한다. 대조 중 구체 구현 작업이 발견되면 그 작업을 현재 문서 대조
   항목보다 앞에 새 queue 항목으로 추가하고 즉시 구현한다.
   - Reference Documents 표의 일반 `pending`은 queue 10에서 해소했다.
   - 남은 queue 항목 11, 12는 POSD 반복과 전체 Phase 10 검증으로, queue 10 이후의
     별도 실행 단계라 이 항목에서 완료 처리하지 않는다.
11. `verified`: POSD 리뷰를 반복해 `posd-review.md`와 `sample-posd-review.md`에 새
   red flag가 없다고 기록한다.
   - 검증 결과: `posd-review.md` Iteration 4와 `sample-posd-review.md` Current State를
     갱신했다. session gateway internal DTO, raw stream reply, sample fake transport
     위험 신호가 해소되었고 새 red flag는 남지 않았다.
12. `verified`: Phase 10의 모든 build/test/smoke 명령을 다시 실행한다. SessionGateway
   smoke는 실제 framework routed channel API 위에서 동작하기 전까지 완료로 보지
   않는다.
   - 검증 결과:
     ```bash
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
     /home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug -f net8.0 --no-build
     /home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.RuntimeTests/Zlink.Framework.RuntimeTests.csproj -c Release -f net8.0 --no-build
     /home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MonitoringRuntimeTests/Zlink.Framework.MonitoringRuntimeTests.csproj -c Release -f net8.0 --no-build
     /home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0 --no-build
     /home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj -c Release -f net8.0 --no-build
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln -c Debug
     /home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Release --no-build
     ```
     모두 통과했다. SessionGateway sample 출력에서
     `created=sample-game, reconnect=player-x`,
     `final=XXXOO...., status=Won, winner=player-x`,
     `notifications=state:13, joined:1`을 확인했다.

재개 시 첫 작업자는 위 목록에서 가장 위에 있는 `pending` 항목부터 시작한다. 이미
완료된 항목을 다시 확인해야 하면 근거 명령과 파일 경로를 적고 다음 항목으로
넘어간다.

## Autonomy Review Log

### Iteration 1

문제:

- 없는 `framework/samples` 경로가 `rg` 명령에 직접 들어가면 검증 중 에러가 날 수 있다.
- old submit API inventory가 `verified` 상태인데도 현재 남아 있는 항목처럼 읽혔다.
- solution duplicate 정리 방법이 구체적이지 않아 다음 작업자가 다시 판단해야 했다.

수정:

- 계획서와 worklog의 old API 검색 명령을 조건부 `framework/samples` 검색으로 바꿨다.
- inventory 문장을 초기 발견 항목과 처리 결과로 나눴다.
- 유지/제거할 solution GUID와 후속 명령을 resume queue에 적었다.

### Iteration 2

문제:

- Direct sample suffix 규칙에서 `Msg`와 `Notify`가 충돌할 수 있었다.
- Phase 10 필수 명령에 runtime, monitoring, stream connector test가 빠져 있었다.
- current/planned sample project 목록이 smoke project 생성 후 상태와 맞지 않았다.

수정:

- client-facing push는 `Notify`를 우선한다는 구체 규칙을 계획서에 추가했다.
- Phase 10 필수 명령에 runtime, monitoring, stream connector test를 추가했다.
- 현재 존재하는 smoke project와 아직 없는 gateway/API project를 분리해서 적었다.

### Iteration 3

리뷰 결과:

- 현재 문서에는 사용자 판단을 기다려야만 다음 단계로 갈 수 있는 항목이 없다.
- 남은 구체 구현 `pending` 항목은 모두 `Autonomous Resume Queue`에 구현 순서와 검증 방법이 있다.
- reference 문서별 일반 `pending` 항목은 queue 10번의 문서 대조 리뷰에서 해소한다.
- 부분 smoke marker는 완료로 보지 않는다는 규칙이 계획서와 sample POSD worklog에 모두 있다.

### Iteration 4

상태: `not-applicable`

판단 폐기:

- 이 반복에서 기존 `Client/Server/Shared` TicTacToe sample을 제거하고 별도 direct
  project로 재구성한 판단은 잘못된 판단이었다.
- 이 반복에서 만든 gateway sample은 실제 framework routed channel public API가 아니라
  sample 내부 대체 transport를 사용했으므로 완료 근거가 될 수 없다.
- 이 반복의 sample 관련 검증 명령과 완료 판단은 Iteration 6에서 폐기했다.

유지할 수 있는 결과:

- stream connector public resolver/attribute 이름을 `PacketName` 기준으로 바꾼
  변경은 framework public API 정리 작업으로 유지한다.

### Iteration 5

상태: `not-applicable`

판단 폐기:

- 이 반복의 sample smoke 경로는 현재 올바른 sample 구조와 맞지 않는다.
- 전체 검증은 현재 queue 12번에서 실제 framework routed channel 기반 gateway
  sample을 만든 뒤 다시 실행해야 한다.

### Iteration 6

문제:

- Iteration 4에서 기존 `Client/Server/Shared` TicTacToe sample을 제거하고
  `Direct/` project로 대체했다. 사용자가 요구한 방향은 기존 sample 보존과
  별도 `SessionGateway/` sample 추가였으므로 이 변경은 잘못된 판단이었다.
- `SessionGateway` sample은 실제 framework routed channel public API를 쓰지 않고
  sample 내부 `InMemoryRoutedChannel`로 흐름을 흉내 냈다. 따라서 완료로 볼 수 없다.

수정:

- 기존 TicTacToe sample tree를 `Client/Server/Shared/TicTacToe.sln` 구조로
  복원했다.
- fake `Direct/`, fake `SessionGateway/`, smoke/tool project를 제거했다.
- gateway version sample의 실제 대상 경로를
  `framework/languages/dotnet/samples/TicTacToe(session-gateway)`로 고정했다.
- 기존 client의 `ExecAsync(...)` 호출만 현재 connector API인 `Async(...)`로
  최소 수정했다.
- `Zlink.Framework.sln`은 다시 `TicTacToe.Shared`, `TicTacToe.Server`,
  `TicTacToe.Client` project를 참조한다.

검증:

```bash
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln -c Debug
rg -n "Direct/|SessionGateway|TicTacToe.SmokeTests|TicTacToeSmoke|InMemoryRoutedChannel|ExecAsync|WithDontWait" framework/languages/dotnet/samples/TicTacToe framework/languages/dotnet/Zlink.Framework.sln
```

결과:

- 기존 TicTacToe sample solution Debug build 통과.
- 잘못 추가한 fake session gateway/sample smoke 경로와 `ExecAsync` 검색 결과 없음.
- 실제 SessionGateway sample은 다시 `pending`이다.

### Iteration 7

문제:

- 계획서의 "첫 번째 pending" 규칙이 Reference Documents 표의 일반 `pending`을
  먼저 잡게 만들 수 있었다.
- routed channel과 session gateway가 async submit runtime보다 앞에 있어 request
  sequence, pending request cleanup, ready drain 구현 순서가 흔들릴 수 있었다.
- `SendAsync(...)`는 public builder 실행 함수로 금지해야 하는데, old API inventory가
  검색 결과 없음으로 되어 있었다. 실제로 draft sample 예시와 actor/session stream
  builder public surface에 남아 있다.
- monitoring, registry, stage wrapper, Unity connector 범위가 완료 조건에는 있지만
  queue에 구체 대조 항목이 없었다.
- 이전 계획의 framework runtime 표현이 native `core/`와 `Zlink.Framework` runtime을 혼동시킬 수
  있었다.

수정:

- 재개 기준은 `Autonomous Resume Queue`의 첫 `pending`이라고 계획서와 worklog에
  명시했다. Reference Documents 표의 일반 `pending`은 문서 대조 단계에서 해소한다.
- queue 순서를 `SendAsync(...)` 정리, 공통 async submit runtime, routed channel,
  session gateway, gateway sample 순서로 바꿨다.
- `SendAsync(...)` 검색과 제거 기준을 추가했다. `WebSocket.SendAsync(...)` 같은 외부
  transport API 호출은 금지 대상이 아니라고 분리했다.
- monitoring, registry, stage wrapper, Unity connector 대조 항목을 queue에 추가했다.
- 혼동 가능성이 있던 framework runtime 표현을 `Zlink.Framework` runtime으로 바꿨다.

다음 재개 작업:

- queue 4번부터 시작한다. framework public builder 실행 함수와 draft sample 예시에서
  `SendAsync(...)`를 제거하고, 검증 검색 결과가 외부 transport API 호출만 남는지
  확인한다.
