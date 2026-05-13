<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework .NET STREAM Samples](stream-samples.ko.md) | [다음: ZLink Framework For Java](../java/README.ko.md)
<!-- framework-adapter-nav:end -->

# TicTacToe Game Sample 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, `.NET` framework adapter와
> stream connector를 함께 보여 주는 TicTacToe 샘플의 구현 기준을 정한다.

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [Session Actor Dispatch](../../policy/session-gateway-usability.ko.md)

## 1. 목적

TicTacToe 샘플은 두 가지 구성을 가진다.

- direct 샘플: client가 API 서버에서 game 정보를 받은 뒤 Play 서버 STREAM endpoint에
  직접 연결한다.
- session actor dispatch 샘플: client는 Session 서버 STREAM endpoint 하나만 알고,
  Session 서버가 API 서버와 Play 서버로 request를 relay한다.

두 샘플 모두 actor 식별 필드 이름은 public DTO에서 `ActorId`로 통일한다. token은
인증 입력값일 뿐이며, actor identity로 쓰지 않는다.

## 2. Direct 샘플 구성

direct 샘플의 현재 코드 기준 public DTO는
`framework/languages/dotnet/samples/TicTacToe/Shared/Contracts/Messages.cs`를 따른다.

HTTP와 server channel:

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

client STREAM:

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string ActorId);

public sealed record JoinGameReq(string GameId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(int Cell);

public sealed record PlaceMarkRes(GameState State);
```

Play actor가 game room SPOT에 join할 때 사용하는 내부 request/response:

```csharp
public sealed record TicTacToeGameJoinReq(
    string GameId,
    string ActorId);

public sealed record TicTacToeGameJoinRes(GameState State);
```

server push와 state:

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

direct 샘플은 일반 channel client/server와 STREAM header session, SPOT game room을
사용한다. `Api` channel client와 `Play` channel client는 현재 샘플 코드에서
`UseManualConnections(...)`로 endpoint를 직접 연결한다. session actor dispatch 샘플의
자동 discovery 정책과 혼동하지 않는다.

## 3. Session Actor Dispatch 샘플 구성

session actor dispatch 샘플의 DTO는
`framework/languages/dotnet/samples/TicTacToe(session-gateway)/Contracts/Messages.cs`를 따른다.

이 샘플은 service channel과 routed channel 모두 전역 `UseDiscovery(...)` 기반 자동
연결을 사용한다. game room SPOT node는 `AddSpotMesh(...)` 안에서
`mesh.UseDiscovery(...)`로 같은 registry에 붙는다. sample 코드에는
`UseManualConnections(...)`를 두지 않는다.

핵심 계약은 아래와 같다.

- `AuthenticateReq.ActorId`가 인증 요청의 actor identity다.
- 인증 성공 뒤 Session 서버는 `BindActorHandleAsync(...)`로 local `SpotNode` actor
  runtime의 actor handle을 만들고 현재 stream session binding을 framework/core 내부
  상태로 기록한다.
- `CreateMatchReq`는 Session 서버에서 API 서버로 channel request로 relay된다.
  client는 match id나 room name을 지정하지 않는다.
- API 서버는 Play 서버에 room 생성을 요청하고, Play 서버는 `IZLinkSpotManager`로
  game room SPOT을 만든다. `CreateMatchRes.MatchId`는 생성된 room의 `SpotId` hex다.
- `JoinMatchReq`와 `PlaceMarkReq`는 Session 서버에서 Play 서버 actor로 dispatch된다.
- Play actor는 `JoinMatchReq` 처리 중 해당 game room SPOT에 join한다. 이후
  `PlaceMarkReq`는 actor가 join한 room SPOT의 상태를 변경한다.
- Play 서버가 client에게 push할 때는 `IZLinkSessionProxy`를 사용한다.
- `OpponentJoinedNotify`, `TurnChangedNotify`, `GameEndedNotify`는 actor id 기준으로
  현재 binding된 Session 서버를 찾아 client stream으로 전달된다.

## 4. 완료 기준

- direct 샘플 문서와 code DTO 이름이 일치한다.
- session actor dispatch 샘플 문서와 code DTO 이름이 일치한다.
- sample spec과 code에서 actor 식별 public field는 `ActorId`로만 표현한다.
- session actor dispatch 샘플은 수동 연결 없이 discovery 기반 자동 연결만 사용한다.
- session actor dispatch 샘플은 Play 서버에 game room SPOT을 만들고, scenario는
  생성된 `MatchId`가 실제 SPOT room으로 존재하는지 확인한다.
- direct 샘플의 수동 연결 설명은 direct 샘플 범위로만 제한한다.
