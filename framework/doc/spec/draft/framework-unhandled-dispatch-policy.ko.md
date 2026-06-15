# Framework unhandled dispatch 정책 초안

이 문서는 **정식 공개 계약 편입 전 draft** 이며 **현재 공개 계약이 아니다**.
정식 공개 계약은 `core/include/zlink.h`, `core/include/zlink/*.h`,
`doc/spec/core/`, 각 binding spec, framework spec 에 나누어 반영한다.

이 초안은 `.NET framework` 런타임에서 packet 또는 message 를 받았지만 등록된
application handler 가 없을 때 어떤 기본 동작을 해야 하는지 정리한다. 대상은
Channel, DealerMeshChannel, RouteMeshChannel, Spot, EntrySpot, Actor dispatch
경로다. Session 은 등록형 handler 경로가 아니라
`IZLinkSession.OnDispatchAsync(...)` callback 이 application 에 그대로 전달되는
표면이므로 별도 절에서 다룬다.

이 문서에서 "첫 구현"은 handler miss 의미, request 실패 분류, 최소 logging/trace/metrics,
그리고 deterministic reply 에 필요한 public API 경계를 맞추는 범위를 뜻한다. message 흐름의
상세 샘플링, trace context 전파, duration histogram, native ref count 같은 항목은 같은
설계 안에 있지만 단계적으로 구현할 수 있다. 이 구분은 공개 API 를 얕고 넓게 만들지 않기
위한 것이다.

## 1. 배경

현재 framework 의 여러 dispatch 경로는 handler 가 없을 때 서로 다른 방식으로
동작한다. 어떤 경로는 예외를 던지고, 어떤 경로는 reply error 를 만들고, 어떤
경로는 조용히 return 한다.

이 차이는 사용자에게 다음 문제를 만든다.

- request 는 실패를 알 수 있지만 send 나 publish 는 실패 여부가 보이지 않는다.
- handler 등록 누락과 의도한 no-op 을 구분하기 어렵다.
- Session 은 모든 packet 이 application callback 으로 전달되므로 handler miss 정책과
  섞이면 책임 경계가 흐려진다.
- Spot, Actor, Channel 마다 "handler 없음"의 의미가 달라진다.
- 운영 중에는 어느 session, channel, packet 이 버려졌는지 추적하기 어렵다.

따라서 framework 는 handler miss 를 각 dispatcher 의 우연한 구현 결과로 두지
않고, 하나의 정책으로 정의해야 한다.

## 2. 목표

1. 등록된 handler 가 없을 때 기본 동작을 message kind 별로 통일한다.
2. request 는 호출자에게 실패를 돌려줄 수 있으므로 error reply 를 기본으로 한다.
3. send 와 publish 는 reply 경로가 없으므로 관측 가능한 drop 을 기본으로 한다.
4. Session packet 은 callback 기반 표면이므로 application 이 모든 inbound packet 책임을
   가진다는 점을 분명히 한다.
5. handler miss 는 표준 `ILogger<T>` 로 기록하고 zlink 전용 logger 등록 API 는
   추가하지 않는다.
6. message 흐름 추적은 logging, OpenTelemetry trace, metrics 로 나누어 제공한다.
7. 사용자가 엄격한 서버나 테스트 환경에서 정책을 바꿀 수 있도록 최소 옵션을 둔다.

## 3. 비목표

- core C API 의 errno 계약을 이 초안에서 바로 변경하지 않는다.
- zlink 전용 logging provider 나 logger callback 등록 API 를 추가하지 않는다.
- payload 본문을 로그에 기록하지 않는다.
- OpenTelemetry exporter 를 framework 가 자동 등록하지 않는다.
- publish sender 에게 subscriber 별 handler miss 를 실패로 돌려주지 않는다.
- actor 가 bind 된 session 에서 framework 가 target actor 를 추론하거나 대신 전달하지 않는다.
- timer, monitoring event, lifecycle callback 의 no-op handler 의미를 packet
  handler miss 와 섞지 않는다.

## 4. 용어

| 용어 | 의미 |
|------|------|
| handler miss | runtime 이 받은 packet 또는 message 에 대응하는 application handler 를 찾지 못한 상태다. |
| unhandled dispatch | handler miss 뒤 framework 정책에 따라 reply, drop, throw 중 하나를 수행하는 단계다. Session actor relay 는 application 이 별도로 선택하는 동작이다. |
| request | caller 가 reply 를 기다리는 message 다. 실패를 error reply 로 전달할 수 있다. |
| send | reply 를 기다리지 않는 fire-and-forget message 다. 실패를 caller 에게 직접 돌려줄 수 없다. |
| publish | pub/sub fanout message 다. subscriber 하나의 handler miss 를 publisher 실패로 볼 수 없다. |
| bound actor | session 에 `BindActorHandleAsync(...)` 로 연결된 actor handle 이다. |
| observable drop | message 를 버리되, logger, trace event, metric 중 하나 이상으로 원인을 남기는 동작이다. |
| message flow trace | 한 message 가 receive, decode, handler resolve, dispatch, relay, send, reply 같은 단계를 어떻게 통과했는지 남기는 진단 기록이다. |
| diagnostic field | 일반 운영 로그에는 너무 상세하지만 문제 분석 때 필요한 size, part count, ownership, ref count 같은 필드다. |

## 5. 기본 정책

기본 정책은 아래와 같다.

| 조건 | 기본 동작 |
|------|-----------|
| handler 있음 | handler dispatch |
| Session `OnDispatchAsync(...)` | unhandled dispatch 정책 대상이 아니다. 모든 packet 을 application callback 에 그대로 전달 |
| Channel request handler 없음 | error reply |
| Channel send handler 없음 | `Warning` log 후 drop |
| Channel publish handler 없음 | `Debug` log 후 drop |
| DealerMesh request handler 없음 | error reply |
| DealerMesh send handler 없음 | `Warning` log 후 drop |
| DealerMesh unexpected response/error | pending request 없음으로 log/drop |
| RouteMesh request handler 없음 | error reply |
| RouteMesh send handler 없음 | `Warning` log 후 drop |
| Spot route request handler 없음 | error reply |
| Spot route send handler 없음 | `Warning` log 후 drop |
| Spot subscription handler 없음 | `Debug` log 후 drop |
| Actor request handler 없음 | error reply |
| Actor send handler 없음 | `Warning` log 후 drop |
| EntrySpot actor join handler 없음 | rejected reply, `Debug` log |

request 의 error reply 는 해당 surface 가 이미 가진 reply writer 로 보낸다. Channel 은
framework envelope error 를 사용하고, RouteMesh, Spot, Actor, DealerMesh 는 각 dispatcher 의
request reply 경로를 사용한다. send 와 publish 는 caller 가 기다리는 reply 경로가 없으므로
error reply 를 만들지 않는다.

request 라도 header decode 실패처럼 reply target 이나 request sequence 를 신뢰할 수 없는
상태에서는 error reply 를 만들지 않는다. 이 경우에는 잘못된 대상에게 reply 를 보내는 위험이
더 크므로 warning log 후 drop 한다. header 는 정상이고 payload decode 만 실패한 경우에는
reply target 을 알 수 있으므로 `PayloadDecodeFailed` error reply 를 보낸다.

request 실패는 handler miss 와 transport/route 실패를 구분한다. 실패는 크게 두 단계에서
나온다. 첫 번째는 request 를 socket 에 제출하지 못한 submit 단계 실패이고, 두 번째는 request
가 제출된 뒤 reply completion 으로 돌아오는 실패다. core/binding 이 `NotConnected` 또는
`NotFound` 로 전달한 결과를 framework 가 timeout 으로 바꾸면 안 된다.

| core/binding 결과 | 의미 | framework 동작 |
|-------------------|------|----------------|
| submit `NotConnected` / errno `EHOSTUNREACH` | target node rid 로 가는 route 나 pipe 가 없거나 현재 도달할 수 없음 | route unavailable 예외로 실패 |
| request completion `NotFound` / errno `ENOENT` | request 는 접수됐지만 target spot, actor, local route 를 찾지 못함 | target not found 예외로 실패 |
| request completion `TimedOut` / errno `ETIMEDOUT` | deadline 안에 reply 가 오지 않음 | timeout 예외로 실패 |
| framework handler miss | target 은 도달했지만 application handler 가 없음 | framework error reply |

## 6. 적용 대상

### 6.1 Stream Session packet

대상 public 표면은 다음과 같다.

- `IZLinkSession.OnDispatchAsync(...)`
- `IZLinkSessionContext.BoundActors`
- `IZLinkSessionContext.RelayToActorAsync(...)`

Session 은 다른 surface 와 다르다. Channel, DealerMeshChannel, Spot, Actor 는 등록된
handler registry 를 runtime 이 조회한다. Session packet 은 `IZLinkSession.OnDispatchAsync(...)` callback 이
application session object 로 그대로 전달된다. 이 callback 은 `bool handled` 를 반환하지
않으므로 framework runtime 은 callback 이 끝난 뒤 처리 여부를 자동으로 판정할 수 없다.
따라서 Session 은 이 문서의 unhandled dispatch 정책 대상이 아니다. Session 으로 전달된
모든 packet 의 처리 책임은 application callback 에 있다.

Session packet 의 기본 흐름은 아래와 같다.

1. framework 가 session 별 실행 줄에서 packet 을 받는다.
2. framework 가 session object 의 `OnDispatchAsync(...)` callback 을 호출한다.
3. application 은 callback 안에서 직접 처리, actor relay, drop, close, error reply 중
   하나를 선택한다.
4. framework 는 callback 반환만 보고 "처리 안 됨"을 추론하지 않는다. 정상 반환은
   application 이 자기 정책대로 처리했다는 뜻이다.

