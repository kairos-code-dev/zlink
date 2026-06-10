# Framework Sample Scenario 구현 계획

이 문서는 공통 sample scenario 문서를 실제 언어별 framework sample로 옮길 때 쓰는
실행 계획이다. 공개 API 계약이나 sample scenario 자체를 새로 정의하지 않는다.
scenario의 기준은 `framework/doc/spec/sample/` 아래 문서이며, 이 문서는 구현 순서,
공통 선행 작업, 검증 기준, 완료 판정을 고정한다.

## 1. 목표

최종 목표는 아래 세 sample을 같은 품질 기준으로 구현하는 것이다.

1. [SupportChat](../spec/sample/supportchat/README.ko.md)
2. [ShoppingMallCheckout](../spec/sample/event/shoppingmall-checkout.ko.md)
3. [GameQuest](../spec/sample/event/gamequest.ko.md)

각 sample은 언어별 문법과 framework 관용은 달라도 같은 서버 역할, 같은 message 이름,
같은 client scenario, 같은 self-check 의미를 가져야 한다. 구현 중 sample scenario와
코드가 충돌하면 먼저 scenario 문서를 다시 읽고, scenario 문서가 실제 구현에 부족하면
scenario 문서를 보강한 뒤 구현한다.

## 2. 비목표

- 이 문서는 새 framework public API를 설계하지 않는다.
- Redis Stream, Kafka, MongoDB 같은 외부 제품을 반드시 붙이는 계획이 아니다.
- 세 sample을 하나의 거대한 공통 harness로 묶지 않는다.
- sample 흐름을 숨기는 shared helper를 늘리는 계획이 아니다.
- 모든 언어를 한 번에 끝내는 계획이 아니다. 언어별 구현은 이 계획을 기준으로
  자기 언어의 실행 계획을 더 좁게 만들 수 있다.

## 3. 기준 문서

구현자는 아래 문서를 먼저 읽는다.

1. 공통 sample 기준
   - [공통 sample 목록](../spec/sample/README.ko.md)
   - [SupportChat scenario](../spec/sample/supportchat/README.ko.md)
   - [ShoppingMallCheckout scenario](../spec/sample/event/shoppingmall-checkout.ko.md)
   - [GameQuest scenario](../spec/sample/event/gamequest.ko.md)
2. framework 공통 스펙
   - [Framework API](../spec/framework-api.ko.md)
   - [Interaction model](../spec/interaction-model.ko.md)
   - [Message model](../spec/message-model.ko.md)
   - [Channel topology](../spec/channel-topology.ko.md)
   - [Actor model](../spec/actor-model.ko.md)
   - [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md)
3. 구현 품질 기준
   - [POSD 설계 원칙](../../../doc/principal/software-design-principles.md)

## 4. 전체 구현 순서

권장 순서는 아래와 같다.

1. `SupportChat`
2. `ShoppingMallCheckout`
3. `GameQuest`

`SupportChat`을 먼저 구현하는 이유는 session, stream, actor binding, conversation
Spot, bound push, reconnect, idle close처럼 framework의 실시간 연결 기능을 직접
검증하기 때문이다. 이 sample이 안정되면 stream client, push wait helper, sample
runner, multi-client self-check의 기본 형태가 잡힌다.

`ShoppingMallCheckout`은 그 다음에 구현한다. 이 sample은 WebSocket이나 actor session을
사용하지 않고 HTTP API, Spot owner routing, event store, projection만으로 workflow를
보여 준다. 앞 단계와 대조되기 때문에 "ZLink가 항상 client stream을 필요로 한다"는
오해를 피할 수 있다.

`GameQuest`는 마지막에 구현한다. 이 sample은 stateless Game API, gameplay event fanout,
event-sourced quest aggregate, projection, WebSocket notify가 함께 들어간다. 앞의 두
sample에서 검증한 stream, Spot, projection, self-check 형태를 조합해야 하므로 가장
뒤에 두는 편이 좋다.

## 5. 공통 선행 작업

세 sample을 시작하기 전에 아래 항목을 확인한다.

