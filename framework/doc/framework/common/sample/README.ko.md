# Framework Common Sample Scenarios

이 디렉토리는 모든 framework 언어가 공유하는 샘플 시나리오를 정의한다.
언어별 샘플은 구현 방식과 문법은 달라도 이 문서의 서버 역할, 메시지 흐름,
메시지 필드, 검증 기준에 맞춘다.

공통 샘플 문서는 언어별 guide가 아니라 framework 공통 spec 아래에 둔다. 같은 샘플을
.NET, Java, Node, C++ 등에서 구현할 때 한 언어 문서가 다른 언어의 사실상 기준이
되지 않게 하기 위해서다.

정본 6종(Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall,
GameQuest)은 `.NET` 샘플에서 검증된 흐름을 기준으로 삼되, 모든 framework 언어
(dotnet/java/kotlin/node/cpp)가 동일하게 코드와 문서로 구현한다. 언어별 framework를
구현할 때 같은 역할 분리, 같은 request/response/notify 이름, 같은 상태 필드, 같은
smoke 검증 순서를 따라야 한다. 언어별 API 모양은 달라도 사용자가 샘플을 읽었을 때
같은 framework 기능을 같은 순서로 확인할 수 있어야 한다.

## 샘플 목록

| 샘플 | 목적 | 서버 구성 | 연결 방식 | Handler 등록 방식 | 기본 payload codec |
|------|------|-----------|-----------|-------------------|--------------------|
| [Bingo](bingo/README.ko.md) | session gateway, actor binding, Entry Spot, room Spot, timer, bound push를 한 흐름으로 보여 준다. | `Session`, `Api`, `Play` 분리 | location store 기반 자동 연결 | typed handler 계약 명시 등록 | Protobuf |
| [TicTacToe](tictactoe/README.ko.md) | 2개 API와 2개 Play로 수동 endpoint scale-out, Redis 기반 room route 조회, 실시간 게임 흐름을 보여 준다. | `Api` 2개, `Play` 2개, 별도 `Session` 서버 없이 `Play`가 stream session을 함께 소유 | 수동 endpoint 연결 + Redis room route store | 선언형 등록 우선, 불가능하면 명시 등록 | JSON |
| [SupportChat](supportchat/README.ko.md) | 고객과 상담원이 같은 conversation Spot에서 대화하고, reconnect, idle timer, close, bound push를 확인한다. | `Session`, `Api`, `Support` 분리 | location store 기반 자동 연결 | typed handler와 domain event publisher | JSON |
| [DeliveryDispatch](deliverydispatch/README.ko.md) | 배송 배차, timeout 재배정, 상태 push, 고객 stream push를 확인한다. | `Dispatch`, `CourierSession`, `CourierSpotNode` 2개, `Tracking`, `CustomerGateway` 분리 | location store 기반 자동 연결 | channel handler, Spot actor join | JSON |
| [ShoppingMall](event/shoppingmall.ko.md) | `CommerceApi`(HTTP edge)와 `OrderWorkflow`(주문 owner)를 분리해 event-sourced 주문 처리와 조회 모델을 구성한다. | `CommerceApi`, `OrderWorkflow` 분리 | location store 기반 자동 연결 | event-sourced OrderWorkflowSpot, 조회 모델 adapter | JSON |
| [GameQuest](event/gamequest.ko.md) | gameplay event를 player별 owner spot에 모아 event sourced quest aggregate와 조회 모델을 갱신한다. | `Session Server`, `PlayerQuestSpot` owner를 spot-mesh로 분산 | location store 기반 자동 연결 | owner routing handler, event-sourced PlayerQuestSpot, 조회 모델 adapter | JSON |

## 메시지 이름 원칙

샘플 메시지 이름은 도메인 사건 이름보다 framework 호출 방식이 먼저 드러나야 한다. 같은 업무
흐름이라도 request/reply인지, 단방향 send인지, client push인지에 따라 호출자가 기다리는 값과
handler 계약이 달라지기 때문이다. 언어별 샘플과 e2e는 아래 접미어를 같은 뜻으로 사용한다.

