<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: ZLink Stream Connector For .NET](./streaming-client.ko.md) | [다음: Bingo Game Sample](./bingo-game-sample.ko.md)
<!-- framework-adapter-nav:end -->

# TicTacToe Game Sample

[.NET 묶음](../../README.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [Session Actor Dispatch](../../../../../doc/spec/session-actor-dispatch.ko.md)

## 1. 목적

TicTacToe 샘플은 두 가지 구성으로 나누어 둔다.

- direct 샘플: 클라이언트가 API 서버에서 game 정보를 받은 뒤, Play 서버의
  STREAM[^stream] endpoint 에 직접 연결한다.
- session actor dispatch[^session-actor-dispatch] 샘플: 클라이언트는 Session
  서버의 STREAM endpoint 하나만 알고 있다. 그 뒤 Session 서버가 클라이언트의
  요청을 API 서버와 Play 서버로 relay 한다.

두 샘플 모두 actor[^actor] 식별 필드의 이름은 public DTO[^dto] 상에서
`ActorId` 로 통일한다. token 은 인증 입력값으로만 쓰며, actor identity 로는
사용하지 않는다.

## 2. Direct 샘플 구성

direct 샘플의 public DTO 는 현재 코드 기준으로
`framework/languages/dotnet/samples/TicTacToe/Shared/Contracts/Messages.cs` 를
따른다.

HTTP 와 server channel 쪽 DTO 는 다음과 같다.

```csharp
public sealed record CreateGameHttpReq(string? GameName);

public sealed record CreateGameHttpRes(
    string GameId,
    string PlayEndpoint,
    string GameName);

public sealed record CreateGameReq(string GameName);

public sealed record CreateGameRes(
    string GameId,
    string PlayEndpoint,
    string GameName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(string ActorId);
```

client STREAM 쪽 DTO 는 다음과 같다.

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string ActorId);