message 소유권은 session callback 전체에서 framework runtime 이 가진다.
`OnDispatchAsync(...)` 로 전달된 `Message payload` 는 callback 이 끝날 때까지 빌려 쓰는
값이며, application 은 dispose 하거나 다른 곳으로 move 하지 않는다.
relay 나 비동기 queue 경계를 넘어야 하면 framework 내부에서 필요한 복사 또는 move 를
수행한다. 이 규칙이 있어야 session code 가 handler 호출마다 `payload.Move()` 를 반복하지
않고도 안전하게 callback 을 작성할 수 있다.

actor relay 가 필요한 경우에는 application 이 packet name, metadata, session state
등 자기 프로토콜에 맞는 selector 나 broadcast 정책을 직접 구현해야 한다. 한 packet 을
어떤 actor 하나로 보낼지, 여러 actor 에 동시에 보낼지, session 에서 직접 처리할지는
application protocol 의 일부다. framework 가 첫 번째 actor 를 고르거나 여러 actor 상태를
오류로 단정하면 protocol 결정을 framework 가 대신하게 된다.

이 selector 또는 broadcast 결정은 application code 안에서 끝나야 한다. framework 는 actor
수를 보고 relay target 을 고르지 않는다. bound actor 가 하나인 경우에도 framework 가 그
actor 를 자동 기본값으로 삼으면 protocol 결정을 framework 가 대신하게 된다. application 이
actor 로 보내려면 명시적으로 actor 를 고르고 `RelayToActorAsync(...)` 를 호출해야 한다.
`RelayToActorAsync(...)` 는 unhandled 정책이 아니라 application 이 선택한 actor target 으로
전달하는 명시적 relay API 다.

`OnDispatchAsync` 가 정상 반환하면 framework 는 application 이 처리했다고 본다. 이 규칙이
있어야 session callback 안에서 의도적으로 drop 하거나 application protocol 에 맞게 비동기
처리하는 코드를 framework 가 다시 건드리지 않는다.

### 6.2 Channel message

대상 public 표면은 다음과 같다.

- `AddChannel(...)`
- `IZLinkRequestHandler<TRequest, TReply>`
- `IZLinkSendHandler<TMessage>`
- `IZLinkPublishHandler<TMessage>`

socket 관점의 대상은 다음과 같다.

- Router 로 수신한 request
- Router 로 수신한 send
- Sub 로 수신한 publish

일반 ClientServerChannel 의 client DEALER 는 주로 outbound 역할이다. 다만 reply tracking
또는 internal routed reply 처리에서 unexpected packet 을 받는 경우에는 logging 대상이 될
수 있다. DealerMeshChannel 의 DEALER 는 별도 절처럼 inbound/outbound 를 모두 처리한다.

Channel request 는 handler 가 없으면 error reply 를 만든다. Channel send 는
`Warning` 으로 기록하고 drop 한다. Channel publish 는 subscriber 별 optional handler
성격이 강하므로 기본 `Debug` 로 기록하고 drop 한다.

### 6.3 DealerMeshChannel message

대상 public 표면은 다음과 같다.

- `AddDealerMeshChannel(...)`
- `IZLinkClient.Send(...)`
- `IZLinkClient.Request(...)`
- `IZLinkDealerMeshChannelBuilder.AddHandlerGroup(...)`
- `IZLinkDealerMeshChannelBuilder.AddSendHandler(...)`
- `IZLinkDealerMeshChannelBuilder.AddRequestHandler(...)`
- `IZLinkSendHandler<TMessage>`
- `IZLinkRequestHandler<TRequest, TReply>`

DealerMeshChannel 은 DEALER socket 을 mesh 로 연결한 channel 이다. DEALER socket 은
request/send 를 밖으로 보내기만 하는 단방향 client socket 이 아니다. 같은 socket 으로
peer 에서 온 request/send/response/error 도 받을 수 있다. 따라서 DealerMeshChannel 은
outbound diagnostics 대상이면서 동시에 inbound dispatch 정책 대상이다.

따라서 현재 public builder 가 handler registration 을 열지 않는다면 구현에서 반드시
수정해야 한다. bidirectional DealerMesh 로 정의하려면 builder, registration, runtime
receive loop 가 inbound send/request handler 를 등록하고 dispatch 할 수 있어야 한다.

필수 API delta 는 아래와 같다.

```csharp
public interface IZLinkDealerMeshChannelBuilder
{
    void EnableClient(
        Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}
```

DealerMesh inbound handler 는 일반 Channel handler interface 를 재사용한다. 이유는
DealerMesh packet 이 특정 target routing id 를 application handler 에 노출하는 route
handler 가 아니기 때문이다. routing id 를 handler 에 넘겨야 하는 경우는 RouteMeshChannel
의 `IZLinkRouteSendHandler<>`, `IZLinkRouteRequestHandler<,>` 를 사용한다.

handler 등록의 namespace 는 같은 channel name 과 handler group 규칙을 따른다.
`AddDealerMeshChannel("mesh", ...)` 에 등록한 `AddHandlerGroup(...)`,
`AddSendHandler(...)`, `AddRequestHandler(...)` 는 기존 Channel handler scanner 와
validator 가 다루는 `IZLinkSendHandler<>`, `IZLinkRequestHandler<,>` catalog 에 노출되어야
한다. 구현은 DealerMesh 전용 handler registry 를 새로 만들기보다 기존 Channel handler
registration 구조를 재사용한다. 단, validator 는 DealerMesh 에서 send/request handler
노출을 금지하던 현재 제약을 제거해야 한다.

등록된 handler type 은 framework DI 에 자동으로 등록되어야 한다. 사용자가 옵션에서
handler 를 노출했는데 별도로 `AddScoped<THandler>()` 를 하지 않아도 handler 생성과
constructor injection 이 동작해야 한다. 이 규칙은 DealerMesh 에 새로 추가되는 handler 에도
동일하게 적용한다.

`EnableClient(...)` 이름은 이 초안의 구현 범위에서는 유지한다. 이 이름은 "이 channel 이
outbound 전용 client 다"라는 뜻이 아니라 "이 channel 이 DEALER socket 역할을 가진다"는
뜻으로 문서화한다. 이름 변경은 이 정책 구현과 별도 breaking change 로 분리해야 한다.

DealerMeshChannel 에서 기록해야 하는 outbound 단계는 아래와 같다.

| 단계 | 기록 대상 |
|------|-----------|
| outbound send/request submit | channel name, kind, packet name, message id, size |
| peer route 선택 | discovery/manual connection 결과, target routing id 를 알 수 있으면 기록 |
| send 완료 또는 실패 | sent, not-connected, backpressure, timeout, cancellation |
| request reply 대기 | request sequence, deadline, timeout |
| reply 수신 | response/error, correlation id, duration |

DealerMeshChannel 의 inbound 정책은 Channel 과 RouteMeshChannel 의 중간 성격이다.

| inbound kind | 기본 동작 |
|--------------|-----------|
| request | handler 가 있으면 dispatch 하고 reply 한다. handler 가 없으면 error reply 를 보낸다 |
| send | handler 가 있으면 dispatch 한다. handler 가 없으면 `Warning` log 후 drop 한다 |
| response/error | pending request 를 먼저 완료한다. pending request 가 없으면 unexpected reply 로 log/drop 한다 |
| publish | DealerMeshChannel 의 기본 message kind 가 아니다 |

source DealerMeshChannel 에서 보낸 request 의 target peer 가 handler miss 로 error reply 를
보내면, source 쪽에서는 handler miss 가 아니라 error reply 수신으로 기록한다. target 쪽
DealerMeshChannel 에서는 handler miss 로 기록한다.

현재 backend surface 가 DEALER socket 의 수신을 노출하지 않는다면 구현에서 반드시 아래
delta 를 포함한다. bidirectional DealerMesh 는 native/binding `Request(...)` callback 만으로
구현할 수 없다. 같은 socket 으로 들어온 request/send 와 response/error 를 framework 가
분류해야 하기 때문이다.

여기서 중요한 제약은 DEALER 의 reply target 이다. 일반 DEALER `Recv(...)` 로 받은 message 는
source peer 를 public 계약으로 노출하지 않는다. 따라서 framework 가 임의로 같은 socket 에
`Send(...)` 를 호출해서 reply 를 흉내 내면 여러 peer 가 붙은 mesh 에서 잘못된 peer 로 reply 가
갈 수 있다. 또한 `requestSeq` 만으로는 충분하지 않다. sequence 값이 어떤 peer/pipe 와 묶였는지
core 가 알아야 deterministic reply 가 가능하다.

따라서 DealerMesh request handler 를 public 으로 열려면 core/binding 이 아래 둘 중 하나를
public API 로 제공해야 한다.

1. DEALER request receive/reply 전용 public API
2. DEALER receive 가 source peer identity 와 directed reply 를 안전하게 노출하는 public API

이 API 는 framework 가 binding 내부 멤버를 reflection 으로 호출해서 대신 만들 수 없다.
아래 코드는 기존 `Send(...)`, `Request(...)`, `AttachDiscovery(...)`, `OnSendReady(...)` 를
제거하라는 뜻이 아니라, 추가되어야 하는 수신/reply 능력을 개념적으로 보여준다.

```csharp
internal interface IZLinkBackendDealerSocket : IZLinkBackendConnectableSocket
{
    DealerReceived? RecvDealer(RecvFlags flags = RecvFlags.None);

    bool RequestFrame(
        ulong requestSeq,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None);

    void Reply(ulong requestToken, IReadOnlyList<Message> parts);
}
```

