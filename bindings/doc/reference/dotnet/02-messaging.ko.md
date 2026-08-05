한국어 | [English](02-messaging.en.md)

[레퍼런스 목차](README.ko.md)

# 02. Messaging

이 category는 message 소유권, receive envelope 타입(`Received`, `TopicMessage`,
`SubscriptionEvent`), 그리고 Sockets category의 모든 socket-type 진입점이 반환하는
send/request/reply operation-builder 공유 형태를 다룬다. 정확한 signature는
[`Contracts/Messaging/`](../../../../bindings/dotnet/src/Zlink/Contracts/Messaging/)이
소유한다. `Contracts/Messaging/MessageEnvelopeParts.cs`는 `internal`이라 public contract
항목이 없다.

---

## `Message`

zlink message payload 하나를 소유한다 — 모든 send·request·reply·receive API가 옮기는
단위다.

```csharp
using Message empty = new Message();
using Message sized = new Message(4096);
using Message copy = Message.From("payload"u8);
using Message fromString = Message.From("hello", Encoding.UTF8);
```

**Options.** 생성자: `Message()`(빈 메시지), `Message(int size)`(쓰기 가능 storage,
음수 크기는 `ArgumentOutOfRangeException`), `Message(ReadOnlySpan<byte>)`/
`Message(ReadOnlyMemory<byte>)`(스냅샷 복사). Static factory: `From(byte[])`,
`From(ReadOnlySpan<byte>)`, `From(ReadOnlyMemory<byte>)`, `From(ReadOnlySequence<byte>)`,
`From(Message)`(다른 메시지의 payload 복사), `From(string)`/`From(string, Encoding)`
(기본 UTF-8). Instance member: `Size`, `IsEmpty`, `RefCount`, `Allocate(int)`(static),
`AsSpan()`/`AsReadOnlySpan()`(이 인스턴스 storage에 backing된 쓰기/읽기 전용 view),
`AsReadOnlyMemory()`(native-backed message는 여기서 managed memory로 복사됨),
`ToArray()`, `CopyTo(Span<byte>)`/`CopyTo(IBufferWriter<byte>)`/
`TryCopyTo(Span<byte>, out int)`, `GetString()`/`GetString(Encoding)`.

**Completion result.** 모든 member는 동기다. `Message`는
`IDisposable`/`IAsyncDisposable`이다 — 해제하면 payload storage가 반환된다. span을
반환하는 member는 메시지가 해제·이동되지 않은 동안만 유효하다 — 이 메시지를 소비하는
submit(Sockets/Messaging builder category)은 성공 후 managed instance를 빈 상태로
남긴다 — 그 후 payload를 읽으면 예외가 발생하지만, disposing은 여전히 안전하고 pooled
instance를 반환하기 위해 여전히 필요하다.

**선택 기준.** outbound payload를 만들 땐 크기 지정 또는 스냅샷 복사 생성자/factory를
쓴다. 추가 복사 없이 그대로 읽거나 쓸 땐 `AsSpan()`/`AsReadOnlySpan()`을, 독립된
managed 복사가 허용될 땐 `ToArray()`/`GetString()`을 쓴다. `RefCount`는 아직 자신의
storage를 직접 소유한(이동·해제되지 않은) 메시지에서만 native reference count를
보고한다.

---

## `Received.Create()`

caller-provided-storage receive 형태를 위한 재사용 가능한 receive envelope을 만든다.

```csharp
using Received received = Received.Create();
bool ok = dealer.Recv(received);
```

**Options.** 인자 없음. `Received`는 public 생성자가 없다 — `Create()`만 있다.

**Completion result.** `Received`를 동기로 반환한다. caller가 소유하며 반드시
dispose해야 한다. receive API(Sockets category)는 성공적인 호출마다 내부 상태를
덮어쓴다 — 매 message마다 새로 할당하지 말고 같은 instance를 receive loop 전체에서
재사용한다.

**선택 기준.** receive loop·스레드마다 `Received` 하나를 만들어 그 이후 모든
`Recv(Received, ...)` 호출에 넘긴다. message마다 새 instance를 할당하지 않는다.

---

## `Received` member

envelope 메타데이터·message part를 읽거나, envelope의 source를 향한 reply/send를
시작한다.

```csharp
if (received.RequestSeq is { } seq)
{
    received.Reply().Message(Message.From("ok")).Submit();
}
Message first = received.FirstPart();
```

**Options.** 읽기 전용 속성: `RoutingId`(`RoutingId?`, receive 경로가 제공할 때만 존재),
`RequestSeq`(`ulong?`, reply 가능할 때만 존재), `MessageType`(`ReceivedMessageType`:
`Raw`/`Request`/`Reply`/`ErrorReply`), `Parts`(`IReadOnlyList<Message>`), `IsSinglePart`.
Method: `FirstPart()`(소유권 이전 없음), `SinglePartOrThrow()`(multipart면 예외),
`Reply()`(`RequestSeq`가 값을 가질 때만 유효 — 아래 공유 builder 형태 참고),
`Send()`(envelope의 source route로 향함).

**Completion result.** 모든 member는 동기다. `Dispose()`는 다른 API가 이미 소유권을
이전하지 않은 한 이 envelope이 소유한 message part를 해제한다. `Reply()`/`Send()`는
아래에 문서화된 공유 operation-builder 형태의 builder를 반환한다 — 제출하면 `Message`
part가 submit 시 소비되는 것과 같은 방식으로 이 envelope이 포착한 route가 소비된다.

**선택 기준.** `MessageType`/`RequestSeq`로 분기해 envelope이 reply 가능한지 판단한다.
source route를 따로 찾지 않고 요청에 답하려면 `Reply()`를, 같은 source로 향한
non-reply 메시지엔 `Send()`를 쓴다.

