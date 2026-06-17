# Bingo Sample Scenario

[샘플 목록](../README.ko.md)

> 이 문서는 모든 framework 언어가 공유하는 Bingo 샘플 시나리오를 정의한다.
> 언어별 샘플은 이 문서를 기준으로 서버 역할, 메시지 흐름, smoke 검증 기준을 맞춘다.

## 1. 목적

Bingo 샘플은 client가 하나의 Session 서버 stream 연결만 유지해도 인증, 매칭,
게임 진행, server push를 모두 처리할 수 있음을 보여 준다. Session 서버는 client
연결과 actor binding을 맡고, Play 서버는 player actor와 room Spot을 소유한다.
API 서버는 인증과 매칭 요청을 처리하며, Registry는 서버 간 endpoint 발견을 맡는다.

이 샘플에서 확인해야 하는 핵심은 아래와 같다.

- client는 Session 서버 stream endpoint 하나만 알고 연결한다.
- Session 서버는 인증 후 현재 stream session을 Play 서버의 player actor에 bind한다.
- client packet은 bound actor로 relay된다.
- API 서버는 매칭 요청을 받고 Play 서버에 room 배정을 요청한다.
- Play 서버는 Entry Spot에서 room Spot으로 actor를 join시킨다.
- client는 자기 bingo card를 입력해 room Spot에 제출한다.
- room Spot은 제출된 카드, 번호 추첨, mark, 승리 판정을 소유한다.
- 두 client의 card가 모두 제출되면 room Spot이 server timer로 번호를 뽑고,
  Play 서버는 번호와 state를 bound session으로 Notify한다.
- Registry/Discovery를 사용해 서버 간 endpoint를 자동으로 발견하고 연결한다.
- handler는 interface 구현체를 framework에 명시 등록하는 방식을 사용한다.
- Bingo의 stream, channel, actor, room Spot payload는 Protobuf를 사용한다.

Client self-check도 샘플의 일부다. client는 `.NET` 샘플처럼 각 request 응답과 server
push payload를 즉시 검증해야 한다. 특히 `PlayerJoinedNotify`,
`BingoGameStartedNotify`, `BingoNumberDrawnNotify`, `BingoGameEndedNotify` 대기는
stream connector의 public wait interface를 직접 사용한다. inbox를 두더라도 push 도착을
기다리는 로직을 sample-local polling 함수로 숨기면 안 된다.

Bingo가 Protobuf를 맡는 이유는 이 샘플이 여러 서버 역할과 많은 request/response/notify
계약을 가진 gateway형 게임이기 때문이다. Protobuf schema는 언어별 샘플이 같은 필드와
같은 wire 이름을 유지하도록 돕는다. JSON payload 가독성은 TicTacToe와 다른 JSON 기반
샘플이 맡는다.

## 2. 서버 구성

```mermaid
graph LR
    C[Client]
    S[Session Server]
    API[Api Server]
    P[Play Server]
    R[Registry]

    C -->|STREAM packets| S
    S -->|API channel| API
    S -->|Actor gateway| P
    API -->|Play channel| P
    P -->|Bound session Notify| S
    S -->|STREAM Notify| C
    S -. discovery .-> R
    API -. discovery .-> R
    P -. discovery .-> R
```

client가 직접 연결하는 서버는 Session 서버뿐이다. API 서버와 Play 서버는
client-facing stream endpoint를 열지 않는다. Session 서버는 인증과 session lifecycle을
소유하지만, 게임 규칙을 해석하지 않는다. 게임 packet은 현재 session에 bind된 actor로
전달되고, actor와 room Spot이 domain state를 처리한다.

## 3. 자동 연결 방식

Bingo는 Registry/Discovery 기반 자동 연결을 사용한다. 각 서버는 자신이 제공하는
channel, stream, Spot node endpoint를 Registry에 등록하고, 다른 서버는 service 이름으로
상대 endpoint를 찾는다.

| 연결 | 연결 방식 | 이유 |
|------|-----------|------|
| Session -> API channel | Discovery 자동 연결 | Session 서버가 API 서버 주소를 직접 들고 있지 않게 한다. |
| API -> Play channel | Discovery 자동 연결 | matching API가 현재 Play 서버 endpoint를 Registry에서 찾는다. |
| Session -> Play actor gateway | Registry 기반 actor locator | Session 서버가 Play 서버 actor의 위치를 직접 관리하지 않게 한다. |
| Play -> Session bound push | Registry 기반 session route | Play 서버가 현재 client session 위치를 framework route로 찾는다. |

