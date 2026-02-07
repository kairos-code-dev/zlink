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
try (var ctx = new Context()) {
    try (var server = ctx.createSocket(SocketType.PAIR)) {
        server.bind("tcp://*:5555");

        try (var client = ctx.createSocket(SocketType.PAIR)) {
            client.connect("tcp://127.0.0.1:5555");
            client.send("Hello".getBytes());

            byte[] reply = server.recv();
            System.out.println(new String(reply));
        }
    }
}
```

## 4. 빌드

```groovy
// build.gradle
dependencies {
    implementation files('path/to/zlink.jar')
}
```

## 5. 네이티브 라이브러리 로드

`src/main/resources/native/` 디렉토리에서 플랫폼별 자동 로드.