public sealed record JoinGameReq(string GameId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(int Cell);

public sealed record PlaceMarkRes(GameState State);
```

Play actor 가 game room SPOT[^spot] 에 join 할 때 쓰는 내부 request / response
는 다음과 같다.

```csharp
public sealed record TicTacToeGameJoinReq(
    string GameId,
    string ActorId);

public sealed record TicTacToeGameJoinRes(GameState State);
```

server push 와 state DTO 는 다음과 같다.

```csharp
public sealed record PlayerJoinedNotify(
    string GameId,
    string ActorId,
    string Mark,
    GameState State);

public sealed record GameStateNotify(GameState State);

public sealed record GameState(
    string GameId,
    string Board,
    string Status,
    string? Winner,
    string NextTurn,
    string? XActorId,
    string? OActorId,
    string? LastMoveActorId,
    int? LastMoveCell);
```

direct 샘플은 다음 세 가지를 함께 사용한다.

- 일반 channel client / server
- STREAM header session
- SPOT game room

`Api` channel client 와 `Play` channel client 는 현재 샘플 코드에서
`UseManualConnections(...)` 로 endpoint 를 직접 지정해 연결한다. 이 점은
session actor dispatch 샘플의 자동 discovery 정책과 혼동하지 않도록 주의한다.

## 3. Session Actor Dispatch 샘플 구성

session actor dispatch 샘플의 DTO 는
`framework/languages/dotnet/samples/TicTacToe(session-gateway)/Contracts/Messages.cs`
를 따른다.

이 샘플은 service channel 과 routed channel 모두 전역 `UseDiscovery(...)`
기반의 자동 연결을 사용한다. game room SPOT node 는 `AddSpotMesh(...)` 안에서
`mesh.UseDiscovery(...)` 를 호출해 같은 registry 에 붙는다. 샘플 코드에는
`UseManualConnections(...)` 를 두지 않는다.

핵심 계약은 다음과 같다.

- `AuthenticateReq.ActorId` 가 인증 요청의 actor identity 역할을 한다.
- 인증이 성공하면, Session 서버는 두 가지 일을 한다. 먼저 local actor 가
  필요한 경우 `IZLinkActorManager.GetOrCreateAsync(...)` 로 actor 를 준비한다.
  그 뒤 `BindActorHandleAsync(...)` 로 actor handle 을 얻고 현재 stream session
  binding 을 framework / core 내부 상태에 기록한다.
- `CreateMatchReq` 는 Session 서버에서 API 서버로 channel request 로 relay
  된다. 클라이언트는 match id 나 room 이름을 따로 지정하지 않는다.
- API 서버는 Play 서버에 room 생성을 요청한다. Play 서버는
  `IZLinkSpotManager` 로 game room SPOT 을 만든다. `CreateMatchRes.MatchId` 는
  생성된 room 의 `SpotId` 를 hex 로 표현한 값이다.
- `JoinMatchReq` 와 `PlaceMarkReq` 는 Session 서버에서 Play 서버의 actor 로
  dispatch 된다.
- Play actor 는 `JoinMatchReq` 를 처리하는 중에 해당 game room SPOT 에 join
  한다. 이후 들어오는 `PlaceMarkReq` 는 actor 가 join 한 room SPOT 의 상태를
  변경한다.
- Play actor 가 자기 client 로 push 할 때는 actor context 의
  `IZLinkSessionProxy` 를 사용한다. 특정 actor id 의 client 로 보내는
  application service 는 `IZLinkActorSessionClient` 를 사용한다.
- `OpponentJoinedNotify`, `TurnChangedNotify`, `GameEndedNotify` 는 actor id 를
  기준으로 현재 binding 되어 있는 Session 서버를 찾는다. 그 뒤 해당 서버의
  client stream 을 통해 전달된다.

## 4. 완료 기준

- direct 샘플 문서와 실제 코드의 DTO 이름이 일치한다.
- session actor dispatch 샘플 문서와 실제 코드의 DTO 이름이 일치한다.
- 샘플 spec과 코드 모두에서 actor 식별용 public field는 `ActorId`만 사용한다.
- session actor dispatch 샘플은 수동 연결을 사용하지 않고, discovery 기반 자동
  연결만 사용한다.
- session actor dispatch 샘플은 Play 서버에 game room SPOT을 만들고, scenario
  단계에서 생성된 `MatchId`가 실제 SPOT room으로 존재하는지 확인한다.
- direct 샘플의 수동 연결 설명은 direct 샘플 범위 안으로만 한정한다.

## 5. 회귀 테스트

틱택토 샘플은 Direct 경로와 Session Actor Dispatch 경로가 모두 public
framework 표면만 사용하는지 확인한다. 샘플을 수정할 때는 다음 흐름을 아래
테스트와 함께 맞춘다.

- API 서버
- Play 서버
- STREAM connector
- SPOT actor

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamIntegrationTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session gateway 경로에서 request/reply sequence가 actor dispatch와 맞물려 동작한다. |
| `SpotIntegrationTests.EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets` | Entry Spot[^entry-spot]과 room Spot actor handler가 각각 정상 동작한다. |
| `SpotIntegrationTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | match room으로 이동한 뒤에는 이전 room으로 stale dispatch가 발생하지 않는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | 게임 클라이언트 역할의 connector request/reply 계약이 그대로 유지된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^stream]: `STREAM` 은 클라이언트와 서버 사이에 지속 연결을 유지하면서 framework Header 기반 packet 을 주고받는 세션형 통신 추상이다.
[^session-actor-dispatch]: session actor dispatch 는 클라이언트 세션에서 들어온 요청을, 그 세션과 묶인 actor 로 자동 전달하는 패턴이다.
[^actor]: actor 는 자신만의 메일박스와 상태를 가지고 메시지를 순서대로 처리하는 동시성 단위다. framework 에서는 클라이언트 세션과 묶여 사용자별 게임 상태를 다루는 데 쓰인다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.
