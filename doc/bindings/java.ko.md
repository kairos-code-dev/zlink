[English](java.md) | [한국어](java.ko.md)

# Java 바인딩

## 1. 개요

- **FFM API** (Foreign Function & Memory API, Java 22+)
- JNI 없이 네이티브 라이브러리 직접 호출
- Arena/MemorySegment 기반 메모리 관리

## 2. 주요 클래스

| 클래스 | 설명 |
|--------|------|
| `Context` | 컨텍스트 |
| `Socket` | 소켓 (send/recv/bind/connect) |
| `Message` | 메시지 |
| `Poller` | 이벤트 폴러 |
| `Monitor` | 모니터링 |
| `Discovery` | 서비스 디스커버리 |
| `Gateway` | 게이트웨이 |
| `Receiver` | 리시버 |
| `SpotNode` / `Spot` | SPOT PUB/SUB |

## 3. 기본 예제

```java
try (var ctx = new Context();
     var server = new Socket(ctx, SocketType.PAIR);
     var client = new Socket(ctx, SocketType.PAIR)) {
    ctx.setOption(ContextOption.IO_THREADS, 4);
    server.bind("tcp://*:5555");
    client.connect("tcp://127.0.0.1:5555");

    client.send("Hello".getBytes(), SendFlag.NONE);
    byte[] reply = server.recv(256, ReceiveFlag.NONE);
    System.out.println(new String(reply));
}
```

## 4. 성능 API

- span 인터페이스
  - `ByteSpan.of(byte[] / ByteBuffer / MemorySegment)`
  - `send(ByteSpan span, SendFlag flags)`
  - `recv(ByteSpan span, ReceiveFlag flags)`
- span 스타일 배열 경로 (임시 슬라이스 할당 최소화)
  - `send(byte[] data, int offset, int length, SendFlag flags)`
  - `recv(byte[] data, int offset, int length, ReceiveFlag flags)`
- `ByteBuffer` 직접 경로
  - `send(ByteBuffer buffer, SendFlag flags)`
  - `recv(ByteBuffer buffer, ReceiveFlag flags)`
- Netty `ByteBuf` 경로
  - `send(io.netty.buffer.ByteBuf buf, SendFlag flags)`
  - `recv(io.netty.buffer.ByteBuf buf, ReceiveFlag flags)`
- 컨텍스트 튜닝
  - `ctx.setOption(ContextOption.IO_THREADS, n)`
- 메시지 zero-copy 뷰
  - `Message.fromNativeData(MemorySegment data[, offset, length])`
  - `Message.fromDirectByteBuffer(ByteBuffer direct)`
  - `MemorySegment dataSegment()`
  - `ByteBuffer dataBuffer()`
  - `copyTo(byte[]/ByteBuffer)`
- Gateway/SPOT 저복사 경로
  - `Gateway.sendMove(String service, Message[] parts, SendFlag flags)`
  - `Gateway.prepareService(String service)` + `send/sendMove(PreparedService, ...)`
  - `Gateway.sendTo(..., String routingId, ...)` (Java 친화 routing id 경로)
  - `Gateway.createSendContext()` + `send/sendMove(PreparedService, ..., SendContext)` (send vector 재사용)
  - `Gateway.send/sendMove(PreparedService, Message part, SendFlag, SendContext)` (단일 part fast path)
  - `Gateway.recvMessages(ReceiveFlag flags)` (`Gateway.GatewayMessages`, `AutoCloseable`)
  - `Gateway.createRecvContext()` + `recvRaw(ReceiveFlag, RecvContext)` (`Gateway.GatewayRawMessage`)
  - `Gateway.createRecvContext()` + `recvRawBorrowed(ReceiveFlag, RecvContext)` (`Gateway.GatewayRawBorrowed`, 래퍼 재사용)
  - `Gateway.GatewayRawBorrowed.serviceNameBuffer()` + `serviceNameLength()` (슬라이스 없이 식별자 접근)
  - `Spot.publishMove(String topic, Message[] parts, SendFlag flags)`
  - `Spot.prepareTopic(String topic)` + `publish/publishMove(PreparedTopic, ...)`
  - `Spot.createPublishContext()` + `publish/publishMove(PreparedTopic, ..., PublishContext)` (publish vector 재사용)
  - `Spot.publish/publishMove(PreparedTopic, Message part, SendFlag, PublishContext)` (단일 part fast path)
  - `Spot.recvMessages(ReceiveFlag flags)` (`Spot.SpotMessages`, `AutoCloseable`)
  - `Spot.createRecvContext()` + `recvRaw(ReceiveFlag, RecvContext)` (`Spot.SpotRawMessage`)
  - `Spot.createRecvContext()` + `recvRawBorrowed(ReceiveFlag, RecvContext)` (`Spot.SpotRawBorrowed`, 래퍼 재사용)
  - `Spot.SpotRawBorrowed.topicIdBuffer()` + `topicIdLength()` (슬라이스 없이 식별자 접근)
  - `recvRaw`는 `RecvContext` 내부 `Message[]`를 재사용하므로 반환된 part를 직접 close하면 안 됩니다
  - `recvRawBorrowed`는 래퍼 객체도 재사용하므로 다음 recv 전에 값을 소비해야 합니다
  - `sendMove/publishMove`는 메시지 소유권을 이동시키므로 이동된 `Message`는 재사용하면 안 됩니다

## 5. STREAM 콜백 API

`Socket` STREAM 헬퍼:
- `attachStreamRaw(StreamPacketHandler handler)` (`onPacket(int routingIdU32, Message payload)`)
- `attachStreamLen32be(StreamPacketBatchHandler handler)` (`onPackets(int routingIdU32, List<Message> packets)`)
- `detachStream()`
- `streamPeerRoutingId(int index)`
- `streamPeerRoutingIdU32(int index)` (STREAM uint32 routing id 뷰)
- `streamSend(byte[]/ByteBuffer/ByteSpan/MemorySegment routingId, ... payload, SendFlag flags)`
- `streamSend(long routingIdU32, byte[]/Message payload, SendFlag flags)`

모드 규칙:
- attach 상태에서는 콜백에서 STREAM 페이로드를 수신합니다.
- attach 상태에서 STREAM 페이로드 수신에 `recv(...)`를 혼용하지 않습니다.
- `detachStream()` 이후에는 기존 `recv(...)` 경로를 다시 사용할 수 있습니다.
- 콜백으로 전달되는 payload `Message`의 소유권은 Java 콜백으로 이전됩니다.
- `StreamPacketHandler`는 `streamSend(..., Message, ...)`로 소유권 이동하지 않는 한 `Message`를 직접 close해야 합니다.
- `StreamPacketBatchHandler`도 소유권을 가진 `Message`들을 받으므로 유지하는 메시지는 직접 close해야 합니다.

```java
try (var stream = new Socket(ctx, SocketType.STREAM)) {
    stream.attachStreamRaw((ridU32, payload) -> {
        try (payload) {
            stream.streamSend(ridU32, payload, SendFlag.NONE);
        }
        return 0;
    });
}
```

## 6. 빌드

```groovy
// build.gradle
dependencies {
    implementation files('path/to/zlink.jar')
    compileOnly 'io.netty:netty-buffer:4.1.100.Final' // optional
}
```

## 7. 네이티브 라이브러리 로드

`src/main/resources/native/` 디렉토리에서 플랫폼별 자동 로드.