| 호출 방식 | 접미어 | 기준 |
|-----------|--------|------|
| request/reply | `Req` / `Res` | `Request(...)`, `RequestToChannel(...)`, route request, stream request, HTTP request처럼 응답을 기다리는 호출 |
| send | `Msg` | `Send(...)`처럼 응답 없이 전달하는 단방향 메시지 |
| client push | `Notify` | server가 stream/session으로 client에 밀어 주고 client가 기다려 받는 알림 |

request로 호출하는 메시지는 업무 이름이 `Changed`, `Accepted`, `Created`처럼 보여도 `Req`와
`Res` 쌍으로 이름 붙인다. 예를 들어 상태 변경을 요청하고 ack를 기다리는 흐름은
`DeliveryStatusChangedReq`와 `DeliveryStatusChangedRes`가 맞다. 반대로 server가 고객 client에
상태 변경을 밀어 주는 흐름은 `DeliveryStatusNotify`처럼 `Notify`를 사용한다.

`Event`, `Command`, `Result`, `Ack` 같은 접미어는 샘플의 wire message 이름으로 새로 늘리지
않는다. 이런 이름은 내부 도메인 event, 업무 명령, 처리 결과, transport 응답을 서로 섞어 보이게
할 수 있다. 이미 존재하는 샘플 메시지를 손볼 때도 호출 방식 기준으로 `Req`/`Res`, `Msg`,
`Notify` 중 하나로 정리한다.

이 규칙은 stream, channel, actor, Spot 경계를 실제로 넘나드는 ZLink wire message에
적용한다. 아래 두 경우는 wire message가 아니므로 예외로 둔다.

- **도메인 event stream(SoR) 레코드**: event sourcing 샘플(ShoppingMall, GameQuest)의
  `OrderStartedEvent`, `QuestProgressed`처럼 event store에 append되는 도메인 이벤트는 이 규칙의
  대상이 아니다. 이 이름은 그 자체로 전송되는 packet이 아니라 durable store 안에 쌓이는
  기록이며, event sourcing 어휘가 곧 도메인 표현이라 `Event` 접미어를 강제하지도, 금지하지도
  않는다 — 도메인이 자연스러운 이름(`OrderStartedEvent`, `QuestProgressed`)을 정한다.
- **in-process 도메인/application port 계약**: 같은 서버 프로세스 안에서 도메인 module을
  호출하는 port DTO(예: `ReserveInventoryCommand`)는 ZLink로 dispatch되지 않는 언어 중립
  계약이므로 `Command`/`Result` 접미어를 유지할 수 있다.

반대로 entry-spot에서 owner spot으로 실제 `SendToSpot`/`RequestToSpot`으로 전달되는
내부 메시지는 예외가 아니다. 이런 메시지는 호출 방식에 맞춰 `Msg`(one-way send) 또는
`Req`/`Res`(request/reply)로 이름 붙인다.

## 언어별 구현 수준

공통 시나리오는 샘플이 최종적으로 보여 주어야 하는 역할과 흐름을 정의한다. 다만 언어별 샘플은
현재 구현 단계가 다를 수 있으므로, 문서와 코드를 비교할 때 아래 구분을 먼저 확인한다.

| 샘플 | full 구조 구현 | compact 구현 |
|------|----------------|---------------|
| Bingo | .NET, Java, Kotlin, Node.js, C++ | 없음 |
| TicTacToe | .NET, Java, Kotlin, Node.js, C++ | 없음 |
| SupportChat | .NET, Java, Kotlin, Node.js, C++ | 없음 |
| DeliveryDispatch | .NET, Java, Kotlin, C++ | Node.js |
| ShoppingMall | .NET | Java, Kotlin, Node.js, C++ |
| GameQuest | .NET, Java, Kotlin | Node.js, C++ |