---

## `TopicMessage`

수신된 publish 하나 — topic, source routing id, message part를 담는다.

```csharp
using TopicMessage published = new TopicMessage();
bool ok = sub.Recv(published);
string topic = published.Topic;
```

**Options.** public 생성자 `TopicMessage()`(`Received`와 달리 이 타입은 factory가 아니라
직접 생성한다). 읽기 전용 member: `RoutingId`(`RoutingId?`), `Topic`(topic byte에서
지연 디코딩), `Parts`, `IsSinglePart`, `FirstPart()`, `SinglePartOrThrow()`.

**Completion result.** 동기다. `Dispose()`는 이 instance가 소유한 part를 해제한다.

**선택 기준.** `Received`를 재사용하는 것과 같은 방식으로 subscribe-receive loop 전체에서
instance 하나를 재사용하며, 매 호출마다 SUB/XSUB receive API(Sockets category)에 넘긴다.

---

## `SubscriptionEvent`

XPUB socket이 관찰한 구독자 한 명의 subscribe·unsubscribe를 보고한다.

```csharp
using SubscriptionEvent evt = new SubscriptionEvent();
bool ok = xpub.Recv(evt);
```

**Options.** public 생성자 `SubscriptionEvent()`. 읽기 전용 member: `RoutingId`
(`RoutingId?`), `Topic`(`string`), `Subscribed`(`bool` — 이 event가 topic을
구독했는지 구독 취소했는지). `SubscriptionEntry(string Filter, bool IsPattern)`는 활성
구독 하나(filter와 pattern 여부)를 기술하는 관련 record다.

**Completion result.** 동기다. dispose 없음 — 이 타입은 자신의 native resource를
소유하지 않는다.

**선택 기준.** application 레벨 프로토콜 메시지로 구독자 변동을 추론하는 대신, XPUB
socket의 subscription-event receive 경로(Sockets category)에서 이를 관찰한다.

---

## Send / request / reply operation-builder 형태

Sockets category의 모든 `Send`, routed send, `Publish`, `Request`, `Reply` 진입점이
반환하는, part·flag·terminal submit을 누적하는 fluent builder.

```csharp
dealer.Send(routingId).Message(Message.From("part-1")).Message(Message.From("part-2")).Submit();

IReadOnlyList<Message> reply = await dealer
    .Request(routingId)
    .Message(Message.From("payload"))
    .Timeout(TimeSpan.FromSeconds(5))
    .Async();

received.Reply().Message(Message.From("ok")).Submit();
```

**Options.** `SendOperation.Message(Message)`가 chain을 시작하고,
`SendSubmitOperation.Message(...)`가 part를 추가하며, `.Flags(SendFlags)`가 submit-time
flag를 설정하고, `.Submit()`이 terminal이다. `RequestOperation`/`RequestSubmitOperation`은
같은 형태에 `.Timeout(TimeSpan)`을 더한 것이다. `RequestSubmitOperation`에서
`.Flags(...)`를 호출하면 builder가 `RequestCallbackSubmitOperation`으로 좁혀지고,
awaitable `.Async()` 경로가 사라진다 — 그 지점 이후엔 `.Submit(RequestCallback)`만
도달 가능하다. `ReplyOperation`/`ReplySubmitOperation`은 `SendOperation`/
`SendSubmitOperation`과 같은 형태지만 flags 단계가 없다(core reply 함수가 send-flag
인자를 받지 않기 때문이다). `Messages(IReadOnlyList<Message>)` extension
(`MessageOperations`)은 네 family 전체의 모든 단계에서 쓸 수 있는 builder modifier다 —
여러 part를 순서대로 한 번에 추가하고 같은 builder 타입을 반환한다. 독립된 진입점이
아니다.

**Completion result.** `SendSubmitOperation.Submit()`/`ReplySubmitOperation.Submit()`은
동기다 — `Send`의 반환값은 `bool`(`SendFlags.DontWait`가 설정되고 send가 block됐을
때만 `false` — 그 외 모든 실패는 `ZlinkException`을 던진다), `Reply`의 반환값은
`void`다. `RequestSubmitOperation.Async(CancellationToken)`은
`Task<IReadOnlyList<Message>>`를 반환한다 — caller가 reply message를 소유하며
반드시 dispose해야 한다. `RequestSubmitOperation`/
`RequestCallbackSubmitOperation.Submit(RequestCallback)`은 `bool`을 반환하고(`Send`와
같은 `DontWait` 관례) 결과를 나중에 `RequestCallback` delegate
`(RequestResult result, IReadOnlyList<Message> parts)`로 전달한다 — `result`가
`RequestResult.Ok`일 때만 parts가 채워진다. 모든 builder는 성공적인 submit에서만
누적된 `Message` part를 소비한다 — 실패 시 소유권은 caller에게 복원된다.

**선택 기준.** async 코드에선 awaitable `.Async()` 경로를 쓴다. callback-completion
표면이 필요할 땐(예: await할 수 없는 동기 dispatch 스레드에서) `.Flags(...)
.Submit(callback)`을 쓴다. 목적지 route를 손으로 재구성하는 대신 `Received` envelope의
`Reply()`/`Send()`를 쓴다. `.Message(...)`를 part마다 chain하는 대신, 미리 만든 part
목록을 추가할 땐 `Messages(...)` 편의 기능을 쓴다.

---

[`Contracts/Messaging/`](../../../../bindings/dotnet/src/Zlink/Contracts/Messaging/)와
[.NET 바인딩 스펙](../../spec/dotnet/README.ko.md)에서 전체 근거를 확인한다.
