<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: Bingo Game Sample](bingo-game-sample.ko.md) | [다음: DeliveryDispatch Sample](deliverydispatch-sample.ko.md)
<!-- framework-adapter-nav:end -->

# SupportChat Sample

[.NET 묶음](../../README.ko.md) | [STREAM](../../../common/spec/languages/dotnet/aspnet-core-stream.ko.md) | [SPOT](../../../common/spec/languages/dotnet/aspnet-core-spot.ko.md) | [Actor](../../../common/spec/languages/dotnet/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../../../common/spec/languages/dotnet/session-actor-dispatch.ko.md)

> 이 문서는 실행 가능한 SupportChat sample의 DTO, 서버 구조, 실행 흐름을 설명한다.
> 언어 중립 공통 시나리오는
> [spec/sample/supportchat](../../../common/sample/supportchat/README.ko.md)이 다룬다.

## 1. 목적

SupportChat 은 고객 1명과 상담원 1명이 같은 conversation 에서 대화하는 1:1 상담
샘플이다. Bingo 와 같은 session gateway 구조(Session·Api·로직 서버 분리)를 쓰되, 게임
규칙 대신 **업무형 대화 상태·재접속·idle 종료** 를 보여 준다.

이 샘플이 한 번에 보여 주는 것:

- client 는 Session 서버 STREAM[^stream] endpoint 하나만 알고 연결한다.
- 인증 후 현재 stream session 을 Support 서버 actor[^actor] 에 bind 하고, 이후
  conversation packet 은 bound actor 로 relay 한다.
- 참여자, 메시지 순서(`MessageSeq`), typing, idle deadline, close 상태는 domain
  `Conversation` aggregate 가 소유하고, `ConversationSpot`[^spot] 의 단일 actor 큐를 통해
  직렬로만 접근된다.
- reconnect 가 발생해도 같은 actor 와 conversation 상태를 유지한다.
- idle timer 가 새 메시지 없는 conversation 을 `WaitingForClose` 로 전환한다. `Closed` 는
  명시적 `CloseConversationReq` 처리에서만 일어난다.
- 배정 가능한 상담원이 없으면 오류가 아니라 `WaitingForAgent` 상태로 남는다.

payload codec 은 읽기 쉬운 JSON 을 사용한다.

## 2. 샘플 구성

public DTO[^dto] 는 현재 코드 기준으로
`framework/languages/dotnet/samples/SupportChat/Shared/Contracts/Messages.cs` 를 따른다.

client STREAM 인증과 상담 흐름 DTO:

```csharp
public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string ActorId,
    string DisplayName,
    string Role);

public sealed record OpenConversationReq(string Subject);

public sealed record OpenConversationRes(
    string ConversationId,
    ConversationState State);

public sealed record SetAgentAvailableReq(bool IsAvailable);

public sealed record JoinConversationReq(string ConversationId);

public sealed record SendChatMessageReq(
    string ConversationId,
    string Text);

public sealed record SendChatMessageRes(
    ChatMessage Message,
    ConversationState State);

public sealed record SetTypingReq(
    string ConversationId,
    bool IsTyping);

public sealed record CloseConversationReq(
    string ConversationId,
    string? Reason);
```

server push 와 state DTO:

```csharp
public sealed record ParticipantJoinedNotify(
    string ConversationId,
    string ActorId,
    string Role,
    ConversationState State);

public sealed record ConversationAssignedNotify(
    string ConversationId,
    ConversationState State);

public sealed record ChatMessageNotify(
    string ConversationId,
    ChatMessage Message,
    ConversationState State);

public sealed record TypingChangedNotify(
    string ConversationId,
    string ActorId,
    bool IsTyping,
    ConversationState State);

public sealed record ConversationIdleNotify(
    string ConversationId,
    ConversationState State);

public sealed record ConversationClosedNotify(
    string ConversationId,
    ConversationState State);

public sealed record ConversationState(
    string ConversationId,
    string Subject,
    string Status,
    string CustomerActorId,
    string? AgentActorId,
    ulong LastMessageSeq,
    long? LastMessageAtUnixMs,
    long? IdleDeadlineUnixMs);
```