full 구조 구현은 공통 시나리오의 Spot owner, actor/session, fanout, location store 기반 자동
연결과 위치 조회 경계를 샘플 코드에 그대로 둔 구현이다. compact 구현은 같은 업무 흐름과 client self-check를 제공하지만,
일부 상태 소유 흐름을 일반 channel handler나 role service로 접어 넣은 구현이다. compact 구현을
full 구조라고 설명하면 안 된다. full 구조로 승격할 때는 먼저 해당 언어의 framework public API와
공통 시나리오 문서가 요구하는 Spot owner 경계를 맞춘 뒤 sample regression을 갱신한다.

## Spot yield dispatch 샘플 기준

Bingo의 Entry Spot match handler는 player actor 한 명의 입장 준비를 보여 주는 기준
샘플이다. 이 흐름은 player actor의 방 배정과 room Spot join처럼 입장 준비에 필요한
I/O를 기다린다. 언어별 sample의 방 배정 경로는 channel request일 수도 있고 Entry
Spot 내부 allocator일 수도 있지만, await 전후에 Entry Spot의 room list나 match queue
같은 공용 mutable state를 이어서 판단하지 않는 admission I/O라는 조건은 같다. 이 조건을
만족하는 Bingo match 흐름에는 yield 계열 terminator를 사용한다.

언어별 이름은 각 framework public API를 따른다. `.NET`은 `Yield(...)`, Java는
`yield(...)`, Kotlin은 `yield(call, ...)`, Node.js는 `yield(...)`,
C++은 `yield()`를 사용한다. TicTacToe의 game join처럼 handler가 게임 상태 흐름의
일부로 바로 이어지는 코드는 기본 terminator를 유지한다.

## 샘플 포팅 기준

Bingo와 TicTacToe는 각자 맡은 기능을 보여 주는 예외 샘플이다. Bingo는 Protobuf
payload와 location store 기반 gateway 분리를 보여 주고, TicTacToe는 Redis room route
store와 수동 endpoint 기반 scale-out 흐름을 보여 준다.

그 밖의 정본 샘플(SupportChat, DeliveryDispatch, ShoppingMall, GameQuest)은
아래 기준을 따른다.

- payload codec은 JSON을 기본으로 사용한다. 샘플끼리 payload를 비교하기 쉽고, event
  sourcing과 projection state를 사람이 읽기 쉬워야 하기 때문이다.
- Protobuf나 MessagePack이 필요한 샘플은 framework codec extension package를 설치하고
  구성 단계에서 extension을 등록한다. 샘플의 DTO, handler, client 호출 모양은 codec 때문에
  바꾸지 않는다.
- 서버 간 연결은 공유 location store 기반 자동 연결로 구성한다. 샘플 코드가 endpoint
  연결 순서나 route warmup을 직접 관리하지 않게 하기 위해서다.
- framework가 handler를 스캔하고 등록할 수 있는 언어에서는 모든 handler를 자동 등록한다.
  샘플마다 handler 목록을 반복해서 적으면 public 사용 예시가 장황해지고, handler 추가
  누락을 client 시나리오가 늦게 발견하게 된다.
- C++ 샘플은 handler 자동 등록 예외다. C++ framework는 compile-time 타입과 명시 등록을
  기준으로 삼으므로, C++ 샘플은 같은 메시지·역할·JSON codec·연결 방식을 유지하되 handler
  등록은 해당 C++ public builder 표면에 맞게 명시한다.

## Dispatch 오류 로그 기준

모든 언어별 샘플은 framework message dispatch 오류를 샘플 로그에 남겨야 한다.
등록되지 않은 request, payload decode 실패, handler 예외처럼 dispatch 단계에서
처리할 수 없는 메시지는 샘플 실행 중 바로 확인할 수 있어야 하기 때문이다.

샘플마다 자기 샘플 안에 observer 또는 handler를 둔다. Bingo, TicTacToe,
SupportChat 같은 서로 다른 샘플이 같은 helper 파일을 공유하지 않는다. 샘플은 독립적으로
읽고 옮길 수 있어야 하며, dispatch 오류 로그 코드가 다른 샘플의 디렉토리에 의존하면
그 기준이 깨진다.

