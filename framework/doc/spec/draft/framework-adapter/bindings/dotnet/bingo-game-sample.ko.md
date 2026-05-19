<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: TicTacToe Game Sample 초안](tictactoe-game-sample.ko.md) | [다음: ZLink Framework For Java](../java/README.ko.md)
<!-- framework-adapter-nav:end -->

# Bingo Game Sample 초안

> 이 문서는 **구현 전 초안**이다.
> 즉 아직 공개 계약[^public-contract]이 아니며, `.NET` framework adapter 와
> stream connector를 함께 보여 주는 4인 멀티 빙고 샘플의 구현 기준을 정리한다.

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [Actor](./aspnet-core-actor.ko.md) | [Session Actor Dispatch](./session-actor-dispatch.ko.md)

## 1. 목적

빙고 샘플은 4명이 같은 room 에 들어와, 서버가 뽑은 번호를 기준으로 동시에
진행되는 게임을 보여 준다. 이 샘플은 TicTacToe 보다 게임 규칙은 단순하지만,
framework 의 여러 표면을 한 번에 보여 주기 좋다.

이 샘플이 보여 줄 핵심은 다음과 같다.

- STREAM session 에서 인증한 뒤 actor handle 을 bind 하는 흐름
- actor 가 Entry Spot 을 거쳐 user Spot 인 `BingoRoomSpot` 에 join 하는 흐름
- user Spot 이 room 상태를 단일 authoritative 상태로 들고 game rule 을 처리하는 흐름
- server push 로 참가자 입장, 번호 추첨, 카드 변경, 게임 종료 이벤트를 전달하는 흐름
- 같은 draw sequence 에서 여러 플레이어가 동시에 빙고가 되는 경우를 순서 문제 없이 처리하는 흐름

## 2. 샘플 범위

첫 구현 범위는 direct 샘플 하나로 둔다.

- 클라이언트는 API 서버에 room 생성을 요청한다.
- API 서버는 Play 서버 channel 로 room 생성을 요청한다.
- Play 서버는 `BingoRoomSpot` 을 만든 뒤 client STREAM endpoint 와 room id 를
  응답한다.
- 각 클라이언트는 Play 서버 STREAM endpoint 에 연결하고 인증한다.
- 인증된 session 은 player actor 를 만들거나 조회한 뒤 현재 stream 과 bind 한다.
- 클라이언트가 `JoinRoomReq` 를 보내면 session 이 actor 로 relay 하고, actor 는
  `BingoRoomSpot` 에 join 한다.

session actor dispatch 를 사용하는 gateway 샘플은 두 번째 단계로 둔다. 이 문서는
direct 샘플을 먼저 고정하지만, DTO 이름과 actor / spot 역할은 gateway 샘플로
옮겨도 바뀌지 않게 잡는다.

## 3. 런타임 구성

샘플의 서버 구성은 다음과 같다.

| 구성 요소 | 역할 |
|-----------|------|
| `ApiServer` | HTTP 요청을 받아 room 생성을 Play 서버 channel request 로 넘긴다. |
| `PlayServer` | STREAM endpoint, Play channel server, SpotNode 를 가진다. |
| `PlaySession` | 클라이언트 STREAM 연결 하나를 담당한다. 인증 전 packet 을 제한하고, 인증 뒤에는 player actor 로 relay 한다. |
| `BingoPlayerActor` | player identity 와 현재 room join 상태를 가진다. session 과 bind 된 client 로 알림을 push 한다. |
| `BingoEntrySpot` | actor 가 room 에 들어가기 전 기본 위치다. |
| `BingoRoomSpot` | 4인 room 상태와 게임 규칙을 소유한다. |

`BingoRoomSpot` 이 게임 상태의 유일한 기준이다. 클라이언트와 actor 는 빙고 판정을
직접 결정하지 않는다. actor 는 user identity 와 client push 표면을 제공하고,
room 은 카드, mark, 번호 추첨, 승리 판정을 담당한다.

## 4. 게임 규칙

첫 샘플의 규칙은 의도적으로 단순하게 둔다.

| 항목 | 규칙 |
|------|------|
| 플레이어 수 | 정확히 4명 |
| 보드 | 5 x 5 |
| 번호 범위 | 1부터 75까지 |
| 가운데 칸 | free cell 로 시작부터 mark 처리 |
| 시작 조건 | 4명이 모두 join 하면 자동 시작 |
| 번호 추첨 | room timer 가 일정 주기로 하나씩 뽑는다 |
| mark 방식 | 자동 mark. 서버가 뽑은 번호가 카드에 있으면 room 이 바로 mark 한다 |
| 승리 조건 | 새 draw sequence 기준으로 complete line 이 1개 이상이면 승리 |
| 동시 승리 | 같은 draw sequence 에서 승리한 플레이어는 공동 승리 |
| 종료 조건 | 첫 승리 draw sequence 가 나오면 종료 |