정확한 type 이름과 시그니처는 native DEALER reply 계약에 맞춰 조정할 수 있다. 중요한 계약은
DEALER backend 가 inbound message kind 와 request token 을 framework receive loop 에 넘기고,
framework 가 source peer 를 추측하지 않고 같은 request 에 reply 할 수 있어야 한다는 점이다.
framework 는 이 public API 만 사용해야 한다. binding 내부 멤버를 reflection 으로 호출하거나
`InternalsVisibleTo` 로 우회하면 framework 와 binding 의 변경 경계가 깨진다.

이 절의 초기안은 DEALER가 request sequence와 reply token을 직접 다루는 C API를
전제로 했다. 현재 C API 정리 기준에서는 그 방향을 폐기한다. DEALER는 특정 peer를
지정해 reply할 수 있는 주체가 아니므로 public API에 request sequence 주입이나
reply helper를 두지 않는다.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_dealer_recv_part(
  void *dealer,
  uint8_t *message_type_out,
  uint64_t *request_seq_out,
  zlink_msg_t *part_out,
  zlink_part_flag_t *has_more_out,
  zlink_recv_flags_t flags);
```

`zlink_dealer_recv_part(...)` 가 request 를 돌려줄 때의 `request_seq_out` 은 단순 숫자가
아니다. framework는 이 값을 새 outbound request sequence로 재사용하거나 reply token으로
해석하지 않는다. DEALER에서 reply 동작이 필요해 보이는 흐름은 ROUTER 또는 SPOT reply
context로 모델을 다시 잡아야 한다.

`message_type_out` 은 raw send, request, reply 를 구분할 수 있어야 한다. raw send 는 request
sequence 가 없어야 하고, request/reply 는 request sequence 를 보존해야 한다. 이 이름과 상수
값은 core naming 규칙에 맞춰 조정할 수 있지만, 계약의 세 가지 능력은 빠지면
안 된다.

### 6.4 RouteMeshChannel message

대상 public 표면은 다음과 같다.

- `AddRouteMeshChannel(...)`
- `IZLinkRouteSendHandler<TMessage>`
- `IZLinkRouteRequestHandler<TRequest, TReply>`

RouteMeshChannel 은 routed Router/Dealer 기반으로 request 와 send 를 처리한다.
publish handler 는 없다.

request handler 가 없으면 source routing id 와 request sequence 로 error reply 를
돌려준다. send handler 가 없으면 source routing id, channel id, packet name 을 로그에
남기고 drop 한다.

### 6.5 Spot route packet

대상 public 표면은 다음과 같다.

- `IZLinkSpotPacketHandler<TSpot, TMessage>`
- `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>`
- spot client 의 send/request 계열 API

Spot route dispatcher 는 현재 handler 를 찾지 못하면 조용히 return 할 수 있다. 이
동작은 수정 대상이다.

request handler 가 없으면 request caller 에게 error reply 를 돌려준다. send handler 가
없으면 `Warning` log 후 drop 한다. Spot rid 와 packet name 은 로그에 남기되 payload 는
남기지 않는다.

### 6.6 Spot subscription packet

대상 public 표면은 다음과 같다.

- `IZLinkSpotSubscriptionHandler<TSpot, TEvent>`
- spot pub/sub subscribe 설정

subscription 은 보통 handler 등록에서 subscribe 대상이 만들어지므로 handler miss 가
정상적으로는 잘 생기지 않는다. 그래도 registry 와 runtime 상태가 어긋났거나 내부
mapping 이 깨진 경우를 대비해 `Debug` log 후 drop 을 둔다.

publish 는 fanout 이므로 publisher 에게 error reply 를 보낼 수 없다. handler miss 를
운영 신호로 보고 싶으면 사용자가 policy 에서 `Warning` 으로 올릴 수 있어야 한다.

### 6.7 Actor packet

대상 public 표면은 다음과 같다.

- `IZLinkActorSendHandler<TMessage>`
- `IZLinkActorRequestHandler<TRequest, TReply>`
- `IZLinkActorPacketHandler<TActor, TMessage>`
- `IZLinkActorRequestHandler<TActor, TRequest, TReply>`

Actor packet 은 Spot actor handler 를 먼저 찾고, 없으면 actor 자체 handler 로 fallback
할 수 있다. 두 단계 모두 handler 를 찾지 못하면 unhandled actor packet 이다.

request 는 error reply 를 만든다. send 는 `Warning` log 후 drop 한다. actor id,
actor type, packet name, source session 여부는 로그에 남길 수 있다.

handler precedence 는 actor 의 현재 실행 위치에 따라 결정한다.

| 현재 위치 | 우선순위 |
|-----------|----------|
| EntrySpot | `IZLinkEntrySpotActorSendHandler` / `IZLinkEntrySpotActorRequestHandler` → actor 자체 handler → unhandled |
| user Spot | `IZLinkSpotActorSendHandler` / `IZLinkSpotActorRequestHandler` → actor 자체 handler → unhandled |
| local actor only | actor 자체 handler → unhandled |

EntrySpot handler 와 user Spot handler 는 동시에 같은 actor packet 을 처리하지 않는다. actor
가 user Spot 에 join 되어 있으면 user Spot handler 우선순위를 사용하고, EntrySpot 에만
있으면 EntrySpot handler 우선순위를 사용한다. actor 자체 handler 는 Spot 특화 handler 가
없는 경우의 fallback 이다.

### 6.8 EntrySpot actor packet

대상 public 표면은 다음과 같다.

- `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>`
- `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>`
- `IZLinkEntrySpotActorJoinedHandler<TEntrySpot, TActor>`
- `IZLinkEntrySpotActorLeftHandler<TEntrySpot, TActor>`

EntrySpot actor send/request 는 Actor packet 과 같은 정책을 사용한다. joined/left
handler 는 optional notification 이므로 handler miss 를 오류로 보지 않는다. 기본은 로그 없음이고,
diagnostic mode 에서만 `Debug` 로 기록한다.

### 6.9 EntrySpot actor join

대상 public 표면은 다음과 같다.

- `IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>`

actor join 은 request 성격이지만 일반 packet handler miss 와 다르다. join handler 가
없거나 target actor 를 찾을 수 없으면 accepted=false 로 rejected reply 를 돌려준다.
이 동작은 유지한다.

보강할 점은 로그다. rejected 이유가 handler miss 인지, target actor 없음인지,
application handler reject 인지 구분할 수 있어야 한다.

## 7. 제외 대상

아래 대상은 이 초안의 unhandled packet 정책에서 제외한다.

| 대상 | 제외 이유 |
|------|-----------|
| `IZLinkSpotTimerHandler<TSpot>` | 외부 packet 이 아니라 등록된 timer 만 실행한다. handler 없음은 요청 실패가 아니다. |
| Spot actor joined/left notification | optional notification 이며 handler 없음은 정상 no-op 이다. |
| EntrySpot actor joined/left notification | optional notification 이며 handler 없음은 정상 no-op 이다. |
| `IZLinkRuntimeEventHandler<TEvent>` | monitoring event 이며 application packet dispatch 와 의미가 다르다. |
| lifecycle callback | connected, disconnected, closing 같은 runtime lifecycle 은 packet handler miss 가 아니다. |

## 8. Logging 정책

framework 는 `Microsoft.Extensions.Logging` 의 `ILogger<T>` 를 사용한다. zlink 전용
logger 등록 API 는 추가하지 않는다.

사용자는 일반적인 host 설정으로 로그를 제어한다.

```csharp
builder.Logging.AddConsole();
builder.Logging.SetMinimumLevel(LogLevel.Warning);
```

logging provider 가 없거나 사용자가 logging 을 등록하지 않은 경우 framework 는
`NullLogger<T>` 로 동작해야 한다. logging 때문에 framework DI 구성이 실패하면 안 된다.

framework 는 출력 포맷을 직접 고정하지 않는다. framework 책임은 category, event id,
event name, structured field 를 안정적으로 제공하는 것이다. console, JSON, file,
OpenTelemetry exporter 같은 실제 출력 형식은 사용자가 등록한 logging provider 가
결정한다.

예를 들어 framework 내부에서는 다음과 같은 구조화 로그를 남긴다.

```csharp
logger.LogWarning(
    "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason}",
    "handler-missing",
    "channel",
    "request",
    "StartBingoReq",
    "reply-error",
    "no-handler");
