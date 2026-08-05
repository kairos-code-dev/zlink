한국어 | [English](02-messaging.en.md)

[레퍼런스 목차](README.ko.md)

# 02. Messaging

이 category는 message 소유권, receive envelope 타입(`Received`, `TopicMessage`,
`SubscriptionEvent`), 그리고 모든 socket type 진입점이 반환하는 공유
send/request/reply operation-builder family를 다룬다. 정확한 signature는
[`contracts/messaging/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/messaging/)가
소유한다.

---

## `Message`

native zlink frame 하나를 소유한다 — 모든 send·request·reply·receive API가 옮기는
단위다. Java는 borrowed-payload wrapper를 노출하지 않는다 — native queue lifetime이
Java object reachability로 안전하게 bound되지 않기 때문이다 — `copyOf*` 스타일
factory는 항상 message 소유 storage로 복사한다.

```java
Message empty = new Message();
Message sized = new Message(4096);
Message copy = Message.from("payload".getBytes(StandardCharsets.UTF_8));
Message fromString = Message.from("hello");
```

**Options.** 생성자: `Message()`(빈 메시지), `Message(int size)`(쓰기 가능
storage). Static factory: `allocate(int)`, `from(byte[])`, `from(byte[] data,
int offset, int length)`(범위 복사), `from(Message)`(다른 메시지의 payload
복사), `from(String)`(UTF-8), `from(ByteBuffer)`(source cursor를 바꾸지 않고
남은 byte 복사), `from(io.netty.buffer.ByteBuf)`(Netty interop, readable byte
복사). Instance member: `size()`, `more()`(multipart-continuation flag),
`refCount()`, `empty()`/`isEmpty()`, `data()`/`toByteArray()`(복사해서 반환),
`toUtf8String()`, `dataBuffer()`(읽기 전용 `ByteBuffer` view)/
`mutableDataBuffer()`, `copyTo(byte[])`/`copyTo(byte[], int offset)`/
`copyTo(byte[] dst, int srcOffset, int dstOffset, int length)`/
`copyTo(ByteBuffer)`/`copyTo(ByteBuf)`, `tryCopyTo(ByteBuffer)`/
`tryCopyTo(ByteBuf)`(bounds-check, `boolean`), `copyFrom(byte[]|Message, int
srcOffset, int dstOffset, int length)`, in-place binary accessor
`readByte`/`readIntLe`/`readIntBe`/`readLongLe`/`writeByte`/`writeIntLe`/
`writeIntBe`/`writeLongLe`/`fill(byte)`/`fill(byte, offset, length)`,
`contentEquals(byte[])`. static `closeAll(Message[])`/
`closeAll(Iterable<? extends Message>)`는 한 호출로 모든 closeable part를
닫으며 개별 close 실패는 조용히 무시한다.

**Completion result.** 모든 member는 동기다. `Message implements
AutoCloseable`이다. 메시지를 보내면 native frame이 socket으로 이전돼 이후
읽기에 대해 instance가 무효화된다 — 보내지 않을 메시지를 해제하려면
`close()`를 쓴다. 범위를 벗어난 offset/length는 `IndexOutOfBoundsException`을
던진다.

**선택 기준.** outbound payload를 만들 땐 크기 지정 생성자나 복사하는
`from(...)` factory를 쓴다. wire format을 중간 `byte[]` 없이 message storage에
직접 파싱·쓰기하려면 in-place binary accessor(`readIntLe` 등)를 쓴다. 수신되거나
구성된 multipart 배열의 모든 part를 손으로 짠 loop 대신 한 호출로 해제하려면
`closeAll(...)`을 쓴다.

---

## `Received`

recv 결과 하나를 집계한다: 선택적 routing id, request sequence, 소유한 message
part. 반환된 `parts()` view는 불변이며 밑에 깔린 배열을 복사하지 않는다.

```java
Received received = new Received();
if (dealer.recv(received)) {
    received.requestSeq().ifPresent(seq ->
        received.reply().message(Message.from("ok")).submit());
}
```

**Options.** caller-provided storage용 public 인자 없는 생성자
`Received()` — binding이 매 성공적인 receive마다 내부 상태를 그 자리에서
덮어쓴다(receive마다 할당을 피함). Instance member: `getRoutingId()`
(`Optional<RoutingId>`), `requestSeq()`(`Optional<Long>`), `parts()`
(`List<Message>`, 불변 view), `isSinglePart()`, `firstPart()`,
`singlePartOrThrow()`, `reply()`(공유 `ReplyOperation` builder 시작 —
유효한 reply context가 없으면 `submit()`에서 `ZlinkSubmitException`),
`send()`(공유 `SendOperation` builder 시작, 이 envelope이 포착한 source
route로 향함). `Received implements AutoCloseable`이며, `close()`는 소유한
모든 part를 닫는다.

**Completion result.** 모든 member는 동기다. `firstPart()`/
`singlePartOrThrow()`는 각각 데이터가 없거나 part 개수가 맞지 않을 때
`ZlinkRecvException`을 던진다 — Errors category에 문서화된 receive측 result
code를 그대로 반영한다.

**선택 기준.** message마다 새로 생성하는 대신 receive loop 전체에서
`Received` 하나를 재사용한다. `reply()`를 호출하기 전에 `requestSeq()`로
envelope이 reply 가능한지 확인한다.

---

## `TopicMessage`

raw subscription 경로가 쓰는 topic-aware recv 결과 — 수신된 publish의 topic,
source routing id, message part.

```java
TopicMessage published = new TopicMessage();
if (sub.subscribe(published)) {
    String topic = published.topic();
}
```

**Options.** public 인자 없는 생성자 `TopicMessage()`. Instance member:
`getRoutingId()`(`Optional<RoutingId>`), `topic()`(`String`), `parts()`
(`List<Message>`), `isSinglePart()`, `firstPart()`, `singlePartOrThrow()`.
`TopicMessage implements AutoCloseable`.

**Completion result.** 동기다. `firstPart()`/`singlePartOrThrow()`는
`Received`의 대응 메서드와 같은 방식으로 `ZlinkRecvException`을 던진다.

**선택 기준.** `Received`와 같은 방식으로 subscribe-receive loop 전체에서
instance 하나를 재사용한다.

---

## `SubscriptionEvent` / `SubscriptionEntry`

XPUB socket이 관찰한 구독자 한 명의 subscribe·unsubscribe를 보고하고, 활성 구독
항목 하나를 기술한다.

```java
SubscriptionEvent evt = new SubscriptionEvent();
if (xpub.receiveSubscriptionEvent(evt)) { /* ... */ }
```

**Options.** `SubscriptionEvent()` public 인자 없는 생성자. Instance member:
`getRoutingId()`(`Optional<RoutingId>`), `topic()`(`String`),
`subscribed()`(`boolean`). `SubscriptionEntry`는 record다:
`SubscriptionEntry(String filter, boolean pattern)`, `filterBytes()`(UTF-8
인코딩)와 static `fromBytes(byte[], boolean)`을 제공.

**Completion result.** 둘 다 async 동작이 없는 순수 데이터 홀더다.
`SubscriptionEvent`는 `close()`가 없다 — native resource를 소유하지 않는다.

**선택 기준.** XPUB socket의 subscription-event receive 경로(Sockets
category)에서 구독자 변동을 관찰할 때 쓴다. `SubscriptionEntry`는 socket의
subscription-snapshot 조회(Sockets category)의 반환 타입이다.

---

## Send / request / reply operation-builder 형태

모든 socket type의 `send`/`publish`/`request`/`reply` 진입점(Sockets
category)이 part·flag·terminal submit을 누적하기 위해 반환하는 fluent
builder. 모든 builder interface는 공유 `MessageBuilderStage<TSubmit>`
(`TSubmit message(Message part)`)를 확장하며, request family는 추가로
`TimeoutSubmitOperation<TResult, TCallback>`을 확장한다.

```java
dealer.send().message(part1).message(part2).submit();

CompletionStage<List<Message>> future = dealer.request()
    .message(Message.from("payload"))
    .timeout(Duration.ofSeconds(5))
    .submit();
List<Message> reply = future.toCompletableFuture().join();

// 또는 virtual thread에서:
List<Message> reply2 = dealer.request().message(Message.from("payload")).await();

received.reply().message(Message.from("ok")).submit();
```

**Options.** `SendOperation.message(Message)`가 chain을 시작해
`SendSubmitOperation`을 반환하고, 그 `.message(...)`/`.flags(SendFlags)`/
`.submit()`이 part를 더 추가하고, flag를 설정하고, 종료한다.
`RequestOperation`/`RequestSubmitOperation`은 같은 형태에
`.timeout(Duration)`을 더한 것이다. `RequestSubmitOperation`에서
`.flags(SendFlags)`를 호출하면 builder가 `RequestCallbackSubmitOperation`으로
좁혀지고, `CompletionStage`를 반환하는 `.submit()`이 사라진다 — 그 지점
이후엔 `.submit(RequestCallback)`만 도달 가능하다. `ReplyOperation`/
`ReplySubmitOperation`은 `SendOperation`/`SendSubmitOperation`과 같은
형태지만 flags 단계가 없다. `TimeoutSubmitOperation`은 또한 `default
await()`을 제공한다 — submit하고 결과가 완료될 때까지 현재 스레드를
block한다, 명시적으로 virtual thread를 위한 것이다(virtual thread를 parking하면
그 carrier platform thread가 풀리는 반면, platform thread를 직접 block하는
것과 다르다) — framework 자신의 async 경로는 대신 `submit()`을 쓴다.

**Completion result.** `SendSubmitOperation.submit()`/
`ReplySubmitOperation.submit()`은 동기다 — send의 반환값은 `boolean`
(`SendFlags.DONT_WAIT`가 설정되고 send가 block됐을 때만 `false` — 그 외 모든
실패는 `ZlinkException`을 던진다), reply의 반환값은 `void`다.
`RequestSubmitOperation.submit()`은 `CompletionStage<List<Message>>`를
반환한다 — caller가 reply message를 소유하며 반드시 close해야 한다.
`RequestSubmitOperation`/`RequestCallbackSubmitOperation.submit(
RequestCallback)`은 `boolean`을 반환하고(같은 `DONT_WAIT` 관례) 결과와
part를 나중에 콜백에 전달한다 — 결과가 `RequestResult.OK`일 때만 콜백이
part를 소유한다. 모든 builder는 성공적인 submit에서만 누적된 `Message`
part를 소비한다 — 실패 시 소유권은 caller에게 복원된다.

**선택 기준.** 일반 async 코드에선 `submit()`의 `CompletionStage`를 쓴다.
순차 호출처럼 자연스럽게 읽히는 코드가 필요한 virtual thread에선 대신
`await()`을 쓴다. 전혀 block·park해선 안 되는 스레드에서 callback-completion
표면이 필요할 땐 `.flags(...).submit(callback)`을 쓴다. 목적지 route를
손으로 재구성하는 대신 `Received.reply()`/`send()`를 쓴다.

---

[`contracts/messaging/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/messaging/)와
[Java 바인딩 스펙](../../spec/java/README.ko.md)에서 전체 근거를 확인한다.