| 항목 | 확인 기준 |
|------|-----------|
| server role 실행 | sample runner가 여러 server process를 안정적으로 시작하고 종료할 수 있다. |
| readiness | 임의 sleep으로 readiness를 숨기지 않는다. server가 준비되었음을 확인하는 명시적 marker나 health check가 있어야 한다. |
| client scenario | Bingo sample처럼 client code가 시나리오 테스트로 읽혀야 한다. helper 안에 business flow를 숨기지 않고 request, response 검증, push 대기, 상태 조회가 순서대로 보여야 한다. |
| message contract | wire message는 이름 있는 contract로 둔다. 호출 지점에 임시 object literal이나 문자열 packet name을 흩어 놓지 않는다. |
| wait helper | stream push를 기다릴 때는 connector public helper를 사용한다. sample-local inbox가 기준 경로가 되면 안 된다. |
| state owner structure | 상태를 소유하는 server는 `Domain`, `Application`, `Adapters` 책임을 나눈다. |
| self-check output | 성공 로그만으로 완료하지 않는다. response와 push payload 또는 projection 값의 의미를 직접 검증한다. |
| smoke evidence | client 성공뿐 아니라 server log나 state store evidence로 실제 server path가 실행되었음을 확인한다. |

## 6. Bingo식 client scenario 기준

`SupportChat`, `ShoppingMallCheckout`, `GameQuest` 세 문서는 모두 Bingo sample의 client
검증 흐름과 같은 방식으로 작성되어야 한다. 여기서 "Bingo식"은 게임 도메인을 뜻하지
않고, client self-check가 실제 사용 흐름처럼 순서대로 읽히는 형식을 뜻한다.

필수 기준은 아래와 같다.

- client 생성, server process 실행, 기본 endpoint 설정은 scenario 함수 밖에서 준비할 수 있다.
- scenario 함수 안에서는 실제 client 흐름만 보이게 한다.
- request 호출은 helper 뒤에 숨기지 않는다. `AuthenticateReq`, `OpenConversationReq`,
  `StartOrderReq`, `KillMonsterReq` 같은 업무 request가 코드에서 직접 보여야 한다.
- response 검증은 마지막에 모으지 않고 request 직후 수행한다.
- server가 먼저 보내는 push는 해당 단계에서 기다리고, 받은 payload의 의미 값을 즉시 검증한다.
- storage, server log, event stream 검증은 client가 storage endpoint를 직접 호출하는
  방식으로 만들지 않는다. sample runner나 server-side assertion이 검증하고, client
  scenario 함수에는 사용자-facing request, response, push, projection 조회만 남긴다.
- 순서가 확정된 push는 순서대로 기다린다.
- 순서가 확정되지 않은 push는 "필요한 payload 집합"을 먼저 정의하고, 각 client가 그
  집합을 모두 받을 때까지 기다리는 방식으로 검증한다.
- 결과 report 객체를 만들더라도 검증의 기준 경로로 쓰지 않는다. 검증은 scenario 중간에
  실패해야 한다.
- sample-local inbox나 queue는 보조 기록으로만 둘 수 있다. push 도착 대기는 connector
  public wait helper를 사용한다.
- 시나리오 설명 문서의 단계와 client code의 단계가 같은 순서로 대응되어야 한다.

세 sample을 구현하기 전에 각 scenario 문서의 `Client 시나리오 작성 기준` 절을 이 기준으로
다시 확인한다. 부족한 단계가 있으면 구현보다 문서 보강을 먼저 한다.

## 7. Sample별 구조 기준

디렉토리 구조는 이 plan에서 새로 정의하지 않는다. 각 sample scenario 문서에 이미 있는
서버 구성, DDD/hexagonal 구조, adapter 위치를 기준으로 구현한다. 언어별 관용 때문에
이름을 조금 바꿔야 하면 책임과 의존 방향은 유지하고, 변경 이유를 언어별 구현 계획이나
sample README에 기록한다.

## 8. Sample별 구현 기준

sample별 상세 구현 내용은 각 scenario 문서에만 둔다. 이 plan은 같은 내용을 반복하지
않고, 구현자가 어떤 문서를 기준으로 삼아야 하는지만 고정한다.

