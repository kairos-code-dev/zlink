# Framework Common Sample Scenarios

이 디렉토리는 모든 framework 언어가 공유하는 샘플 시나리오를 정의한다.
언어별 샘플은 구현 방식과 문법은 달라도 이 문서의 서버 역할, 메시지 흐름,
메시지 필드, 검증 기준에 맞춘다.

공통 샘플 문서는 언어별 guide가 아니라 framework 공통 spec 아래에 둔다. 같은 샘플을
.NET, Java, Node, C++ 등에서 구현할 때 한 언어 문서가 다른 언어의 사실상 기준이
되지 않게 하기 위해서다.

정본 7종(Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall,
ShoppingMall, GameQuest)은 `.NET` 샘플에서 검증된 흐름을 기준으로 삼되, 모든 framework 언어
(dotnet/java/kotlin/node/cpp)가 동일하게 코드와 문서로 구현한다. 언어별 framework를
구현할 때 같은 역할 분리, 같은 request/response/notify 이름, 같은 상태 필드, 같은
smoke 검증 순서를 따라야 한다. 언어별 API 모양은 달라도 사용자가 샘플을 읽었을 때
같은 framework 기능을 같은 순서로 확인할 수 있어야 한다.

## 샘플 목록

| 샘플 | 목적 | 서버 구성 | 연결 방식 | Handler 등록 방식 | 기본 payload codec |
|------|------|-----------|-----------|-------------------|--------------------|
| [Bingo](./bingo/README.ko.md) | session gateway, actor binding, Entry Spot, room Spot, timer, bound push를 한 흐름으로 보여 준다. | `Session`, `Api`, `Play`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler 계약 명시 등록 | Protobuf |
| [TicTacToe](./tictactoe/README.ko.md) | API 서버와 Play 서버만으로 가장 작은 실시간 게임 흐름을 보여 준다. | `Api` 역할 분리, 별도 `Session` 서버 없이 `Play`가 stream session을 함께 소유 | 수동 endpoint 연결 | 선언형 등록 우선, 불가능하면 명시 등록 | JSON |
| [SupportChat](./supportchat/README.ko.md) | 고객과 상담원이 같은 conversation Spot에서 대화하고, reconnect, idle timer, close, bound push를 확인한다. | `Session`, `Api`, `Support`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler와 domain event publisher | JSON |
| [DeliveryDispatch](./deliverydispatch/README.ko.md) | 배송 배차, timeout 재배정, 상태 fanout, 고객 stream push를 확인한다. | `DispatchApi`, `DispatchCenter`, `Courier`, `Tracking`, `Session`, `Registry` 분리 | Registry/Discovery 자동 연결 | channel handler, fanout subscriber, Spot actor join | JSON |
| ShoppingMall | commerce API가 주문을 시작하고 order workflow가 상태 전이와 projection을 처리한다. | `CommerceApi`, `OrderWorkflow`, `Registry` 분리 | Registry/Discovery 자동 연결 | workflow handler와 projection adapter | JSON |
| [ShoppingMall](./event/shoppingmall.ko.md) | 단일 Commerce API 서버 타입에서 event-sourced 주문 workflow와 projection을 구성한다. | `CommerceApi`, `Registry`, `OrderEventStore`, `OrderReadModelStore`, `CommerceStateStore` 분리 | Registry/Discovery 자동 연결 | event-sourced OrderWorkflow Spot, projection adapter | JSON |
| [GameQuest](./event/gamequest.ko.md) | stateless Game API action event를 ZLink fanout으로 받아 event sourced quest aggregate와 projection을 갱신한다. | `GameApi`, `QuestMission`, `Registry`, `QuestEventStore`, `QuestReadModelStore`, `GameplayStateStore` 분리 | Registry/Discovery 자동 연결 | fanout subscriber, event-sourced PlayerQuest Spot, projection adapter | JSON |

## 샘플 포팅 기준

Bingo와 TicTacToe는 각자 맡은 기능을 보여 주는 예외 샘플이다. Bingo는 Protobuf
payload와 Registry/Discovery 기반 gateway 분리를 보여 주고, TicTacToe는 작은 실시간
game 흐름을 수동 endpoint 연결로 보여 준다.