```

JSON provider 를 쓰면 위 값들이 검색 가능한 field 로 남아야 한다. console provider 를
쓰면 사람이 읽는 한 줄 또는 여러 줄 로그로 출력된다. framework 가 자체 문자열 formatter
를 만들면 provider 생태계와 충돌하고 OpenTelemetry 연동도 어려워지므로 피한다.

### 8.1 로그 필드

unhandled dispatch 로그와 message flow 로그에는 가능한 한 아래 정보를 포함한다. Session 은
unhandled dispatch 대상이 아니지만, session dispatch 또는 명시적 actor relay flow 를 추적할
때 같은 field shape 를 사용할 수 있다.

| 필드 | 설명 |
|------|------|
| `kind` | `request`, `send`, `publish`, `response`, `error` 중 하나 |
| `surface` | `session`, `channel`, `dealer-mesh-channel`, `route-mesh-channel`, `spot`, `actor`, `entry-spot` 중 하나 |
| `packetName` | application packet name |
| `channelName` | Channel, DealerMeshChannel, RouteMeshChannel 이름. 없으면 기록하지 않는다 |
| `sessionId` | Session flow 진단이면 session id |
| `actorId` | Actor 관련 packet 이면 actor id |
| `actorType` | Actor type 을 알 수 있으면 기록 |
| `spotRid` | Spot packet 이면 target spot rid |
| `boundActorCount` | Session callback 이 명시적 actor relay 진단을 남길 때 actor 개수 |
| `action` | dispatch, relay, reply-error, drop, throw, rejected 중 하나 |
| `reason` | no-handler, no-target-actor, unexpected-reply 등 |
| `messageId` | framework 가 부여하거나 upstream metadata 에서 가져온 message 식별자 |
| `correlationId` | request/reply 또는 외부 trace 와 묶기 위한 correlation id |
| `sizeBytes` | 기록 가능한 경우 전체 message byte 크기 |
| `partCount` | multipart message 의 part 개수 |
| `payloadSizeBytes` | payload body 크기. payload 본문은 기록하지 않는다 |

payload bytes, payload JSON, access token, user secret 은 로그에 남기지 않는다.
field 값은 사람이 읽기 쉬운 lowercase/kebab-case 를 기본으로 한다. public type 이름이나
exception type 이름이 필요한 경우에는 별도 field 로 남기고, `kind`, `action`, `reason` 값에
섞지 않는다.

### 8.2 기본 로그 레벨

| 상황 | 기본 레벨 |
|------|-----------|
| request handler 없음 + error reply | `Warning` |
| send handler 없음 + drop | `Warning` |
| publish handler 없음 + drop | `Debug` |
| optional notification handler 없음 | 로그 없음 또는 `Debug` |
| actor join handler 없음 + rejected | `Debug` |
| throw 정책 실행 | `Warning` 후 throw |

publish 는 정상적인 subscriber 차이일 수 있으므로 기본 `Warning` 으로 두지 않는다.
운영 환경에서 publish miss 를 반드시 보고 싶으면 사용자가 옵션으로 레벨을 올릴 수
있어야 한다.

## 9. Message flow diagnostics

handler miss 로그는 message flow diagnostics 의 일부다. message 유실, timeout,
복사, relay 위치 문제를 찾으려면 "문제가 생긴 순간"만으로는 부족하고 message 가 어떤
단계를 통과했는지 볼 수 있어야 한다.

framework 는 diagnostics 를 세 층으로 나눈다.

| 층 | 목적 | 기본 상태 |
|----|------|-----------|
| 운영 로그 | handler 없음, drop, timeout, route 없음, reply error 를 사람이 확인한다 | `Warning` 이상 중심 |
| OpenTelemetry trace | message 가 hop 별로 어디를 지나갔는지 추적한다 | instrumentation 은 존재하되 exporter 는 사용자가 등록 |
| metrics | receive, dispatch, drop, timeout, size 분포를 숫자로 본다 | 낮은 비용의 counter/histogram 중심 |

### 9.1 flow event 단계

flow event 이름은 최대한 작고 안정적으로 유지한다.

| event | 의미 |
|-------|------|
| `received` | transport 나 runtime 이 message 를 받았다 |
| `decoded` | header 와 routing metadata 를 decode 했다 |
| `handler-resolved` | application handler 를 찾았다 |
| `handler-missing` | application handler 를 찾지 못했다 |
| `enqueued` | session, actor, spot 실행 queue 에 work item 을 넣었다 |
| `dispatched` | handler 실행을 시작했다 |
| `relay-selected` | session 또는 actor relay target 을 골랐다 |
| `sent` | outbound send 를 완료했다 |
| `reply-sent` | request reply 또는 error reply 를 보냈다 |
| `dropped` | 정책에 따라 message 를 버렸다 |
| `timed-out` | request 나 relay 가 timeout 으로 끝났다 |
| `failed` | 처리 중 예외나 runtime 오류가 발생했다 |

기본 운영 로그에서는 모든 event 를 출력하지 않는다. 상세 흐름은 trace 와 diagnostic
옵션을 켰을 때만 남긴다.

### 9.2 출력 형식

framework 는 message flow 를 직접 파일 포맷으로 쓰지 않는다. 대신 같은 event shape 를
`ILogger<T>`, `ActivitySource`, `Meter` 에 맞게 나누어 기록한다.

권장 JSON field shape 는 아래와 같다. 이 모양은 logger provider 가 JSON 을 출력할 때
검색 기준으로 삼을 수 있도록 문서화하는 예시다.

```json
{
  "event": "handler-missing",
  "surface": "channel",
  "kind": "request",
  "packetName": "StartBingoReq",
  "channelName": "bingo-room",
  "action": "reply-error",
  "reason": "no-handler",
  "messageId": "01J...",
  "correlationId": "01J...",
  "sizeBytes": 128,
  "partCount": 2
}
```

timestamp, log level, category, span id, trace id 의 실제 출력 이름은 logging provider
또는 OpenTelemetry exporter 가 정한다.

### 9.3 OpenTelemetry trace

진짜 tracing 은 OpenTelemetry `ActivitySource` 로 연결한다. framework 는 exporter 를
등록하지 않고 source 만 제공한다.

```csharp
internal static class ZLinkTelemetry
{
    public const string ActivitySourceName = "Zlink.Framework";
    public const string MeterName = "Zlink.Framework";

    public static readonly ActivitySource ActivitySource = new(ActivitySourceName);
    public static readonly Meter Meter = new(MeterName);
}
```

사용자는 host 에서 exporter 를 선택한다.

```csharp
builder.Services.AddOpenTelemetry()
    .WithTracing(tracing =>
    {
        tracing
            .AddSource("Zlink.Framework")
            .AddOtlpExporter();
    })
    .WithMetrics(metrics =>
    {
        metrics
            .AddMeter("Zlink.Framework")
            .AddOtlpExporter();
    });
```

권장 span 이름은 아래와 같다.

| span 이름 | kind | 의미 |
|-----------|------|------|
| `zlink.session.dispatch` | Consumer | session 이 client packet 을 받아 처리 |
| `zlink.channel.dispatch` | Consumer | channel 이 request/send/publish 를 받아 처리 |
| `zlink.dealer_mesh.send` | Producer | dealer mesh channel 로 outbound send |
| `zlink.dealer_mesh.request` | Client | dealer mesh channel 로 outbound request |
| `zlink.route.dispatch` | Consumer | route mesh channel 이 routed packet 을 받아 처리 |
| `zlink.spot.dispatch` | Consumer | Spot route/subscription packet 처리 |
| `zlink.actor.dispatch` | Consumer | Actor packet handler 처리 |
| `zlink.actor.relay` | Producer | session 에서 actor 로 relay |
| `zlink.message.send` | Producer | outbound message send |
| `zlink.message.request` | Client | outbound request |

권장 tag 는 아래와 같다.

| tag | 설명 |
|-----|------|
| `zlink.surface` | `session`, `channel`, `dealer-mesh-channel`, `route-mesh-channel`, `spot`, `actor`, `entry-spot` |
| `zlink.kind` | request, send, publish, response, error |
| `zlink.packet.name` | packet name |
| `zlink.message.id` | message 식별자 |
| `zlink.correlation.id` | correlation id |
| `zlink.channel.name` | channel 이름 |
| `zlink.session.id` | session id |
| `zlink.actor.id` | actor id |
| `zlink.spot.rid` | spot routing id |
| `zlink.source.rid` | source routing id |
| `zlink.target.rid` | target routing id |
| `zlink.request.seq` | request sequence |
| `zlink.action` | `dispatch`, `relay`, `drop`, `reply-error` 등 |
| `zlink.reason` | `no-handler`, `timeout`, `route-missing` 등 |
| `zlink.message.size_bytes` | 전체 message 크기 |
| `zlink.message.part_count` | multipart part 개수 |
| `zlink.message.payload_size_bytes` | payload 크기 |

trace tag 는 cardinality 를 조심해야 한다. `messageId`, `sessionId`, `actorId` 처럼 값
종류가 많은 field 는 사용자가 trace 를 켠 상태에서만 의미 있게 수집해야 한다.

### 9.3.1 trace context propagation

`ActivitySource` 로 span 을 만드는 것만으로는 hop 간 trace 가 이어지지 않는다. framework
는 outbound message 를 만들 때 current activity context 를 envelope metadata 에 주입하고,
inbound message 를 받을 때 metadata 에서 parent context 를 추출해야 한다.

이 절은 message flow diagnostics 의 완성형이다. 첫 구현은 local span, handler miss event,
drop/reply-error counter 까지 먼저 제공할 수 있다. 다만 envelope metadata 로 trace context 를
실어 나르는 설계와 충돌하는 public API 를 만들면 안 된다.

기본 규칙은 아래와 같다.

1. outbound request/send/publish/relay 를 만들 때 current `Activity.Current` 가 있으면
   W3C `traceparent`, `tracestate` 를 framework metadata 로 주입한다.
2. inbound dispatch 에서는 envelope metadata 에 있는 `traceparent`, `tracestate` 를 읽어
   parent context 로 사용한다.
3. inbound metadata 가 없으면 새 root span 을 만들 수 있다.
4. application metadata 와 framework tracing metadata 의 namespace 를 분리한다.
5. 사용자가 propagation 을 끄면 metadata 주입은 하지 않지만 local span 생성과 metrics 는
   계속 사용할 수 있다.

metadata key 는 W3C 표준 이름을 그대로 쓰되, application metadata forwarding 정책과
충돌하지 않도록 framework-reserved key 로 취급한다.

| key | 의미 |
|-----|------|
| `traceparent` | W3C trace context parent |
| `tracestate` | W3C trace state |
| `baggage` | 선택 사항. 기본 off 로 두고 명시적으로 켠 경우만 전달 |

`baggage` 는 민감 정보가 섞일 수 있으므로 기본 전달 대상에서 제외한다. 사용자가 켠 경우에도
allowlist 기반으로 제한해야 한다.

### 9.4 Metrics

metrics 는 "어느 구간에서 수가 줄었는지"를 먼저 찾기 위한 것이다. 개별 message 를
따라가는 trace 와 목적이 다르다.

권장 metric 은 아래와 같다.

| metric | 종류 | 설명 |
|--------|------|------|
| `zlink.messages.received` | counter | 수신한 message 수 |
| `zlink.messages.dispatched` | counter | handler 또는 relay 로 전달한 message 수 |
| `zlink.messages.dropped` | counter | 정책에 따라 drop 한 message 수 |
| `zlink.messages.handler_missing` | counter | handler miss 수 |
| `zlink.messages.reply_error` | counter | error reply 수 |
| `zlink.messages.timed_out` | counter | timeout 수 |
| `zlink.message.size_bytes` | histogram | message size 분포 |
| `zlink.message.payload_size_bytes` | histogram | payload size 분포 |
| `zlink.message.part_count` | histogram | multipart part count 분포 |
| `zlink.dispatch.duration_ms` | histogram | dispatch 처리 시간 |
| `zlink.relay.duration_ms` | histogram | relay 처리 시간 |

metric tag 는 낮은 cardinality 중심이어야 한다.

권장 tag:

- `surface`
- `kind`
- `action`
- `reason`

기본 metric tag 에 `messageId`, `sessionId`, `actorId` 는 넣지 않는다. cardinality 가 너무
높아 metric backend 비용과 성능 문제를 만들 수 있기 때문이다.

metric tag 값도 logging field 와 같은 lowercase/kebab-case 를 사용한다. 같은 사건이 log,
trace, metric 에 남을 때 `surface`, `kind`, `action`, `reason` 값이 달라지면 운영자가 같은
message 흐름을 서로 다른 쿼리로 찾아야 하므로 피한다.

### 9.5 Message size, part count, ref count

message size 와 part count 는 기본 진단 field 로 취급한다. payload 본문 없이도 큰 message
병목, multipart 손상, 특정 hop 의 크기 증가를 확인할 수 있기 때문이다.

기본 기록 대상:

- 전체 message size
- part count
- header size
- payload size

ref count 와 ownership 은 더 조심해야 한다. 이 값은 binding 과 native runtime 구현
세부에 가까우며, 모든 언어에서 같은 의미를 보장하기 어렵다. 따라서 diagnostic/verbose
모드에서만 optional field 로 기록한다.

| 필드 | 기본 기록 | diagnostic 기록 | 설명 |
|------|-----------|-----------------|------|
| `zlink.message.size_bytes` | 예 | 예 | 전체 message 크기 |
| `zlink.message.part_count` | 예 | 예 | multipart part 수 |
| `zlink.message.header_size_bytes` | 예 | 예 | header 크기 |
| `zlink.message.payload_size_bytes` | 예 | 예 | payload 크기 |
| `zlink.message.ref_count` | 아니오 | 가능 | native message ref count. 지원되는 runtime 에서만 기록 |
| `zlink.message.ownership` | 아니오 | 가능 | borrowed, owned, cloned, moved 같은 ownership 상태 |
| `zlink.message.native_handle_id` | 아니오 | 가능 | raw pointer 가 아닌 process-local diagnostic id |

raw pointer, unmanaged address, payload bytes 는 기록하지 않는다. native handle 을
식별해야 하면 raw address 대신 process 안에서만 의미가 있는 diagnostic id 를 만들어
사용한다.

### 9.6 Diagnostics 옵션

초기 public 옵션 형태는 아래로 제한한다.

```csharp
public enum ZLinkMessageFlowLogMode
{
    Off,
    ErrorsOnly,
    KeyTransitions,
    Verbose,
    Diagnostic
}