| Sample | 기준 문서 | 구현자가 확인할 절 |
|--------|-----------|--------------------|
| `SupportChat` | [SupportChat scenario](../spec/sample/supportchat/README.ko.md) | 서버 구성, 메시지 계약, 흐름, `Client 시나리오 작성 기준`, 구현 완료 기준 |
| `ShoppingMallCheckout` | [ShoppingMallCheckout scenario](../spec/sample/event/shoppingmall-checkout.ko.md) | 서버 구성, DDD와 Hexagonal 구조, 메시지 계약, `Client 시나리오 작성 기준`, 구현 완료 기준 |
| `GameQuest` | [GameQuest scenario](../spec/sample/event/gamequest.ko.md) | 서버 구성, DDD와 Hexagonal 구조, 메시지 계약, `Client 시나리오 작성 기준`, 구현 완료 기준 |

각 sample을 구현하기 전에는 해당 문서의 `Client 시나리오 작성 기준` 절을 먼저 읽고,
client self-check 구조를 확정한다. 그 다음 서버 구조와 메시지 계약을 구현한다. 구현
중 새로운 판단이 필요하면 이 plan에 상세 내용을 추가하지 않고, 해당 scenario 문서를
보강한다.

## 9. Sample별 주의점

이 절은 중복 구현 목록이 아니라 sample 간 경계만 기록한다.

- `SupportChat`은 session, actor binding, conversation Spot, bound push를 보여 준다.
- `ShoppingMallCheckout`은 client stream 없이 HTTP API, Spot owner routing, event store,
  projection만 보여 준다.
- `GameQuest`는 stateless Game API event와 QuestMission의 fanout/event-sourced Spot을
  함께 보여 준다.

## 10. 언어별 구현 계획 작성 규칙

특정 언어에서 이 세 sample을 구현할 때는 이 문서를 그대로 복사하지 않는다. 대신 해당
언어의 `framework/languages/<lang>/doc/draft/` 또는 그 언어가 쓰는 plan 위치에 더 좁은
문서를 만든다.

언어별 plan에는 아래 항목을 반드시 적는다.

- 구현 대상 sample 목록
- 현재 언어가 이미 지원하는 framework 기능
- 빠진 framework 기능과 선행 구현 항목
- sample별 project 또는 package 구조
- sample runner 명령
- build, unit test, integration test, sample smoke 명령
- 완료하지 못한 항목을 숨기지 않는 known issue 목록

## 11. 검증 순서

각 sample은 아래 순서로 검증한다.

1. build
2. contract compile 또는 schema check
3. domain unit test
4. application use case test
5. adapter integration test
6. sample client self-check
7. server log evidence 확인
8. full sample runner
9. `git diff --check`

중간 단계가 실패하면 다음 단계로 넘어가지 않는다. readiness 실패를 sleep으로 숨기지
않고, 실제 연결 또는 handler path의 버그로 보고 원인을 수정한다.

## 12. 리뷰 체크리스트

구현 후 아래 항목을 다시 확인한다.

- sample scenario 문서의 message contract와 구현 contract가 같은가
- client code가 시나리오 테스트처럼 읽히는가
- business flow가 helper에 숨어 있지 않은가
- 상태 전이 규칙이 `Domain` 안에 있고 handler가 직접 상태를 조작하지 않는가
- framework type이 `Domain`에 새어 들어가지 않았는가
- push가 필요한 sample과 필요 없는 sample의 경계가 분명한가
- event store와 projection의 역할이 섞이지 않았는가
- retry, dedupe, terminal state, reconnect 또는 delayed query 보정이 검증되는가
- server log evidence가 실제 server path를 증명하는가

## 13. 최종 완료 기준

세 sample 전체 완료 기준은 아래와 같다.

- `SupportChat`, `ShoppingMallCheckout`, `GameQuest`가 모두 기준 scenario 문서를 따른다.
- 세 sample의 client scenario는 Bingo처럼 request, response 검증, push 대기 또는
  projection 조회가 순서대로 읽힌다.
- 세 sample 모두 client self-check가 성공한다.
- 세 sample 모두 server-side evidence가 남는다.
- 각 sample의 DDD와 adapter 경계가 리뷰를 통과한다.
- 특정 sample에 필요 없는 framework 기능이 억지로 섞이지 않는다.
- 공통 sample README와 개별 scenario 문서가 구현 결과와 충돌하지 않는다.
- 언어별 sample runner가 세 sample을 독립 실행할 수 있다.
- 전체 검증 명령이 통과한다.
