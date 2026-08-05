[한국어](02-messaging.ko.md) | English

[Reference index](README.en.md)

# 02. Messaging

This category covers message ownership, the receive envelope types (`Received`, `TopicMessage`,
`SubscriptionEvent`), and the shared send/request/reply operation-builder family every socket
type's entry point returns. The exact signatures are owned by
[`contracts/messaging/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/messaging/).

---

## `Message`

Owns one native zlink frame — the unit every send, request, reply, and receive API moves. Java
does not expose borrowed-payload wrappers, because native queue lifetime is not safely bounded by
Java object reachability — `copyOf*`-style factories always copy into message-owned storage.

```java
Message empty = new Message();
Message sized = new Message(4096);
Message copy = Message.from("payload".getBytes(StandardCharsets.UTF_8));
Message fromString = Message.from("hello");
```

**Options.** Constructors: `Message()` (empty), `Message(int size)` (writable storage). Static
factories: `allocate(int)`, `from(byte[])`, `from(byte[] data, int offset, int length)` (range
copy), `from(Message)` (copies another message's payload), `from(String)` (UTF-8), `from(ByteBuffer)`
(copies remaining bytes without mutating the source cursor), `from(io.netty.buffer.ByteBuf)`
(Netty interop, copies readable bytes). Instance members: `size()`, `more()` (multipart-continuation
flag), `refCount()`, `empty()`/`isEmpty()`, `data()`/`toByteArray()` (copy out), `toUtf8String()`,
`dataBuffer()` (read-only `ByteBuffer` view)/`mutableDataBuffer()`, `copyTo(byte[])`/
`copyTo(byte[], int offset)`/`copyTo(byte[] dst, int srcOffset, int dstOffset, int length)`/
`copyTo(ByteBuffer)`/`copyTo(ByteBuf)`, `tryCopyTo(ByteBuffer)`/`tryCopyTo(ByteBuf)` (bounds-checked,
`boolean`), `copyFrom(byte[]|Message, int srcOffset, int dstOffset, int length)`, in-place binary
accessors `readByte`/`readIntLe`/`readIntBe`/`readLongLe`/`writeByte`/`writeIntLe`/`writeIntBe`/
`writeLongLe`/`fill(byte)`/`fill(byte, offset, length)`, `contentEquals(byte[])`. Static
`closeAll(Message[])`/`closeAll(Iterable<? extends Message>)` close every closeable part in one
call, silently ignoring individual close failures.

**Completion result.** Every member is synchronous. `Message implements AutoCloseable`; sending a
message transfers its native frame to the socket, invalidating the instance for further reads —
`close()` releases a message that will not be sent. Out-of-range offsets/lengths throw
`IndexOutOfBoundsException`.

**When to use.** Use a sized constructor or a copying `from(...)` factory to build an outbound
payload. Use the in-place binary accessors (`readIntLe`, etc.) to parse/write a wire format directly
against message storage without an intermediate `byte[]`. Use `closeAll(...)` to release every part
of a received or constructed multipart array in one call instead of a hand-written loop.

---

## `Received`

Aggregates one recv result: an optional routing id, request sequence, and the owned message parts.
The returned `parts()` view is immutable and does not copy the underlying array.

```java
Received received = new Received();
if (dealer.recv(received)) {
    received.requestSeq().ifPresent(seq ->
        received.reply().message(Message.from("ok")).submit());
}
```

**Options.** Public no-arg constructor `Received()` for caller-provided storage — the binding
overwrites internal state in place on each successful receive (avoiding a per-recv allocation).
Instance members: `getRoutingId()` (`Optional<RoutingId>`), `requestSeq()` (`Optional<Long>`),
`parts()` (`List<Message>`, immutable view), `isSinglePart()`, `firstPart()`, `singlePartOrThrow()`,
`reply()` (starts the shared `ReplyOperation` builder — throws `ZlinkSubmitException` on `submit()`
if there is no valid reply context), `send()` (starts the shared `SendOperation` builder, addressed
to this envelope's captured source route). `Received implements AutoCloseable`; `close()` closes
every owned part.

**Completion result.** All members are synchronous. `firstPart()`/`singlePartOrThrow()` throw
`ZlinkRecvException` when there is no data or the part count doesn't match, respectively — mirroring
the receive-side result codes documented in the Errors category.

**When to use.** Reuse one `Received` across a receive loop rather than constructing a new one per
message. Check `requestSeq()` before calling `reply()` to confirm the envelope is replyable.

---

## `TopicMessage`

Topic-aware recv result used by raw subscription paths: a received publish's topic, source routing
id, and message parts.

```java
TopicMessage published = new TopicMessage();
if (sub.subscribe(published)) {
    String topic = published.topic();
}
```

**Options.** Public no-arg constructor `TopicMessage()`. Instance members: `getRoutingId()`
(`Optional<RoutingId>`), `topic()` (`String`), `parts()` (`List<Message>`), `isSinglePart()`,
`firstPart()`, `singlePartOrThrow()`. `TopicMessage implements AutoCloseable`.

**Completion result.** Synchronous; `firstPart()`/`singlePartOrThrow()` throw `ZlinkRecvException`
the same way as `Received`'s equivalents.

**When to use.** Reuse one instance across a subscribe-receive loop the same way as `Received`.

---

## `SubscriptionEvent` / `SubscriptionEntry`

Reports one subscriber's subscribe or unsubscribe (as observed by an XPUB socket), and describes
one active subscription entry.

```java
SubscriptionEvent evt = new SubscriptionEvent();
if (xpub.receiveSubscriptionEvent(evt)) { /* ... */ }
```

**Options.** `SubscriptionEvent()` public no-arg constructor. Instance members: `getRoutingId()`
(`Optional<RoutingId>`), `topic()` (`String`), `subscribed()` (`boolean`). `SubscriptionEntry` is a
record: `SubscriptionEntry(String filter, boolean pattern)`, with `filterBytes()` (UTF-8 encode) and
static `fromBytes(byte[], boolean)`.

**Completion result.** Both are plain data holders with no async behavior; `SubscriptionEvent` has
no `close()` — it owns no native resources.

**When to use.** Use on an XPUB socket's subscription-event receive path (Sockets category) to
observe subscriber churn. `SubscriptionEntry` is the return type of a socket's subscription-snapshot
lookup (Sockets category).

---

## Send / request / reply operation-builder shape

The fluent builder every socket type's `send`/`publish`/`request`/`reply` entry point (Sockets
category) returns to accumulate parts, flags, and a terminal submit. All builder interfaces extend
the shared `MessageBuilderStage<TSubmit>` (`TSubmit message(Message part)`), and the request family
additionally extends `TimeoutSubmitOperation<TResult, TCallback>`.

```java
dealer.send().message(part1).message(part2).submit();

CompletionStage<List<Message>> future = dealer.request()
    .message(Message.from("payload"))
    .timeout(Duration.ofSeconds(5))
    .submit();
List<Message> reply = future.toCompletableFuture().join();

// or, on a virtual thread:
List<Message> reply2 = dealer.request().message(Message.from("payload")).await();

received.reply().message(Message.from("ok")).submit();
```

**Options.** `SendOperation.message(Message)` starts the chain, returning `SendSubmitOperation`,
whose `.message(...)`/`.flags(SendFlags)`/`.submit()` accumulate further parts, set flags, and
terminate. `RequestOperation`/`RequestSubmitOperation` mirror this shape plus `.timeout(Duration)`;
calling `.flags(SendFlags)` on `RequestSubmitOperation` narrows the builder to
`RequestCallbackSubmitOperation`, dropping the `CompletionStage`-returning `.submit()` — only
`.submit(RequestCallback)` remains reachable past that point. `ReplyOperation`/`ReplySubmitOperation`
mirror `SendOperation`/`SendSubmitOperation` but have no flags stage. `TimeoutSubmitOperation` also
provides a `default await()` — submits and blocks the current thread until the result completes,
explicitly intended for virtual threads (parking a virtual thread frees its carrier platform thread,
unlike blocking a platform thread directly); the framework's own async path uses `submit()`
instead.

**Completion result.** `SendSubmitOperation.submit()`/`ReplySubmitOperation.submit()` are
synchronous; send's returns `boolean` (`false` only when `SendFlags.DONT_WAIT` is set and the send
would have blocked — other failures throw `ZlinkException`), reply's returns `void`.
`RequestSubmitOperation.submit()` returns `CompletionStage<List<Message>>` — the caller owns and
must close the reply messages. `RequestSubmitOperation`/`RequestCallbackSubmitOperation.submit(
RequestCallback)` returns `boolean` (same `DONT_WAIT` convention) and delivers the result and parts
to the callback later — the callback owns the parts only when the result is
`RequestResult.OK`. Every builder consumes its accumulated `Message` parts on a successful submit
only; on failure ownership is restored to the caller.

**When to use.** Use `submit()`'s `CompletionStage` in ordinary async code; use `await()` instead on
a virtual thread for code that reads more naturally as a sequential call; use
`.flags(...).submit(callback)` when a callback-completion surface is needed on a thread that
shouldn't block or park at all. Use `Received.reply()`/`send()` rather than reconstructing the
destination route by hand.

---

See [`contracts/messaging/`](../../../../bindings/java/src/main/java/systems/zlink/contracts/messaging/)
and the [Java binding spec](../../spec/java/README.en.md) for the full rationale.