public interface IZLinkDiagnosticsOptions
{
    ZLinkMessageFlowLogMode MessageFlow { get; set; }

    double SampleRate { get; set; }

    bool IncludeMessageSizes { get; set; }

    bool IncludeNativeDiagnostics { get; set; }
}
```

권장 기본값:

| 옵션 | 기본값 |
|------|--------|
| `MessageFlow` | `ErrorsOnly` |
| `SampleRate` | `1.0` for errors, 낮은 값 for verbose flow |
| `IncludeMessageSizes` | `true` |
| `IncludeNativeDiagnostics` | `false` |

`IncludeNativeDiagnostics` 가 `false` 이면 ref count, ownership, native diagnostic id 를
기록하지 않는다.

## 10. 정책 옵션

옵션은 복잡한 rule engine 이 아니라 message kind 별 기본 action 을 바꾸는 정도로
제한한다.

```csharp
options.ConfigureDispatch(dispatch =>
{
    dispatch.Unhandled.Request = ZLinkUnhandledDispatchAction.ReplyError;
    dispatch.Unhandled.Send = ZLinkUnhandledDispatchAction.LogAndDrop;
    dispatch.Unhandled.Publish = ZLinkUnhandledDispatchAction.LogAndDrop;
    dispatch.Unhandled.PublishLogLevel = LogLevel.Debug;
});
```

초기 public 형태는 아래로 제한한다.

```csharp
public enum ZLinkUnhandledDispatchAction
{
    ReplyError,
    LogAndDrop,
    Drop,
    Throw
}

public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }

    ZLinkUnhandledDispatchAction Send { get; set; }

    ZLinkUnhandledDispatchAction Publish { get; set; }

    LogLevel SendLogLevel { get; set; }

    LogLevel PublishLogLevel { get; set; }
}
```

제약은 다음과 같다.

- `ReplyError` 는 request 에만 유효하다.
- send 또는 publish 에 `ReplyError` 를 설정하면 configuration validation 에서 실패한다.
- request 에 `Drop` 을 설정하면 caller 는 framework error reply 대신 timeout 또는 transport
  failure 를 볼 수 있다. 이 값은 handler miss 를 숨기므로 운영 기본값으로 쓰지 않는다.
- request 에 `Throw` 를 설정하면 reply target 을 신뢰할 수 있는 경우 error reply 를 먼저
  보내고 local dispatcher 에서 예외를 던진다. reply 를 보낼 수 없는 상태에서는 로그 후
  예외만 던진다.
- send/publish 의 `Drop` 은 로그 없이 버리는 동작이므로 기본값으로 두지 않는다.
- `Throw` 는 개발과 테스트에서 handler miss 를 즉시 드러내기 위한 action 이다. 운영 receive
  loop 를 조용히 죽이는 용도로 쓰면 안 된다. runtime loop 는 throw 정책으로 생긴 예외를
  loop 경계에서 기록하고 host 중단 요청이 아닌 한 다음 message 처리를 계속할 수 있어야 한다.
- surface 별 세부 정책이 필요해지기 전까지는 public 옵션을 늘리지 않는다.

`CloseSession` 은 공통 `Request/Send/Publish` 옵션에 넣지 않는다. session 에서만 의미가
있는 action 을 공통 enum 에 넣으면 Channel, DealerMeshChannel, Spot, Actor 에 같은 값이
적용되는지 validation 기준이 흐려진다. session packet 을 닫고 싶으면 application 이
`OnDispatchAsync(...)` 에서 직접 session close 를 선택한다.

다만 모든 상황을 `Request/Send/Publish` 세 값으로 표현하지는 않는다. 아래 항목은
message kind 정책과 분리된 surface-specific fixed policy 로 둔다.

| 상황 | 정책 |
|------|------|
| DealerMesh unexpected response/error | pending request 없음으로 log/drop |
| EntrySpot actor join handler 없음 | rejected reply |
| optional joined/left notification handler 없음 | no-op 또는 `Debug` |
| header decode 실패 | target 과 request sequence 를 신뢰할 수 없으므로 warning log 후 drop |
| valid request header + payload decode 실패 | error reply |

이 항목까지 public option 으로 모두 열면 설정 표면이 얕고 넓어진다. 처음 구현은 고정
정책으로 시작하고, 실제 운영 요구가 반복될 때 surface 별 option 을 추가한다.

## 11. Error reply 의미

request handler miss 는 application handler exception 과 구분되어야 한다.

권장 error kind 는 다음과 같다.

| 상황 | error kind |
|------|------------|
| handler 없음 | `HandlerNotFound` |
| payload decode 실패 | `PayloadDecodeFailed` |
| header decode 실패 | error reply 를 만들 수 없으면 로그 후 drop. error kind 를 wire 로 보내지 않는다 |
| DealerMesh pending request 없는 response/error | `UnexpectedReply` 는 로그/trace reason 으로만 사용하고 기본적으로 reply 를 보내지 않는다 |
| explicit actor relay target 없음 | `ActorRouteNotFound` |
| request target node route 없음 | `RouteNotConnected` 또는 `ZlinkRequestException(NotConnected)` |
| request target spot/actor 없음 | `SpotRouteNotFound`, `ActorRouteNotFound`, 또는 `ZlinkRequestException(NotFound)` |
| request timeout | `TimeoutException` 또는 timeout 전용 framework exception |
| DealerMesh request handler 없음 | `HandlerNotFound` |
| actor request handler 없음 | `ActorDispatchHandlerNotFound` |
| route request handler 없음 | `RouteHandlerNotFound` |

현재 framework error kind 에 위 이름이 없으면 첫 구현에서 `ZLinkFrameworkErrorKind` 를
확장한다. 최소 후보는 `HandlerNotFound`, `RouteHandlerNotFound`,
`ActorDispatchHandlerNotFound`, `PayloadDecodeFailed`, `RouteNotConnected`,
`RequestTargetNotFound`, `RequestRejected`, `RequestProtocolError`, `RequestFailed` 다.
wire error envelope 가 enum 값을 직접 담지 못하는 경우에도 message 에 원인 이름을 보존해
caller 가 로그와 exception message 로 구분할 수 있어야 한다.
`UnexpectedReply` 는 request caller 에게 돌려줄 error reply 가 아니라 수신 peer 의
diagnostics reason 이므로 enum 에 넣을 필요가 낮다.

## 12. 구현 방향

### 12.1 공통 helper

각 dispatcher 가 같은 문자열과 조건문을 반복하지 않도록 내부 helper 를 둔다.

```csharp
internal sealed class ZLinkUnhandledDispatchPolicy
{
    public ValueTask HandleRequestAsync(...);