이 샘플이 자동 연결을 쓰는 이유는 운영형 gateway 구조에서 서버 증설과 endpoint
변경을 application 코드 밖으로 밀어내는 흐름을 보여 주기 위해서다. 수동 endpoint
연결은 TicTacToe 샘플이 맡는다.

## 4. 프로세스와 책임

| 프로세스 | 구성 요소 | 책임 |
|----------|-----------|------|
| `Bingo.Registry` | registry host | Session, API, Play 서버 endpoint를 발견 가능하게 한다. |
| `Bingo.Api` | `Api` channel server | access token 인증과 matching API 요청을 처리한다. |
| `Bingo.Api` | `Play` channel client | Play 서버에 room 배정을 요청한다. |
| `Bingo.Session` | stream server | client 연결, 인증 packet, actor binding, actor relay를 처리한다. |
| `Bingo.Session` | session Spot node | ActorGateway attach와 bound session push 수신을 담당한다. |
| `Bingo.Play` | actor runtime | player actor를 만들고 Entry Spot에 join시킨다. |
| `Bingo.Play` | `BingoEntrySpot` | actor가 특정 room에 들어가기 전의 admission 지점을 맡는다. |
| `Bingo.Play` | `BingoRoom` room Spot | room 참가자, 제출된 카드, draw deck, 승리 판정, Notify 생성을 소유한다. |
| `Bingo.Play` | `Play` channel server | API 서버의 room 배정 요청을 받는다. |

## 5. 디렉토리와 파일 구성

Bingo 샘플은 Session, API, Play, Registry가 분리된 gateway형 게임 샘플이다. 규모가
TicTacToe보다 크기 때문에 DDD와 헥사고날 아키텍처의 경계를 더 분명히 유지해야 한다.
이 구조는 C++, Java, Kotlin, TypeScript, .NET 샘플을 작성할 때 함께 따라야 하는
공통 기준이다. 언어별 빌드 도구, 파일 확장자, package/module 표현은 달라질 수 있지만,
`Client`, `Shared`, `Server/Api`, `Server/Session`, `Server/Play/Domain`,
`Server/Play/Application`, `Server/Play/Adapters`, `Server/Registry` 경계와 각 책임은
유지해야 한다.

```text
Bingo/
  README
  build files
  run sample script
  Client/
    Program
    BingoClientScenario
    client build/module files
  Probe/
    Program
    probe build/module files
  Shared/
    shared build/module files
    Configuration/
      SampleNames
      SampleTopology
    Contracts/
      bingo_messages.proto
  Server/
    Registry/
      Program
      RegistryHostFactory
      registry build/module files
    Api/
      Program
      ApiServerHostFactory
      api build/module files
      Handlers/
        AuthenticatePlayerHandler
        MatchBingoHandler
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
      Application/
        RoomAllocation/
          BingoRoomAllocator
      Adapters/
        ZLink/
          Actors/
            PlayerActor
            PlayerActorFactory
          Handlers/
            AllocateBingoRoomHandler
            EnsurePlayerActorHandler
          Notifications/
            BingoNotificationPublisher
            BingoRoomEvent
            BingoRoomEventMapper
          Spots/
            BingoEntrySpot
            BingoRoom
            Handlers/
              MatchBingoActorHandler
              SubmitBingoCardHandler
              BingoRoomDrawTimerHandler
```

위 구조는 파일명 고정 규칙이 아니라 역할과 경계의 기준이다. 예를 들어 Java/Kotlin은
package와 class 이름으로, TypeScript는 module과 file 이름으로, C++은 header/source
쌍과 namespace로, .NET은 project와 class 이름으로 같은 구조를 표현할 수 있다. 중요한
점은 같은 책임의 코드가 같은 위치에 있고, 다른 레이어로 섞이지 않는 것이다.

각 영역의 역할은 아래와 같다.

