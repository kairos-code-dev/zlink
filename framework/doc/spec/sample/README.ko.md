# Framework Common Sample Scenarios

이 디렉토리는 모든 framework 언어가 공유하는 샘플 시나리오를 정의한다.
언어별 샘플은 구현 방식과 문법은 달라도 이 문서의 서버 역할, 메시지 흐름,
메시지 필드, 검증 기준에 맞춘다.

공통 샘플 문서는 언어별 guide가 아니라 framework 공통 spec 아래에 둔다. 같은 샘플을
.NET, Java, Node, C++ 등에서 구현할 때 한 언어 문서가 다른 언어의 사실상 기준이
되지 않게 하기 위해서다.

Bingo와 TicTacToe 공통 시나리오는 `.NET` 샘플에서 검증된 흐름을 반영한다.
SupportChat과 CheckoutFlow는 추가 공통 샘플 시나리오이며, 언어별 framework를 구현할
때 같은 역할 분리, 같은 request/response/notify 이름, 같은 상태 필드, 같은 smoke 검증
순서를 따라야 한다. 언어별 API 모양은 달라도 사용자가 샘플을 읽었을 때 같은 framework
기능을 같은 순서로 확인할 수 있어야 한다.

## 샘플 목록

| 샘플 | 목적 | 서버 구성 | 연결 방식 | Handler 등록 방식 |
|------|------|-----------|-----------|-------------------|
| [Bingo](./bingo/README.ko.md) | session gateway, actor binding, Entry Spot, room Spot, timer, bound push를 한 흐름으로 보여 준다. | `Session`, `Api`, `Play`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler 계약 명시 등록 |
| [TicTacToe](./tictactoe/README.ko.md) | API 서버와 Play 서버만으로 가장 작은 실시간 게임 흐름을 보여 준다. | `Api` 역할 분리, 별도 `Session` 서버 없이 `Play`가 stream session을 함께 소유 | 수동 endpoint 연결 | 선언형 등록 우선, 불가능하면 명시 등록 |
| [SupportChat](./supportchat/README.ko.md) | 고객과 상담원이 같은 conversation Spot에서 대화하고, reconnect, idle timer, close, bound push를 확인한다. | `Session`, `Api`, `Support`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler와 domain event publisher |
| [CheckoutFlow](./checkoutflow/README.ko.md) | 일반 backend의 service call, microservice event 전파, workflow state, client notify를 한 흐름으로 보여 준다. | `Session`, `Api`, `Inventory`, `Payment`, `Order`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler와 fanout event subscriber |

## 공통 작성 원칙

- 샘플은 framework가 어떤 일을 대신해 주는지 보여 주어야 한다.
- 도메인 규칙은 작게 유지하고, session, actor, Spot, channel, timer, push 흐름이
  코드에서 잘 보이게 둔다.
- 실시간 상태를 소유하는 서버는 `Domain`, `Application`, `Adapters` 레이어를 나누어 구현한다.
  디렉토리 이름은 언어 관용에 맞게 바꿀 수 있지만 같은 책임 분리는 유지해야 한다.
  - `Domain`은 순수 도메인 규칙, 상태 전이, 결과 판정, 도메인 event 생성을 맡는다.
    framework 타입, socket, stream, handler, logger, DI container에 의존하지 않는다.
  - `Application`은 room 생성, room 배정처럼 domain을 사용하는 use case를 맡는다.
    framework adapter가 호출할 수 있는 작고 명확한 진입점을 제공한다.
  - `Adapters`는 framework와 외부 연결을 맡는다. ZLink actor, session, Spot,
    handler, notification publisher, channel request handler는 이 레이어에 둔다.
- 언어별 샘플은 같은 역할과 메시지 이름을 사용한다. 언어 관용구 때문에 이름을
  바꿔야 하면 공통 문서에 차이를 먼저 기록한다.
- 공통 문서의 메시지 계약은 언어 중립 schema로 읽는다. 언어별 샘플은 record,
  class, struct, interface, type alias처럼 자기 언어에 맞는 표현으로 같은 필드와
  의미를 구현한다.
- channel, route, stream, actor, Spot 경계를 넘는 wire message는 이름 있는 계약으로
  둔다. Python `dict` 나 Node.js object literal 처럼 동적 객체를 쉽게 만들 수 있는
  언어에서도 호출 지점에 `{ ... }` 를 바로 쓰거나 packet name 문자열을 흩어 놓지
  않는다. 요청, 응답, 알림 payload factory 또는 schema를 `Shared/Contracts` 같은
  공용 계약 위치에 두어야 한다.
- inline object literal은 한 함수 안에서만 쓰는 local state, 테스트 보조 값, 파싱 결과처럼
  wire 계약이 아닌 값에만 사용한다. 샘플은 짧은 데모보다 여러 언어에서 같은 메시지
  흐름을 비교할 수 있는 가시성을 우선한다.
- Bingo와 TicTacToe는 같은 기능을 반복해서 보여 주지 않는다. Bingo는 Registry/Discovery를
  이용한 분리 gateway 구조를, TicTacToe는 수동 endpoint를 쓰는 직접 play 연결 구조를 맡는다.
- 식별자는 도메인 의미가 드러나게 이름 붙인다. 예를 들어 TicTacToe에서 client가 받는
  값은 임의의 core routing id hex 문자열이 아니라 명시적인 `RoomId`이며, room Spot
  routing id는 각 언어의 routing id 생성 API로 `RoomId` 문자열에서 만든다.

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
