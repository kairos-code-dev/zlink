# SupportChat TypeScript Sample (Node/NestJS)

> 언어 중립 시나리오 정본은 [공통 샘플 — SupportChat](../../../../doc/framework/common/sample/supportchat/README.ko.md)다.
> Node/NestJS framework 표면 정의는 [SupportChat Sample (Node/NestJS)](../../../../doc/framework/node/guide/samples/supportchat-sample.ko.md)를 따른다.
> payload codec은 JSON이며, TypeScript Client/Server/Shared 구조로 dotnet `SupportChat` 역할 배치를 미러링한다.

고객과 상담원이 같은 `ConversationSpot`에서 대화하는 고객 지원 채팅 샘플이다. session
gateway, actor binding, conversation Spot, idle timer, 명시적 close, 양방향 bound push,
reconnect를 한 흐름으로 보여 준다. Bingo와 같은 gateway 구조를 쓰되 게임 규칙 대신 업무형
대화 상태를 다룬다.

## Topology

`Registry` · `Api` · `Support` · `Session` 4개 서버를 별도 프로세스로 띄우고 Registry/Discovery로
서로를 자동 발견한다.

- `Registry` — Api/Support/Session endpoint 발견.
- `Api` (`supportchat.api` 채널 서버) — token 검증(`AuthenticateUserReq`)과 상담 시작
  orchestration(`OpenConversationApiReq` → Support `AllocateConversationReq`/`AssignAgentReq`).
- `Support` (`supportchat.support` 채널 서버 + `supportchat.notifications` 채널 서버) — customer/agent
  actor, `SupportEntrySpot`, `ConversationSpot`, idle timer, domain event publisher를 가진 상태 소유 서버.
- `Session` (STREAM 서버) — client 연결, `AuthenticateReq` 처리, actor binding,
  conversation packet relay, bound push pump.

customer client와 agent client는 모두 `Session` STREAM endpoint 하나만 연다.

```text
Customer/Agent Client --STREAM--> Session --channel--> Api/Support
                                  Support --notifications--> Session --STREAM--> Client
Session/Api/Support  ..discovery..> Registry
```

도메인/응용/어댑터 책임은 다음처럼 나뉜다.

- `Server/Support/Domain/SupportChat` — `Conversation` aggregate가 메시지 순서, 참여자, typing,
  idle/close 전이를 소유한다. framework 타입을 참조하지 않는다.
- `Server/Support/Application/ConversationAssignment` — `SupportConversationAllocator`,
  `AgentAvailabilityDirectory`, `AgentAssignmentService` use case.
- `Server/Support/Adapters/ZLink` — Spot lifecycle, actor/handler 등록, notification publish.
  `ConversationSpot`의 `onActorJoin`/`onLeaveActor`/`onDisconnectActor` lifecycle와
  `SupportEntrySpot`의 actor admission을 어댑터가 맡는다.

## 실행

서버 4종을 각각 별도 프로세스로 실행한 뒤 client self-check를 돌린다. 포트 할당, 서버
기동 순서, discovery 준비 대기, 종료 정리는 `run_sample.sh`(Linux/macOS)와
`run_sample.ps1`(Windows)이 소유한다.

```bash
./run_sample.sh
```

```powershell
./run_sample.ps1
```

client는 `loadSampleConfig`로 `Session` endpoint를 읽고, JSON codec stream connector로
연결한다. client self-check는 각 request 직후 response를 검증하고 server push는 stream
connector의 `waitFor` public API로 기다린다.

## Success Condition

- customer/agent client가 각각 `Session` STREAM 연결 하나만 연다.
- 네 서버가 Registry/Discovery로 서로를 자동 발견한다.
- 인증 후 current stream session이 Support actor에 bind 된다.
- agent 배정 후 customer는 `ParticipantJoinedNotify`, agent는 `ConversationAssignedNotify`를 받는다.
- 배정 가능한 agent가 없으면 conversation은 오류가 아니라 `WaitingForAgent`로 남는다.
- agent greeting `MessageSeq = 1`, customer 답변 `MessageSeq = 2`.
- typing 변경은 요청자 response와 상대방 `TypingChangedNotify`로 검증한다.
- reconnect 시 같은 actor와 conversation 상태(`Subject` 포함)가 유지된다.
- idle timeout 뒤 양쪽 client가 `ConversationIdleNotify`를 받고 상태가 `WaitingForClose`가 된다.
- 명시적 `CloseConversationReq`는 요청자에게 close response, 상대방에게 `ConversationClosedNotify`.
- closed conversation에 보낸 메시지·typing·close와 customer actor의 `SetAgentAvailableReq`는
  오류 response를 반환한다.
- 마지막에 client가 `PASS SupportChat.Ts`를 출력한다.

## 회귀 테스트

`framework/languages/node/test/contract/sample-regression.test.js`가 SupportChat.Ts의 역할
배치(파일 목록), 모듈 wiring, runner 구성, top-level runner 등록을 검증한다. typecheck와
`npm run build`가 dist를 산출하면 회귀 게이트가 SupportChat.Ts를 다른 샘플과 함께 검증한다.
