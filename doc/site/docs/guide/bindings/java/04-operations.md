[← 서비스](./03-services.md) · [Java 가이드](./index.md) · [다음: 레퍼런스 →](./05-reference.md)

# 운영

소켓 옵션, TLS, 모니터링, 폴러/타이머, 스레딩, 네이티브 라이브러리를 다룹니다.

---

## 소켓 옵션

소켓별 옵션 퍼사드를 통해 설정합니다. ([코어 참고](../../12-socket-options.md))

```java
var opts = socket.options();

// 공통 옵션
opts.sendHwm(1000);
opts.recvHwm(1000);
opts.sendTimeout(Duration.ofMillis(500));
opts.recvTimeout(Duration.ofMillis(500));
opts.linger(Duration.ZERO); // Close() 후 대기 없음

// DEALER 전용
var dealerOpts = dealer.options();
dealerOpts.requestTimeout(Duration.ofSeconds(2));

// ROUTER 전용
var routerOpts = router.options();
routerOpts.mandatory(true); // 없는 라우팅 ID로 보내면 에러
```

자동 HWM 프로파일:

```java
ctx.options().autoHwmEnabled(true);
ctx.options().autoHwmProfile(AutoHwmProfile.BALANCED);
```

---

## TLS 보안

서버와 클라이언트 양쪽에 인증서를 설정합니다. ([코어 참고](../../05-tls-security.md))

```java
// 서버
socket.setTlsServer("cert.pem", "key.pem", false);

// 클라이언트
socket.setTlsClient("ca.pem", "server-hostname", false);
```

`tls+tcp://` 트랜스포트로 연결합니다:

```java
server.bind("tls+tcp://0.0.0.0:5556");
client.connect("tls+tcp://server.example.com:5556");
```

---

## 모니터링

소켓의 연결 수명 이벤트를 구독합니다. ([코어 참고](../../06-monitoring.md))

```java
// 특정 이벤트만 구독
try (var monitor = socket.monitorOpen(MonitorEventType.CONNECTION_READY)) {

    // 이벤트 수신 (블로킹)
    MonitorEvent event = monitor.recv();
    if (event.event() == MonitorEventType.CONNECTION_READY) {
        System.out.println("피어 연결됨: " + event.remoteAddr());
    }

    // 콜백 방식
    monitor.onEvent(e -> {
        System.out.printf("event=%s addr=%s%n", e.event(), e.remoteAddr());
    });
}
```

---

## 폴러 / 타이머

여러 소켓을 동시에 폴링합니다.

```java
try (Poller poller = Zlink.createPoller()) {
    poller.add(socket1, 1, PollEventFlags.POLLIN);
    poller.add(socket2, 2, PollEventFlags.POLLIN);

    PollEvents events = new PollEvents(16);
    int n = poller.wait(events, Duration.ofMillis(100));
    for (int i = 0; i < n; i++) {
        switch ((int) events.slot(i)) {
            case 1 -> { /* socket1 수신 준비됨 */ }
            case 2 -> { /* socket2 수신 준비됨 */ }
        }
    }
}
```

타이머:

```java
try (ZlinkTimer timer = Zlink.createTimer()) {
    timer.start(Duration.ofMillis(500), 0); // 0 = 무한 반복

    timer.onFire((fireCount) ->
        System.out.printf("타이머 %d회 발화%n", fireCount));

    // 폴러와 함께 사용
    poller.add(timer, 99);
}
```

---

## 스레딩

Java 바인딩의 스레딩 규칙입니다. ([코어 참고](../../11-thread-safety.md))

| 항목 | 규칙 |
|------|------|
| `Context` | 스레드 간 공유 가능 |
| 소켓 | **하나의 스레드에서만** 사용. 동시에 여러 스레드가 접근하면 경쟁 조건 발생 |
| 디스패치 핸들러 | zlink 내부 워커 스레드에서 호출됨. 핸들러 내에서 오래 블록하지 않을 것 |
| `Message.data()` | 메시지 수명 동안만 유효 |

```java
// 올바른 패턴: 소켓 per-스레드
Thread t = new Thread(() -> {
    try (DealerSocket socket = ctx.createDealerSocket()) {
        socket.connect("tcp://...");
        // 이 스레드에서만 socket 사용
    }
});

// 잘못된 패턴: 두 스레드가 동일 소켓 접근
new Thread(() -> socket.recv(...)).start(); // 위험
new Thread(() -> socket.send(...)).start(); // 위험
```

---

## 네이티브 라이브러리

Java 바인딩은 플랫폼별 공유 라이브러리를 내장합니다. 별도 설치 없이 Gradle/Maven으로
추가하면 됩니다.

사용 중인 네이티브 버전 확인:

```java
int[] version = Zlink.version();
System.out.printf("zlink %d.%d.%d%n", version[0], version[1], version[2]);
```

특정 기능 지원 여부:

```java
if (Zlink.has("draft")) {
    System.out.println("draft API 지원");
}
```
