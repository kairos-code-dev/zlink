[TicTacToe Sample](./README.ko.md) | [Session Actor Dispatch 버전](./session-gateway.ko.md)

# TicTacToe Direct Sample

> 이 문서는 일반 TicTacToe 샘플의 구현 기준을 정한다.
> client는 API 서버에서 match 정보를 받은 뒤 Play 서버의 stream endpoint에 직접
> 연결한다.

## 1. 목적

일반 버전은 가장 작은 실시간 게임 샘플이다. API 서버는 match 생성과 인증 토큰
발급을 맡고, Play 서버는 client stream, actor, game room을 모두 소유한다. 이 구조는
client와 gameplay 서버가 직접 연결되는 모델을 보여 준다.

이 샘플에서 확인해야 하는 핵심은 아래와 같다.

- API 서버가 Play 서버에 channel request로 match 생성을 요청한다.
- client가 Play 서버 stream에 직접 연결한다.
- Play session이 API 서버에 인증 request를 보내고 `playerId`를 얻는다.
- Play actor의 `ActorId`는 인증 결과의 `playerId`와 같다.
- game room은 틱택토 board와 turn을 authoritative state로 관리한다.
- 상대 입장, 턴 변경, 게임 종료는 `Notify` 메시지로 client에게 push한다.

## 2. 서버 구성

```mermaid
flowchart LR
    C[Client]
    API[Api Server]
    PLAY[Play Server]

    C -->|HTTP CreateMatchReq| API
    API -->|Channel Play / CreateMatchReq| PLAY
    PLAY -->|Channel Api / AuthenticatePlayerReq| API
    C -->|STREAM AuthenticateReq / Game Packets| PLAY
```

다이어그램의 흐름은 아래와 같다.

- client는 `CreateMatchReq`를 API 서버 HTTP endpoint로 보낸다.
- API 서버는 `Play` channel client로 Play 서버에 `CreateMatchReq`를 보낸다.
- Play 서버는 match room을 만들고 stream endpoint와 match id를 반환한다.
- client는 반환받은 stream endpoint로 Play 서버에 연결한다.
- Play 서버는 stream 인증 시 API 서버의 `Api` channel로 인증을 확인한다.

## 3. 프로세스와 책임

| 프로세스 | 구성 요소 | 책임 |
|----------|-----------|------|
| `TicTacToe.Api` | HTTP endpoint | match 생성 요청을 받고 client에 접속 정보를 반환한다. |
| `TicTacToe.Api` | `Api` channel server | Play 서버의 인증 요청을 처리한다. |
| `TicTacToe.Api` | `Play` channel client | Play 서버에 match 생성을 요청한다. |
| `TicTacToe.Play` | `Play` channel server | match room을 만들고 stream endpoint를 반환한다. |
| `TicTacToe.Play` | stream server | client 연결과 session dispatch를 처리한다. |
| `TicTacToe.Play` | actor runtime | 인증된 player actor를 생성하고 room에 join한다. |
| `TicTacToe.Play` | spot/game room | board, turn, 승패 판정을 소유한다. |

## 4. Endpoint와 Channel

| 이름 | 소유 프로세스 | 방향 | 용도 |
|------|---------------|------|------|
| `http://api/matches` | API | client -> API | match 생성 |
| `Play` channel | Play server | API -> Play | match room 생성 |
| `Api` channel | API server | Play -> API | access token 인증 |
| `client-stream` | Play server | client -> Play | 게임 메시지 |
| `play-spot` | Play server | 내부 | match room 실행 context |

`Play` channel과 `Api` channel은 일반 channel messaging을 사용한다. 특정 node로 직접
보내는 routed channel은 이 버전에서 사용하지 않는다.

## 5. 메시지 계약

HTTP와 server channel에서 사용하는 메시지:

```csharp
public sealed record CreateMatchReq(string? MatchName);

public sealed record CreateMatchRes(
    string MatchId,
    string StreamEndpoint,
    string PlayerAToken,
    string PlayerBToken);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(
    bool Accepted,
    string? PlayerId,
    string? Reason);
```

client stream에서 사용하는 request/response:

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string PlayerId,
    string ActorId);

public sealed record JoinMatchReq(string MatchId);

public sealed record JoinMatchRes(
    string MatchId,
    string PlayerId,
    string Mark,
    TicTacToeState State);

public sealed record PlaceMarkReq(
    string MatchId,
    int Cell);

public sealed record PlaceMarkRes(
    TicTacToeState State);
```

server push 메시지:

```csharp
public sealed record OpponentJoinedNotify(
    string MatchId,
    string OpponentPlayerId);

public sealed record TurnChangedNotify(
    string MatchId,
    string TurnPlayerId,
    TicTacToeState State);