자동 mark 를 기본으로 두는 이유는 샘플이 game rule 보다 framework 흐름을 보여
주는 데 목적이 있기 때문이다. 수동 mark 를 넣으면 클라이언트 조작 검증, 누락
mark 복구, claim 순서 같은 규칙이 늘어난다. 첫 샘플에서는 room 이 모든 카드를
검사해 결과를 확정하는 쪽이 더 명확하다.

## 5. 상태 모델

room 상태는 다음 정보를 가진다.

```csharp
public sealed record BingoRoomState(
    string RoomId,
    string Status,
    int DrawSeq,
    int? LastDrawnNumber,
    IReadOnlyList<int> DrawnNumbers,
    IReadOnlyList<BingoPlayerState> Players,
    IReadOnlyList<string> Winners);
```

player 상태는 다음 정보를 가진다.

```csharp
public sealed record BingoPlayerState(
    string ActorId,
    string DisplayName,
    int Seat,
    int[] Card,
    bool[] Marks,
    int CompletedLines);
```

`Card` 와 `Marks` 는 25칸을 row-major 순서로 담는다. row-major 는 첫 행의 5칸,
둘째 행의 5칸 순서로 1차원 배열에 저장하는 방식이다. UI 는 이 배열을 5 x 5
격자로 그리면 된다.

room status 는 문자열로 시작한다.

| 값 | 의미 |
|----|------|
| `WaitingForPlayers` | 아직 4명이 모이지 않았다. |
| `Running` | 번호 추첨이 진행 중이다. |
| `Finished` | 승자가 확정되어 게임이 끝났다. |

## 6. DTO 초안

HTTP 와 channel 쪽 DTO 는 다음과 같다.

```csharp
public sealed record CreateBingoRoomHttpReq(string? RoomName);

public sealed record CreateBingoRoomHttpRes(
    string RoomId,
    string PlayEndpoint,
    string RoomName);

public sealed record CreateBingoRoomReq(string RoomName);

public sealed record CreateBingoRoomRes(
    string RoomId,
    string PlayEndpoint,
    string RoomName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(
    string ActorId,
    string DisplayName);
```

client STREAM request / response 는 다음과 같다.

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string ActorId,
    string DisplayName);

public sealed record JoinRoomReq(string RoomId);

public sealed record JoinRoomRes(BingoRoomState State);

public sealed record LeaveRoomReq(string RoomId);

public sealed record LeaveRoomRes(BingoRoomState State);
```

actor 가 room SPOT 에 join 할 때 쓰는 내부 request / response 는 다음과 같다.

```csharp
public sealed record BingoRoomJoinReq(
    string RoomId,
    string ActorId,
    string DisplayName);

public sealed record BingoRoomJoinRes(BingoRoomState State);
```

server push DTO 는 다음과 같다.

```csharp
public sealed record PlayerJoinedNotify(
    string RoomId,
    string ActorId,
    string DisplayName,
    int Seat,
    BingoRoomState State);

public sealed record BingoNumberDrawnNotify(
    string RoomId,
    int DrawSeq,
    int Number,
    BingoRoomState State);

public sealed record BingoStateNotify(BingoRoomState State);