로그 출력은 새 logging 체계를 만들지 않고 각 샘플이 이미 쓰는 logger를 따른다.
파일 로그를 이미 직접 쓰는 샘플은 그 파일 logger에 기록하고, 실행 스크립트가
stdout/stderr를 `logs/*.log`로 저장하는 샘플은 기존 console logger에 기록하면 된다.
로그 한 줄에는 적어도 `surface`, `messageKind`, `reason`, `action`, `packetName`,
`correlationId` 값을 포함한다. channel 경로에서는 `channelName`, Spot 경로에서는
`spotRid`, actor 경로에서는 `actorId`처럼 surface에 맞는 식별자를 함께 남긴다. 운영자가
메시지 등록 누락인지, payload decode 실패인지, handler 예외인지 빠르게 구분할 수 있어야
하기 때문이다.

샘플은 각 서버 프로세스의 `AddZLinkFramework(...)` 설정에서
`ConfigureDispatch().SetMessageFlowObserver<...>()`를 등록하거나 message-flow 로그 파일을
지정한다. `run_sample.sh`와 `run_sample.ps1`은 프로세스 출력을 `logs/*.log`에 저장하므로,
message-flow error 줄도 같은 샘플 로그 파일에서 확인한다.

샘플 handler는 framework가 처리하는 dispatch 오류를 handler 안에서 다시 잘게
처리하지 않는다. request, actor request, session packet handler 안에서 예외를 잡아
로그만 남긴 뒤 다시 던지거나, domain 예외를 임의의 `error` 필드 응답으로 바꾸지
않는다. 그런 예외는 framework dispatch 경계가 error reply, drop, dispatch error
observer, 기본 로그로 처리하게 둔다. 샘플 handler는 성공 경로와 도메인 동작을
보여 주는 데 집중해야 하며, 실패를 정상 업무 응답으로 바꾸는 코드는 해당 메시지
계약이 명시적으로 그런 실패 상태를 정의할 때만 둔다.

## 공통 작성 원칙

- 샘플은 framework가 어떤 일을 대신해 주는지 보여 주어야 한다.
- 도메인 규칙은 작게 유지하고, session, actor, Spot, channel, timer, push 흐름이
  코드에서 잘 보이게 둔다.
- 샘플 애플리케이션 코드는 각 언어 framework가 공개한 package entrypoint, DI token,
  builder, client interface만 사용한다. `internal`, `runtime`, `dist/runtime`처럼
  유지보수용 구현 위치를 직접 import하거나 reflection으로 접근하지 않는다. 필요한 기능이
  공개 계약에 없으면 샘플에서 우회하지 않고 framework의 public contract를 먼저 보완한다.
- 이 문서에서 `Spot`은 독립적인 생명주기를 가지는 stateful coordination point를 뜻한다.
  Spot은 room, conversation, workflow instance, player quest처럼 상태와 이벤트가 모이는
  단위를 표현한다. Spot은 actor 참여를 받을 수 있지만 actor가 필수는 아니다. Spot은
  directed request를 처리하거나, event를 publish하거나, timer를 실행하거나, pub/sub event에
  반응할 수 있다.
- 실시간 상태를 소유하는 서버는 `Domain`, `Application`, `Infrastructure` 책임을 나누어 구현한다.
  아래 이름은 권장 구조이며, 디렉토리 이름은 언어 관용과 기존 샘플 구조에 맞게 바꿀 수 있다.
  다만 같은 책임 분리와 의존 방향은 유지해야 한다.
  - `Domain`은 순수 도메인 규칙, 상태 전이, 결과 판정, 도메인 event 생성을 맡는다.
    framework 타입, socket, stream, handler, logger, DI container에 의존하지 않는다.
  - `Application`은 room 생성, room 배정처럼 domain을 사용하는 use case를 맡는다.
    framework adapter가 호출할 수 있는 작고 명확한 진입점을 제공한다.
  - `Infrastructure`는 framework와 외부 연결을 맡는다. ZLink actor, session, Spot,
    handler, notification publisher, channel request handler는 이 레이어에 둔다.
