# Bingo Sample Scenario

[샘플 목록](../README.ko.md)

> 이 문서는 모든 framework 언어가 공유하는 Bingo 샘플 시나리오를 정의한다.
> 언어별 샘플은 이 문서를 기준으로 서버 역할, 메시지 흐름, smoke 검증 기준을 맞춘다.
>
> **자동 routing id 필수.** Bingo의 API·Matchmaking·Play·Session runtime은 역할을
> 나타내는 prefix만 설정해야 한다. Framework가 process를 시작할 때마다 새 UUID를 붙여
> MeshNode RID를 만든다. 고정 RID를 설정하거나 RID를 업무 object 배치에 사용하면 안 된다.

## 1. 목적

Bingo 샘플은 client가 하나의 Session 서버 stream 연결만 유지해도 인증, 매칭,
게임 진행, server push를 모두 처리할 수 있음을 보여 준다. Session 서버는 client
연결과 actor binding을 맡고, Play 서버는 player actor와 room Spot을 소유한다.
API 서버는 인증과 매칭 요청을 처리한다. Matchmaking 서버의 Instance Spot은 Redis에서
waiting room 예약을 조율한다. 서버 간 endpoint 발견은 공유 Location Store가 맡는다.
샘플은 `api-a`, `api-b`, `matchmaking`, `play-a`, `play-b`, `session-a`, `session-b`를 실행해
gateway 구조에서도 scale-out, remote Spot join, Logical Multicast event fan-out이 함께
동작하는지 보여 준다.

이 샘플에서 확인해야 하는 핵심은 아래와 같다.

- client는 Session 서버 stream endpoint 하나만 알고 연결한다.
- Session, API, Play 역할은 각각 2개 실행한다. Matchmaking 서버는 샘플 topology에서 1개를
  실행하지만 framework 계약상 singleton으로 고정하지 않는다.
- Session 서버는 인증 후 현재 stream session을 Play 서버의 player actor에 bind한다.
- client packet은 bound actor로 relay된다.
- API 서버는 level bucket의 Matchmaker Instance Spot에 매칭을 요청한다.
- Matchmaker Instance Spot은 Redis를 source of truth로 사용해 waiting room과 actor
  reservation을 원자적으로 결정한다.
- API 서버는 예약받은 `RoomId`와 동일한 settings로 Play mesh의 Room User Spot
  `GetOrCreate`를 호출하고 Ready 결과를 기다린다.
- Matchmaking Redis는 reservation state만 저장한다. Room Spot owner lookup은 Location Store가 맡는다.
- Play 서버는 Entry Spot에서 room Spot으로 actor를 join시킨다. player actor와 room Spot이
  서로 다른 Play 서버에 있으면 framework가 Location Store에서 current owner를 확인해
  remote Spot join을 실행한다.
- client는 자기 bingo card를 입력해 room Spot에 제출한다.
- room Spot은 제출된 카드, 번호 추첨, mark, 승리 판정을 소유한다.
- 두 player client의 card가 모두 제출되면 room Spot이 server timer로 번호를 뽑고,
  Play 서버는 번호와 state를 bound session으로 Notify한다.
- 승자가 나오고 결과 처리 중 희귀 보상이 지급되면 owner `BingoRoom`은 Logical Multicast
  topic으로 `BingoRewardAcquiredEvent`를 publish하고, 다른 Play 서버의 `BingoRoom`은
  event를 받아 observer client로 push한다.
- 공유 Location Store를 사용해 서버 간 endpoint를 자동으로 발견하고 연결한다.
- handler는 typed handler 계약을 구현하고 스캔·선언형 metadata로 **자동 등록**된다.
- Bingo의 stream, channel, actor, room Spot payload는 Protobuf를 사용한다.

Client self-check도 샘플의 일부다. client는 `.NET` 샘플처럼 각 request 응답과 server
push payload를 즉시 검증해야 한다. 특히 `PlayerJoinedNotify`,
`BingoGameStartedNotify`, `BingoNumberDrawnNotify`, `BingoGameEndedNotify`,
`BingoRewardAnnouncedNotify` 대기는
stream connector의 public wait interface를 직접 사용한다. inbox를 두더라도 push 도착을
기다리는 로직을 sample-local polling 함수로 숨기면 안 된다.

Bingo가 Protobuf를 맡는 이유는 이 샘플이 여러 서버 역할과 많은 request/response/notify
계약을 가진 gateway형 게임이기 때문이다. Protobuf schema는 언어별 샘플이 같은 필드와
같은 wire 이름을 유지하도록 돕는다. JSON payload 가독성은 TicTacToe와 다른 JSON 기반
샘플이 맡는다.

## 2. 서버 구성

