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
- 메시지 zero-copy 뷰
  - `MemorySegment dataSegment()`
  - `ByteBuffer dataBuffer()`
  - `copyTo(byte[]/ByteBuffer)`

## 5. 빌드

```groovy
// build.gradle
dependencies {
    implementation files('path/to/zlink.jar')
}
```

## 6. 네이티브 라이브러리 로드

`src/main/resources/native/` 디렉토리에서 플랫폼별 자동 로드.