- 언어별 샘플은 같은 역할과 메시지 이름을 사용한다. 언어 관용구 때문에 이름을
  바꿔야 하면 공통 문서에 차이를 먼저 기록한다.
- 클라이언트에서 실제 서버에 접속해 request, push, final state를 확인하는 흐름은
  `ClientScenario` 이름으로 둔다. 예를 들어 `BingoClientScenario`처럼 샘플 이름과
  client scenario 역할이 함께 드러나야 한다. `TestScenario`는 별도 테스트 fixture로
  오해될 수 있으므로 샘플 client 실행 흐름의 이름으로 쓰지 않는다.
- 공통 문서의 메시지 계약은 언어 중립 schema로 읽는다. 언어별 샘플은 record,
  class, struct, interface, type alias처럼 자기 언어에 맞는 표현으로 같은 필드와
  의미를 구현한다.
- channel, route, stream, actor, Spot 경계를 넘는 wire message는 이름 있는 계약으로
  둔다. Python `dict` 나 Node.js object literal 처럼 동적 객체를 쉽게 만들 수 있는
  언어에서도 호출 지점에 `{ ... }` 를 바로 쓰거나 packet name 문자열을 흩어 놓지
  않는다. 요청, 응답, 알림 payload는 `Shared/Contracts` 같은 공용 계약 위치에
  message type 또는 schema로 두고, client와 server는 그 객체의 public interface만
  사용해야 한다.
- codec별 편의 wrapper나 샘플 전용 helper로 message 객체의 계약을 감추면 안 된다.
  JSON, MessagePack, Protobuf 중 어떤 codec을 쓰더라도 샘플 코드는 connector와
  message 객체가 제공하는 public interface를 직접 사용해야 한다. connector 전용 codec
  package나 bindings codec package를 샘플의 표준 사용법으로 안내하지 않는다.
- inline object literal은 한 함수 안에서만 쓰는 local state, 테스트 보조 값, 파싱 결과처럼
  wire 계약이 아닌 값에만 사용한다. 샘플은 짧은 데모보다 여러 언어에서 같은 메시지
  흐름을 비교할 수 있는 가시성을 우선한다.
- Bingo와 TicTacToe는 같은 기능을 반복해서 보여 주지 않는다. Bingo는 공유 location store를
  이용한 분리 gateway 구조를, TicTacToe는 수동 endpoint와 Redis room route store를 쓰는
  scale-out 구조를 맡는다.
- codec 선택은 샘플의 역할을 방해하지 않도록 단순하게 둔다. Bingo는 여러 언어가 공유하는
  schema가 분명한 Protobuf payload를 맡고, TicTacToe와 나머지 샘플은 읽고 비교하기 쉬운
  JSON payload를 기본으로 둔다. Bingo의 Protobuf 사용도 업무 API 차이가 아니라 dependency와
  framework codec extension 등록 차이로만 드러나야 한다.
- 식별자는 도메인 의미가 드러나게 이름 붙인다. 예를 들어 TicTacToe에서 client가 받는
  값은 임의의 core routing id hex 문자열이 아니라 명시적인 `RoomId`이며, room Spot
  routing id는 각 언어의 routing id 생성 API로 `RoomId` 문자열에서 만든다.
