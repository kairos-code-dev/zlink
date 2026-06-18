# SupportChat Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — SupportChat](../../../common/sample/supportchat/README.ko.md)다.
> 이 문서는 같은 시나리오를 Node/NestJS framework 표면으로 구체화한다. payload codec은 JSON이다.

## 1. 목적

고객과 상담원이 같은 conversation Spot에서 대화하는 고객 지원 채팅이다. session gateway,
actor binding, conversation Spot, idle timer, close, 양방향 bound push, reconnect를
한 흐름으로 보여 준다. Bingo와 같은 gateway 구조를 쓰되 게임 규칙 대신 업무형 대화 상태,
재접속, 상담 종료 흐름을 다룬다.

## 2. 서버 구성

`Session`·`Api`·`Support`·`Registry` 분리. `Support`가 conversation Spot(`ConversationSpot`)과
Entry Spot, customer/agent actor, idle/close timer, domain event publisher를 가진 상태 소유
서버다. 네 서버는 Registry/Discovery로 자동 발견한다. customer client와 agent client는 모두
`Session` STREAM endpoint 하나만 연다.

## 3. 전체 흐름

1. customer/agent client는 각각 `Session` STREAM 연결 하나만 연다.
2. 인증 후 customer는 conversation 생성을 요청한다. agent가 join 하면 `Active`가 된다.
   customer는 `ParticipantJoinedNotify`, agent는 `ConversationAssignedNotify`를 받는다.
3. `ConversationSpot`이 단조 증가 `MessageSeq`를 부여하고 상대방에게 `ChatMessageNotify`/
   `TypingChangedNotify`를 push 한다.
4. idle deadline 초과 시 `ConversationIdleNotify`, close grace 후 `ConversationClosedNotify`를
   양쪽 bound session에 push 한다. timer는 신호만 전달하고 전이 판정은 domain이 한다.
5. 같은 `ActorId`가 재인증하면 새 actor 없이 stream session binding만 갱신한다(reconnect).

## 4. 상태 모델

conversation은 `WaitingForAgent → Active → WaitingForClose → Closed`로 전이한다. 배정 가능한
agent가 없으면 `WaitingForAgent`로 남고 오류 response가 아니다. closed conversation에 보낸
메시지·typing·close는 오류 response를 반환한다.

## 5. 호출 표면 (객체-메시징)

high-level 호출은 업무 객체를 직접 주고받고, codec(JSON)·packet name은 framework 내부가
처리한다. handler는 `@zlinkRequestHandler` 계열 decorator로 등록하고 typed reply 객체를
반환한다. 호출부에서 `Message.from(...)`/`.toJson()`을 쓰지 않는다.

```ts
const opened = await client
  .requestToChannel("support", new OpenConversationReq(subject))
  .submit<OpenConversationRes>();
```

## 6. Client self-check

agent greeting `MessageSeq = 1`, customer 답변 `MessageSeq = 2`. reconnect 시 같은 actor와
conversation 상태(`Subject` 포함)가 유지되는지, customer actor의 `SetAgentAvailableReq`가 오류
response인지 확인한다. push 대기는 stream connector의 public wait API로 한다.

## 7. 완료 기준

- customer/agent client가 각각 `Session` STREAM 연결 하나만 연다.
- 네 서버가 Registry/Discovery로 자동 발견된다.
- 인증 후 current stream session이 Support actor에 bind 된다.
- 배정 가능한 agent가 없으면 conversation은 오류가 아니라 `WaitingForAgent`로 남는다.
- high-level 호출이 업무 객체 기반이고 codec helper가 노출되지 않는다.
- Domain / Application / Adapters 책임 분리가 유지된다.
