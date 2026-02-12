[English](java.md) | [한국어](java.ko.md)

# Java Binding

## 1. Overview

- **FFM API** (Foreign Function & Memory API, Java 22+)
- Direct native library calls without JNI
- Memory management based on Arena/MemorySegment

## 2. Main Classes

| Class | Description |
|-------|-------------|
| `Context` | Context |
| `Socket` | Socket (send/recv/bind/connect) |
| `Message` | Message |
| `Poller` | Event poller |
| `Monitor` | Monitoring |
| `Discovery` | Service discovery |
| `Gateway` | Gateway |
| `Receiver` | Receiver |
| `SpotNode` / `Spot` | SPOT PUB/SUB |

## 3. Basic Example

```java
try (var ctx = new Context();
     var server = new Socket(ctx, SocketType.PAIR);
     var client = new Socket(ctx, SocketType.PAIR)) {
    server.bind("tcp://*:5555");
    client.connect("tcp://127.0.0.1:5555");

    client.send("Hello".getBytes(), SendFlag.NONE);
    byte[] reply = server.recv(256, ReceiveFlag.NONE);
    System.out.println(new String(reply));
}
```

## 4. Performance APIs

- Span interface
  - `ByteSpan.of(byte[] / ByteBuffer / MemorySegment)`
  - `send(ByteSpan span, SendFlag flags)`
  - `recv(ByteSpan span, ReceiveFlag flags)`
- Const send path (native memory only)
  - `sendConst(ByteBuffer direct, SendFlag flags)`
  - `sendConst(MemorySegment native, SendFlag flags)`
- Span-style array path (no temporary slice allocation)
  - `send(byte[] data, int offset, int length, SendFlag flags)`
  - `recv(byte[] data, int offset, int length, ReceiveFlag flags)`
- Direct `ByteBuffer` path
  - `send(ByteBuffer buffer, SendFlag flags)`
  - `recv(ByteBuffer buffer, ReceiveFlag flags)`
- Zero-copy message view
  - `Message.fromNativeData(MemorySegment data[, offset, length])`
  - `Message.fromDirectByteBuffer(ByteBuffer direct)`
  - `MemorySegment dataSegment()`
  - `ByteBuffer dataBuffer()`
  - `copyTo(byte[]/ByteBuffer)`
- Gateway/SPOT low-copy path
  - `Gateway.sendMove(String service, Message[] parts, SendFlag flags)`
  - `Gateway.prepareService(String service)` + `send/sendMove(PreparedService, ...)`
  - `Gateway.createSendContext()` + `send/sendMove(PreparedService, ..., SendContext)` (reused send vector)
  - `Gateway.send/sendMove(PreparedService, Message part, SendFlag, SendContext)` (single-part fast path)
  - `Gateway.sendConst(..., MemorySegment nativePayload, ...)` (single-frame zero-copy path)
  - `Gateway.recvMessages(ReceiveFlag flags)` (`Gateway.GatewayMessages`, `AutoCloseable`)
  - `Gateway.createRecvContext()` + `recvRaw(ReceiveFlag, RecvContext)` (`Gateway.GatewayRawMessage`)
  - `Gateway.createRecvContext()` + `recvRawBorrowed(ReceiveFlag, RecvContext)` (`Gateway.GatewayRawBorrowed`, reused object)
  - `Gateway.GatewayRawBorrowed.serviceNameBuffer()` + `serviceNameLength()` (slice-free ID access)
  - `Spot.publishMove(String topic, Message[] parts, SendFlag flags)`
  - `Spot.prepareTopic(String topic)` + `publish/publishMove(PreparedTopic, ...)`
  - `Spot.createPublishContext()` + `publish/publishMove(PreparedTopic, ..., PublishContext)` (reused publish vector)
  - `Spot.publish/publishMove(PreparedTopic, Message part, SendFlag, PublishContext)` (single-part fast path)
  - `Spot.publishConst(..., MemorySegment nativePayload, ...)` (single-frame zero-copy path)
  - `Spot.recvMessages(ReceiveFlag flags)` (`Spot.SpotMessages`, `AutoCloseable`)
  - `Spot.createRecvContext()` + `recvRaw(ReceiveFlag, RecvContext)` (`Spot.SpotRawMessage`)
  - `Spot.createRecvContext()` + `recvRawBorrowed(ReceiveFlag, RecvContext)` (`Spot.SpotRawBorrowed`, reused object)
  - `Spot.SpotRawBorrowed.topicIdBuffer()` + `topicIdLength()` (slice-free ID access)
  - `recvRaw` reuses internal `Message[]` instances in the `RecvContext`; do not close returned parts directly
  - `recvRawBorrowed` also reuses the wrapper object itself; consume values before the next recv call
  - `sendMove/publishMove` transfer message ownership (do not reuse moved `Message` instances)
  - `sendConst/publishConst` require native payload memory to stay valid until send is completed by native code

## 5. Build

```groovy
// build.gradle
dependencies {
    implementation files('path/to/zlink.jar')
}
```

## 6. Native Library Loading

Platform-specific libraries are automatically loaded from the `src/main/resources/native/` directory.