그 밖의 정본 샘플(SupportChat, DeliveryDispatch, ShoppingMall, GameQuest)은
아래 기준을 따른다.

- payload codec은 JSON을 기본으로 사용한다. 샘플끼리 payload를 비교하기 쉽고, event
  sourcing과 projection state를 사람이 읽기 쉬워야 하기 때문이다.
- Protobuf나 MessagePack이 필요한 샘플은 framework codec extension package를 설치하고
  구성 단계에서 extension을 등록한다. 샘플의 DTO, handler, client 호출 모양은 codec 때문에
  바꾸지 않는다.
- 서버 간 연결은 Registry/Discovery 기반 자동 연결로 구성한다. 샘플 코드가 endpoint
  연결 순서나 route warmup을 직접 관리하지 않게 하기 위해서다.
- framework가 handler를 스캔하고 등록할 수 있는 언어에서는 모든 handler를 자동 등록한다.
  샘플마다 handler 목록을 반복해서 적으면 public 사용 예시가 장황해지고, handler 추가
  누락을 client 시나리오가 늦게 발견하게 된다.
- C++ 샘플은 handler 자동 등록 예외다. C++ framework는 compile-time 타입과 명시 등록을
  기준으로 삼으므로, C++ 샘플은 같은 메시지·역할·JSON codec·Registry/Discovery 기준을
  유지하되 handler 등록은 해당 C++ public builder 표면에 맞게 명시한다.

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
- 실시간 상태를 소유하는 서버는 `Domain`, `Application`, `Adapters` 책임을 나누어 구현한다.
  아래 이름은 권장 구조이며, 디렉토리 이름은 언어 관용과 기존 샘플 구조에 맞게 바꿀 수 있다.
  다만 같은 책임 분리와 의존 방향은 유지해야 한다.
  - `Domain`은 순수 도메인 규칙, 상태 전이, 결과 판정, 도메인 event 생성을 맡는다.
    framework 타입, socket, stream, handler, logger, DI container에 의존하지 않는다.
  - `Application`은 room 생성, room 배정처럼 domain을 사용하는 use case를 맡는다.
    framework adapter가 호출할 수 있는 작고 명확한 진입점을 제공한다.
  - `Adapters`는 framework와 외부 연결을 맡는다. ZLink actor, session, Spot,
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
- Bingo와 TicTacToe는 같은 기능을 반복해서 보여 주지 않는다. Bingo는 Registry/Discovery를
  이용한 분리 gateway 구조를, TicTacToe는 수동 endpoint를 쓰는 직접 play 연결 구조를 맡는다.
- codec 선택은 샘플의 역할을 방해하지 않도록 단순하게 둔다. Bingo는 여러 언어가 공유하는
  schema가 분명한 Protobuf payload를 맡고, TicTacToe와 나머지 샘플은 읽고 비교하기 쉬운
  JSON payload를 기본으로 둔다. Bingo의 Protobuf 사용도 업무 API 차이가 아니라 dependency와
  framework codec extension 등록 차이로만 드러나야 한다.
- 식별자는 도메인 의미가 드러나게 이름 붙인다. 예를 들어 TicTacToe에서 client가 받는
  값은 임의의 core routing id hex 문자열이 아니라 명시적인 `RoomId`이며, room Spot
  routing id는 각 언어의 routing id 생성 API로 `RoomId` 문자열에서 만든다.

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
  Adapters/
    ZLink/
      Actors/
      Handlers/
      Sessions/
      Spots/
        Handlers/
      Notifications/
```

필요 없는 디렉토리는 생략할 수 있다. 예를 들어 TicTacToe는 별도 notification mapper가
필요 없으면 `Notifications/`를 두지 않아도 된다. 반대로 Bingo처럼 bound session
push와 domain event 변환이 필요하면 `Notifications/`를 둔다.

중요한 기준은 이름 자체가 아니라 의존 방향이다. `Domain`은 `Application`이나
`Adapters`를 알면 안 된다. `Application`은 domain을 사용하지만 framework transport
세부 구현에 기대지 않는다. `Adapters`는 framework 객체와 message codec, logging,
DI 등록을 다루며 domain state를 직접 조작하지 않고 domain 객체의 method를 호출한다.
