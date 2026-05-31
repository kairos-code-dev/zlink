[Java 가이드](./index.md) · [다음: 메시징 →](./02-messaging.md)

# 시작하기

이 문서는 의존성 추가부터 첫 메시지 송수신, 그리고 모든 기능이 공유하는 핵심 타입과
소유권 규칙까지 다룹니다. 메시징 개념 자체는 [코어 가이드](../../01-overview.md)가
소유하며, 여기서는 Java 표면만 설명합니다.

---

## 설치

Gradle 또는 Maven으로 추가합니다. 네이티브 코어가 플랫폼별로 번들됩니다.

**Gradle (build.gradle):**

```groovy
dependencies {
    implementation 'systems.zlink:zlink-java:6.0.4'
}
```

**Maven (pom.xml):**

```xml
<dependency>
    <groupId>systems.zlink</groupId>
    <artifactId>zlink-java</artifactId>
    <version>6.0.4</version>
</dependency>
```

- **Java 17** 이상.
- 네이티브 별도 설치 불필요 — RID별 공유 라이브러리를 자동 로드합니다.

```java
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
```

---

## 5분 예제 — PING/ACK

`Pair` 소켓으로 한쪽이 `PING`을 보내고 다른 쪽이 `ACK`로 답하는 최소 예제입니다.
모든 리소스는 `try-with-resources`로 관리합니다.

```java
// 서버
try (Context ctx = Zlink.createContext();
     var server = ctx.createPairSocket()) {

    server.bind("tcp://127.0.0.1:5555");

    try (Received received = new Received()) {
        server.recv(received, RecvFlags.NONE);
        String text = received.firstPart().toUtf8String();
        System.out.println(text); // PING

        try (Message reply = Message.from("ACK")) {
            received.send().message(reply).submit();
        }
    }
}
```

```java
// 클라이언트
try (Context ctx = Zlink.createContext();
     var client = ctx.createPairSocket()) {

    client.connect("tcp://127.0.0.1:5555");

    try (Message ping = Message.from("PING")) {
        client.send().message(ping).submit();
    }

    try (Received received = new Received()) {
        client.recv(received, RecvFlags.NONE);
        System.out.println(received.firstPart().toUtf8String()); // ACK
    }
}
```

---

## 핵심 타입

모든 기능이 공유하는 4가지 기본 타입입니다.

### 1. 컨텍스트 (Context)

프로세스의 런타임 진입점입니다. `AutoCloseable`을 구현하므로 try-with-resources로
관리합니다. 컨텍스트를 닫으면 하위 소켓·서비스에 대한 블로킹 작업이 중단됩니다.

```java
try (Context ctx = Zlink.createContext()) {
    // 소켓과 서비스를 여기서 생성합니다
    var socket = ctx.createPairSocket();
    // ...
} // ctx.close() 자동 호출 → 하위 소켓 종료
```

I/O 스레드 수 조정:

```java
ctx.options().ioThreads(4);
```

### 2. 메시지 (Message)

페이로드 프레임 하나를 소유합니다. `AutoCloseable`을 구현합니다.
전송하면 소유권이 이전되어 별도로 닫을 필요가 없습니다.
전송에 실패하면 소유권이 유지되어 재시도하거나 명시적으로 닫아야 합니다.

```java
// 문자열에서 복사본 생성
try (Message msg = Message.from("payload")) {
    socket.send().message(msg).submit();
}
// submit 성공 시 msg는 이미 소비됨 — try 블록이 닫혀도 무방

// 바이트 배열에서 복사본 생성
try (Message msg = Message.from(bytes)) { ... }

// 크기 지정으로 빈 프레임 할당
try (Message msg = new Message(256)) {
    msg.mutableDataBuffer().put(data);
    socket.send().message(msg).submit();
}
```

수신된 메시지 읽기:

```java
int size = msg.size();
String text = msg.toUtf8String();    // UTF-8 변환
byte[] data = msg.data();            // 바이트 배열 복사
ByteBuffer buf = msg.dataBuffer();   // 읽기 전용 뷰
```

### 3. Received — 수신 봉투

수신한 메시지 봉투입니다. 라우팅 ID, 파트 목록, 선택적 회신 컨텍스트를 담습니다.
재사용이 가능합니다. `AutoCloseable`을 구현합니다.

```java
try (Received received = new Received()) {
    socket.recv(received, RecvFlags.NONE);

    // 단일 파트 접근
    Message part = received.firstPart();         // 첫 번째 파트
    Message part = received.singlePartOrThrow();  // 파트가 정확히 하나여야 함

    // 멀티파트 접근
    List<Message> parts = received.parts();

    // 라우팅 ID (ROUTER/SPOT 수신 시)
    Optional<RoutingId> rid = received.getRoutingId();
}
```

### 4. 라우팅 ID (RoutingId)

피어나 스팟을 식별하는 1~255 바이트의 불변 값입니다.

```java
RoutingId rid = RoutingId.from("server-01".getBytes(StandardCharsets.UTF_8));
RoutingId rid = RoutingId.from(RoutingId.fromString("server-01"));
```

---

## 소유권 규칙

Java 바인딩의 소유권 규칙입니다. try-with-resources를 기본 패턴으로 사용합니다.

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | 추가한 `Message`의 소유권이 전송 스택으로 이전됩니다. 별도 `close()` 불필요 |
| `submit()` 실패(예외) | 소유권이 호출자에게 유지됩니다. try-with-resources가 자동 처리 |
| `recv()` 성공 | 호출자가 `Received`의 소유권을 가집니다. try-with-resources 필수 |
| `submitAsync()` 완료 | 회신 `List<Message>`는 호출자 소유. `Message.closeAll(reply)` 필요 |
| `Context.close()` | 컨텍스트 하위의 모든 블로킹 작업을 중단합니다 |

```java
// 패턴: try-with-resources로 안전하게
try (Message msg = Message.from("data")) {
    boolean submitted = socket.send().message(msg).submit();
    // submitted=true면 msg가 소비됨, false면 백프레셔(DONT_WAIT일 때만)
} // submit이 예외를 던지면 try-with-resources가 msg를 닫음
```