public sealed record BingoGameEndedNotify(BingoRoomState State);
```

## 7. 메시지 흐름

room 생성 흐름은 다음과 같다.

1. client 는 `POST /bingo/rooms` 로 room 생성을 요청한다.
2. API 서버는 Play channel 로 `CreateBingoRoomReq` 를 보낸다.
3. Play 서버는 room 설정을 multipart create payload로 만들고
   `IZLinkSpotManager.GetOrCreateAsync(..., createParts, ...)` 로
   `BingoRoomSpot` 을 확보한다.
4. `BingoRoomSpot.OnCreateAsync(...)` 는 create payload를 해석해서 room 설정과
   초기 draw seed를 저장한다.
5. API 서버는 `CreateBingoRoomHttpRes` 로 `RoomId` 와 `PlayEndpoint` 를 돌려준다.

player join 흐름은 다음과 같다.

1. client 는 Play STREAM endpoint 에 연결한다.
2. client 는 `AuthenticateReq` 를 request 로 보낸다.
3. session 은 API channel 로 `AuthenticatePlayerReq` 를 보내 player 를 확인한다.
4. session 은 `IZLinkActorManager.GetOrCreateAsync(...)` 로 player actor 를 준비한다.
5. session 은 `BindActorHandleAsync(...)` 로 현재 stream 과 actor 를 bind 한다.
6. client 는 `JoinRoomReq` 를 보낸다.
7. session 은 request 를 actor 로 relay 한다.
8. actor request handler 는 `JoinSpot(...)` 으로 `BingoRoomSpot` 에 join 한다.
9. room 은 player 를 seat 에 배치하고 카드를 만든 뒤 `JoinRoomRes` 를 반환한다.

게임 진행 흐름은 다음과 같다.

1. 네 번째 player join 이 완료되면 room status 가 `Running` 으로 바뀐다.
2. room timer 는 draw deck 에서 다음 번호를 하나 뽑는다.
3. room 은 모든 player card 를 검사해서 해당 번호를 mark 한다.
4. room 은 같은 draw sequence 에서 새 complete line 이 생긴 player 를 모두 찾는다.
5. 승자가 있으면 `Finished` 로 바꾸고, 없으면 다음 timer tick 을 기다린다.
6. 각 단계의 상태는 actor session binding 을 통해 client 로 push 한다.

## 8. POSD 기준 설계 결정

이 샘플은 단순한 게임이지만, 다음 설계 기준을 명확히 보여 주는 데 목적이 있다.

| 결정 | 이유 |
|------|------|
| 게임 규칙은 `BingoRoomSpot` 에 둔다. | 카드 생성, 번호 추첨, mark, 빙고 판정을 한 모듈 안에 숨긴다. |
| actor 는 player identity 와 client push 만 맡는다. | actor 가 빙고 규칙을 알 필요가 없고, session binding 과 사용자 상태만 다루면 된다. |
| client 는 mark 나 bingo claim 을 보내지 않는다. | 첫 샘플에서는 불필요한 검증 분기를 없애고 server authoritative 흐름을 보여 준다. |
| 같은 draw sequence 의 승자는 공동 승리로 처리한다. | 네트워크 도착 순서나 push 순서가 게임 결과를 바꾸지 않게 한다. |
| room 은 정확히 4명일 때 자동 시작한다. | 샘플에서 별도 ready API 를 두지 않아도 actor join 과 timer 흐름을 보여 줄 수 있다. |

수동 mark, ready 버튼, 여러 라운드, 랭킹 유지 같은 기능은 이 샘플의 첫 범위에서
제외한다. 필요한 경우 두 번째 샘플 단계에서 추가한다.

## 9. 완료 기준

- 4개의 client connector 가 같은 room 에 접속하고 각각 다른 actor 로 인증된다.
- 4명의 actor 가 같은 `BingoRoomSpot` 에 join 하면 게임이 자동으로 시작된다.
- room 이 번호를 뽑고, 각 player card 의 mark 를 server 쪽에서 갱신한다.
- 같은 draw sequence 에서 여러 winner 가 나오면 모두 `Winners` 에 포함된다.
- actor request reply 는 handler 반환값으로 처리하고, actor context `Reply(...)`
  를 사용하지 않는다.
- client 로 가는 push 는 actor session binding 을 통해 전달된다.
- 샘플의 public DTO 에서 player identity 필드는 `ActorId` 로 통일한다.

## 10. 회귀 테스트

빙고 샘플을 구현할 때는 새 샘플 실행 테스트를 추가하기 전에도, 아래 기존 회귀
테스트가 깨지지 않아야 한다. 이 테스트들은 빙고 샘플이 사용할 framework 표면을
이미 고정하고 있다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamIntegrationTests.LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler` | session 이 local actor 로 request 를 relay 하고, actor request handler 반환값으로 stream response 를 작성한다. |
| `StreamIntegrationTests.ActorPacketRegistry_DoesNot_Resolve_Request_To_Send_Handler` | actor request packet 이 send handler 로 fallback dispatch 되지 않는다. |
| `StreamIntegrationTests.SpotActorRegistry_DoesNot_Resolve_Request_To_Send_Handler` | Entry Spot 과 user Spot actor request packet 이 send handler 로 fallback dispatch 되지 않는다. |
| `SpotIntegrationTests.EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets` | Entry Spot actor handler 와 user Spot actor handler 가 각각 등록되어 dispatch 된다. |
| `SpotIntegrationTests.Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work` | room timer 기반 진행과 spot lifecycle 정리가 framework timer 계약과 맞는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | client connector request/reply correlation 이 유지된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