- 모든 샘플의 routing id는 샘플 애플리케이션이 명시적으로 정한 문자열에서 만든다.
  node, Spot, room, conversation, workflow instance처럼 메시지나 로그에 드러나는 식별자는
  `play-node-1`, `bingo-room-...`, `supportchat-conversation-...`처럼 사람이 읽을 수 있는
  샘플 ID를 그대로 사용한다. framework가 자동 배정한 Spot routing id나 core routing id의
  hex 표현을 샘플 계약으로 노출하지 않는다. 따라서 샘플 코드는 hex로 직렬화한 값을
  다시 `FromHex`/`fromHex`로 복원하는 흐름을 쓰지 않고, 각 언어의 일반 routing id 생성
  API(`RoutingId.From(...)`, `RoutingId.from(...)`, 문자열 routing id 등)로 샘플 ID를
  routing id로 만든다.

## Client self-check 기준

Bingo와 TicTacToe는 `.NET` 샘플의 client 검증 흐름을 기준으로 삼는다. 샘플 client는
성공 로그를 출력하는 데서 끝나면 안 된다. request로 보낸 값이 response와 push payload에
같은 의미로 돌아오는지 직접 확인해야 한다.

언어별 client는 아래 항목을 반드시 검증한다.

- 인증 요청에 사용한 token 또는 actor id가 인증 응답의 actor id와 일치한다.
- room 생성이나 matching 요청이 반환한 식별자, endpoint, state status가 요청 시나리오와
  일치한다. `GameName`처럼 특정 샘플에만 있는 필드는 해당 샘플에서만 확인한다.
- 첫 번째 참가자는 waiting 상태를 받고, 두 번째 참가자는 running 또는 in-progress 상태를
  만든다.
- 자기 자신에게 보내면 안 되는 join notify는 받지 않았음을 확인한다.
- 상대 참가자의 join notify는 actor id, room id, state status를 확인한다. TicTacToe의
  `Mark`처럼 특정 샘플에만 있는 필드는 해당 샘플에서 추가로 확인한다.
- game start, move, draw, ended notify는 단순 수신 개수가 아니라 payload 안의 board,
  turn, draw sequence, winner, player list 같은 의미 값을 확인한다.
- deterministic sample은 마지막 winner와 최종 state를 고정값으로 확인한다.

push message 대기는 sample-local polling 함수가 아니라 stream connector의 public
interface를 사용한다. 예를 들어 `.NET` 샘플의 `WaitFor<TPayload>().Where(...).Async(...)`
처럼 connector 객체가 제공하는 wait API를 직접 호출한다. codec별 JSON, MessagePack,
Protobuf wrapper나 샘플 전용 함수 뒤에 대기 흐름을 숨기면 안 된다. sample은 connector가
반환한 message 객체의 public interface로 payload를 읽고 바로 검증한다. notification
수집용 inbox나 로그 queue는 결과 출력과 추가 검증을 위해 둘 수 있지만, push 도착을
기다리는 기준 경로가 되어서는 안 된다.

## 상태 소유 서버 공통 디렉토리 구조

언어별 문법과 build system은 달라도 상태를 소유하는 서버 소스는 아래 구조를 기준으로 맞춘다.

```text
Server/<StateOwner>/
  Domain/
    <DomainName>/
      ... pure domain rules ...
  Application/
    <UseCase>/
      ... use case services ...
  Infrastructure/
    ZLink/
      Actors/
      Handlers/
      Sessions/
      Spots/
        EntrySpot/
          Handlers/
        <DomainSpot>/
          Handlers/
          Notifications/
```

필요 없는 디렉토리는 생략할 수 있다. Entry Spot이 없으면 `EntrySpot/`를 두지 않아도 되고,
별도 notification mapper가 필요 없으면 `<DomainSpot>/Notifications/`를 두지 않아도 된다.
반대로 Bingo처럼 bound session push와 domain event 변환이 필요하면 해당 domain Spot 아래에
`Notifications/`를 둔다.

중요한 기준은 이름 자체가 아니라 의존 방향이다. `Domain`은 `Application`이나
`Infrastructure`를 알면 안 된다. `Application`은 domain을 사용하지만 framework transport
세부 구현에 기대지 않는다. `Infrastructure`는 framework 객체와 message codec, logging,
DI 등록을 다루며 domain state를 직접 조작하지 않고 domain 객체의 method를 호출한다.