| 위치 | 공통 아키텍처 역할 | 책임 |
|------|----------------------|------|
| `Client/Program` | 외부 driving adapter | Session stream 연결을 만들고 client self-check 시나리오를 실행한다. |
| `Client/BingoClientScenario` | sample scenario | 인증, matching, card 제출, draw push, final state 검증을 순서대로 수행한다. |
| `Probe/*` | topology probe | Registry와 서버 endpoint가 준비되었는지 샘플 실행 전에 확인한다. |
| `Shared/Configuration/*` | shared settings | sample service 이름, packet 이름, endpoint topology를 공유한다. |
| `Shared/Contracts/*` | shared contract | Protobuf schema처럼 언어 간 동일해야 하는 payload 계약을 둔다. |
| `Server/Registry/*` | discovery adapter | 각 서버의 channel, stream, Spot endpoint를 발견 가능하게 한다. |
| `Server/Api/*` | API channel adapter | 인증과 matching 요청을 처리하고 Play 서버 room allocation으로 연결한다. |
| `Server/Session/*` | stream gateway adapter | client stream, 인증, actor binding, bound session relay를 처리한다. |
| `Server/Play/Domain/Bingo/*` | domain model | card, draw deck, room status, winner 판정 같은 게임 규칙을 framework 타입 없이 표현한다. |
| `Server/Play/Application/RoomAllocation/*` | application use case | waiting room 재사용과 room Spot 생성을 조율한다. |
| `Server/Play/Adapters/ZLink/*` | ZLink adapter | channel, actor, Spot callback, notification publish를 application/domain 호출로 변환한다. |

의존 방향은 `Adapters -> Application -> Domain`이다. Domain은 ZLink framework, Registry,
stream session, actor gateway, logger를 알지 않는다. Application은 room 배정 같은 use
case 조율만 맡고, server endpoint 발견, session binding, push 전송 같은 외부 입출력은
adapter에 둔다. 이 규칙 덕분에 다른 언어로 옮겨도 gateway 구조와 게임 규칙의 위치가
같게 유지된다.

## 6. 언어별 구현 기준

Bingo 샘플은 현재 .NET 샘플에서 검증된 실행 형태와 같은 수준으로 C++, Java, Kotlin,
TypeScript에서도 작성되어야 한다. 언어 문법과 빌드 도구는 달라도 사용자가 샘플을 열었을
때 같은 서버 역할, 같은 gateway 흐름, 같은 검증 지점을 찾을 수 있어야 한다.

언어별 구현은 아래 기준을 만족해야 한다.

- 샘플 루트에는 client, probe, shared contracts, registry, api, session, play 역할이
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
  샘플 실행은 Registry, API, Session, Play server를 띄우고 client가 Session stream에
  접속해 `bingo=completed`와 server evidence에 해당하는 성공 결과를 만들 수 있어야 한다.
- Probe 또는 동등한 readiness 확인 흐름을 둔다. 단순 sleep으로 서버 준비 상태를 숨기지
  말고, Registry와 필요한 endpoint가 실제로 준비되었는지 확인한다.
- push 대기는 connector 객체의 public wait interface를 직접 사용한다. 필요한 push를 고를
  때는 connector wait API의 filter 기능을 사용하고, 받은 message 객체의 public interface로
  payload를 읽어 `Ensure(condition)`처럼 조건식이 직접 보이는 방식으로 검증한다.
- client는 stream connector를 만든 직후, `connect` 전에 inbound observer를 등록해
  `stream-inbound` marker가 포함된 수신 로그를 남긴다. 이 로그는 request 응답과
  server push 수신을 관찰하기 위한 것이며, payload 검증이나 push 대기를 대신하지 않는다.
  observer callback에서는 connector send/request/wait를 다시 호출하지 않는다.
- Java와 Kotlin client scenario의 `submit`과 `await` 의미는
  [framework 공통 비동기 정책](../../async-execution-policy.ko.md)을 따른다.
  `submit`은 작업을 시작하고 future를 반환하는 이름으로, `await`는 완료를 기다려
  결과를 받는 이름으로 사용한다.
- sample-local inbox, sleep, 임시 polling 함수로 준비 상태나 push 도착을 숨기면 안 된다.
  대기와 검증은 connector, probe, message 객체 인터페이스를 사용하는 샘플 시나리오 코드에서
  드러나야 한다.