`Role` 은 `Customer`/`Agent`, `Status` 는 `WaitingForAgent`/`Active`/`WaitingForClose`/
`Closed` 를 쓴다. `ConversationId` 는 conversation Spot 의 routing id hex 문자열
(`SpotRid.ToHex()`)을 그대로 쓴다. Api·Support 서버 사이의 orchestration DTO
(`OpenConversationApiReq`, `AllocateConversationReq`, `AssignAgentReq`,
`EnsureSupportUserActorReq`, `ActorRefSnapshot`)도 같은 파일에 둔다.

## 3. 서버 구성

| 프로세스 | 책임 |
|----------|------|
| `SupportChat.Server.Session` | client STREAM 연결, 인증, actor binding, conversation packet relay |
| `SupportChat.Server.Api` | token 검증, 상담 시작 orchestration, agent 배정 요청 |
| `SupportChat.Server.Support` | customer/agent actor, `SupportEntrySpot`, `ConversationSpot` 호스팅 |
| location store | 세 서버 endpoint 자동 연결에 필요한 위치 row 공유 |

(실행은 위 서버 외에 `SupportChat.Client`, `SupportChat.Probe` 프로세스를 함께 띄운다.)

customer 와 agent client 가 직접 연결하는 서버는 Session 하나뿐이다. Api·Support 는
client-facing endpoint 를 열지 않는다. 서버 간 연결은 location store 자동 연결을
사용한다(수동 endpoint 연결은 TicTacToe 샘플이 맡는다).

Support 서버는 domain logic 과 framework adapter 를 분리한다.

```text
Server/Support/
  Domain/
    SupportChat/        # Conversation aggregate, 메시지 sequence, status 전이, policy
  Application/
    ConversationAssignment/   # SupportConversationAllocator, AgentAssignmentService 등
  Infrastructure/
    ZLink/
      Actors/           # SupportUserActor (customer/agent)
      Spots/            # SupportEntrySpot, ConversationSpot
        Handlers/       # OpenConversation, SendChatMessage, SetTyping, CloseConversation, SetAgentAvailable
      Handlers/         # AllocateConversation, AssignAgent, EnsureSupportUserActor (channel)
      Notifications/    # ConversationNotificationPublisher
```

`Domain/SupportChat` 의 `Conversation` 은 aggregate root 로, 상태 전이·메시지 sequence·
idle/close 판정을 한 곳에서 처리하고 ZLink 타입을 알지 않는다. `ConversationSpot` 은
framework callback 을 받아 domain method 를 호출하고, domain 이 반환한 event 를
`ConversationNotificationPublisher` 가 push message 로 바꾼다. `SetAgentAvailable` 은
conversation 에 들어가기 전 단계라 `SupportEntrySpot` 에서 처리한다. idle 판정은 별도
timer handler 파일이 아니라 `ConversationSpot` 의 idle check 와 domain `MarkIdle` 로 처리한다.

Support 서버는 customer/agent actor가 다른 node로 이동해도 identity와 conversation 연결을 유지하도록
transfer adapter를 등록한다.

```csharp
options.AddSpotMesh(SampleNames.SupportSpotDiscovery)
    .AddActorFactory<SupportUserActorFactory>(SampleNames.SupportActorType)
    // 표시 이름, 역할, participant id, conversation id를 target actor로 복원한다.
    .AddActorTransferAdapter<SupportUserActor, SupportUserActorTransferAdapter>(SampleNames.SupportActorType);
```

adapter는 conversation aggregate를 복제하지 않는다. actor가 보유한 identity와 현재 conversation id만
옮긴다. source `TransferOutAsync`가 반환한 `ZLinkMessage`는 target `TransferInAsync`의 `state`로
전달되고, conversation domain state는 `ConversationSpot`이 계속 소유한다. target admission은 actor
id와 request만 받고, 복원된 actor는 commit 뒤 `OnJoinedActorAsync`에 전달된다.