Bingo의 역할과 물리 연결은
[공통 topology 기준](../README.ko.md#channel-역할과-물리-topology-기준)을 따른다.

- `bingo.play` RouteMesh는 Room·Actor의 logical object routing을 담당한다. API와 Session은
  Object Client, Play는 Object Server다.
- `bingo.matchmaking` RouteMesh는 level bucket별 Matchmaker Instance Spot 호출을 담당한다.
  API는 Object Client, Matchmaking 서버는 Object Server다.
- `bingo.api`는 Session·Play가 API에 request를 보내는 독립 ClientServer Channel이다.
  Object Client와 ClientServer Server를 같은 RouteMesh에 합치지 않는다.

```mermaid
graph LR
    C[Client]
    SA[Session A]
    SB[Session B]
    APIA[Api A]
    APIB[Api B]
    MM[Matchmaking Server]
    PA[Play A]
    PB[Play B]
    LS[("Location Store<br/>공유 저장소 · 예: Redis")]
    REDIS[(Redis reservation state)]

    C -->|STREAM player-1 packets| SA
    C -->|STREAM player-2 packets| SB
    C -->|STREAM observer packets| SB
    SA -->|API channel| APIA
    SB -->|API channel| APIB
    SA -->|Actor gateway| PA
    SB -->|Actor gateway| PB
    APIA -->|Instance Spot request| MM
    APIB -->|Instance Spot request| MM
    MM -->|Reservation| REDIS
    APIA -->|Room GetOrCreate| PA
    APIB -->|Room GetOrCreate| PB
    PA <-->|Remote Spot join| PB
    PA <-->|Logical Multicast| PB
    PA -->|Bound session Notify| SA
    PA -->|Bound session Notify| SB
    PB -->|Observer push| SB
    SA -->|STREAM Notify| C
    SB -->|STREAM Notify| C
    SA -. peer discovery .-> LS
    SB -. peer discovery .-> LS
    APIA -. peer discovery .-> LS
    APIB -. peer discovery .-> LS
    MM -. peer discovery .-> LS
    PA -. peer discovery .-> LS
    PB -. peer discovery .-> LS
```

client가 직접 연결하는 서버는 Session 서버뿐이다. API, Matchmaking과 Play 서버는
client-facing stream endpoint를 열지 않는다. Session 서버는 인증과 session lifecycle을
소유하지만, 게임 규칙을 해석하지 않는다. 게임 packet은 현재 session에 bind된 actor로
전달되고, actor와 room Spot이 domain state를 처리한다.

다이어그램의 Play 방향은 예시다. Actor와 room Spot의 실제 owner는
framework가 stable type, Serving 상태, capacity와 node-wide weight를 기준으로 선택한다.
샘플의 설정·업무 메시지·reservation state는 NodeRid를 배치 입력이나 검증값으로 사용하지 않는다.
client는 전역 `ActorId`와 `RoomId`로 요청하고, framework가 Location Store에서 현재 owner를
확인한다. 특정 두 node를 반드시 거치는 검증은 공통 E2E가 담당한다.

## 3. 자동 연결 방식

Bingo는 공유 Location Store 기반 automatic discovery를 사용한다. 각 서버는 MeshName, RID, capability와
MeshNode endpoint를 descriptor로 게시하고, STREAM endpoint는 해당 stream node 계약으로 게시한다.
다른 서버는 같은 MeshName의 descriptor를 확인해 peer를 연결하며 application은 기능별 endpoint를
관리하지 않는다.

| 연결 | 연결 방식 | 이유 |
|------|-----------|------|
| Session·Play -> API | 독립 `bingo.api` ClientServer Channel | API 주소와 request/reply를 object routing에서 분리한다. |
| API -> Matchmaker Instance Spot | `bingo.matchmaking` object routing | level bucket별 reservation을 처리하는 Instance Spot을 찾거나 cold activation한다. |
| API -> Room User Spot | `bingo.play` object routing | reservation의 `RoomId`로 Room을 만들거나 Ready room을 얻는다. |
| Session -> Play session relay | location store 기반 actor locator | Session 서버가 Play 서버 actor의 위치를 직접 관리하지 않게 한다. |
| Play actor -> remote room Spot | Location Store 기반 Spot routing | actor가 다른 Play 서버의 room Spot에 join할 수 있게 한다. |
| Play Logical Multicast | 같은 RouteMesh의 routed multicast | reward event를 target channel의 각 node에 한 번 전달하고 local `BingoRoom` subscription으로 fan-out한다. |
| Play -> Session bound push | Session owner가 보관하는 binding route | Play 서버가 Location Store를 조회하지 않고 current binding token과 route로 client에 push한다. |
| Matchmaker Instance Spot -> Redis | Redis endpoint 설정 | waiting room과 actor reservation의 source of truth를 제공한다. |

이 샘플이 자동 연결을 쓰는 이유는 운영형 gateway 구조에서 서버 증설과 endpoint
변경을 application 코드 밖으로 밀어내는 흐름을 보여 주기 위해서다. 수동 endpoint
연결은 TicTacToe 샘플이 맡는다.

Logical Multicast는 별도 PUB/SUB connection을 만들지 않는다. origin MeshNode는 target ChannelName의
ready remote node마다 같은 ROUTER mesh로 routed message를 한 번 제출하고, 수신 MeshNode는 topic과
일치하는 local `BingoRoom` subscription에 message ref를 공유한다. 언어별 sample은 peer별 반복 전송이나
topic과 일치하는 local Spot subscription에 message ref를 공유하는 동작을 application code에서 구현하지 않는다.

Bingo는 Redis를 두 가지 다른 용도로 쓴다. 하나는 framework의 Location Store 구현체로,
peer discovery와 actor·Spot authority를 담당한다. Session binding route는 Session owner가 보관한다.
Logical Multicast도 같은 RouteMesh peer set을 사용한다. 다른 하나는
§3.1의 reservation store로, matching 중인 waiting room의 짧은 상태만 저장한다. 두 용도는
같은 Redis 인스턴스를 공유할 수 있지만 책임은 다르다 — room Spot owner 조회 같은
위치 정보는 Location Store가, waiting room reservation은 Matchmaking Redis가 맡는다.

### 3.1 Matchmaker Instance Spot과 Redis reservation

Matchmaking 서버는 `bingo.matchmaker` stable type의 Instance Spot factory만 제공한다.
샘플 runner는 Matchmaking process를 1개 실행하지만 이것은 샘플 규모일 뿐 singleton
계약이 아니다. Server를 늘려도 같은 Redis reservation을 사용하므로 결과가 같아야 한다.

API는 player level을 bucket으로 변환하고 `match:{levelBucket}` SpotId로 request한다.
Call에는 `InstanceSpot("bingo.matchmaker")`와 `InMesh("bingo.matchmaking")`을 명시한다.
Ready Instance Spot이 있으면 그 위치로 보내고, 없으면 framework가 eligible Matchmaking
node에서 cold activation한 뒤 같은 request를 첫 job으로 처리한다.

Redis key는 mode 기준으로 잡는다.

```text
bingo:match:{LevelBucket}:{Mode} {
  RoomId: string
  ReservedActorIds: string[]
  RequiredPlayers: int
  CreatedAtUnixMs: int64
}
```

첫 player가 matching을 요청하면 Instance Spot은 Redis에서 waiting room을 찾는다.
없으면 새 `RoomId`와 첫 actor reservation을 원자적으로 저장한다. 두 번째 player는 같은
record에 예약되고 같은 `RoomId`를 받는다. Redis가 source of truth이므로 Instance Spot의
process memory를 복구 근거로 사용하지 않는다.

동시 matching 때문에 같은 mode의 waiting room이 둘 생기면 scale-out 검증이 깨진다.
언어별 구현은 Redis transaction, Lua script, 또는 같은 수준의 atomic operation으로
waiting room 선택과 actor reservation을 하나의 결정으로 처리해야 한다. 샘플 전용 in-memory
fallback으로 성공시키면 안 된다.

Instance Spot은 `RecreateOnRelocation` policy를 사용한다. State는 Redis에서 다시 읽으므로 application state
payload를 만들지 않는다. 처리할 waiting room과 진행 중 request가 없으면 internal idle
timer가 `IZLinkInstanceSpotContext.CloseAsync()`를 호출한다. 외부 caller나 SpotManager가
Instance Spot을 닫지 않는다.

Reservation을 받은 API는 `bingo.play` mesh에서
`GetOrCreate(roomId, "bingo.room")`에 room settings를 전달하고 terminal이 Ready가 될 때까지
기다린다. 같은 reservation에 참가한 모든 caller는 byte-for-byte 같은 settings를 전달해야
한다. 첫 caller가 factory를 실행하는 동안 다른 caller는 같은 Creating authority를 기다린다.
다른 caller가 별도 room factory를 실행하거나 Play node를 선택하면 안 된다.

### 3.2 Reservation Redis 실행 책임

샘플 애플리케이션은 Docker를 직접 호출하지 않는다. Docker container 준비는 runner의
책임이다.

- `run_sample`은 실행마다 pinned Redis image로 전용 container를 시작하고 ready 상태를 확인한 뒤
  Matchmaking과 Play 프로세스에 전달한다. 이미 떠 있는 Redis나 host Redis endpoint를 재사용하지 않는다.
- runner가 만든 Redis container는 실행마다 고유한 이름을 사용하고, Docker가 배정한
  loopback port를 Matchmaking과 framework provider 구성에 전달한다. 다른 테스트가 쓰는 Redis container나 호스트
  Redis와 섞이지 않게 하기 위해서다.
- runner는 Matchmaking과 framework provider에 실행마다 고유한 Redis key prefix를 전달한다. Prefix는 같은 전용
  container 안에서 sample 내부 key를 구분하기 위한 값이지, 여러 실행이 Redis를 공유하기 위한
  장치가 아니다.
- runner는 정상 종료와 실패 종료 모두에서 Redis container를 정리한다.
- Docker를 사용할 수 없으면 runner는 명확한 오류를 출력하고 중단한다.
- C++, .NET, Java, Kotlin, Node 샘플은 모두 같은 Redis endpoint 계약을 사용한다.

### 3.3 서버 routing id 자동 할당

API, Matchmaking, Play와 Session runtime은 `SetRoutingIdPrefix(...)`로 역할을
구분한다. Framework는 각 process가 시작될 때
`<prefix>-<lowercase-canonical-uuid-v4>` 형식의 RID를 만든다. Slot count와
allocation group은 public contract가 아니다.

| 역할 | 설정하는 prefix | 역할 |
|------|-----------------|------|
| API의 Play Mesh | `api` | Play Object Client |
| API의 Matchmaking Mesh | `api-matchmaking` | Matchmaking Object Client |
| Matchmaking | `matchmaking` | `bingo.matchmaker` Instance Spot factory |
| Play | `play` | Room·Actor Object Server |
| Session | `session` | Play Object Client, 독립 API Channel Client |

같은 API process에는 서로 다른 MeshName의 Object Client 두 개가 존재한다. 하나는
`bingo.matchmaking`, 다른 하나는 `bingo.play`다. API request/reply Server는
독립 ClientServer Channel로 등록한다.

```csharp
// API: object routing 두 개와 API request/reply를 분리한다.
options.AddRouteMesh("bingo.matchmaking").Objects().Client();
options.AddRouteMesh("bingo.play").Objects().Client();
options.AddClientServerChannel("bingo.api").Server();

// Matchmaking: Instance Spot factory만 제공한다.
options.AddRouteMesh("bingo.matchmaking")
    .Objects()
    .Server()
    .AddInstanceSpotFactory<BingoMatchmaker>(
        "bingo.matchmaker",
        factory => factory.RecreateOnRelocation());

// Play: Room·Actor provider다. Matchmaker factory를 등록하지 않는다.
options.AddRouteMesh("bingo.play")
    .Objects()
    .Server()
    .AddEntrySpot<BingoEntrySpot>()
    .AddSpotFactory<BingoRoom>(
        "bingo.room",
        factory => factory
            .ExecutionMode(ZLinkUserSpotExecutionMode.SpotWide)
            .RelocationReadiness(
                ZLinkSpotRelocationReadinessMode.ApplicationSignaled)
            .PreserveStateWith<BingoRoomRelocationAdapter>())
    .AddActorFactory<PlayerActor, PlayerActorFactory>(
        "bingo.player",
        factory => factory
            .PreserveStateWith<PlayerActorRelocationAdapter>());
```

모든 역할에서 fixed `SetRoutingId(...)`를 사용하지 않는다. Sample topology는 fixed
RID field를 두지 않고 endpoint만 유지한다. 자동 발급한 RID는 connection과
관측에만 사용하며 Actor·Spot의 위치나 업무 identity로 해석하지 않는다.

Room은 game 진행 중 임의의 turn에서 relocation하지 않는다. 승부가 끝나 결과와
Logical Multicast event를 제출한 turn, 또는 observer의 membership·event 처리가 끝난
turn에서 `Context.RelocationReady().Defer()`를 호출한다. 정상 실행에서는 readiness 등록마다 logical
completion 하나를 만들지만 recovery에서는 `OnRelocationReadyCompletedAsync(...)`를 다시 호출할 수 있다.
Callback은 retry-safe해야 하며 Application이 다음 round를 시작해야 하는 경우에는 completion이 끝난 뒤
시작해야 한다.
Room state adapter는 game·observer application state만 저장한다. Actor participant,
대기 중인 message, accepted journal과 logical timer는 Framework가 보존하므로 adapter에
중복 저장하지 않는다.

Location Store와 reservation Redis가 같은 Redis instance를 사용하더라도 책임은 합치지 않는다.
공식 Location Store가 owner lease와 generation을 관리하고, Matchmaking adapter는
`RoomId`와 actor reservation만 저장한다. Reservation adapter는 node를 선택하거나 lease를 갱신하지 않는다.

API, Matchmaking, Play와 Session은 renew interval 5초, owner lease TTL 15초, fencing margin 5초와 renew
timeout 3초의 Framework 기본값을 함께 사용한다. 역할별 override는 두지 않는다.

#### Actor와 room의 배치

Session 인증 handler는 전역 `ActorId`와 stable actor type으로 `GetOrCreate`를 요청한다. Actor 생성
request에는 display name만 담으며 NodeRid를 전달하지 않는다. API의 matching handler도 Play
node를 선택하지 않는다. Matchmaker reservation 뒤 API가 받은 `RoomId`로 Spot
`GetOrCreate`를 요청하면 framework가 배치와 current owner route를 처리한다.

runner는 여러 Play runtime이 Ready인지 확인하지만 각 Actor와 room이 어느 runtime에 배치되는지는
성공 조건으로 고정하지 않는다. cross-node object routing은 별도 E2E에서 target 배치를 통제해
검증한다.

#### 시작과 교체

runner는 Matchmaking과 두 Play runtime이 Ready인지 확인한 뒤 API와 Session을 시작한다.
Process 시작 순서와 RID를 결합하지 않는다.

Replacement process는 기존 process의 RID를 재사용하지 않는다. 새 RID와 lifecycle
generation으로 descriptor를 게시하면 peer가 새 endpoint에 연결한다. 이 동작만으로
종료된 process의 메모리 actor나 진행 중인 room state를 복구하지는 않는다.
Matchmaker는 Redis reservation을 다시 읽고, Room·Actor relocation은 Framework의
relocation 계약을 따른다.

## 4. 프로세스와 책임

| 프로세스 | 구성 요소 | 책임 |
|----------|-----------|------|
| `Bingo.Api` | 독립 `bingo.api` ClientServer Server | access token 인증과 matching API 요청을 처리한다. |
| `Bingo.Api` | player record store(프로세스 메모리) | player 전적 조회(`GetPlayerRecordReq`)와 경기 결과 기록(`ReportBingoResultReq`)을 소유한다. room Spot은 이 값을 계산하지 않고 join/leave에서 `yield`로 물어본다(§7.1). |
| `Bingo.Api` | `bingo.matchmaking` Object Client | level bucket Matchmaker Instance Spot에 reservation을 요청한다. |
| `Bingo.Api` | `bingo.play` Object Client | reservation의 `RoomId`로 Room User Spot `GetOrCreate`를 호출하고 Ready를 기다린다. |
| `Bingo.Matchmaking` | `BingoMatchmaker` Instance Spot | Redis에서 waiting room과 actor reservation을 결정한다. `RecreateOnRelocation` policy와 internal idle close를 사용한다. |
| `Bingo.Session` | stream server | client 연결, 인증 packet, actor binding, actor relay를 처리한다. |
| `Bingo.Session` | `bingo.play` Object Client | actor 생성·lookup, session relay와 bound session push 수신을 담당한다. |
| `Bingo.Session` | 독립 `bingo.api` ClientServer Client | 인증과 matching API request를 보낸다. |
| `Bingo.Play` | actor runtime | player actor를 만들고 Entry Spot에 join시킨다. |
| `Bingo.Play` | `BingoEntrySpot` | actor가 특정 room에 들어가기 전의 admission 지점을 맡는다. |
| `Bingo.Play` | `BingoRoom` room Spot | game room에서는 room 참가자, 제출된 카드, draw deck, 승리 판정, player Notify 생성을 소유한다. observer용 local room에서는 reward topic 수신과 observer push 전달만 맡는다. |
| `Bingo.Play` | 독립 `bingo.api` ClientServer Client | room Spot의 join/leave callback이 전적을 조회·기록한다. 이 왕복은 `yield`로 기다린다(§7.1). |
| `Location Store` | framework Location Store provider | 두 RouteMesh의 discovery와 actor·Spot authority를 저장한다. Session binding route는 저장하지 않는다. |

## 5. 디렉토리와 파일 구성

Bingo 샘플은 Session, API, Matchmaking, Play가 분리된 gateway형 게임 샘플이다. 규모가
TicTacToe보다 크기 때문에 DDD와 헥사고날 아키텍처의 경계를 더 분명히 유지해야 한다.
이 구조는 C++, Java, Kotlin, TypeScript, .NET 샘플을 작성할 때 함께 따라야 하는
공통 기준이다. 언어별 빌드 도구, 파일 확장자, package/module 표현은 달라질 수 있지만,
`Client`, `Shared`, `Server/Api`, `Server/Matchmaking`, `Server/Session`, `Server/Play/Domain`,
`Server/Play/Application`, `Server/Play/Infrastructure` 경계와 각 책임은 유지해야 한다.

```text
Bingo/
  README
  build files
  run sample script
  Client/
    Program
    BingoClientScenario
    client build/module files
  Shared/
    shared build/module files
    Configuration/
      SampleNames
      SampleTopology
    Contracts/
      bingo_messages.proto
  Server/
    Api/
      Program
      ApiServerHostFactory
      api build/module files
      Handlers/
        AuthenticatePlayerHandler
        MatchBingoHandler
    Matchmaking/
      Program
      MatchmakingServerHostFactory
      Application/
        BingoMatchReservationStore
      Infrastructure/
        Redis/
          RedisBingoMatchReservationStore
        ZLink/
          BingoMatchmaker
    Session/
      Program
      SessionServerHostFactory
      session build/module files
      Sessions/
        BingoSession
        Handlers/
          AuthenticateSessionHandler
    Play/
      Program
      PlayServerHostFactory
      play build/module files
      Domain/
        Bingo/
          BingoCard
          BingoGame
          BingoRoomGame
          BingoRoomModels
      Infrastructure/
        ZLink/
          Actors/
            PlayerActor
            PlayerActorFactory
            PlayerActorRelocationAdapter
          Spots/
            EntrySpot/
              BingoEntrySpot
              Handlers/
                ObserveBingoEventsHandler
                MatchBingoActorHandler
            BingoRoomSpot/
              BingoRoom
              BingoRoomRelocationAdapter
              Handlers/
                BingoRoomDrawTimerHandler
                StopObservingBingoEventsHandler
                BingoRewardAcquiredEventHandler
                SubmitBingoCardHandler
```

위 구조는 파일명 고정 규칙이 아니라 역할과 경계의 기준이다. 예를 들어 Java/Kotlin은
package와 class 이름으로, TypeScript는 module과 file 이름으로, C++은 header/source
쌍과 namespace로, .NET은 project와 class 이름으로 같은 구조를 표현할 수 있다. 중요한
점은 같은 책임의 코드가 같은 위치에 있고, 다른 레이어로 섞이지 않는 것이다.

각 영역의 역할은 아래와 같다.

| 위치 | 공통 아키텍처 역할 | 책임 |
|------|----------------------|------|
| `Client/Program` | 외부 driving adapter | Session stream 연결을 만들고 client self-check 시나리오를 실행한다. |
| `Client/BingoClientScenario` | sample scenario | 인증, matching, observer 등록, card 제출, draw push, rare reward publish 수신, final state 검증을 순서대로 수행한다. |
| `Shared/Configuration/*` | shared settings | sample service 이름, packet 이름, endpoint topology를 공유한다. |
| `Shared/Contracts/*` | shared contract | Protobuf schema처럼 언어 간 동일해야 하는 payload 계약을 둔다. |
| `Server/Api/*` | API adapter | 인증 요청을 처리한다. Matching 요청은 Matchmaker reservation과 Room `GetOrCreate`를 순서대로 수행한다. |
| `Server/Matchmaking/*` | matching application과 adapter | level bucket별 Instance Spot request를 Redis reservation으로 처리한다. |
| `Server/Session/*` | stream gateway adapter | client stream, 인증, actor binding, bound session relay를 처리한다. |
| `Server/Play/Domain/Bingo/*` | domain model | card, draw deck, room status, winner 판정 같은 게임 규칙을 framework 타입 없이 표현한다. |
| `Server/Play/Infrastructure/ZLink/*` | ZLink adapter | actor, Spot callback, bound session push, Logical Multicast publish/subscribe를 application/domain 호출로 변환한다. |
| `Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/BingoRoomRelocationAdapter` | room relocation adapter | room application state를 byte 배열로 저장하고 새 instance에 복원한다. Framework가 보존하는 queue·accepted journal·logical timer는 중복 저장하지 않는다. |
| `Server/Play/Infrastructure/ZLink/Actors/PlayerActorRelocationAdapter` | actor relocation adapter | player application state와 Actor Join completion의 중복 처리 방지 상태를 byte 배열로 저장하고 복원한다. |

의존 방향은 `Infrastructure -> Application -> Domain`이다. Domain은 ZLink framework, location store,
stream session, session relay, logger를 알지 않는다. Matchmaking Application은 reservation
use case만 맡고, server endpoint 발견, session binding, push 전송 같은 외부 입출력은
adapter에 둔다. 이 규칙 덕분에 다른 언어로 옮겨도 gateway 구조와 게임 규칙의 위치가
같게 유지된다.

알림 전송을 위해 별도 notification Spot이나 별도 notification publisher 계층을 만들지 않는다.
player에게 가는 게임 진행 알림은 `PlayerActor`가 자기 bound session으로 보내고, 서버 간 보상
fan-out은 `BingoRoom`이 Logical Multicast topic으로 publish/subscribe한다. Logical Multicast event
수신도 `Spots/BingoRoomSpot/BingoRoom` 안에서 처리한다.

## 6. 언어별 구현 기준

Bingo 샘플은 현재 .NET 샘플에서 검증된 실행 형태와 같은 수준으로 C++, Java, Kotlin,
TypeScript에서도 작성되어야 한다. 언어 문법과 빌드 도구는 달라도 사용자가 샘플을 열었을
때 같은 서버 역할, 같은 gateway 흐름, 같은 검증 지점을 찾을 수 있어야 한다.

언어별 구현은 아래 기준을 만족해야 한다.

- 샘플 루트에는 client, shared contracts, api, matchmaking, session, play 역할이
  한 번만 보이게 구성한다. IDE나 build tool에서 같은 역할의 프로젝트나 module이 중복으로
  보이면 안 된다.
- 각 실행 역할은 명시적인 entry point를 가진다. 실행 시작 코드는 짧게 두고, host 구성과
  client self-check 시나리오는 역할별 구성 요소로 분리한다.
- `Shared/Contracts`에는 Protobuf schema를 둔다. 각 언어는 이 schema에서 생성한 message를
  사용해야 하며, Protobuf와 별도로 손으로 만든 parallel message 정의를 유지하면 안 된다.
  client와 server는 생성된 message 객체의 public interface만 사용해야 하며, 샘플 전용
  helper로 payload 계약을 감추면 안 된다.
- Bingo의 payload codec은 Protobuf다. stream, channel, actor, room Spot payload는 같은
  schema와 같은 packet 이름을 사용하고, JSON이나 MessagePack으로 바꾸지 않는다.
- client self-check는 별도 테스트 프로젝트가 아니라 샘플 client 실행 흐름 안에 둔다.
  샘플 실행은 API 2개, Matchmaking 1개, Session 2개, Play 2개 server를 시작하고 client가 Session
  stream에 접속해 `bingo=completed`와 server evidence에 해당하는 성공 결과를 만들 수 있어야
  한다.
- runner는 서버 프로세스를 시작한 뒤 필요한 TCP endpoint가 열린 것을 확인하고 곧바로
  client self-check를 시작한다. 고정 sleep을 준비 상태 확인으로 사용하지 않는다. 별도 readiness 프로젝트나 게임 시나리오
  안의 Logical Multicast 확인 message를 사용하지 않는다.
- 샘플 실행에는 Matchmaking reservation Redis가 필요하다. 애플리케이션 코드는 Redis endpoint만 설정으로
  받고, Docker container 생성이나 종료를 직접 맡지 않는다. `run_sample`은 실행마다 Docker로
  전용 Redis container를 준비하고, 샘플 종료 시 자신이 만든 container만 정리한다.
- Redis client dependency는 Matchmaking reservation adapter 안에만 둔다. Handler, actor, Room Spot, Domain
  코드가 Redis client 타입을 직접 참조하면 안 된다.
- Reservation Redis는 waiting room state 공유에만 사용한다. Spot·Actor owner lookup과 Logical Multicast
  peer discovery는 Location Store가 맡는다. Session binding route는 Session owner가 보관한다.
- actor가 room에 join하는 흐름은 public actor/Spot API와 global `RoomId`를 사용한다.
  Framework가 Location Store에서 current owner를 확인한다. Internal runtime 객체나
  sample-local route helper로 remote join을 우회하면 안 된다.
- Logical Multicast 흐름은 각 언어 framework의 public Logical Multicast API를 사용해야 한다.
  `BingoRoom`은 public publish API로 reward event를 발행하고, 같은 `BingoRoom` 타입이
  public subscribe 등록 API로 reward topic을 구독한다. reward event를 받기 위해
  `BingoNotificationSpot` 같은 별도 Spot 타입을 만들면 안 된다.
  topic 이름은 `bingo.room.reward`처럼 모든 언어에서 같은 문자열 의미를 유지해야 한다.
- push 대기는 connector 객체의 public wait interface를 직접 사용한다. 필요한 push를 고를
  때는 connector wait API의 filter 기능을 사용하고, 받은 message 객체의 public interface로
  payload를 읽어 `Ensure(condition)`처럼 조건식이 직접 보이는 방식으로 검증한다.
- client는 stream connector를 만든 직후, `connect` 전에 inbound observer를 등록해
  `stream-inbound` marker가 포함된 수신 로그를 남긴다. 이 로그는 request 응답과
  server push 수신을 관찰하기 위한 것이며, payload 검증이나 push 대기를 대신하지 않는다.
  observer callback에서는 connector send/request/wait를 다시 호출하지 않는다.
- Java와 Kotlin client scenario의 `submit`과 `await` 의미는
  [framework 공통 비동기 정책](../../spec/05-async-execution-policy.ko.md)을 따른다.
  `submit`은 작업을 시작하고 future를 반환하는 이름으로, `await`는 완료를 기다려
  결과를 받는 이름으로 사용한다.
- sample-local inbox, sleep, 임시 polling 함수로 준비 상태나 push 도착을 숨기면 안 된다.
  대기와 검증은 connector와 message 객체 인터페이스를 사용하는 샘플 시나리오 코드에서
  드러나야 한다.
- Session 서버는 gateway 역할만 한다. 인증, actor binding, packet relay, bound session
  push 수신을 맡고, card 검증, draw, winner 판정 같은 게임 규칙을 해석하지 않는다.
- Domain에는 card, draw deck, room status, winner 판정만 둔다. location store, stream session,
  session relay, handler, logger, codec, endpoint 설정은 Domain으로 들어오면 안 된다.
- Matchmaking Application은 Redis reservation use case를 조율한다. Framework callback이나
  transport 세부 구현은 adapter에 둔다.
- Infrastructure는 Location Store automatic discovery, channel, stream session, actor, Spot, timer,
  bound session push, reward Logical Multicast publish, codec 연결을 맡는다. handler나 Spot
  adapter가 card 판정, draw order, winner 판정을 직접 구현하면 안 된다.

이 기준은 샘플의 모양을 통일하려는 목적만이 아니다. 같은 시나리오를 여러 언어에서
나란히 읽었을 때 framework 기능 차이와 언어 차이만 보이고, 샘플 구조 차이 때문에
흐름을 다시 해석하지 않아도 되게 하기 위한 기준이다.

## 7. Play 서버 내부 레이어

Bingo Play 서버는 domain logic과 framework adapter를 분리해야 한다. 다른 언어
framework로 구현할 때도 아래 책임 분리를 유지한다.

```text
Server/Play/
  Domain/
    Bingo/
      BingoCard
      BingoGame
      BingoRoomGame
      BingoRoomModels
  Infrastructure/
    ZLink/
      Actors/
        PlayerActor
        PlayerActorFactory
      Spots/
        EntrySpot/
          BingoEntrySpot
          Handlers/
            ObserveBingoEventsHandler
            MatchBingoActorHandler
        BingoRoomSpot/
          BingoRoom
          Handlers/
            BingoRoomDrawTimerHandler
            StopObservingBingoEventsHandler
            BingoRewardAcquiredEventHandler
            SubmitBingoCardHandler
```

역할은 아래처럼 나눈다.

| 위치 | 책임 |
|------|------|
| `Domain/Bingo/BingoCard` | 3 x 3 card 검증, free cell, mark, complete line 계산을 소유한다. |
| `Domain/Bingo/BingoGame` | 제출된 card, draw deck, drawn numbers, winners, draw 종료 조건을 소유한다. |
| `Domain/Bingo/BingoRoomGame` | player join, room status, card 제출 가능 여부, draw timer 시작/종료 신호, room event 생성을 소유한다. |
| `Infrastructure/ZLink/Spots/BingoRoomSpot/BingoRoom` | ZLink Spot lifecycle, actor join callback, draw 진행, domain 호출, player actor push, reward Logical Multicast publish/subscribe를 맡는다. |
| `Infrastructure/ZLink/Spots/BingoRoomSpot/BingoRoom` | observer용 local room 인스턴스에서 reward topic subscribe callback을 받고 observer actor에게 push를 전달한다. |
| `Infrastructure/ZLink/Actors/PlayerActor` | player별 bound session push를 감싼다. room Spot은 immutable `ActorRef`로 typed actor message를 보내며 mutable actor instance나 stream frame을 직접 다루지 않는다. |
| `Infrastructure/ZLink/Spots/EntrySpot/Handlers/*` | Session에서 생성한 Actor의 lifecycle과 Entry Spot 안의 message dispatch를 처리한다. Matching과 room 생성은 API가 Matchmaker Instance Spot과 Room Spot manager를 순서대로 호출해 처리한다. |
| `Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/*` | room Spot actor request와 timer/event callback을 받아 domain operation을 호출한다. |

Domain 객체는 ZLink framework 타입을 직접 참조하지 않는다. `BingoRoom` Spot은 framework
callback을 받아 domain method를 호출하고, domain이 반환한 change와 event를 adapter가
message로 바꾼다. card validation, draw order, winner 판정이 handler나 Spot handler에
흩어지면 안 된다.

### 7.1 실행 turn과 `yield` — actor join/leave의 외부 조회

**room Spot은 하나의 실행 줄이다.** draw timer, card 제출, join/leave callback, reward
publish가 모두 그 줄에서 순서대로 실행된다. 그래서 handler 안의 대기 하나가 **room 전체를
멈춘다.**

`BingoRoom`의 player join/leave callback에는 **room의 공유 상태와 아무 관련 없는 대기**가 있다 —
player의 **전적(record)은 Api 서버가 소유한다.** room은 전적을 계산하거나 보관하지 않는다.

| 지점 | 호출 | terminator | 왜 |
|------|------|-----------|-----|
| `BingoRoom.OnJoinedActor` (player) | Api 서버에 `GetPlayerRecordReq` | **`yield`** | 전적 조회는 board·draw와 무관하다. room 실행 줄을 붙잡을 이유가 없다 |
| `BingoRoom.OnLeaveActor` (player) | Api 서버에 `ReportBingoResultReq` | **`yield`** | 결과 기록도 마찬가지다. 이 왕복이 room teardown을 붙잡으면 안 된다 |
| API의 matching (`MatchBingoApiReq`) | Matchmaker Instance Spot request 뒤 Room `GetOrCreate` | **`async`** | Redis reservation 결과의 `RoomId`와 settings로 Room이 `Ready`가 될 때까지 기다린 뒤 Actor에 결과를 반환한다 |

**card 제출·draw 진행·winner 판정은 terminator 대상이 아니다.** 그건 room이 소유한 domain
객체의 동기 호출이라 기다릴 것이 없다. terminator는 **완료를 기다리는 framework 호출**(request,
actor join, worker, HTTP client)에만 적용된다([04 §1.1](../../spec/05-async-execution-policy.ko.md)).

**`observer`는 전적을 조회하지 않는다.** observer actor는 `BingoRoomJoinReq.ObserveOnly = true`로
관전 전용 local room에 join하며, player membership·card·draw·winner 판정에 참여하지 않는다(§14).
`BingoRoom`의 join/leave callback은 **같은 callback**이므로, 전적 조회·기록은 `ObserveOnly`가
`false`일 때만 수행한다. observer의 `BingoPlayerState`는 만들지 않으므로 `Wins`/`Losses`도 없다.

### 왜 `yield`인가

`.Async()`로 기다리면 그 왕복 동안 **room 실행 줄이 통째로 멈춘다.** draw timer의 다음 draw,
같은 room의 card 제출, 다른 player의 leave가 전부 그 뒤로 밀린다. `.Yield()`는 turn을 반납하므로
그것들이 그대로 진행되고, 조회가 끝난 continuation이 room 실행 줄의 큐에 재삽입되어 순서대로
재개된다([04 §1.1](../../spec/05-async-execution-policy.ko.md)).

**이 샘플의 스크립트 흐름만으로는 그 차이가 눈에 띄지 않는다.** client 시나리오는 join → 시작 →
카드 제출 → draw를 순서대로 밟기 때문에, join 왕복과 겹칠 작업이 마침 없다(§10). 실부하에서는
겹친다 — 그리고 **turn 유지/반납의 결정적 검증은 이 샘플이 아니라
[config-8 execution turn](../../e2e/config-8-execution-turn.ko.md)의 TD-A3·TD-B1이 소유한다.**
샘플의 몫은 **어느 대기에 어느 terminator를 고르는지**를 보여 주는 것이다.

### `yield` 앞뒤로 같은 mutable state를 이어서 판단하지 않는다

- `OnJoinedActor`는 **먼저 `yield`로 전적을 가져오고, 재개한 뒤 그 turn 안에서** domain join,
  시작 조건 판정, `PlayerJoinedNotify` 생성을 한 번에 끝낸다. yield 전에 room 상태를 바꿔 두고
  yield 후에 그 가정을 이어서 쓰지 않는다.
- 재개 시점에 room이 이미 `Finished`이거나 actor가 사라졌을 수 있다. 그 경우를 **재개 후에 다시
  확인**한다.

Api 서버는 이 샘플 안의 channel 서버이므로 framework request의 `.Yield()`를 그대로 쓴다. 같은
대기가 다른 표면이었어도 **turn을 반납한다는 계약은 같다.**

| 대기의 정체 | 쓰는 표면 |
|---|---|
| 이 샘플처럼 ZLink channel request | request의 `.Yield()` |
| 외부 HTTP·레거시 API | HTTP response 호출을 `RunIoWorker(...)`로 감싼 뒤 Worker call의 `.Yield()`로 기다린다([12 §3.1](../../spec/http-client/12-http-client.ko.md)) |
| DB 드라이버·외부 SDK처럼 자체 terminator가 없는 비동기 대기 | `RunIoWorker(...).Yield()`([04 §1.2](../../spec/05-async-execution-policy.ko.md)) |

turn 유지/반납의 엄밀한 검증은 [config-8 execution turn](../../e2e/config-8-execution-turn.ko.md)이
소유한다. 이 샘플은 그 계약을 **실제 흐름에서 어떻게 쓰는지**를 보여 준다.

### 7.2 Actor Join 완료의 중복 처리 방지

Actor가 다른 node의 room으로 이동하는 Join은 handler가 끝난 뒤 실행한다. Framework는
결과를 `PlayerActor.OnJoinCompletedAsync(...)`에 전달한다. Cross-node commit 뒤 process가
종료되면 같은 `Accepted` 결과가 다시 전달될 수 있다.

`PlayerActor`는 callback에 포함된
[Actor Join OperationId](../../spec/01-glossary.ko.md#actor-join-operation-id)를 마지막으로
적용한 결과와 함께 보관한다. 이미 처리한 ID이면 room 상태 변경, client 응답과 후속 Notify를
다시 실행하지 않는다. 새로운 ID이면 결과를 한 번 적용한 뒤 ID와 처리 결과를 Actor state에
함께 기록한다.

이 ID는 Framework가 발급한다. `MatchBingoReq`, `BingoRoomJoinReq` 같은 application message에
복사하거나 caller가 만들지 않는다. RoomId, reservation ID와 relocation ID로 대신하지도 않는다.
`PlayerActorRelocationAdapter`는 이 중복 처리 방지 상태를 다른 node에서도 이어서 사용할 수 있도록
Actor payload에 포함한다.

## 8. Handler 등록 방식

**Bingo는 자동 등록 샘플이다.** handler는 framework가 정의한 typed handler 계약을 구현하고,
서버는 assembly·module 스캔과 선언형 metadata로 그 handler를 **자동 등록**한다. 구성 코드에
handler 목록을 다시 나열하지 않는다.

이 방식의 목적은 아래와 같다.

- handler가 어떤 request/response 계약을 처리하는지 **타입 선언과 선언형 metadata**로 드러난다.
- Session, channel, Entry Spot, room Spot handler가 같은 등록 원칙을 공유한다.
- handler를 추가하면 등록 호출을 잊어 조용히 빠지는 실수가 생기지 않는다.

언어별 표현은 `.NET` attribute, Java/Kotlin annotation, Node decorator다. **C++은 예외**로,
runtime 스캔이 없으므로 compile-time 타입과 명시 builder 호출로 같은 handler 집합을 등록한다
([05 §8](../../spec/06-framework-api.ko.md#8-handler-등록과-dispatch)). 등록 방식이 달라도 handler 역할과 메시지 이름은
같다.

수동 endpoint topology를 사용하는 **TicTacToe**도 handler는 자동 등록한다([샘플 규약](../README.ko.md)).

## 9. 게임 규칙

Bingo는 샘플 흐름을 짧게 유지하기 위해 2인 자동 시작 규칙을 사용한다.

| 항목 | 규칙 |
|------|------|
| 플레이어 수 | 정확히 2명 |
| 보드 | 3 x 3 |
| 번호 범위 | 1부터 15까지 |
| 가운데 칸 | free cell로 시작부터 mark 처리 |
| 카드 입력 | 각 client가 3 x 3 bingo card를 제출한다. |
| 시작 조건 | 두 번째 player가 join하면 room이 자동으로 시작한다. |
| 번호 추첨 | 두 player client의 card가 모두 제출되면 room Spot timer가 일정 간격으로 번호를 하나씩 뽑는다. |
| mark 방식 | 서버 자동 mark. client는 card만 제출하고 mark나 bingo claim은 보내지 않는다. |
| 승리 조건 | complete line이 1개 이상 생기면 승리 |
| 종료 조건 | 첫 승리 draw sequence가 나오면 종료 |

방장, ready 버튼, 수동 mark, 여러 라운드, 랭킹은 공통 샘플 범위에서 제외한다.
이 기능들은 게임 샘플을 크게 만들지만 framework 흐름을 이해하는 데 꼭 필요하지 않다.

## 10. Client 검증 흐름

Bingo client는 아래 순서로 scenario를 실행하고 각 단계의 값을 확인한다.

1. `player-1`, `player-2`, `observer`로 stream 인증을 요청하고, 각
   `AuthenticateRes.ActorId`가 요청한 actor id와 같은지 확인한다. `player-1`은
   `SessionA`에 연결하고, `player-2`와 `observer`는 `SessionB`에 연결한다.
2. `player-1`이 먼저 `MatchBingoReq`를 보내고 `WaitingForPlayers` 상태와 room id를
   확인한다. 이 시점에 `player-1`이 자기 join notify를 받지 않았는지도 확인한다.
3. `observer`가 `ObserveBingoEventsReq(RoomId)`를 보내고
   `ObserveBingoEventsRes.Subscribed = true`를 확인한다. 이 요청은 관찰 전용 `BingoRoom`
   인스턴스를 만들거나 찾은 뒤, observer actor를
   `BingoRoomJoinReq.ObserveOnly = true` payload로 그 `BingoRoom`에 join시킨다. 이 응답을
   받은 뒤에 `player-2` matching과 card 제출을 진행해야 reward event를 놓치지 않는다.
4. `player-2`가 `MatchBingoReq`를 보내면 같은 room id와 `Running` 상태를 확인한다.
5. `player-1`은 connector wait API로 `PlayerJoinedNotify`를 기다리고,
   payload의 `ActorId`가 `player-2`인지 확인한다. `player-2`는 자기 join notify를
   받지 않아야 한다. 같은 push의 `State.Players`에서 두 player의 `Wins`/`Losses`가 채워져
   있는지도 확인한다 — 이 값은 room이 계산하지 않고 `OnJoinedActor`가 `yield`로 Api 서버에서
   가져온 것이다(§7.1).
6. 두 player client는 connector wait API로 `BingoGameStartedNotify`를 기다리고,
   push state가 `Running`인지 확인한다.
7. 두 player client가 deterministic card를 제출한 뒤 response state에 두 player card가 모두
   9칸으로 들어갔는지 확인한다.
8. 두 player client는 draw sequence별로 `BingoNumberDrawnNotify`를 기다리고, 양쪽 push의
   `DrawSeq`, `Number`, state가 서로 같은지 확인한다.
9. 두 player client는 `BingoGameEndedNotify`를 기다리고, final state의 `Finished`, drawn
   number sequence, winners, player list, center free-cell mark를 확인한다.
10. observer client는 connector wait API로 `BingoRewardAnnouncedNotify`를 기다리고,
   `RoomId`, `ActorId`, `DrawSeq`, `ItemId`, `ItemName`, `Rarity`를 확인한다.
11. observer client는 `StopObservingBingoEventsReq(RoomId)`를 보내 observer actor가
   observer용 local `BingoRoom`에서 나와 Entry Spot으로 돌아왔는지 확인한다. 이 흐름은
   reward event 수신을 위한 세 번째 actor가 game room cleanup과 섞이지 않게 한다.
12. 세 client는 inbound observer 로그에 `stream-inbound` marker가 남았는지 확인한다.
   로그에는 sample 이름, client 역할, message kind, packet name, request sequence,
   payload byte length가 포함되어야 한다. heartbeat control frame은 observer 기능
   검증에는 포함할 수 있지만 기본 sample output에서는 낮은 log level로 두거나 걸러낸다.

이 검증은 성공 시나리오를 눈으로 읽기 위한 로그가 아니라 sample release gate다. 언어별
client가 위 값을 확인하지 않으면 공통 sample 기준을 만족하지 못한다.

### 10.1 배치 독립성 검증

다음 검증은 모든 언어의 Bingo runner가 충족해야 한다.

| ID | 검증 | 성공 기준 |
|----|------|-----------|
| `BINGO-PLACEMENT-1` | 자동 NodeRid | topology, 설정, message와 reservation state에 고정 또는 preferred NodeRid가 없다 |
| `BINGO-PLACEMENT-2` | Actor 생성 | Session은 전역 `ActorId`와 stable actor type으로 `GetOrCreate`를 호출하고 framework가 반환한 `ActorRef`를 그대로 bind한다 |
| `BINGO-PLACEMENT-3` | room 생성 | Matchmaker는 `RoomId`와 settings를 결정하고 API의 `GetOrCreate`가 Spot owner 선택을 framework에 맡긴다 |
| `BINGO-PLACEMENT-4` | 시작 순서 독립 | Play process 시작 순서를 바꿔도 같은 client 시나리오와 상태 검증이 통과한다 |
| `BINGO-PLACEMENT-5` | 전역 route | Actor와 room 요청은 NodeRid가 아니라 `ActorId`와 `RoomId`로 현재 owner를 찾는다 |
| `BINGO-PLACEMENT-6` | Matchmaker cold activation | level bucket SpotId의 첫 request가 `InstanceSpot`과 `InMesh("bingo.matchmaking")`으로 activation되고 이후 request가 Redis의 같은 reservation을 사용한다 |

Runner는 여섯 항목을 모두 확인한 뒤에만 `bingo-placement=completed`를 출력한다.
Client 성공만으로 이 marker를 출력하면 안 된다.

## 11. 메시지 계약

아래 계약은 언어 중립 schema다. 언어별 샘플은 같은 이름과 필드를 자기 언어의
record, class, struct, type alias 등으로 구현한다.

client stream 인증 메시지:

```text
AuthenticateReq {
  AccessToken: string
}

AuthenticateRes {
  ActorId: string
  DisplayName: string
}
```

API 인증과 actor 준비 메시지:

```text
AuthenticatePlayerReq {
  AccessToken: string
}

AuthenticatePlayerRes {
  Accepted: bool
  ActorId: string?
  DisplayName: string?
  Reason: string?
}

EnsurePlayerActorReq {
  ActorId: string
  DisplayName: string
}
```

matching과 room join 메시지:

```text
MatchBingoReq {
  Mode: string
}

MatchBingoRes {
  RoomId: string
  State: BingoRoomState
}

MatchBingoApiReq {
  ActorId: string
  DisplayName: string
  Mode: string
}

MatchBingoApiRes {
  RoomId: string
}

ReserveBingoRoomReq {
  Mode: string
  ActorId: string
  LevelBucket: string
}

ReserveBingoRoomRes {
  RoomId: string
  Settings: BingoRoomSettingsPayload
}

BingoRoomSettingsPayload {
  RoomName: string
  Mode: string
  RequiredPlayers: int
  MaxDrawNumber: int
  Purpose: string
  ObservedRoomId: string?
}

BingoRoomJoinReq {
  RoomId: string
  ActorId: string
  DisplayName: string
  ObserveOnly: bool
}

BingoRoomJoinRes {
  State: BingoRoomState
}

SubmitBingoCardReq {
  RoomId: string
  Card: int[]
}

SubmitBingoCardRes {
  State: BingoRoomState
}

ObserveBingoEventsReq {
  RoomId: string
}

ObserveBingoEventsRes {
  Subscribed: bool
}

StopObservingBingoEventsReq {
  RoomId: string
}

StopObservingBingoEventsRes {
  Stopped: bool
}

```

room Spot이 actor join/leave에서 Api 서버로 보내는 전적 메시지(§7.1). 이 두 왕복은 room의
공유 상태와 무관하므로 **`yield`로 기다린다**:

```text
GetPlayerRecordReq {
  ActorId: string
}

GetPlayerRecordRes {
  ActorId: string
  Wins: int
  Losses: int
}

ReportBingoResultReq {
  RoomId: string
  ActorId: string
  Won: bool
  FinalDrawSeq: int
}

ReportBingoResultRes {
  ActorId: string
  Wins: int
  Losses: int
}
```

Api 서버는 전적을 프로세스 메모리에 보관한다. 이 샘플의 목적은 영속 저장이 아니라
**room 실행 줄 밖의 대기**를 만드는 것이다. player가 처음 조회되면 `Wins = 0`, `Losses = 0`으로
시작한다.

server push 메시지:

```text
PlayerJoinedNotify {
  RoomId: string
  ActorId: string
  DisplayName: string
  Seat: int
  IsHost: bool
  State: BingoRoomState
}

BingoGameStartedNotify {
  State: BingoRoomState
}

BingoNumberDrawnNotify {
  RoomId: string
  DrawSeq: int
  Number: int
  State: BingoRoomState
}

BingoGameEndedNotify {
  State: BingoRoomState
}

BingoRewardAnnouncedNotify {
  RoomId: string
  ActorId: string
  DrawSeq: int
  ItemId: string
  ItemName: string
  Rarity: string
}
```

Logical Multicast으로 전달하는 event:

```text
BingoRewardAcquiredEvent {
  RoomId: string
  ActorId: string
  DrawSeq: int
  ItemId: string
  ItemName: string
  Rarity: string
}
```

공통 state 모델:

```text
BingoRoomState {
  RoomId: string
  Status: string
  HostActorId: string
  CanStart: bool
  DrawSeq: int
  LastDrawnNumber: int?
  DrawnNumbers: int[]
  Players: BingoPlayerState[]
  Winners: string[]
}

BingoPlayerState {
  ActorId: string
  DisplayName: string
  Seat: int
  IsHost: bool
  Card: int[]
  Marks: bool[]
  CompletedLines: int
  Wins: int
  Losses: int
}
```

`Wins`와 `Losses`는 `OnJoinedActor`가 `yield`로 가져온 `GetPlayerRecordRes` 값을 그대로 담는다
(§7.1). room이 계산하는 값이 아니므로 draw 진행 중에 바뀌지 않는다.

`HostActorId`, `CanStart`, `IsHost` 필드는 기존 언어별 구현과 호환을 위해 유지할 수
있다. 2인 자동 시작 공통 시나리오에서는 별도 시작 요청을 보내지 않으므로 client
검증 기준으로 사용하지 않는다.

## 12. 인증과 Actor Binding 흐름

```mermaid
sequenceDiagram
    participant C as Client
    participant S as Session Server
    participant API as Api Server
    participant P as Play Server

    C->>S: Stream AuthenticateReq
    S->>API: Channel Api/AuthenticatePlayerReq
    API-->>S: AuthenticatePlayerRes(actorId, displayName)
    S->>P: Actor GetOrCreate(actorId, stableType, createRequest)
    P-->>S: framework ActorRef
    S->>S: Bind current stream session to actorRef
    S-->>C: Stream AuthenticateRes
```

Session 서버는 인증 성공 후 actor reference를 얻고 현재 stream session을 그 actor에
bind한다. 이후 client gameplay packet은 Session 서버가 직접 처리하지 않고 bound actor로
relay한다.
Session 서버는 전역 `ActorId`와 stable actor type으로 `GetOrCreate`를 호출한다. create request에는
display name만 담는다. framework는 owner lease·capability·`Draining` 상태와 capacity를 기준으로
Actor owner를 선택하고 정확한 `ActorRef`를 반환한다. Session은 그 값을 다시 구성하지 않고 현재
stream session에 bind한다.

## 13. Matching과 카드 제출 흐름

```mermaid
sequenceDiagram
    participant C1 as Client 1
    participant C2 as Client 2
    participant O as Observer Client
    participant S1 as Session A
    participant S2 as Session B
    participant A1 as Player Actor 1
    participant A2 as Player Actor 2
    participant A3 as Observer Actor
    participant API as Api Server
    participant MM as Matchmaker Instance Spot
    participant REDIS as Redis Reservation Store
    participant E1 as Entry Spot A
    participant E2 as Entry Spot B
    participant R as Bingo Room Spot
    participant BR2 as BingoRoom on Play B

    C1->>S1: MatchBingoReq
    S1->>A1: Relay to bound actor
    A1->>API: MatchBingoApiReq(actorId, mode)
    API->>MM: RequestToSpot(level bucket) + InstanceSpot + InMesh
    MM->>REDIS: Reserve or create waiting room
    REDIS-->>MM: roomId + settings
    MM-->>API: ReserveBingoRoomRes
    API->>R: Spot GetOrCreate(roomId, roomType, same settings)
    R-->>API: Ready room ref
    A1->>R: JoinSpot(roomId)
    R->>API: GetPlayerRecordReq (OnJoinedActor, yield — room turn 반납)
    API-->>R: GetPlayerRecordRes(wins, losses)
    R-->>A1: BingoRoomJoinRes(waiting)
    A1-->>S1: MatchBingoRes
    S1-->>C1: MatchBingoRes
    O->>S2: ObserveBingoEventsReq(roomId)
    S2->>A3: Relay to bound observer actor
    A3->>BR2: Create or find local observer BingoRoom
    A3->>BR2: JoinSpot with BingoRoomJoinReq(observeOnly)
    Note over BR2: ObserveOnly=true — 전적을 조회하지 않는다
    BR2-->>A3: BingoRoomJoinRes(observing)
    A3-->>S2: ObserveBingoEventsRes
    S2-->>O: ObserveBingoEventsRes
    C2->>S2: MatchBingoReq
    S2->>A2: Relay to bound actor
    A2->>API: MatchBingoApiReq(actorId, mode)
    API->>MM: RequestToSpot(same level bucket) + InstanceSpot + InMesh
    MM->>REDIS: Reserve same waiting room
    REDIS-->>MM: same roomId + identical settings
    MM-->>API: ReserveBingoRoomRes
    API->>R: Spot GetOrCreate(same roomId, identical settings)
    R-->>API: Existing Ready room ref
    A2->>R: JoinSpot(same roomId)
    R->>API: GetPlayerRecordReq (OnJoinedActor, yield — room turn 반납)
    API-->>R: GetPlayerRecordRes(wins, losses)
    R-->>A1: PlayerJoinedNotify(client2 joined, wins/losses)
    A1->>S1: Bound session PlayerJoinedNotify
    S1-->>C1: PlayerJoinedNotify
    R->>R: Start automatically
    R-->>A1: BingoGameStartedNotify
    A1->>S1: Bound session BingoGameStartedNotify
    S1-->>C1: BingoGameStartedNotify
    R-->>A2: BingoRoomJoinRes(running)
    A2->>S2: Bound session BingoGameStartedNotify
    S2-->>C2: BingoGameStartedNotify
    A2-->>S2: MatchBingoRes
    S2-->>C2: MatchBingoRes
    C2->>S2: SubmitBingoCardReq
    S2->>A2: Relay to bound actor
    A2->>R: Submit card
    R-->>A2: SubmitBingoCardRes(running)
    A2-->>S2: SubmitBingoCardRes(running)
    S2-->>C2: SubmitBingoCardRes(running)
    C1->>S1: SubmitBingoCardReq
    S1->>A1: Relay to bound actor
    A1->>R: Submit card
    R->>R: Start draw timer
    R-->>A1: SubmitBingoCardRes(running)
    A1-->>S1: SubmitBingoCardRes(running)
    S1-->>C1: SubmitBingoCardRes(running)
```

첫 player가 들어오면 room은 대기 상태가 된다. 두 번째 player가 같은 room에 들어오면
room은 별도 `StartBingoGameReq` 없이 자동으로 `Running` 상태가 되고 양쪽 client에
`BingoGameStartedNotify`를 보낸다. client는 game start를 확인한 뒤 3 x 3 card를
제출한다. 두 player client의 card가 모두 제출되면 room Spot이 draw timer를 시작하고,
일정 간격으로 번호를 뽑아 양쪽 client에 `BingoNumberDrawnNotify`를 보낸다.
Matchmaking Redis는 같은 level bucket과 mode의 waiting room을 모든 Matchmaker
Instance Spot이 공유하기 위한 source of truth다. Room owner 조회와 remote `JoinSpot`
route는 Matchmaking Redis가 아니라 Location Store가 처리한다.

`BingoGameStartedNotify` 전달 책임은 join 상태에 따라 나눈다. owner `BingoRoom`은 이미
room 안에 있던 player에게 typed actor message로 start notify를 보낸다. 방금 remote `JoinSpot`으로
이동한 actor는 대상 room의 `OnJoinedActorAsync`가 호출된 시점에 join lifecycle을 완료한
상태이므로, 이 callback이 받은 immutable membership snapshot의 `ActorRef`로 같은 typed actor message를
보낸다. `PlayerActor`는 자기 Actor turn에서 public bound session API를 호출한다. room Spot과 source entry
handler는 mutable actor instance를 보관하거나 join 완료 뒤 다시 사용하지 않는다. remote join으로 actor의
실행 위치가 달라질 수 있기 때문이다. 이 분리는 runner의 endpoint 준비 상태 확인과 별개의 책임 경계이며,
각 언어 샘플도 같은 구조를 따라야 한다.

observer용 `BingoRoom`은 같은 Spot 타입이지만 게임 참가 room이 아니다. SpotId는 관찰 대상
`RoomId`와 observer의 전역 `ActorId`로 `observe:{RoomId}:{ActorId}` 형식으로 만든다. 이 Spot은
`BingoRoomJoinReq.ObserveOnly = true`로 join한 observer actor만 보관하고,
`PlayerJoinedNotify`, card 제출, draw timer, winner 판정에는 참여하지 않는다. 따라서
3번째 actor가 들어와도 2인 게임 규칙과 `RequiredPlayers = 2` 조건을 바꾸지 않는다.
`BingoRoomSettingsPayload.Purpose`는 game room이면 `"Game"`, observer용 local room이면
`"Observer"`로 설정한다. observer용 room은 `ObservedRoomId`에 관찰 대상 owner room id를
담고, `RequiredPlayers`와 `MaxDrawNumber`는 game rule에 사용하지 않는다.
`BingoRewardAcquiredEvent`를 수신한 `BingoRoom`은 `event.RoomId`가 자기 `ObservedRoomId`와 같을
때만 observer actor에게 push한다. owner game room이나 다른 room을 관찰하는 observer room이
같은 topic event를 받더라도, 등록된 observer와 `ObservedRoomId`가 맞지 않으면 drop한다.

## 14. Server Draw Timer와 Bound Push 흐름

```mermaid
sequenceDiagram
    participant C1 as Client 1
    participant C2 as Client 2
    participant O as Observer Client
    participant S1 as Session A
    participant S2 as Session B
    participant A1 as Player Actor 1
    participant A2 as Player Actor 2
    participant A3 as Observer Actor
    participant BR2 as BingoRoom on Play B
    participant R as Bingo Room Spot

    R->>R: Draw timer tick
    R->>R: Draw number and mark cards
    R-->>A1: BingoNumberDrawnNotify
    R-->>A2: BingoNumberDrawnNotify
    A1->>S1: Bound session send
    A2->>S2: Bound session send
    S1-->>C1: Stream BingoNumberDrawnNotify
    S2-->>C2: Stream BingoNumberDrawnNotify
    R->>R: Detect winner
    R-->>A1: BingoGameEndedNotify
    R-->>A2: BingoGameEndedNotify
    A1->>S1: Bound session send
    A2->>S2: Bound session send
    S1-->>C1: Stream BingoGameEndedNotify
    S2-->>C2: Stream BingoGameEndedNotify
    R->>R: Grant rare reward
    R->>BR2: Publish BingoRewardAcquiredEvent
    BR2-->>A3: BingoRewardAnnouncedNotify
    A3->>S2: Bound session send
    S2-->>O: Stream BingoRewardAnnouncedNotify
    O->>S2: StopObservingBingoEventsReq
    S2->>A3: Relay to bound observer actor
    A3->>BR2: Leave observer BingoRoom
    BR2-->>A3: StopObservingBingoEventsRes
    A3-->>S2: StopObservingBingoEventsRes
    S2-->>O: StopObservingBingoEventsRes
```

room Spot은 제출된 card, draw deck, mark, winner 판정을 한 모듈 안에 숨긴다.
client는 자기 card를 제출한 뒤 번호 추첨을 요청하지 않는다. 어떤 번호가 나오는지,
card가 어떻게 mark되는지, 승자가 누구인지는 서버 timer가 보낸 Notify와 state로
확인한다.

`BingoRewardAcquiredEvent`는 game state를 바꾸는 경로가 아니다. 승자 판정과 player push는
owner `BingoRoom`이 먼저 결정한다. owner room은 두 player에게 `BingoGameEndedNotify`를
보낸 뒤, 승자에게 지급된 희귀 보상 정보를 reward event로 Logical Multicast에 publish한다.
Logical Multicast은 이미 결정된 보상 이벤트를 다른 Play 서버의 observer용 local `BingoRoom`에
알리고, observer client push를 검증하기 위해 사용한다.
observer용 `BingoRoom`은 event를 받으면 자기 observer actor에게
`BingoRewardAnnouncedNotify`를 보내고, observer actor는 자기 bound session으로 client에
push한다. observer용 room은 event의 `RoomId`가 자기 `ObservedRoomId`와 일치할 때만 push한다.
별도 notification 전용 Spot 타입을 만들지 않는다.

Publish terminal은 source runtime이 publish 작업을 시작했다는 뜻이다. Subscriber별 수락,
remote Spot queue admission, handler 시작이나 완료를 결과로 반환하거나 monitoring에
집계하지 않는다. 샘플은 publish 반환값으로 fan-out 성공을 판단하지 않고 observer의
`BingoRewardAnnouncedNotify`를 별도로 기다린다.

observer client가 reward notify를 확인한 뒤에는 `StopObservingBingoEventsReq`를 보낸다.
observer actor는 observer용 local `BingoRoom`에서 leave되고 Entry Spot으로 돌아온다. 이
정리는 observer 구독 수명만 끝내며, game room의 player cleanup이나 winner 판정 상태를
바꾸지 않는다.

## 15. Disconnect와 actor destroy 흐름

Bingo 샘플은 stream disconnect와 actor destroy를 서로 다른 lifecycle로 보여 주어야 한다.
disconnect는 client stream session과 bound session 정리이고, actor destroy는 Play 서버의
actor 객체와 native actor ref를 제거하는 종료 작업이다. Session 서버 연결이 끊겼다는
이유만으로 room Spot에서 actor를 제거하거나 actor를 destroy하면 안 된다.

### 15.1 Disconnect 흐름

client stream이 끊기면 Framework는 current binding snapshot을 고정하고 각 exact binding identity에
disconnect를 자동 제출한다. Framework가 binding route와 generation을 검증하고 tombstone과 local cleanup을
완료하므로 Session disconnect callback이 bound Actor를 순회하거나 binding을 직접 제거하지 않는다.

Play 서버의 current Spot은 `OnDisconnectActorAsync`에서 해당 actor가 이 stream으로 push를 받을 수 없다는
domain 상태만 반영한다. Actor와 room membership은 유지한다. Session callback은 connection 단위 logging처럼
application에 필요한 local 작업만 수행하며 room leave와 actor destroy를 호출하지 않는다.

### 15.2 게임 종료 후 actor destroy 흐름

room Spot이 `BingoGameEndedNotify`를 양쪽 client에 전송한 뒤에는 room에 남아 있는 player
actor를 정리한다. 정리 순서는 모든 언어 샘플에서 아래와 같아야 한다.

1. Framework는 create payload로 `onCreateActor`를 실행한다. 같은 생성 attempt를 recovery하면 callback을
   다시 호출할 수 있으므로 초기화와 외부 효과는 retry-safe해야 한다.
2. room Spot은 종료 cleanup이 한 번만 시작되도록 guard를 둔다.
3. room Spot은 각 player actor에 “Entry Spot으로 돌아오면 destroy한다”는 표시를 남긴다.
4. room Spot은 `leaveActor`로 actor를 room에서 내보낸다.
5. framework는 room `onLeaveActor`를 호출한 뒤 actor를 Entry Spot으로 이동시키고 Entry
   Spot `onJoinedActor`를 호출한다. room `onLeaveActor`는 Api 서버에 `ReportBingoResultReq`를
   보내고 **`yield`로 기다린다** — 결과 기록은 room의 공유 상태와 무관하므로 그 왕복이 room
   실행 줄을 붙잡으면 안 된다(§7.1).
6. Entry Spot `onJoinedActor` 또는 Entry Spot handler는 actor의 destroy 표시를 확인하고
   Entry Spot context의 `destroyActor`를 호출한다.
7. `destroyActor`는 `onLeaveActor`나 다른 lifecycle callback을 호출하지 않고 actor 객체,
   native actor ref, framework registry, bound session binding을 정리한다.
8. 같은 incarnation이 이미 없으면 destroy는 idempotent `false`를 반환하고 lifecycle callback을 다시
   호출하지 않는다. 같은 `ActorId`의 다른 generation이면 `InvalidOperation`, relocation seal 중이면
   `Unavailable`로 끝난다.

```mermaid
sequenceDiagram
    participant R as Room Spot
    participant A as Player Actor
    participant API as Api Server
    participant E as Entry Spot
    participant N as Native Actor

    R->>A: Mark destroy after Entry Spot join
    R->>R: leaveActor(A)
    R->>R: onLeaveActor(A)
    R->>API: ReportBingoResultReq (yield — room turn 반납)
    API-->>R: ReportBingoResultRes(wins, losses)
    R->>E: framework moves actor, onJoinedActor(A)
    E->>E: destroyActor(A)
    E->>N: Destroy native actor ref
    E->>E: Remove actor registry and session binding
```

player actor destroy는 client가 직접 관찰하는 protocol 메시지가 아니므로 server-side
evidence로 확인한다. client self-check는 game 종료 notify와 observer 관찰 종료 response를
검증하고, 언어별 `run_sample` 또는 sample regression은 Play 서버 로그, fake backend call,
runtime event, 또는 framework 테스트 중 하나로 아래 사실을 확인해야 한다.

- room Spot `onLeaveActor`가 각 player actor마다 실행되고, 그 안에서 `ReportBingoResultReq`가
  Api 서버로 나간다(§7.1). 이 왕복은 `yield`로 기다리므로 room 실행 줄을 붙잡지 않는다.
- Entry Spot destroy가 각 player actor마다 완료된다.
- Entry Spot destroy 과정에서 Entry Spot `onLeaveActor`나 다른 lifecycle callback이
  추가로 실행되지 않는다.
- disconnect cleanup만으로 actor destroy가 실행되지 않는다.

observer actor는 player actor cleanup 대상이 아니다. observer actor는
`StopObservingBingoEventsReq` 처리 중 observer용 local `BingoRoom`에서 leave되어 Entry Spot으로
돌아온다. 샘플은 observer actor가 reward event 수신 후 관찰 room을 떠났다는 server-side
evidence를 남겨야 한다.

## 16. 완료 기준

- client 세 개가 Session 서버에 하나의 stream 연결만 연다. player-1은 Session A에,
  player-2와 observer는 Session B에 연결한다.
- Session 2개, API 2개, Play 2개가 별도 실행 모드 또는 별도 프로세스로 구분되어 있다.
- Matchmaking process 1개를 별도 실행한다. 이 수는 샘플 topology이며 singleton 계약이 아니다.
- `bingo.play`와 `bingo.matchmaking` RouteMesh를 분리하고 공유 Location Store로 peer를 발견한다.
- API는 두 RouteMesh에 Object Client를 각각 등록한다. Play에 Matchmaker factory를 등록하지 않는다.
- 독립 `bingo.api` ClientServer Channel로 Session·Play의 API request를 처리한다.
- Matchmaker Instance Spot은 level bucket별 SpotId, `RecreateOnRelocation` policy, Redis source of truth와
  internal idle timer `CloseAsync`를 사용한다.
- Redis reservation은 matching state 공유에만 사용하고, Spot owner lookup과 Logical Multicast peer
  discovery를 대신하지 않는다.
- 두 player client와 observer client가 서로 다른 actor로 인증된다.
- Actor와 room Spot은 NodeRid를 application 입력으로 받지 않고 framework 배치 결과를 사용한다.
- Session 서버가 인증된 stream session을 Play 서버 actor에 bind한다.
- 첫 `MatchBingoReq`는 Matchmaker reservation을 얻은 뒤 API가 Room User Spot
  `GetOrCreate`의 Ready 결과를 기다리고 waiting state를 반환한다.
- 같은 reservation에 참가한 모든 request가 동일한 room settings를 사용한다. Concurrent
  `GetOrCreate` caller는 하나의 Creating authority가 Ready가 될 때까지 기다린다.
- observer client는 `ObserveBingoEventsReq(RoomId)`를 보내 observer용 `BingoRoom`에 join하고
  `ObserveBingoEventsRes.Subscribed = true`를 확인한다.
- observer용 `BingoRoom`은 observer actor를 보관하고 reward topic을 구독하지만,
  player membership, card 제출, draw timer, winner 판정에는 참여하지 않는다.
- 두 번째 `MatchBingoReq`는 같은 `RoomId`의 Spot에 join하고 room을 자동 시작시킨다.
- 두 player client는 game start를 확인한 뒤 `SubmitBingoCardReq`로 card를 제출한다.
- 두 card가 모두 제출되면 room Spot timer가 번호를 뽑고 각 player card mark를
  서버에서 갱신한다.
- 승자가 나오면 room state가 `Finished`가 되고 `Winners`가 채워진다.
- `BingoNumberDrawnNotify`와 `BingoGameEndedNotify`가 bound session을 통해 두 player
  client에 전달된다.
- owner `BingoRoom`은 승자에게 지급된 희귀 보상 정보를 담은 `BingoRewardAcquiredEvent`를
  Logical Multicast topic으로 publish한다.
- Publish terminal에는 target별 수락 결과가 없다. Source에서 publish 작업이 시작되면
  완료되며 subscriber handler 실행은 별도 notify로 검증한다.
- topic을 구독한 observer용 `BingoRoom`은 `BingoRewardAcquiredEvent`를 수신하고 observer client에
  `BingoRewardAnnouncedNotify`를 push한다.
- reward event 수신을 위해 `BingoNotificationSpot` 같은 별도 Spot 타입을 만들지 않는다.
- `BingoRewardAnnouncedNotify.ItemId`, `ItemName`, `Rarity`는 owner가 publish한 보상 정보와
  같아야 한다.
- observer client는 `StopObservingBingoEventsReq`를 보내 observer actor가 observer용
  local `BingoRoom`에서 leave되고 Entry Spot으로 돌아온 것을 확인한다.
- client inbound observer 로그에 request 응답과 server push 수신을 나타내는
  `stream-inbound` marker가 남는다.
- stream disconnect는 bound session을 정리하지만 actor를 즉시 destroy하지 않는다.
- 게임 종료 후 room Spot은 actor를 Entry Spot으로 leave시키고, Entry Spot은 actor를
  destroy한다.
- actor destroy는 `onLeaveActor`를 호출하지 않고 actor registry와 native actor ref를
  정리한다.
- `PlayerActor.OnJoinCompletedAsync(...)`는 같은 Actor Join OperationId의 재전달을 한 번만
  적용한다. 이 상태는 Actor relocation payload에 포함한다.
- client는 API 서버나 Play 서버 endpoint를 직접 사용하지 않는다.
- handler 등록은 스캔·선언형 metadata 기반 자동 등록을 사용한다(C++만 compile-time 명시 등록).
- 카드 제출 flow는 제출 dispatch까지 이어지고, 각 timer tick은 별도 `origin=timer` flow로
  room dispatch와 해당 bound push까지 이어진다.
- 언어 표준 meter/registry 연결 예제가 있고 Play drain의 고정 종료 순서를 검증한다.

§3.3의 자동 routing id는 필수 완료 조건이다. 모든 언어에서
API·Matchmaking·Play·Session은 prefix만 설정하고 fixed RID를 사용하지 않아야 한다.
시작할 때 만든 RID를 설정, message 또는 reservation state에 저장해 업무 routing에
사용해서도 안 된다.

## 17. 관측·운영 켜기 (Observability & Ops)

Bingo는 이미 세션 게이트웨이(STREAM)·actor 이동·룸 타이머·bound push를 갖춰 관측·운영 기능이
관측하는 사건을 그대로 만들어 낸다. 그래서 사용자가 **바로 따라 켜 보기** 좋은 샘플이다. 세
기능은 각각 [메시지 흐름 상관관계](../../spec/27-flow-correlation.ko.md), [런타임 메트릭](../../spec/25-runtime-metrics.ko.md),
[Graceful Drain & Handoff](../../spec/28-graceful-drain-handoff.ko.md)이 계약을 소유하고, 언어별 표면은
각 언어 monitoring 문서가 소유한다. **셋 다 공통 케이스는 무설정에 가깝다.**

### 17.1 메시지 흐름 추적 로그 (flow correlation)

네 역할(`Session`/`Api`/`Matchmaking`/`Play`)에서 diagnostics를 켠다. Flow ID는 framework가
처음 처리하는 지점에서 만들고 메시지 경계에 전파한다. 카드 제출 Flow는
STREAM→Actor→room Spot까지 이어진다. Timer tick은 `origin=timer`인 새 Flow를
시작하고 room dispatch→bound push까지 이어진다. 저장 위치와 exporter는 각
application의 표준 logging·telemetry 설정에서 구성한다.

```csharp
// 각 노드 공통 (.NET)
options.ConfigureDispatch().Diagnostics
    .SetLevel(ZLinkDiagnosticsLevel.Normal) // 오류와 주요 처리 경계를 기록한다.
    .SetSampleRate(1.0)                     // 샘플에서는 모든 정상 Flow를 evidence로 남긴다.
    .IncludeMessageSizes(false);            // 업무 payload 내용과 byte 크기는 기록하지 않는다.
```

확인: 카드 제출 ID는 `Session`→`Play` Actor relay→`BingoRoomSpot` dispatch를
잇는다. 별도 timer ID는 `BingoRoomSpot` timer dispatch→bound push를 잇는다.
Spot 경계를 지나도 structured log와 trace의 `flow` 값이 유지된다.

### 17.2 런타임 메트릭

`Session`은 CCU, `Play`는 룸 큐·actor 이동을 방출한다. 언어 표준 meter/registry만 연결하면
된다. .NET 샘플은 별도 HTTP metrics 서버를 추가하지 않고 `MeterListener`로 framework meter를
구독해 runner 로그에서 실제 계기 값을 확인한다.

```csharp
// Session/Play 공통 (.NET) — framework meter의 계기만 구독
listener.InstrumentPublished = static (instrument, meterListener) =>
{
    if (instrument.Meter.Name == ZLinkMeters.Framework)
        meterListener.EnableMeasurementEvents(instrument);
};
listener.SetMeasurementEventCallback<long>(RecordMetric);
listener.SetMeasurementEventCallback<double>(RecordMetric);
listener.Start(); // runner는 "zlink metric" 로그에서 실제 값을 확인
```

Prometheus나 OpenTelemetry exporter가 필요한 애플리케이션은 같은 `ZLinkMeters.Framework` meter를
자기 metrics pipeline에 추가한다. exporter 선택과 metrics HTTP endpoint는 앱의 운영 정책이다.
관찰 포인트: `zlink.stream.connections.active`(=CCU, `Session`)와 player Actor가 다른
`Play`로 relocation될 때의 `zlink.relocation.completed`(`object_kind=actor`).

### 17.3 Graceful Drain

`BingoRoom`은 `SpotWide`와 `ApplicationSignaled` readiness로 등록한다. Game round가
끝나고 다음 round를 시작하기 전처럼 application state가 안전한 turn에서만
`RelocationReady().Defer()`를 호출한다. 이 호출은 relocation을 시작하라는 명령이 아니다.
Framework가 이 room을 대상으로 선택하지 않았으면 source에서 `Continued`, 실제로 옮겼으면
target에서 `Relocated`를 `OnRelocationReadyCompletedAsync`에 전달한다. Application은 callback이
끝난 뒤에만 다음 round를 시작한다.

`BingoRoomRelocationAdapter`는 `CaptureAsync`에서 game status, participant별 card와 mark,
draw sequence, 남은 draw deck과 winner 판정에 필요한 application state를 byte 배열로 만든다.
`RestoreAsync`는 Framework가 target에 만든 `BingoRoom` instance에 그 상태를 적용한다.
Adapter는 Actor instance, 실행하지 않은 message, accepted journal, timer handle과 owner 정보를
직접 저장하지 않는다. 이 항목은 Framework가 아래 aggregate relocation 과정에서 보존한다.

`PlayerActorRelocationAdapter`도 같은 방식으로 player application state와 마지막으로 적용한
Actor Join OperationId를 저장하고 복원한다. Adapter가 받은 byte 배열의 형식과 version 관리는
샘플 application이 맡지만, Location Store record나 Relocation Store key를 직접 다루지는 않는다.

Framework가 room을 선택하면 current turn 뒤 queue를 seal하고 Room User Spot과 member Actor를
한 relocation unit으로 옮긴다. 실행하지 않은 message, accepted journal, logical timer와 pending
tick도 같은 unit에 포함한다. Member Actor의 join·leave callback은 호출하지 않고 SpotId,
ActorId와 ObjectGeneration을 유지한다.

Spot payload와 각 Actor payload는 서로 독립된 immutable data로 만든다. Framework는
Relocation Store write와 target read·restore를 설정된 relocation concurrency와 in-flight byte
한도 안에서 병렬 실행한다. 모든 payload와 checksum이 준비되기 전에는 Location Store의
aggregate owner commit을 실행하지 않는다. 일부 Actor만 새 owner로 공개되는 상태는 허용하지 않는다.

운영 regression은 일반 game flow와 별도로 다음 workload를 실행한다.

| 검증 | 조건 | 성공 기준 |
|------|------|-----------|
| SpotWide aggregate | Room 1개와 member Actor 100개 | Spot과 100개 Actor가 같은 aggregate commit으로 이동한다. Actor 수를 100보다 늘리지 않는다. |
| 실제 payload | Spot state, Actor state, queue와 timer | Adapter가 반환한 byte 크기와 Relocation Store 전체 byte를 기록한다. |
| service interruption | unit의 source admission seal부터 target admission open까지 | 1초 이내를 목표로 계측한다. 1초를 넘겨도 relocation을 취소하거나 source로 rollback하지 않는다. |
| 병렬 payload I/O | Actor payload가 여러 chunk로 나뉜다. | write와 read·restore가 bounded parallelism을 사용하고 in-flight byte 한도를 넘지 않는다. |
| service continuity | 다른 room과 actor에 지속적으로 request를 보낸다. | 대상 unit이 이동하는 동안 다른 unit의 handler와 timer가 계속 처리된다. |
| Message Follow | commit 직후 이전 physical route로 Spot·Actor send/request를 보낸다. | 이전 owner는 application handler를 실행하지 않고 relocation 때 기록한 target으로 전달한다. Operation ID, deadline, FIFO와 reply route를 유지한다. Route missing·만료·loop는 `Unavailable`, generation mismatch는 `InvalidOperation`, message·byte 한도 초과는 `CapacityExceeded`로 끝난다. |
| Ready callback | safe turn에서 `RelocationReady().Defer()`를 호출한다. | 정상 실행에서는 `Continued` 또는 `Relocated`의 logical completion 하나가 끝난 뒤 다음 round를 시작한다. Recovery에서 callback이 다시 호출돼도 같은 결과로 수렴해야 한다. |

여기서 1초는 전체 Play node drain 시간이 아니다. Actor 1개, Instance Spot 1개 또는
User Spot 1개처럼 application이 서비스 단위로 보는 relocation unit 하나의 중단 시간 목표다.
전체 node는 여러 unit을 제한된 동시 실행 수로 옮기므로 더 오래 걸릴 수 있다.

Commit 전 failure는 source queue를 복원한다. Commit 뒤 failure는 일부 participant를 source로
되돌리지 않고 같은 aggregate relocation을 recovery한다. 상세 순서와 Message Follow matrix는
[Config 5](../../e2e/config-5-resilience-lifecycle.ko.md)와
[Config 11](../../e2e/config-11-observability-ops.ko.md)이 소유한다.

### 17.4 언어별 표면

같은 세 기능의 언어별 정식 표면은 각 언어 exact interface 문서를 본다:
[.NET RouteMesh](../../spec/server/languages/dotnet/05-route-mesh.ko.md) ·
[Java interfaces](../../spec/server/languages/java/interfaces/README.ko.md) ·
[Java Channel](../../spec/server/languages/java/interfaces/channel-messaging.ko.md) ·
[Node](../../spec/server/languages/node/interfaces/README.ko.md) ·
[C++](../../spec/server/languages/cpp/interfaces/README.ko.md) ·
[Kotlin](../../spec/server/languages/kotlin/interfaces/README.ko.md). 배포 조건에서의 e2e 검증은
[Config 11 — 관측·운영 배포](../../e2e/config-11-observability-ops.ko.md)가 다룬다.