- Session 서버는 gateway 역할만 한다. 인증, actor binding, packet relay, bound session
  push 수신을 맡고, card 검증, draw, winner 판정 같은 게임 규칙을 해석하지 않는다.
- Domain에는 card, draw deck, room status, winner 판정만 둔다. Registry, stream session,
  actor gateway, handler, logger, codec, endpoint 설정은 Domain으로 들어오면 안 된다.
- Application은 room allocation use case를 조율한다. framework callback을 직접 받거나
  transport 세부 구현을 다루지 않는다.
- Adapters는 Registry/Discovery, channel, stream session, actor, Spot, timer,
  notification publish, codec 연결을 맡는다. handler나 Spot adapter가 card 판정, draw
  order, winner 판정을 직접 구현하면 안 된다.

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
  Application/
    RoomAllocation/
      BingoRoomAllocator
  Adapters/
    ZLink/
      Actors/
        PlayerActor
        PlayerActorFactory
      Handlers/
        AllocateBingoRoomHandler
        EnsurePlayerActorHandler
      Notifications/
        BingoNotificationPublisher
        BingoRoomEvent
        BingoRoomEventMapper
      Spots/
        BingoEntrySpot
        BingoRoom
        Handlers/
          MatchBingoActorHandler
          SubmitBingoCardHandler
          BingoRoomDrawTimerHandler