    public ValueTask HandleSendAsync(...);

    public ValueTask HandlePublishAsync(...);
}
```

helper 는 다음 책임만 가진다.

- action 결정
- 로그 출력
- drop, throw 실행
- request error reply 에 필요한 exception 생성

실제 reply writer 는 surface 마다 다르므로 helper 가 직접 모든 transport 를 알면 안 된다.
request 는 dispatcher 가 가진 reply writer callback 을 넘기는 형태가 낫다.

helper 입력은 `surface`, `kind`, `packetName`, `channelName`, `sessionId`, `actorId`,
`correlationId`, `size` 같은 진단 field 를 담은 작은 context object 로 정리한다. 각
dispatcher 가 로그 문자열과 error exception 을 직접 조립하면 정책이 다시 흩어진다.

### 12.2 Request failure 변환

framework request path 는 submit 단계 실패와 completion 단계 실패를 모두 보존해서
application 에 전달해야 한다. 현재 구현처럼 `RequestResult != Ok` 를 모두 `TimeoutException`
으로 바꾸면 route 없음, target 없음, timeout 을 구분할 수 없다.

submit 단계에서 binding 이 false 를 반환하거나 `ZlinkSubmitException` 같은 submit 예외를
던지면, framework 는 그 원인을 timeout 으로 감싸지 않는다. `NotConnected` 는 target node
rid 로 가는 route 또는 transport 가 아직 없다는 뜻으로 전달한다. submit 이 성공한 뒤에는
reply callback 또는 completion helper 가 받은 `RequestResult` 를 아래 규칙으로 변환한다.

변환 규칙은 아래와 같다.

| `RequestResult` | framework 예외 의미 |
|-----------------|---------------------|
| `NotConnected` | target node rid 로 가는 route 또는 transport 가 없음 |
| `NotFound` | target spot, actor, local route 를 찾지 못함 |
| `TimedOut` | request deadline 초과 |
| `Rejected` / `Conflict` / `Busy` | target runtime 이 명시적으로 거부하거나 현재 처리 불가 |
| `InvalidArgument` | framework 가 잘못된 request envelope, routing id, payload 를 제출함 |
| `InvalidState` | socket, runtime, 또는 target 상태가 request 를 받을 수 없음 |
| `NotSupported` | 해당 surface 가 요청한 request 동작을 지원하지 않음 |
| `Terminated` | request 대기 중 runtime 또는 socket 이 종료됨 |
| `ProtocolError` | reply envelope, request sequence, 또는 transport protocol 오류 |
| `InternalError` | 좁은 의미로 분류하지 못한 runtime 오류 |

`ZLinkEnvelopeReplyCompletion` 과 `ZLinkRawReplyCompletion` 은 completion 단계의 위 결과를
구분하는 예외를 만들어야 한다. 최소 구현은 원래 `RequestResult` 를 보존하는
`ZLinkFrameworkException` 또는 `ZlinkRequestException` 을 inner exception 으로 담는 방식이다.
submitter 와 completion helper 중 어느 쪽에서 실패하더라도 `NotFound` 와 `NotConnected` 를
timeout 으로 변환하지 않는다.

현재 `ZLinkAsyncSubmitter` 같은 submit queue 가 `NotConnected` 를 retryable 상태로만 보고
최종적으로 generic `TimeoutException` 을 만들 수 있다면 이 부분도 수정 대상이다.
`Backpressured` 처럼 socket writable 신호로 풀릴 수 있는 상태와, target route 가 없어
request 를 제출할 수 없는 `NotConnected` 를 같은 queue timeout 으로 합치면 안 된다.
구현은 최소한 request path 에서 terminal submit failure 를 보존하고, retry 를 선택하더라도
최종 예외에는 원래 submit 결과가 남아야 한다.

### 12.3 Session 책임 경계

Session 은 등록형 handler dispatch 가 아니므로 runtime 이 callback 이후에 추가 처리를
실행하면 안 된다. actor relay 도 framework 가 자동으로 수행하면 안 된다. Session packet 은
application callback 으로 전달된 순간 application 이 처리 책임을 가진다.

Session callback 은 packet 마다 아래 중 하나를 명시적으로 선택한다.

1. session 에서 직접 처리한다.
2. application 이 actor 를 선택하고 `RelayToActorAsync(...)` 를 호출한다.
3. request 라면 `Reply(...)` 로 성공 또는 error 의미의 응답을 보낸다.
4. send 라면 의도적으로 drop 하고 application logger 로 기록할 수 있다.
5. protocol 위반이면 session 을 닫거나 application error 처리를 수행한다.

application 이 직접 target actor 를 고르고 싶으면 `OnDispatchAsync(...)` 안에서 직접
selector 를 구현하고 `RelayToActorAsync(...)` 를 호출한다.

framework 는 Session 에 대해 handler miss error reply, log/drop, session close 같은
unhandled 정책을 자동 적용하지 않는다. 이런 결정을 framework 가 대신하면 Session callback
표면의 의미가 흐려지고, application protocol 이 framework 내부 정책에 종속된다.

### 12.4 Channel dispatcher

`ZLinkChannelPacketDispatcher` 는 handler registry 조회 실패를 잡아 request 와 send,
publish 정책을 나눠야 한다.

- request: error reply
- send: log/drop
- publish: log/drop

현재 registry 가 handler 없음에서 어떤 예외를 던지는지 확인하고, dispatcher 에서
명확히 `TryGet...` 형태로 바꾸는 것이 좋다. handler miss 를 예외 흐름으로만 처리하면
정책 적용 지점이 흐려진다.

### 12.5 DealerMeshChannel dispatcher

DealerMeshChannel 은 inbound 와 outbound 를 모두 처리한다. 구현은 request/reply pending
map 과 inbound handler dispatch 를 같은 DEALER socket receive loop 에서 분리해야 한다.

outbound `Request(...)` 호출이 native/binding callback 안에서 reply 를 완료하고,
inbound request 가 framework receive loop 로 올라오지 않는 구조는 DealerMesh inbound
dispatch 와 충돌한다. 같은 DEALER socket 에서 request/send 와 response/error 를 framework 가
구분해야 하는데, native callback 이 request/reply control frame 을 내부에서 소비하면 handler
miss 정책을 적용할 수 없기 때문이다.

DealerMesh request 는 아래 두 방식 중 첫 번째 방식을 선택한다.

| 방식 | 장점 | 단점 | 선택 기준 |
|------|------|------|-----------|
| core/binding DEALER request receive/reply API 추가 | native request/reply 의미를 유지하고 peer reply target 을 core 가 숨긴다 | core 와 binding surface 변경이 필요하다 | 여러 peer mesh 에서 deterministic reply 가 필요하므로 기본 선택 |
| framework envelope 를 send 로 보내고 correlation id 로 pending map 관리 | framework 코드만으로 request envelope 를 통일하기 쉽다 | DEALER 가 source peer/directed send 를 노출하지 않으면 reply target 을 보장할 수 없다 | core 가 source peer identity 를 public 으로 제공할 때만 가능 |

send handler 는 일반 DEALER receive 로 받을 수 있지만, request handler miss 에 error reply 를
돌려주는 계약은 request receive/reply API 가 있어야 한다. 따라서 framework 는 native callback
기반 request completion 과 DealerMesh receive loop 를 동시에 사용하지 않는다.

DealerMesh 는 framework 가 소유하는 pending map 또는 native completion 과 receive loop 의
소유권을 한 곳에서 정해야 한다. 두 경로가 같은 socket 을 동시에 소비하면 response/error 가
pending request 완료로 가지 못하거나 request/send 가 application handler 로 가지 못한다.
선택한 core/binding API 는 이 소유권 경계를 명확하게 보장해야 한다.

receive loop 는 다음 순서로 동작한다.

1. response/error 이고 request sequence 또는 correlation 이 pending map 에 있으면 pending request 를 완료한다.
2. response/error 이지만 pending request 가 없으면 unexpected reply 로 log/drop 한다.
3. request 이면 inbound request handler 를 찾아 dispatch 하고 reply 한다.
4. request handler 가 없으면 error reply 를 보낸다.
5. send 이면 inbound send handler 를 찾아 dispatch 한다.
6. send handler 가 없으면 log/drop 한다.

outbound path 에도 message flow telemetry 를 심어야 한다.

- submit 시점에 send/request event 를 남긴다.
- peer 연결이 없거나 socket 이 준비되지 않았으면 not-connected 또는 backpressure 로 기록한다.
- request 는 request sequence, deadline, timeout, cancellation 을 기록한다.
- reply 를 받으면 response/error 와 duration 을 기록한다.
- target peer 가 handler miss 로 error reply 를 보낸 경우 source DealerMeshChannel 은
  handler miss 가 아니라 error reply 수신으로 기록한다.

구현 checklist 는 아래와 같다.

1. core/binding 에 DEALER request 수신과 deterministic reply public API 를 제공한다.
   framework 는 binding 내부 멤버를 reflection 으로 호출하지 않는다.
2. `IZLinkBackendDealerSocket` 과 dotnet binding wrapper 에 non-blocking receive 와
   request receive/reply API 를 둔다.
3. DealerMesh bundle 은 outbound submitter 와 같은 DEALER socket 을 소유하는 receive loop 를
   함께 가진다.
4. `ZLinkChannelRuntimeManager` 는 DealerMesh client 역할 시작 시 inbound pump 도
   시작한다.
5. DealerMesh dispatcher 는 response/error 를 pending completion 으로 먼저 보내고,
   request/send 는 handler dispatch 로 보낸다.
6. `ZLinkChannelRegistrationValidator` 는 DealerMesh handler group, send handler,
   request handler 에 기존 Channel handler 노출 검증을 적용하고 publish handler 는 거부한다.
7. framework service 등록 단계는 DealerMesh 에 노출된 handler type 을 자동 service 등록
   대상에 포함한다.

### 12.6 RouteMeshChannel dispatcher

`ZLinkRoutePacketDispatcher` 는 route handler registry 조회 실패를 잡아야 한다.

- internal packet 은 먼저 처리한다.
- internal packet 도 아니고 application handler 도 없으면 unhandled policy 를 적용한다.
- request 는 source routing id 가 있어야 error reply 를 보낼 수 있다. source routing id
  가 없으면 로그 후 drop 또는 throw 정책으로 처리한다.

### 12.7 Spot route dispatcher

`ZLinkSpotRouteDispatcher` 는 `packets.TryResolve(...)` 실패 시 조용히 return 하지 않는다.

- request 여부를 header 에서 알 수 있으면 request 는 error reply 를 만든다.
- send 는 log/drop 한다.
- response 는 pending request completion 경로가 먼저 처리한다. pending request 가 없으면
  unexpected response 로 log/drop 한다.
- framework internal kind 는 internal dispatcher 가 먼저 소비한다. internal dispatcher 도
  모르는 packet 이면 application handler miss 로 보지 않고 protocol error 로 log/drop 한다.

### 12.8 Actor dispatcher

`ZLinkSpotActorPacketDispatcher` 와 `ZLinkActorPacketDispatcher` 는 handler fallback 을
모두 시도한 뒤에도 없을 때 정책을 적용한다.

request 에서 handler 가 없으면 error reply 로 전달한다. send 는 조용히 return 하지 않고
log/drop 한다. handler miss 를 일반 handler exception 과 구분하기 위해 `HandlerNotFound`
계열 error kind 를 사용한다.

### 12.9 EntrySpot actor join

`ZLinkSpotActorJoinDispatcher` 는 rejected reply 를 유지하되 이유별 로그를 추가한다.

- join handler 없음
- target actor 없음
- handler exception
- application reject

handler exception 은 join protocol 에서 accepted=false rejected reply 로 돌려주고
`Warning` 로그를 남긴다. join wire 가 일반 request error envelope 를 지원하지 않는 한,
handler exception 을 별도 error reply 형태로 바꾸지 않는다.

### 12.10 Telemetry 구현

telemetry 구현은 hot path 비용을 줄여야 한다.

- log level 과 diagnostics mode 를 먼저 확인한 뒤 field 객체를 만든다.
- payload body 는 decode 하지 않고 size 만 계산한다.
- OpenTelemetry `ActivitySource` 는 listener 가 없으면 span 생성 비용이 낮아야 한다.
- metrics tag 는 low-cardinality field 만 사용한다.
- ref count 와 native diagnostic id 는 diagnostic mode 에서만 조회한다.

## 13. 테스트 계획

테스트는 public surface 계약, runtime policy, end-to-end flow, diagnostics 를 분리해서 둔다.
handler miss 정책은 transport 별 reply writer 와 receive loop 를 건드리므로 단일 테스트
종류로 충분하지 않다.

테스트도 두 단계로 나눈다. 첫 구현 gate 는 handler miss 결과, request 실패 분류,
Session 책임 경계, DealerMesh deterministic reply 전제, 기본 logging/trace/metrics 가
깨지지 않는지를 본다. trace context 전파, duration histogram, native ref count 같은 고급
진단 항목은 같은 설계 안의 후속 gate 로 둔다.

### 13.1 Contract tests

| 테스트 | 기대 결과 |
|--------|-----------|
| `IZLinkDealerMeshChannelBuilder` surface | `AddHandlerGroup`, `AddSendHandler`, `AddRequestHandler` 가 public 계약에 존재한다 |
| DealerMesh handler exposure validation | DealerMesh 에 send/request handler 와 handler group 을 등록할 수 있고 publish handler 는 거부된다 |
| registered handler DI | 옵션에 등록된 Channel, DealerMesh, RouteMesh, Spot, Actor, Session 관련 application type 이 별도 수동 등록 없이 DI 로 생성된다 |
| unhandled option validation | send/publish 에 `ReplyError` 를 설정하면 validation 실패 |
| diagnostics option defaults | `MessageFlow=ErrorsOnly`, `IncludeMessageSizes=true`, `IncludeNativeDiagnostics=false` |
| handler miss error kind | handler miss 전용 `ZLinkFrameworkErrorKind` 값이 존재한다 |

### 13.2 Unit tests

| 테스트 | 기대 결과 |
|--------|-----------|
| session callback 책임 경계 | callback 정상 반환 뒤 framework 가 추가 fallback 을 실행하지 않는다 |
| session explicit actor relay | application 이 선택한 actor 로 relay |
| channel request handler 없음 | error reply |
| channel send handler 없음 | warning log, drop |
| channel publish handler 없음 | debug log, drop |
| dealer mesh request handler 없음 | error reply |
| dealer mesh send handler 없음 | warning log, drop |
| dealer mesh receive loop kind 분기 | response/error 는 pending completion 으로, request/send 는 handler dispatch 로 분리된다 |
| dealer mesh unexpected response | pending request 없음으로 log/drop |
| dealer mesh target error reply | source 에서는 error reply 수신으로 기록 |
| dealer mesh handler DI | explicit handler 의 constructor dependency 가 주입된다 |
| request submit NotConnected | timeout 이 아니라 route unavailable 예외로 전달 |
| request submit Backpressured | queue retry 뒤 성공하거나 submit timeout 으로 실패하되 원인을 보존 |
| request submit terminal failure | retry 불가능한 submit 실패가 generic timeout 으로 변환되지 않음 |
| request completion NotConnected | timeout 이 아니라 route unavailable 예외로 전달 |
| request completion NotFound | timeout 이 아니라 target not found 예외로 전달 |
| request completion TimedOut | timeout 예외로만 전달 |
| route request handler 없음 | error reply |
| route send handler 없음 | warning log, drop |
| spot route request handler 없음 | error reply |
| spot route send handler 없음 | warning log, drop |
| actor request handler 없음 | error reply |
| actor send handler 없음 | warning log, drop |
| EntrySpot actor join handler 없음 | rejected reply, debug log |
| header decode 실패 | warning log, drop |
| valid request header + payload decode 실패 | error reply |
| logging provider 없음 | dispatch 가 실패하지 않음 |

### 13.3 E2E tests

| 경로 | 검증 |
|------|------|
| Stream session → ActorGateway | session callback 이 actor 를 선택하고 `RelayToActorAsync(...)` 를 호출하면 actor 로 relay 된다 |
| Stream session callback drop | callback 이 아무 작업 없이 정상 반환하면 framework 가 error reply 나 relay 를 자동 수행하지 않는다 |
| Channel request | handler 없는 request 에 error reply 가 온다 |
| Channel send | handler 없는 send 가 connection 을 끊지 않고 drop 된다 |
| Channel publish | handler 없는 publish 가 connection 을 끊지 않는다 |
| DealerMesh inbound request | peer A 가 보낸 request 를 peer B DEALER receive loop 가 dispatch 한다 |
| DealerMesh inbound request handler 없음 | peer B 가 handler miss 를 error reply 로 돌려준다 |
| DealerMesh inbound send | peer A 가 보낸 send 를 peer B DEALER receive loop 가 dispatch 한다 |
| DealerMesh inbound send handler 없음 | peer B 가 warning log 후 drop 한다 |
| DealerMesh unexpected response | pending request 가 없으면 peer 가 log/drop 하고 계속 동작한다 |
| DealerMesh concurrent request/reply | 여러 pending request 와 inbound request/send 가 같은 socket 에 섞여도 correlation 이 깨지지 않는다 |
| Routed request target node 없음 | client 가 timeout 이 아니라 route unavailable 오류를 받는다 |
| Routed request target spot 없음 | client 가 timeout 이 아니라 target not found 오류를 받는다 |
| RouteMesh request | handler 없는 routed request 가 error reply 로 끝난다 |
| Spot request | handler 없는 spot request 가 timeout 이 아니라 error reply 로 끝난다 |
| Actor request | handler 없는 actor request 가 timeout 이 아니라 error reply 로 끝난다 |

### 13.4 Diagnostics tests

첫 구현 gate:

| 테스트 | 기대 결과 |
|--------|-----------|
| OpenTelemetry tracing | `ActivitySource` listener 가 dispatch span 을 받는다 |
| metrics | received, dispatched, dropped, handler_missing counter 가 증가한다 |
| message size 기록 | payload 본문 없이 size/part count 만 기록 |
| native diagnostics off | ref count/native handle id 가 로그와 trace 에 남지 않음 |
| raw pointer masking | unmanaged address 가 로그나 trace tag 에 직접 남지 않는다 |

후속 gate:

| 테스트 | 기대 결과 |
|--------|-----------|
| OpenTelemetry propagation | peer 간 request trace 가 같은 trace id 로 이어진다 |
| propagation off | outbound envelope 에 `traceparent` 를 넣지 않지만 local span 은 생성 가능하다 |
| duration histogram | dispatch/relay duration histogram 이 기록된다 |
| native diagnostics on | 지원 runtime 에서 ref count/ownership field 기록 |
| baggage 기본 off | baggage 가 기본 전파되지 않는다 |

### 13.5 Sample regression

sample 에서 아래 패턴이 다시 생기지 않도록 회귀 테스트를 둔다.

- sample 별 actor handle 상태 저장소
- handler miss 에서 조용한 `return`
- `payload.Move()` 를 session handler 호출마다 반복하는 코드
- DealerMesh 를 outbound 전용 client 로만 설명하는 sample 문구
- payload 본문을 로그로 출력하는 sample logging 코드

## 14. 문서 반영 계획

이 draft 는 framework-level 설계 기준으로 유지한다. 정식 공개 계약으로 승격할 때는 아래
정식 문서와 guide 로 나누어 반영한다.

### 14.1 Core / binding spec 문서

| 문서 | 반영 내용 |
|------|-----------|
| `doc/spec/core/` 아래 request/reply 또는 socket spec | `zlink_dealer_recv_part` 의 message kind, request sequence 출력, 반환값, errno, ownership 계약 |
| `doc/spec/core/` 아래 errno spec | DEALER receive/reply API 의 `EAGAIN`, `ENOENT`, `EHOSTUNREACH`, `EPROTO`, `EINVAL` 의미 |
| `doc/spec/bindings/` | DEALER inbound message kind, request token, directed reply, message ownership 규칙 |
| 각 binding public API 문서 | binding 별 `DealerReceived` 또는 같은 의미의 type, `RequestFrame`, `RecvDealer`, `Reply` 표면 |
| `core/include/zlink/socket.h` 기준 API 목록 | 새 C API export 와 `zlink_dealer_message_type_t` enum 값 |

core C API 문서는 `core/include/zlink/socket.h` 와 `core/include/zlink_enum.h` 를 기준으로
작성한다. framework draft 의 설명이 core header 보다 넓은 보장을 만들면 안 된다.
binding 문서는 각 언어에서 이름이 달라도 같은 세 가지 능력, 즉 outbound request frame,
inbound kind 분류, request token 기반 directed reply 가 보존되는지 확인한다.

### 14.2 Framework 공통 문서

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/spec/draft/framework-unhandled-dispatch-policy.ko.md` | 구현 완료 뒤 완료/폐기 상태 표시 |
| `framework/doc/spec/` 아래 새 정식 spec | framework 공통 unhandled dispatch 정책 요약 |
| `framework/doc/spec/` 아래 diagnostics spec | logging, OpenTelemetry, metrics, message size/ref count 정책 |
| `framework/doc/plan/` | 구현 순서와 완료 체크리스트 |

