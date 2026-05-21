<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../doc/README.ko.md) | [이전: TicTacToe Game Sample](./tictactoe-game-sample.ko.md) | [다음: ZLink Framework .NET Behavior Matrix](../../internals/behavior-matrix.ko.md)
<!-- framework-adapter-nav:end -->

# Matching Room Game Sample: Bingo

[.NET 묶음](../../README.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [Actor](../../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../../spec/session-actor-dispatch.ko.md)

> 이 문서는 Bingo 샘플을 구현하기 전 작성한 설계 노트다.
> 현재 공개 사용 가이드가 아니며, 샘플 구현이 끝난 뒤 별도 guide 문서로 다시
> 정리한다.

## 1. 목적

빙고 샘플의 주목적은 빙고 규칙 자체가 아니라, matching room 기반 멀티 플레이
게임을 ZLink Framework .NET 으로 어떻게 단순하게 구성하는지 보여 주는 것이다.

이 샘플은 다음 흐름을 한 번에 보여 준다.

- STREAM session 에서 클라이언트를 인증하고 player actor 를 준비하는 흐름
- Session 서버가 클라이언트 요청을 bound actor 로 relay 하는 흐름
- API 서버가 매칭 요청을 받고 Play 서버에 room 생성 또는 배정을 요청하는 흐름
- actor 가 Entry Spot 을 거쳐 user Spot 인 `BingoRoomSpot` 에 join 하는 흐름
- room 에 처음 들어온 actor 를 host 로 지정하고, host 시작 요청으로 게임을
  시작하는 흐름
- room timer 로 번호 추첨, 자동 mark, 승리 판정, server push 를 처리하는 흐름

## 2. 샘플 범위

첫 구현 범위는 gateway 형태의 샘플 하나로 둔다.

| 구성 요소 | 역할 |
|-----------|------|
| `SessionServer` | 클라이언트 STREAM 연결, 인증, actor binding, client request relay 를 맡는다. |
| `ApiServer` | 회원 정보 조회, matching 요청 처리, room 생성 또는 배정 요청을 맡는다. |
| `PlayServer` | player actor, Entry Spot, `BingoRoomSpot` 을 호스팅하고 실제 게임을 실행한다. |
| `BingoEntrySpot` | actor 가 특정 room 에 들어가기 전 머무는 lobby 역할을 한다. |
| `BingoRoomSpot` | 매칭된 한 room 의 참가자, host, 게임 상태, timer, 승리 판정을 소유한다. |

서버 사이의 Spot route 는 Registry 기반 framework 기본 구현을 사용한다.
샘플 서버는 `UseRegistrySpotRoutes("bingo")` 만 켠다. actor route 는 Play 서버의
actor 준비 응답이 넘기는 snapshot 을 사용하고, actor-session route 는 session bind 시
actor runtime state 에 저장된다.
일반 파일 metadata store 처럼 직접 구현하지 않고, room 배정과 게임 상태 같은 domain
logic 만 샘플 코드에 남긴다.

direct 샘플은 첫 범위에서 제외한다. session 과 spot 을 한 인스턴스에 둘 때는
웹 서버나 API 서버가 lobby 역할을 맡기 쉬워 Entry Spot 의 필요성이 잘 드러나지
않는다. 이 샘플은 Session 서버, API 서버, Play 서버를 분리해 Entry Spot 이
게임 서버 내부 lobby 로 동작하는 모습을 보여 준다.

## 3. 전체 흐름

샘플은 다음 순서로 진행한다.

1. client 는 `SessionServer` STREAM endpoint 에 연결한다.
2. client 는 `AuthenticateReq` 를 보낸다.
3. `SessionServer` 는 `ApiServer` 로 회원 정보를 조회한다.
4. 인증이 성공하면 `SessionServer` 는 player actor 를 만들거나 조회하고 현재
   stream session 과 bind 한다.
5. client 는 `MatchBingoReq` 를 `SessionServer` 로 보낸다.
6. `SessionServer` 는 요청을 bound actor 로 relay 한다.
7. actor handler 는 `ApiServer` 로 matching 요청을 보낸다.
8. `ApiServer` 는 `PlayServer` 에 room 생성 또는 기존 room 배정을 요청한다.
9. `PlayServer` 는 `BingoRoomSpot` 을 확보하고 matching 을 요청한 actor 를
   `BingoEntrySpot` 에서 해당 room 으로 join 시킨다.
10. room 은 처음 join 한 actor 를 `HostActorId` 로 지정한다.
11. matching 결과는 client 에게 `MatchBingoRes` 로 전달된다.
12. host client 가 `StartBingoGameReq` 를 보내면 room 이 시작 조건을 확인한다.
13. 시작 조건을 만족하면 room status 를 `Running` 으로 바꾸고 timer 를 시작한다.
14. timer tick 마다 room 이 번호를 뽑고 모든 player card 를 자동 mark 한다.
15. 승자가 나오면 room status 를 `Finished` 로 바꾸고 모든 참가자에게 종료
    이벤트를 push 한다.

현재 샘플의 `ApiServer` 는 room 생성/배정 요청을 `PlayServer` 의 server channel 로
보낸다. 즉 allocation 단계는 `IZLinkClient.Request(...)`를 쓰는 일반
channel-to-channel request 이다. `IZLinkClient`는 등록된 channel 이름을 보고
client-server 또는 dealer mesh outbound socket 을 선택한다. `PlayServer` 쪽 handler 가 요청을 받은 뒤
`BingoRoomSpot` 을 만들거나 기존 room 을 찾아 Entry Spot 에 join 을 요청한다.

이미 target Spot 의 `RoutingId`를 알고 있고, 일반 channel handler 에서 그 Spot 으로
곧장 보내야 하는 흐름은 별도 routed Spot 패턴으로 본다. 그 경우에는
`IZLinkRoutedSpotClient.ViaEgressChannel(localEgressChannelName)`으로 사용할 egress
channel 을 명시하고, egress channel 등록에는
`EnableSpotRouteEgress(targetSpotNodeChannelName)`으로 Play 서버 SpotNode가 accept 한
ingress channel 이름을 저장한다.

## 4. Entry Spot 역할

`BingoEntrySpot` 은 match room 에 들어가기 전의 lobby 역할을 한다. actor 는
Play 서버의 SpotNode 에 붙으면 먼저 Entry Spot 에 위치한다. Entry Spot 은 actor
가 어느 room 으로 들어갈지 결정하기 전의 공통 관문이다.

이 샘플에서 Entry Spot 은 다음 기능을 맡는다.

- actor 가 아직 특정 room 에 들어가지 않은 상태를 표현한다.
- matching 결과로 받은 room 이 존재하는지 확인한다.
- room 이 입장 가능한 상태인지 확인한다.
- 같은 actor 가 이미 room 에 들어가 있는 경우 중복 join 을 막는다.
- 입장이 가능하면 actor 를 `BingoRoomSpot` 으로 이동시킨다.

게임 시작, 번호 추첨, 승리 판정은 Entry Spot 이 처리하지 않는다. 이 동작들은
room 상태의 일부이므로 `BingoRoomSpot` 이 맡는다. 이렇게 나누면 Entry Spot 은
lobby 와 admission 에 집중하고, room 은 실제 match 진행에 집중한다.

## 5. 게임 규칙

첫 샘플의 규칙은 framework 흐름을 보기 쉽도록 단순하게 둔다.

| 항목 | 규칙 |
|------|------|
| 플레이어 수 | 정확히 4명 |
| 보드 | 5 x 5 |
| 번호 범위 | 1부터 75까지 |
| 가운데 칸 | free cell 로 시작부터 mark 처리 |
| host 지정 | room 에 처음 join 한 actor 가 host 가 된다. |
| 시작 조건 | host 가 시작 요청을 보내고, 4명이 모두 join 되어 있어야 한다. |
| 번호 추첨 | room timer 가 일정 주기로 하나씩 뽑는다. |
| mark 방식 | 자동 mark. 서버가 뽑은 번호가 카드에 있으면 room 이 바로 mark 한다. |
| 승리 조건 | 새 draw sequence 기준으로 complete line 이 1개 이상이면 승리 |
| 동시 승리 | 같은 draw sequence 에서 승리한 플레이어는 공동 승리 |
| 종료 조건 | 첫 승리 draw sequence 가 나오면 종료 |

자동 mark 를 기본으로 두는 이유는 샘플이 client 조작 검증보다 matching room
게임 구조를 보여 주는 데 목적이 있기 때문이다. 수동 mark 를 넣으면 누락 mark
복구, claim 순서, 부정 입력 검증 같은 규칙이 늘어난다. 첫 샘플에서는 room 이
모든 카드를 검사해 결과를 확정하는 쪽이 더 명확하다.

## 6. 상태 모델

room 상태는 다음 정보를 가진다.

```csharp
public sealed record BingoRoomState(
    string RoomId,
    string Status,
    string HostActorId,
    bool CanStart,
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
    bool IsHost,
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
| `WaitingForPlayers` | room 이 만들어졌지만 아직 게임이 시작되지 않았다. |
| `Running` | host 시작 요청이 승인되어 번호 추첨이 진행 중이다. |
| `Finished` | 승자가 확정되어 게임이 끝났다. |

`CanStart` 는 client 화면에서 시작 버튼을 켤지 판단하기 위한 편의 정보다. 실제
시작 가능 여부는 `StartBingoGameReq` 를 처리할 때 room 이 다시 확인한다.

## 7. DTO 초안

인증과 회원 정보 조회 DTO 는 다음과 같다.

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string ActorId,
    string DisplayName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(
    string ActorId,
    string DisplayName);
```

matching 요청과 응답 DTO 는 다음과 같다.

```csharp
public sealed record MatchBingoReq(string Mode);

public sealed record MatchBingoRes(
    string RoomId,
    BingoRoomState State);

public sealed record AllocateBingoRoomReq(
    string ActorId,
    string DisplayName,
    string Mode);

public sealed record AllocateBingoRoomRes(
    string RoomId,
    BingoRoomState State);
```

room join 과 게임 진행 DTO 는 다음과 같다.

```csharp
public sealed record BingoRoomJoinReq(
    string RoomId,
    string ActorId,
    string DisplayName);

public sealed record BingoRoomJoinRes(BingoRoomState State);

public sealed record StartBingoGameReq(string RoomId);

public sealed record StartBingoGameRes(BingoRoomState State);

public sealed record LeaveRoomReq(string RoomId);

public sealed record LeaveRoomRes(BingoRoomState State);
```

server push DTO 는 다음과 같다.

```csharp
public sealed record PlayerJoinedNotify(
    string RoomId,
    string ActorId,
    string DisplayName,
    int Seat,
    bool IsHost,
    BingoRoomState State);

public sealed record BingoGameStartedNotify(BingoRoomState State);

public sealed record BingoNumberDrawnNotify(
    string RoomId,
    int DrawSeq,
    int Number,
    BingoRoomState State);

public sealed record BingoStateNotify(BingoRoomState State);

public sealed record BingoGameEndedNotify(BingoRoomState State);
```

## 8. 메시지 흐름

인증 흐름은 다음과 같다.

1. client 는 `SessionServer` 에 STREAM 연결을 만든다.
2. client 는 `AuthenticateReq` 를 보낸다.
3. `SessionServer` 는 `ApiServer` 로 `AuthenticatePlayerReq` 를 보내 회원 정보를
   확인한다.
4. `SessionServer` 는 `PlayServer` 로 player actor 준비를 요청하고, 응답으로
   actor route snapshot 을 받는다. 이 snapshot 에는 router channel id, target
   node rid, actor generation 이 들어 있다.
5. `SessionServer` 는 route 를 받는 `BindActorHandleAsync(...)` overload 로 현재
   stream session 과 actor 를 bind 한다. session handler 는 actor route resolver
   를 직접 호출하지 않는다.
6. client 는 `AuthenticateRes` 로 `ActorId` 와 `DisplayName` 을 받는다.

matching 흐름은 다음과 같다.

1. client 는 `MatchBingoReq` 를 보낸다.
2. `SessionServer` 는 요청을 현재 session 에 bind 된 actor 로 relay 한다.
3. actor handler 는 `ApiServer` 로 matching 요청을 보낸다.
4. `ApiServer` 는 `PlayServer` 로 `AllocateBingoRoomReq` 를 보낸다.
5. `PlayServer` 는 사용 가능한 room 을 찾거나 새 `BingoRoomSpot` 을 만든다.
6. actor 는 `BingoEntrySpot` 에서 `BingoRoomSpot` 으로 join 된다.
7. room 은 seat 를 배정하고, 첫 참가자이면 `HostActorId` 를 설정한다.
8. client 는 `MatchBingoRes` 로 room id 와 현재 room state 를 받는다.
9. room 은 모든 참가자에게 `PlayerJoinedNotify` 를 push 한다.

게임 시작 흐름은 다음과 같다.

1. host client 는 `StartBingoGameReq` 를 보낸다.
2. `SessionServer` 는 요청을 bound actor 로 relay 한다.
3. actor 는 현재 join 된 `BingoRoomSpot` 에 시작 요청을 전달한다.
4. room 은 요청 actor 가 `HostActorId` 와 같은지 확인한다.
5. room 은 참가자가 정확히 4명인지 확인한다.
6. 조건을 만족하면 room status 를 `Running` 으로 바꾸고 timer 를 시작한다.
7. room 은 모든 참가자에게 `BingoGameStartedNotify` 를 push 한다.

게임 진행 흐름은 다음과 같다.

1. room timer 는 draw deck 에서 다음 번호를 하나 뽑는다.
2. room 은 모든 player card 를 검사해서 해당 번호를 mark 한다.
3. room 은 같은 draw sequence 에서 새 complete line 이 생긴 player 를 모두 찾는다.
4. room 은 각 draw 결과를 `BingoNumberDrawnNotify` 로 push 한다.
5. 승자가 있으면 room status 를 `Finished` 로 바꾼다.
6. room 은 공동 승자를 모두 `Winners` 에 넣고 `BingoGameEndedNotify` 를 push 한다.

## 9. 설계 결정

이 샘플은 단순한 게임이지만, matching room 기반 멀티 플레이 게임의 기본 구조를
명확히 보여 주는 데 목적이 있다.

| 결정 | 이유 |
|------|------|
| gateway 형태로 구성한다. | Session 서버, API 서버, Play 서버의 역할이 분리되어 matching room 구조와 Entry Spot 역할이 잘 드러난다. |
| Entry Spot 은 lobby 와 admission 만 맡는다. | room 입장 전 공통 처리를 모으고, 실제 게임 규칙은 room 에 숨긴다. |
| 게임 규칙은 `BingoRoomSpot` 에 둔다. | host, seat, 카드 생성, 번호 추첨, mark, 빙고 판정을 한 모듈 안에 숨긴다. |
| actor 는 player identity 와 client push 표면을 맡는다. | actor 가 빙고 규칙을 알 필요가 없고, session binding 과 사용자 상태만 다루면 된다. |
| room 의 첫 참가자를 host 로 지정한다. | 별도 방장 선택 API 없이 시작 권한을 자연스럽게 보여 줄 수 있다. |
| host 시작 요청 후 timer 를 시작한다. | 실제 게임처럼 대기 room 과 진행 중 room 의 차이를 보여 준다. |
| client 는 mark 나 bingo claim 을 보내지 않는다. | 첫 샘플에서는 불필요한 검증 분기를 없애고 server authoritative 흐름을 보여 준다. |
| 같은 draw sequence 의 승자는 공동 승리로 처리한다. | 네트워크 도착 순서나 push 순서가 게임 결과를 바꾸지 않게 한다. |

수동 mark, ready 버튼, 방장 위임, 여러 라운드, 랭킹 유지 같은 기능은 첫 구현
범위에서 제외한다. 필요한 경우 두 번째 샘플 단계에서 추가한다.

## 10. 완료 기준

- 4개의 client connector 가 `SessionServer` 에 접속하고 각각 다른 actor 로
  인증된다.
- client 는 `MatchBingoReq` 하나로 room 배정과 actor room join 결과를 받는다.
- 첫 번째로 room 에 join 된 actor 가 `HostActorId` 로 설정된다.
- host 가 아닌 actor 의 `StartBingoGameReq` 는 거부된다.
- 4명이 모이기 전 host 의 `StartBingoGameReq` 는 거부된다.
- host 가 4명 room 에서 시작 요청을 보내면 room status 가 `Running` 으로 바뀐다.
- room timer 가 번호를 뽑고, 각 player card 의 mark 를 server 쪽에서 갱신한다.
- 같은 draw sequence 에서 여러 winner 가 나오면 모두 `Winners` 에 포함된다.
- actor request reply 는 handler 반환값으로 처리하고, actor context `Reply(...)`
  를 사용하지 않는다.
- client 로 가는 push 는 actor session binding 을 통해 전달된다.
- 샘플의 public DTO 에서 player identity 필드는 `ActorId` 로 통일한다.
- 샘플은 Registry 기반 Spot route 기본 구현을 사용하고, 자체 metadata store 를
  두지 않는다.

## 11. 회귀 테스트

빙고 샘플을 구현할 때는 새 샘플 실행 테스트를 추가하기 전에도, 아래 기존 회귀
테스트가 깨지지 않아야 한다. 이 테스트들은 빙고 샘플이 사용할 framework 표면을
이미 고정하고 있다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session gateway 경로에서 request/reply sequence 가 actor dispatch 와 맞물려 동작한다. |
| `ProtocolTests.ActorPacketRegistry_DoesNot_Resolve_Request_To_Send_Handler` | actor request packet 이 send handler 로 fallback dispatch 되지 않는다. |
| `ProtocolTests.SpotActorRegistry_DoesNot_Resolve_Request_To_Send_Handler` | Entry Spot 과 user Spot actor request packet 이 send handler 로 fallback dispatch 되지 않는다. |
| `ActorRegistryExecutionTests.EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets` | Entry Spot actor handler 와 user Spot actor handler 가 각각 등록되어 dispatch 된다. |
| `ManagerTests.Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work` | room timer 기반 진행과 spot lifecycle 정리가 framework timer 계약과 맞는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | client connector request/reply correlation 이 유지된다. |
| `RegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo 샘플이 sample-only registry metadata store 없이 Registry 기본 API 를 사용한다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