```

역할은 아래처럼 나눈다.

| 위치 | 책임 |
|------|------|
| `Domain/Bingo/BingoCard` | 3 x 3 card 검증, free cell, mark, complete line 계산을 소유한다. |
| `Domain/Bingo/BingoGame` | 제출된 card, draw deck, drawn numbers, winners, draw 종료 조건을 소유한다. |
| `Domain/Bingo/BingoRoomGame` | player join, room status, card 제출 가능 여부, draw timer 시작/종료 신호, room event 생성을 소유한다. |
| `Application/RoomAllocation/BingoRoomAllocator` | matching 요청을 받아 room을 새로 만들거나 기존 waiting room을 재사용한다. |
| `Adapters/ZLink/Spots/BingoRoom` | ZLink Spot lifecycle, actor join callback, timer 등록, domain 호출, notification publish 연결을 맡는다. |
| `Adapters/ZLink/Notifications/*` | domain event를 bound session push message로 바꾸고 전송한다. |
| `Adapters/ZLink/Handlers/*` | channel request와 Spot actor request를 받아 application/domain adapter로 연결한다. |

Domain 객체는 ZLink framework 타입을 직접 참조하지 않는다. `BingoRoom` Spot은 framework
callback을 받아 domain method를 호출하고, domain이 반환한 change와 event를 adapter가
message로 바꾼다. card validation, draw order, winner 판정이 handler나 Spot handler에
흩어지면 안 된다.

## 8. Handler 등록 방식

Bingo 샘플은 typed handler 계약을 명시 등록하는 방식을 사용한다. 각 handler는
framework가 정의한 handler 계약을 구현하고, 서버 구성 코드에서 scan 또는 명시
등록으로 framework에 알려진다.

이 방식의 목적은 아래와 같다.

- handler가 어떤 request/response 계약을 처리하는지 타입 선언으로 드러난다.
- Session, channel, Entry Spot, room Spot handler가 같은 등록 원칙을 공유한다.
- attribute, annotation, decorator가 없는 언어에서도 같은 구조를 옮기기 쉽다.

언어별 framework가 선언형 등록 기능을 제공하더라도 Bingo에서는 handler 계약을
구성 코드에서 명시 등록하는 방식을 우선 사용한다. `.NET`이나 Java처럼 interface를
자연스럽게 쓸 수 있는 언어는 interface로 표현하고, TypeScript처럼 runtime interface가
사라지는 언어는 handler class와 명시 등록으로 같은 의미를 표현한다. 선언형 등록
방식은 TicTacToe 샘플이 맡는다.

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
| 번호 추첨 | 두 client의 card가 모두 제출되면 room Spot timer가 일정 간격으로 번호를 하나씩 뽑는다. |
| mark 방식 | 서버 자동 mark. client는 card만 제출하고 mark나 bingo claim은 보내지 않는다. |
| 승리 조건 | complete line이 1개 이상 생기면 승리 |
| 종료 조건 | 첫 승리 draw sequence가 나오면 종료 |

방장, ready 버튼, 수동 mark, 여러 라운드, 랭킹은 공통 샘플 범위에서 제외한다.
이 기능들은 게임 샘플을 크게 만들지만 framework 흐름을 이해하는 데 꼭 필요하지 않다.

## 10. Client 검증 흐름

Bingo client는 아래 순서로 scenario를 실행하고 각 단계의 값을 확인한다.

1. `player-1`, `player-2`로 stream 인증을 요청하고, 각 `AuthenticateRes.ActorId`가
   요청한 actor id와 같은지 확인한다.
2. `player-1`이 먼저 `MatchBingoReq`를 보내고 `WaitingForPlayers` 상태와 room id를
   확인한다. 이 시점에 `player-1`이 자기 join notify를 받지 않았는지도 확인한다.
3. `player-2`가 `MatchBingoReq`를 보내면 같은 room id와 `Running` 상태를 확인한다.
4. `player-1`은 connector wait API로 `PlayerJoinedNotify`를 기다리고,
   payload의 `ActorId`가 `player-2`인지 확인한다. `player-2`는 자기 join notify를
   받지 않아야 한다.
5. 두 client는 connector wait API로 `BingoGameStartedNotify`를 기다리고,
   push state가 `Running`인지 확인한다.
6. 두 client가 deterministic card를 제출한 뒤 response state에 두 player card가 모두
   9칸으로 들어갔는지 확인한다.
7. 두 client는 draw sequence별로 `BingoNumberDrawnNotify`를 기다리고, 양쪽 push의
   `DrawSeq`, `Number`, state가 서로 같은지 확인한다.
8. 두 client는 `BingoGameEndedNotify`를 기다리고, final state의 `Finished`, drawn
   number sequence, winners, player list, center free-cell mark를 확인한다.
9. 두 client는 inbound observer 로그에 `stream-inbound` marker가 남았는지 확인한다.
   로그에는 sample 이름, client 역할, message kind, packet name, request sequence,
   payload byte length가 포함되어야 한다. heartbeat control frame은 observer 기능
   검증에는 포함할 수 있지만 기본 sample output에서는 낮은 log level로 두거나 걸러낸다.

이 검증은 성공 시나리오를 눈으로 읽기 위한 로그가 아니라 sample release gate다. 언어별
client가 위 값을 확인하지 않으면 공통 sample 기준을 만족하지 못한다.

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

ActorRefSnapshot {
  NodeRid: bytes
  ActorId: string
  Generation: uint64
}

EnsurePlayerActorRes {
  ActorId: string
  ActorType: string
  Actor: ActorRefSnapshot
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

AllocateBingoRoomReq {
  Mode: string
  ActorId: string
}

AllocateBingoRoomRes {
  RoomId: string
}

BingoRoomJoinReq {
  RoomId: string
  ActorId: string
  DisplayName: string
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
```

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
}
```

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
    S->>P: EnsurePlayerActorReq
    P-->>S: EnsurePlayerActorRes(actorRef)
    S->>S: Bind current stream session to actorRef
    S-->>C: Stream AuthenticateRes
```

Session 서버는 인증 성공 후 actor reference를 얻고 현재 stream session을 그 actor에
bind한다. 이후 client gameplay packet은 Session 서버가 직접 처리하지 않고 bound actor로
relay한다.

## 13. Matching과 카드 제출 흐름

```mermaid
sequenceDiagram
    participant C1 as Client 1
    participant C2 as Client 2
    participant S as Session Server
    participant A1 as Player Actor 1
    participant A2 as Player Actor 2
    participant API as Api Server
    participant P as Play Server
    participant E as Entry Spot
    participant R as Bingo Room Spot

    C1->>S: MatchBingoReq
    S->>A1: Relay to bound actor
    A1->>API: MatchBingoApiReq
    API->>P: AllocateBingoRoomReq
    P-->>API: AllocateBingoRoomRes(roomId)
    A1->>E: Join room request
    E->>R: Join actor
    R-->>A1: BingoRoomJoinRes(waiting)
    A1-->>S: MatchBingoRes
    S-->>C1: MatchBingoRes
    C2->>S: MatchBingoReq
    S->>A2: Relay to bound actor
    A2->>API: MatchBingoApiReq
    API->>P: AllocateBingoRoomReq
    P-->>API: AllocateBingoRoomRes(same roomId)
    A2->>E: Join room request
    E->>R: Join actor
    R-->>A1: PlayerJoinedNotify(client2 joined)
    R->>R: Start automatically
    R-->>A1: BingoGameStartedNotify
    R-->>A2: BingoGameStartedNotify
    R-->>A2: BingoRoomJoinRes(running)
    A2-->>S: MatchBingoRes
    S-->>C2: MatchBingoRes
    C2->>S: SubmitBingoCardReq
    S->>A2: Relay to bound actor
    A2->>R: Submit card
    R-->>A2: SubmitBingoCardRes(running)
    C1->>S: SubmitBingoCardReq
    S->>A1: Relay to bound actor
    A1->>R: Submit card
    R->>R: Start draw timer
    R-->>A1: SubmitBingoCardRes(running)
```

첫 player가 들어오면 room은 대기 상태가 된다. 두 번째 player가 같은 room에 들어오면
room은 별도 `StartBingoGameReq` 없이 자동으로 `Running` 상태가 되고 양쪽 client에
`BingoGameStartedNotify`를 보낸다. client는 game start를 확인한 뒤 3 x 3 card를
제출한다. 두 client의 card가 모두 제출되면 room Spot이 draw timer를 시작하고,
일정 간격으로 번호를 뽑아 양쪽 client에 `BingoNumberDrawnNotify`를 보낸다.

## 14. Server Draw Timer와 Bound Push 흐름

```mermaid
sequenceDiagram
    participant C1 as Client 1
    participant C2 as Client 2
    participant S as Session Server
    participant A1 as Player Actor 1
    participant A2 as Player Actor 2
    participant R as Bingo Room Spot

    R->>R: Draw timer tick
    R->>R: Draw number and mark cards
    R-->>A1: BingoNumberDrawnNotify
    R-->>A2: BingoNumberDrawnNotify
    A1->>S: Bound session send
    A2->>S: Bound session send
    S-->>C1: Stream BingoNumberDrawnNotify
    S-->>C2: Stream BingoNumberDrawnNotify
    R->>R: Detect winner
    R-->>A1: BingoGameEndedNotify
    R-->>A2: BingoGameEndedNotify
    S-->>C1: Stream BingoGameEndedNotify
    S-->>C2: Stream BingoGameEndedNotify
```

room Spot은 제출된 card, draw deck, mark, winner 판정을 한 모듈 안에 숨긴다.
client는 자기 card를 제출한 뒤 번호 추첨을 요청하지 않는다. 어떤 번호가 나오는지,
card가 어떻게 mark되는지, 승자가 누구인지는 서버 timer가 보낸 Notify와 state로
확인한다.

## 15. Disconnect와 actor destroy 흐름

Bingo 샘플은 stream disconnect와 actor destroy를 서로 다른 lifecycle로 보여 주어야 한다.
disconnect는 client stream session과 bound session 정리이고, actor destroy는 Play 서버의
actor 객체와 native actor ref를 제거하는 종료 작업이다. Session 서버 연결이 끊겼다는
이유만으로 room Spot에서 actor를 제거하거나 actor를 destroy하면 안 된다.

### 15.1 Disconnect 흐름

client stream이 끊기면 Session 서버는 현재 stream session에 묶인 actor binding을 닫는다.
이때 Play 서버 actor는 즉시 destroy되지 않는다. actor가 room에 들어가 있었다면 room state는
그대로 유지되고, Play 서버는 actor가 다시 bound session을 얻을 수 있는 상태로 둔다.
disconnect callback은 logging, bound session cleanup, 재접속 가능 상태 표시처럼 stream
연결에 한정된 작업만 맡는다.

언어별 샘플은 disconnect hook을 비워 두지 말고, 적어도 현재 actor/session 정리가 실행되는
경로가 드러나게 해야 한다. disconnect hook 안에서 room leave와 actor destroy를 직접 호출하지
않는다. room leave와 actor destroy는 아래의 게임 종료 흐름에서만 실행한다.

### 15.2 게임 종료 후 actor destroy 흐름

room Spot이 `BingoGameEndedNotify`를 양쪽 client에 전송한 뒤에는 room에 남아 있는 player
actor를 정리한다. 정리 순서는 모든 언어 샘플에서 아래와 같아야 한다.

1. actor 객체 생성이 끝나면 framework는 `onCreateActor`를 한 번 호출한다.
2. room Spot은 종료 cleanup이 한 번만 시작되도록 guard를 둔다.
3. room Spot은 각 player actor에 “Entry Spot으로 돌아오면 destroy한다”는 표시를 남긴다.
4. room Spot은 `leaveActor`로 actor를 room에서 내보낸다.
5. framework는 room `onLeaveActor`를 호출한 뒤 actor를 Entry Spot으로 이동시키고 Entry
   Spot `onJoinedActor`를 호출한다.
6. Entry Spot `onJoinedActor` 또는 Entry Spot handler는 actor의 destroy 표시를 확인하고
   Entry Spot context의 `destroyActor`를 호출한다.
7. `destroyActor`는 `onLeaveActor`나 다른 lifecycle callback을 호출하지 않고 actor 객체,
   native actor ref, framework registry, bound session binding을 정리한다.
8. 같은 actor에 대한 중복 destroy나 destroy 중 재진입은 성공 no-op이어야 하며,
   lifecycle callback을 다시 호출하면 안 된다.

```mermaid
sequenceDiagram
    participant R as Room Spot
    participant A as Player Actor
    participant E as Entry Spot
    participant N as Native Actor

    R->>A: Mark destroy after Entry Spot join
    R->>R: leaveActor(A)
    R->>R: onLeaveActor(A)
    A->>E: onJoinedActor(A)
    E->>E: destroyActor(A)
    E->>N: Destroy native actor ref
    E->>E: Remove actor registry and session binding
```

client self-check는 `BingoGameEndedNotify` 수신까지만 검증한다. actor destroy는 client가
직접 관찰하는 protocol 메시지가 아니므로 server-side evidence로 확인한다. 언어별
`run_sample` 또는 sample regression은 Play 서버 로그, fake backend call, runtime event,
또는 framework 테스트 중 하나로 아래 사실을 확인해야 한다.

- room Spot `onLeaveActor`가 각 player actor마다 실행된다.
- Entry Spot destroy가 각 player actor마다 완료된다.
- Entry Spot destroy 과정에서 Entry Spot `onLeaveActor`나 다른 lifecycle callback이
  추가로 실행되지 않는다.
- disconnect cleanup만으로 actor destroy가 실행되지 않는다.

## 16. 완료 기준

- client 두 개가 각각 Session 서버에 하나의 stream 연결만 연다.
- Session, API, Play 서버는 Registry/Discovery로 서로를 자동 발견한다.
- 두 client가 서로 다른 actor로 인증된다.
- Session 서버가 인증된 stream session을 Play 서버 actor에 bind한다.
- 첫 `MatchBingoReq`는 room을 만들고 waiting state를 반환한다.
- 두 번째 `MatchBingoReq`는 같은 room에 join하고 room을 자동 시작시킨다.
- 두 client는 game start를 확인한 뒤 `SubmitBingoCardReq`로 card를 제출한다.
- 두 card가 모두 제출되면 room Spot timer가 번호를 뽑고 각 player card mark를
  서버에서 갱신한다.
- 승자가 나오면 room state가 `Finished`가 되고 `Winners`가 채워진다.
- `BingoNumberDrawnNotify`와 `BingoGameEndedNotify`가 bound session을 통해 두 client에 전달된다.
- client inbound observer 로그에 request 응답과 server push 수신을 나타내는
  `stream-inbound` marker가 남는다.
- stream disconnect는 bound session을 정리하지만 actor를 즉시 destroy하지 않는다.
- 게임 종료 후 room Spot은 actor를 Entry Spot으로 leave시키고, Entry Spot은 actor를
  destroy한다.
- actor destroy는 `onLeaveActor`를 호출하지 않고 actor registry와 native actor ref를
  정리한다.
- client는 API 서버나 Play 서버 endpoint를 직접 사용하지 않는다.
- handler 등록은 typed handler 계약을 구성 코드에서 명시 등록하는 방식을 사용한다.