## 4. 실행 흐름

- **인증·binding**: client `AuthenticateReq` → Session 이 Api 로 token 검증 →
  Support 로 `EnsureSupportUserActorReq` → 현재 stream session 을 actor 에 bind. agent
  client 는 인증 직후 `SetAgentAvailableReq(true)` 로 상담 가능 상태를 등록한다.
- **상담 시작·배정**: customer `OpenConversationReq` → customer actor 가 Api 로
  orchestration → Support 가 `ConversationSpot` 생성(`WaitingForAgent`) → agent join 으로
  `Active`. customer 는 `ParticipantJoinedNotify`, agent 는 `ConversationAssignedNotify` push.
- **메시지·typing**: `ConversationSpot` 이 단조 증가 `MessageSeq` 를 부여하고 상대방에게
  `ChatMessageNotify`/`TypingChangedNotify` 를 push 한다.
- **idle·close**: Spot timer 가 idle deadline 을 넘기면 conversation 을 `WaitingForClose`
  로 두고 `ConversationIdleNotify` 를 양쪽 bound session 에 push 한다. timer 는 신호만
  전달하고 전이 판정은 domain 이 한다. `Closed` 전환과 `ConversationClosedNotify` 는
  명시적 `CloseConversationReq` 처리에서 일어난다.
- **reconnect**: 같은 `ActorId` 가 다시 인증하면 새 actor 를 만들지 않고 새 stream session
  binding 만 갱신한다. client 는 `JoinConversationReq` 로 현재 상태를 다시 확인한다.

## 5. 완료 기준

- customer/agent client 는 각각 Session 서버에 STREAM 연결 하나만 연다.
- Session·Api·Support 는 location store 로 서로를 자동 연결한다.
- agent greeting 은 `MessageSeq = 1`, customer 답변은 `MessageSeq = 2` 로 검증한다.
- 배정 가능한 agent 가 없으면 `WaitingForAgent` 로 남고 오류 response 가 아니다.
- reconnect 시 같은 actor 와 conversation 상태(`Subject` 포함)가 유지된다.
- closed conversation 에 보낸 메시지·typing·close 요청은 오류 response 를 반환한다.
- customer actor 의 `SetAgentAvailableReq` 는 오류 response 를 반환한다.
- Domain / Application / Infrastructure 책임 분리가 유지된다.

## 6. 회귀 테스트

SupportChat 샘플을 구현할 때는 아래 기존 회귀 테스트가 깨지지 않아야 한다. 이
테스트들은 SupportChat 이 사용하는 framework 표면(session gateway, conversation Spot,
idle timer, client connector)을 이미 고정하고 있다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | Session gateway 경로에서 customer/agent request 가 bound actor dispatch 와 sequence 로 맞물려 처리된다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | 두 actor 가 같은 conversation Spot 에 join·move·submit 으로 참여한다. |
| `ManagerTests.Spot_Publish_Timer_And_Close_Stop_Callbacks_Work` | conversation idle timer 진행과 close 시 lifecycle 정리가 framework timer 계약과 맞는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | client connector 의 request/reply correlation 과 bound push 수신이 유지된다. |

[^stream]: `STREAM` 은 클라이언트와 서버 사이에 지속 연결을 유지하면서 framework Header 기반 packet 을 주고받는 세션형 통신 추상이다.
[^actor]: actor 는 자신만의 메일박스와 상태를 가지고 메시지를 순서대로 처리하는 동시성 단위다. framework 에서는 클라이언트 세션과 묶여 사용자별 상태를 다루는 데 쓰인다.
[^spot]: `SPOT` 은 동적으로 생성ㆍ소멸되는 논리적 단위(예: conversation, room 등)로 메시지를 라우팅하는 추상이다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: Bingo Game Sample](bingo-game-sample.ko.md) | [다음: DeliveryDispatch Sample](deliverydispatch-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