### 14.3 .NET spec 문서

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | handler miss 기본 정책과 logging 방식 |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | DealerMesh inbound handler API delta |
| `framework/languages/dotnet/doc/spec/aspnet-core-channel-messaging.ko.md` | Channel, DealerMesh, RouteMesh unhandled 정책 |
| `framework/languages/dotnet/doc/spec/aspnet-core-channel-messaging.ko.md` | DealerMesh bidirectional dispatch, receive loop, unexpected response 정책 |
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | explicit actor relay 와 application selector 정책 |
| `framework/languages/dotnet/doc/spec/aspnet-core-stream.ko.md` | session callback 책임 경계와 명시적 처리 규칙 |
| `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md` | Spot, EntrySpot handler miss 정책 |
| `framework/languages/dotnet/doc/spec/aspnet-core-monitoring.ko.md` | ActivitySource, Meter, log category, event id |
| `framework/languages/dotnet/doc/internals/di-capability-exposure-policy.ko.md` | `ILogger<T>` 사용과 별도 logger API 비추가 이유 |

### 14.4 .NET guide / sample 문서

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/guide/06-actor-session.ko.md` | actor 를 명시적으로 선택해 relay 하는 사용법 |
| `framework/languages/dotnet/doc/guide/07-stream.ko.md` | session callback 에서 직접 처리, reply, relay, drop 을 선택하는 사용법 |
| `framework/languages/dotnet/doc/guide/samples/channel-messaging-samples.ko.md` | DealerMesh inbound/outbound 예제 |
| `framework/languages/dotnet/doc/guide/samples/stream-samples.ko.md` | session callback 책임 경계와 명시적 relay 예 |
| 새 diagnostics guide 또는 monitoring guide | OpenTelemetry 설정, JSON logging, metrics, size/ref count 정책 |

### 14.5 Site 문서

정식 문서 반영 뒤 `doc/site/docs/` 에도 같은 의미를 반영한다. site 문서는 guide 성격이므로
내부 receive loop 나 registry 구현 세부는 넣지 않고, 사용자가 설정하고 관측하는 방법을
중심으로 쓴다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/site/docs/api/` | 변경된 public API 표면 |
| `doc/site/docs/guide/` | unhandled 정책, diagnostics 설정 방법 |
| `doc/site/docs/internals/` | DealerMesh receive loop, telemetry pipeline 의 유지보수자용 구조 링크 |

