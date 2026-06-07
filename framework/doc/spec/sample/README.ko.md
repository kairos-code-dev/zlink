# Framework Common Sample Scenarios

이 디렉토리는 모든 framework 언어가 공유하는 샘플 시나리오를 정의한다.
언어별 샘플은 구현 방식과 문법은 달라도 이 문서의 서버 역할, 메시지 흐름,
검증 기준을 기준으로 맞춘다.

공통 샘플 문서는 언어별 guide가 아니라 framework 공통 spec 아래에 둔다. 같은 샘플을
.NET, Java, Node, C++ 등에서 구현할 때 한 언어 문서가 다른 언어의 사실상 기준이
되지 않게 하기 위해서다.

## 샘플 목록

| 샘플 | 목적 | 서버 구성 | 연결 방식 | Handler 등록 방식 |
|------|------|-----------|-----------|-------------------|
| [Bingo](./bingo/README.ko.md) | session gateway, actor binding, Entry Spot, room Spot, timer, bound push를 한 흐름으로 보여 준다. | `Session`, `Api`, `Play`, `Registry` 분리 | Registry/Discovery 자동 연결 | typed handler 계약 명시 등록 |
| [TicTacToe](./tictactoe/README.ko.md) | API 서버와 Play 서버만으로 가장 작은 실시간 게임 흐름을 보여 준다. | `Api` 역할 분리, `Session`과 `Play` 역할 통합 | 수동 endpoint 연결 | 선언형 등록 우선, 불가능하면 명시 등록 |

## 공통 작성 원칙

- 샘플은 framework가 어떤 일을 대신해 주는지 보여 주어야 한다.
- 게임 규칙은 작게 유지하고, session, actor, Spot, channel, timer, push 흐름이
  코드에서 잘 보이게 둔다.
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