public sealed record GameEndedNotify(
    string MatchId,
    string? WinnerPlayerId,
    bool Draw,
    TicTacToeState State);
```

공통 state 모델:

```csharp
public sealed record TicTacToeState(
    string MatchId,
    string Board,
    string TurnPlayerId,
    string? WinnerPlayerId,
    bool Draw);
```

`Board`는 9글자 문자열이다. 빈 칸은 `-`, `X` player의 mark는 `X`, `O` player의
mark는 `O`로 표현한다. 예를 들어 `"X-O---X--"`는 0번과 6번 cell에 `X`, 2번 cell에
`O`가 놓인 상태다.

## 6. Match 생성 흐름

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant API as Api Server
    participant PLAYC as Play Channel Client
    participant PLAYS as Play Channel Server
    participant ROOM as Game Room Registry

    C->>API: HTTP CreateMatchReq
    API->>PLAYC: Request Play/CreateMatchReq
    PLAYC->>PLAYS: Channel request
    PLAYS->>ROOM: Create match room
    ROOM-->>PLAYS: MatchId
    PLAYS-->>PLAYC: CreateMatchRes
    PLAYC-->>API: CreateMatchRes
    API-->>C: HTTP CreateMatchRes
```

API 서버는 token을 발급하고, Play 서버는 match room과 stream endpoint를 반환한다.
샘플 smoke에서는 `PlayerAToken`, `PlayerBToken`을 함께 반환해서 두 client를 쉽게
실행할 수 있게 한다.

## 7. 인증과 입장 흐름

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant S as Play Session
    participant API as Api Channel
    participant ACT as Play Actor
    participant ROOM as Game Room
    participant OACT as Opponent Actor
    participant OC as Opponent Client

    C->>S: Stream AuthenticateReq
    S->>API: AuthenticatePlayerReq
    API-->>S: AuthenticatePlayerRes(playerId)
    S->>ACT: Create actor(actorId=playerId)
    S-->>C: AuthenticateRes
    C->>S: Stream JoinMatchReq
    S->>ACT: Dispatch JoinMatchReq
    ACT->>ROOM: Join actor
    ROOM-->>ACT: Mark + state
    ACT-->>S: JoinMatchRes
    S-->>C: Stream JoinMatchRes
    ROOM-->>ACT: OpponentJoinedNotify
    ROOM-->>OACT: OpponentJoinedNotify
    ACT-->>C: Stream OpponentJoinedNotify
    OACT-->>OC: Stream OpponentJoinedNotify
```

첫 packet은 반드시 `AuthenticateReq`여야 한다. 인증이 끝나기 전에
`JoinMatchReq`나 `PlaceMarkReq`를 받으면 session은 오류 response를 반환하고 actor를
생성하지 않는다.

## 8. 수 두기와 Notify 흐름

```mermaid
sequenceDiagram
    autonumber
    participant CA as Client A
    participant SA as Session A
    participant AA as Actor A
    participant ROOM as Game Room
    participant AB as Actor B
    participant CB as Client B

    CA->>SA: PlaceMarkReq(cell)
    SA->>AA: Dispatch PlaceMarkReq
    AA->>ROOM: Place mark
    ROOM-->>AA: Accepted state
    AA-->>SA: PlaceMarkRes
    SA-->>CA: PlaceMarkRes
    ROOM-->>AA: TurnChangedNotify
    ROOM-->>AB: TurnChangedNotify
    AA-->>CA: TurnChangedNotify
    AB-->>CB: TurnChangedNotify
```

승리 또는 draw가 만들어지면 `TurnChangedNotify` 대신 `GameEndedNotify`를 양쪽
client에게 보낸다. 잘못된 turn, 이미 사용한 cell, 끝난 match에 대한 요청은
`PlaceMarkRes` 대신 오류 response를 반환한다.

## 9. 완료 기준

- API 서버와 Play 서버가 별도 프로젝트로 분리되어 있다.
- client는 Play 서버 stream endpoint에 직접 연결한다.
- Play session은 `AuthenticateReq`에서 API 서버로 인증 request를 보낸다.
- 인증 응답의 `playerId`를 actor의 `ActorId`로 사용한다.
- `JoinMatchReq` 이후 actor가 game room에 join한다.
- 두 player가 모두 join하면 `OpponentJoinedNotify`가 전달된다.
- 정상 move마다 `PlaceMarkRes`와 `TurnChangedNotify`가 전달된다.
- 게임 종료 시 `GameEndedNotify`가 양쪽 client에 전달된다.
- request/reply는 message name이 아니라 stream request sequence로 매칭된다.
- smoke test는 match 생성, 두 client 인증, join, 최소 한 판 종료까지 검증한다.
