<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: ZLink Stream Connector For .NET](streaming-client.ko.md) | [다음: Bingo Game Sample](bingo-game-sample.ko.md)
<!-- framework-adapter-nav:end -->

# TicTacToe Game Sample

[.NET 묶음](../../README.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md)

> 이 문서는 실행 가능한 game sample 설명이다. 실시간 게임 도메인에 ZLink 를 도입할지
> 판단하려면 [15-case-realtime-game](../case-studies/15-case-realtime-game.ko.md)을 먼저 보고,
> 이 문서에서는 등록 코드, DTO, 실행 흐름을 확인한다.

## 1. 목적

TicTacToe 샘플은 API 서버와 Play 서버만 사용하는 가장 작은 실시간 게임 샘플이다.
client 는 API 서버에서 room 정보를 받은 뒤 Play 서버의 STREAM[^stream] endpoint 에
직접 연결한다.

TicTacToe 는 Api 서버와 Play 서버를 직접 연결하는 단일 구조 샘플이다. Session 서버를
분리한 gateway 구조는 Bingo 샘플이 담당한다.

actor[^actor] 식별 필드의 이름은 public DTO[^dto] 상에서 `ActorId` 로 통일한다.
token 은 인증 입력값으로만 쓰며, actor identity 로는 사용하지 않는다. room 식별자는
`RoomId` 로 노출하고, core routing id 의 hex 문자열을 public DTO 로 넘기지 않는다.

## 2. 샘플 구성

public DTO 는 현재 코드 기준으로
`framework/languages/dotnet/samples/TicTacToe/Shared/Contracts/Messages.cs` 를
따른다.

HTTP 와 server channel 쪽 DTO 는 다음과 같다.

```csharp
public sealed record CreateGameHttpReq(string? GameName);

public sealed record CreateGameHttpRes(
    string RoomId,
    string PlayEndpoint,
    string GameName);

public sealed record CreateGameReq(string GameName);

public sealed record CreateGameRes(
    string RoomId,
    string PlayEndpoint,
    string GameName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(string ActorId);
```

client STREAM 쪽 DTO 는 다음과 같다.

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string ActorId);

public sealed record JoinGameReq(string RoomId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(int Cell);

public sealed record PlaceMarkRes(GameState State);
```

Play actor 가 game room SPOT[^spot] 에 join 할 때 쓰는 내부 request / response 는
다음과 같다.

```csharp
public sealed record TicTacToeGameJoinReq(
    string RoomId,
    string ActorId);

public sealed record TicTacToeGameJoinRes(GameState State);
```

server push 와 state DTO 는 다음과 같다.

```csharp
public sealed record PlayerJoinedNotify(
    string RoomId,
    string ActorId,
    string Mark,
    GameState State);

public sealed record GameStateNotify(GameState State);

public sealed record GameState(
    string RoomId,
    string Board,
    string Status,
    string? Winner,
    string NextTurn,
    string? XActorId,
    string? OActorId,
    string? LastMoveActorId,
    int? LastMoveCell);
```

샘플은 다음 세 가지를 함께 사용한다.

- 일반 channel client / server
- STREAM header session
- SPOT game room

`Api` channel client 와 `Play` channel client 는 현재 샘플 코드에서
`EnableClient(endpoint)` 로 endpoint 를 직접 지정해 연결한다. 이 점은
location store 자동 연결을 사용하는 Bingo 샘플과 다르다.

## 3. Play 서버 구조

Play 서버는 domain logic 과 framework adapter 를 분리한다.

```text
Server/Play/
  Domain/
    TicTacToe/
  Application/
    GameCreation/
  Infrastructure/
    ZLink/
      Actors/
      Handlers/
      Sessions/
      Spots/
        Handlers/
```

`Domain/TicTacToe` 는 board 검증, turn 검증, winner/draw 판정을 맡는다. ZLink
framework 타입, stream session, actor context, logger 를 알면 안 된다.
`Application/GameCreation` 은 명시적인 `RoomId` 를 만들고 room Spot 생성을 요청한다.
`Infrastructure/ZLink` 는 channel handler, stream session, actor, Spot lifecycle, Spot
handler 를 맡는다.

## 4. 완료 기준

- 샘플 문서와 실제 코드의 DTO 이름이 일치한다.
- 샘플 spec과 코드 모두에서 actor 식별용 public field는 `ActorId`만 사용한다.
- room 식별용 public field는 `RoomId`만 사용하고, core routing id hex 문자열을
  public DTO 로 노출하지 않는다.
- TicTacToe는 수동 endpoint 연결만 사용하고 location store 자동 연결을 쓰지 않는다.
- TicTacToe 는 Api + Play 직접 연결 단일 구조를 따른다.
- Play 서버는 Domain / Application / Infrastructure 구조를 유지한다.
- Entry Spot 과 game room Spot 의 actor packet handler 는
  `Context.Handlers.AddActorRequest<THandler, TActor>(...)` 로 등록하고, game room Spot 의
  join admission 은 `OnActorJoinAsync(...)` 로 선언한다.

## 5. 회귀 테스트

틱택토 샘플은 기본 Api + Play 직접 연결 경로가 public framework 표면만 사용하는지
확인한다. 샘플을 수정할 때는 다음 흐름을 아래 테스트와 함께 맞춘다.

- API 서버
- Play 서버
- STREAM connector
- SPOT actor

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ActorRegistryExecutionTests.EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets` | Entry Spot[^entry-spot]과 room Spot actor handler가 각각 정상 동작한다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | room으로 이동한 뒤에는 이전 room으로 stale dispatch가 발생하지 않는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | 게임 클라이언트 역할의 connector request/reply 계약이 그대로 유지된다. |
| `RegressionTests.TicTacToe_SessionGateway_Sample_Is_Removed` | TicTacToe SessionGateway 변형이 sample tree 와 solution 에 남아 있지 않다. |

[^stream]: `STREAM` 은 클라이언트와 서버 사이에 지속 연결을 유지하면서 framework Header 기반 packet 을 주고받는 세션형 통신 추상이다.
[^actor]: actor 는 자신만의 메일박스와 상태를 가지고 메시지를 순서대로 처리하는 동시성 단위다. framework 에서는 클라이언트 세션과 묶여 사용자별 게임 상태를 다루는 데 쓰인다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.
[^spot]: `SPOT` 은 동적으로 생성ㆍ소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^entry-spot]: Entry Spot 은 SpotNode 가 접속한 actor 를 가장 먼저 받아들이는 진입용 spot 이다. 이후 user Spot 으로 옮겨 가기 전 단계 역할을 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: ZLink Stream Connector For .NET](streaming-client.ko.md) | [다음: Bingo Game Sample](bingo-game-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