정식 spec 문서에는 구현 완료 전까지 이 초안의 세부 계약을 섞어 쓰지 않는다. 필요한 경우
짧은 링크만 둔다.

## 15. POSD 검토

이 정책은 다음 설계 문제를 줄이기 위한 것이다.

- **Session 책임 경계 분리**: Session callback 은 application protocol 표면으로 두고,
  handler registry 기반 unhandled 정책과 섞지 않는다.
- **정보 은닉**: handler registry miss, request reply writer 차이를
  application handler 에 노출하지 않는다.
- **복잡성을 아래로 이동**: 사용자는 request/send/publish 의 기본 실패 의미를 외우지
  않아도 된다.
- **오류를 정의로 제거**: handler 없음이 경로마다 timeout, silent drop, exception 으로
  갈라지지 않고 정의된 결과로 끝난다.
- **특수 코드와 범용 코드 분리**: sample 은 자기 protocol handler 만 담고, framework 는
  handler registry 기반 공통 unhandled 정책을 소유한다.
- **정보 은닉**: ref count, native handle 같은 내부 진단 정보는 diagnostic mode 로
  제한해 일반 application 표면으로 새지 않게 한다.

## 16. 구현 제약과 후속 확인

1. `ZLinkFrameworkErrorKind` 에 handler miss 전용 kind 를 추가한다. 최소 후보는
   `HandlerNotFound`, `RouteHandlerNotFound`, `ActorDispatchHandlerNotFound`,
   `PayloadDecodeFailed`, `RouteNotConnected`, `RequestTargetNotFound`,
   `RequestRejected`, `RequestProtocolError`, `RequestFailed` 다.
2. `CloseSession` 은 공통 unhandled action 에 넣지 않는다. session close 는 application
   callback 이 직접 선택한다.
3. header decode 실패는 target 과 request sequence 를 신뢰할 수 없으므로 warning log 후
   drop 한다. header 는 정상이고 payload decode 만 실패한 request 는 error reply 를 만든다.
4. handler miss 는 첫 구현에서 logger, trace, metrics 로만 남긴다. 별도 runtime monitoring
   event 는 운영 요구가 반복될 때 추가한다.
5. message flow 기본값은 `ErrorsOnly` 로 둔다. verbose flow 는 명시적으로 켠다.
6. OpenTelemetry tag 는 안정적인 messaging semantic convention 과 맞출 수 있는 항목은
   맞추고, zlink 고유 항목은 `zlink.*` prefix 를 사용한다.
7. core/native ref count 는 첫 구현에서 public API 로 요구하지 않는다. 지원되는 backend 가
   internal diagnostic hook 을 제공할 때만 diagnostic field 로 기록한다.

## 17. 구현 완료 판단 기준

이 draft 의 구현은 아래 조건을 모두 만족해야 완료로 본다.

| 조건 | 완료 기준 |
|------|-----------|
| DealerMesh request handler API 와 DEALER request receive/reply core API 가 함께 존재한다 | handler miss 에 error reply 를 deterministic 하게 보낼 수 있다 |
| DealerMesh 가 native `Request(...)` callback 과 framework receive loop 를 동시에 사용하지 않는다 | 같은 socket 의 response/error 와 request/send 소유권이 framework receive loop 한 곳에 모인다 |
| Channel, RouteMesh, Spot, Actor handler miss 가 정책을 통과한다 | 사용자에게 message drop 원인이 log, trace, metric 중 하나 이상으로 보인다 |
| request submit `NotConnected` 또는 completion `NotFound` 가 보존된다 | route 없음, target 없음, timeout 을 구분할 수 있다 |
| Session callback 이후 framework 가 actor 전달이나 error reply 를 대신 시도하지 않는다 | Session protocol 책임이 application callback 안에 남는다 |
| logging provider 가 없어도 dispatch 가 계속 동작한다 | logging 은 관측 수단이지 runtime 필수 의존성이 아니다 |
| payload 본문이나 secret 이 log, trace, metric tag 에 남지 않는다 | 운영 진단이 보안 위험을 만들지 않는다 |

반대로 아래 조건은 첫 구현 완료에 필수는 아니다.

| 조건 | 이유 |
|------|------|
| 모든 surface 별 세부 unhandled option 제공 | 초기 public 옵션은 request/send/publish action 으로 제한한다 |
| zlink 전용 logging provider | 표준 `ILogger<T>` provider 와 충돌하므로 제공하지 않는다 |
| OpenTelemetry exporter 자동 등록 | exporter 선택은 host application 책임이다 |
| hop 간 trace context propagation 완성 | local span 과 기본 event 를 먼저 넣고 envelope metadata 설계와 함께 후속 구현한다 |
| dispatch/relay duration histogram 전체 적용 | handler miss 정책과 request 실패 분류 뒤에 hot path 비용을 확인하며 넓힌다 |
| native ref count 를 모든 binding 에서 동일하게 노출 | runtime 별 의미가 달라 diagnostic optional field 로만 다룬다 |
