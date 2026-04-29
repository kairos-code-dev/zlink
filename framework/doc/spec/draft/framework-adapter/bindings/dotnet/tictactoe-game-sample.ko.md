# TicTacToe Game Sample 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, .NET framework adapter와
> stream connector를 함께 보여 주는 게임 샘플의 구현 기준을 정한다.

[.NET 묶음](./README.ko.md) | [Stream Connector](./streaming-client.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [SPOT](./aspnet-core-spot.ko.md)

## 1. 목적

이 샘플은 echo 예제보다 한 단계 실제 게임 서버에 가까운 최소 구조를 보여 준다.
클라이언트는 API 서버에 HTTP로 게임 방 생성을 요청하고, API 서버는 Play 서버의
channel request로 방 생성을 요청한다. Play 서버는 SPOT room을 만들고, 클라이언트는
반환받은 stream endpoint와 room id로 Play 서버에 접속한다. STREAM 접속 후에는
먼저 `Authenticate` request를 보내고, Play 서버는 API 서버에 인증 검증 request를
보낸 뒤 성공한 연결만 room join과 game move를 허용한다.

게임 규칙은 틱택토로 한다. 틱택토는 서버가 authoritative state를 관리하기 쉽고,
두 플레이어 입장, 턴 검증, 승패 판정이 모두 작아서 샘플 코드가 길어지지 않는다.

## 2. 프로세스 구성

```text
+--------+      HTTP       +------------+      channel      +-------------+
| Client | --------------> | Api Server | ----------------> | Play Server |
+--------+  CreateGame    +------------+  CreateGameRoom   +-------------+
     |                         ^                                  |
     |                         | channel                          |
     |                         +----- ValidatePlayerSession ------+
     |                                                            |
     |                         STREAM                             |
     +----------------------------------------------------------> |
                     Authenticate / JoinGame / PlaceMark
```

다이어그램의 `Api Server`는 HTTP endpoint와 `Api` channel server를 연다. API 서버는
`Play` channel client를 등록해 Play 서버의 router로 `CreateGameRoom` request를
보낸다. Play 서버는 `Play` channel server를 열고, STREAM 인증 검증을 위해 `Api`
channel client도 등록한다. 이 샘플에서 API 서버가 `Api` channel client를 가질 필요는
없다.

`Play Server`는 `Play` channel server, `Api` channel client, STREAM node, SPOT node를
연다. 클라이언트는 API가 돌려준 STREAM endpoint로 접속한다.

## 3. 채널과 endpoint

| 이름 | 소유 서버 | 역할 |
|------|-----------|------|
| `Api` | API 서버 | Play 서버가 player session 검증을 요청하는 router channel |
| `Play` | Play 서버 | API 서버가 방 생성을 요청하는 router channel |
| `client-stream` | Play 서버 | 게임 클라이언트가 접속하는 STREAM endpoint |
| `play-node` | Play 서버 | game room SPOT을 생성하고 actor를 join한다 |

API 서버는 `Play` channel에 client capability를 등록하고 `PlayChannelEndpoint`에
connect한다. Play 서버는 `Play` channel에 server capability를 등록하고 같은 endpoint에
bind한다.

Play 서버는 `Api` channel에 client capability를 등록하고 `ApiChannelEndpoint`에
connect한다. API 서버는 `Api` channel에 server capability를 등록하고 같은 endpoint에
bind한다. 이 경로는 STREAM 접속 인증 검증에만 사용한다.

## 4. 메시지 계약

HTTP 요청/응답:

```csharp
sealed record CreateGameHttpRequest(string? RoomName);

sealed record CreateGameHttpReply(
    string RoomId,
    string StreamEndpoint,
    string RoomName,
    string PlayerTokenA,
    string PlayerTokenB);
```

API 서버에서 Play 서버로 보내는 channel request:

```csharp
sealed record CreateGameRoom(string RoomName);

sealed record CreateGameRoomReply(
    string RoomId,
    string StreamEndpoint,
    string RoomName);
```

Play 서버에서 API 서버로 보내는 인증 검증 request:

```csharp
sealed record ValidatePlayerSession(
    string RoomId,
    string PlayerId,
    string PlayerToken);

sealed record ValidatePlayerSessionReply(
    bool Accepted,
    string? Reason);
```

클라이언트에서 Play 서버 STREAM으로 보내는 request:

```csharp
sealed record Authenticate(
    string RoomId,
    string PlayerId,
    string PlayerToken);

sealed record Authenticated(
    string RoomId,
    string PlayerId);

sealed record JoinGame(string RoomId, string PlayerId);

sealed record JoinGameAccepted(
    string RoomId,
    string PlayerId,
    char Mark,
    GameState State);

sealed record PlaceMark(int Cell);

sealed record GameState(
    string RoomId,
    string Board,
    char Turn,
    char? Winner,
    bool Draw);
```

packet 이름은 attribute를 쓰지 않고 CLR 타입 이름을 기본값으로 사용한다. 예를 들어
`JoinGame` body는 helper header의 packet name도 `JoinGame`이다.

## 5. STREAM session

Play 서버의 client stream은 `IZLinkSession`을 사용한다.

```csharp
stream.AddHeaderSession<GameStreamSession>();
```

framework는 zlink stream helper header를 decode해서 `OnDispatchAsync`에 넘긴다.
session 구현은 raw `Message header`를 직접 decode하지 않는다.

```csharp
public ValueTask OnDispatchAsync(
    ZlinkStreamHeader header,
    Message body,
    CancellationToken cancellationToken);
```

STREAM session은 연결별 인증 상태를 가진다. 첫 request는 `Authenticate`여야 한다.
`Authenticate`를 받으면 Play 서버는 `Context.RequestChannel(...)`로 `Api` channel에
`ValidatePlayerSession` request를 보낸다. API 서버가 성공을 반환하면 session은
인증된 player id와 room id를 저장하고
`Authenticated`를 reply한다.

`JoinGame` request는 인증된 session에서만 허용한다. request의 room id와 player id는
인증된 값과 같아야 한다. 검증이 끝나면 actor를 SPOT room에 join한다. `PlaceMark`
request는 이미 join된 actor가 room state를 갱신하도록 전달한다.

## 6. SPOT과 actor 책임

SPOT room은 틱택토 board, 현재 턴, 플레이어 배정, 승패 상태를 소유한다. actor는
client stream과 player id를 소유하고, STREAM request를 room state 변경으로 연결한다.

책임 분리는 아래와 같다.

| 구성 요소 | 책임 |
|-----------|------|
| API HTTP handler | HTTP 요청을 받고 `Play` channel request를 보낸다 |
| API auth handler | Play 서버의 `ValidatePlayerSession` request를 검증한다 |
| Play channel handler | SPOT room을 생성하고 room id와 stream endpoint를 반환한다 |
| Game stream session | STREAM 접속별 actor와 인증 상태를 만들고 header session dispatch를 받는다 |
| Player actor | client stream과 player id를 소유하고 room에 join한다 |
| GameRoom spot | board, turn, winner, draw 판정을 소유한다 |

## 7. 샘플 smoke 흐름

자동 smoke는 한 프로세스에서 API 서버, Play 서버, 두 client를 띄운다.

1. client A가 HTTP `CreateGame`을 호출한다.
2. API 서버가 `Play` channel로 `CreateGameRoom` request를 보낸다.
3. Play 서버가 SPOT room을 만들고 room id, stream endpoint를 반환한다.
4. API 서버는 client smoke가 사용할 `PlayerTokenA`, `PlayerTokenB`도 함께 반환한다.
5. client A와 client B가 STREAM에 접속한다.
6. A/B는 각각 `Authenticate(roomId, playerId, playerToken)` request를 보낸다.
7. Play 서버는 각 request마다 `Api` channel로 `ValidatePlayerSession` request를 보낸다.
8. API 서버가 성공을 반환하면 Play 서버는 `Authenticated`를 reply한다.
9. A는 `JoinGame(roomId, "player-a")`, B는 `JoinGame(roomId, "player-b")` request를 보낸다.
   이 request는 stream session이 직접 처리하지 않고 player actor로 dispatch된다.
10. A/B가 번갈아 `PlaceMark` request를 보낸다.
11. 서버는 매번 `GameState`를 reply한다.
12. 마지막 move에서 winner 또는 draw가 설정되면 smoke가 성공으로 끝난다.

## 8. 완료 기준

- API 서버와 Play 서버 폴더가 분리되어 있다.
- API handler와 Play handler가 각각 `Api/Handlers`, `Play/Handlers` 아래에 있다.
- API 서버는 `Api` channel server와 `Play` channel client를 등록한다.
- Play 서버는 `Play` channel server와 `Api` channel client를 등록한다.
- STREAM 접속 후 첫 request는 `Authenticate`이며, Play 서버는 API 서버에
  `ValidatePlayerSession` request를 보내 검증한다.
- 인증 전 `JoinGame`과 `PlaceMark`는 실패해야 한다.
- client는 `ZlinkStreamConnector.Request(...).Async<TReply>()`만 사용한다.
- Play stream node는 `AddHeaderSession<T>()`를 사용한다.
- sample code에서 packet name attribute를 쓰지 않는다.
- smoke 실행 시 두 client가 같은 room에 join하고, 최소 한 판의 틱택토가 끝난다.
