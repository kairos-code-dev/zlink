[← 시작하기](./01-getting-started.md) · [Java 가이드](./index.md) · [다음: 서비스 →](./03-services.md)

# 메시징

소켓 패턴별 Java API 사용법을 설명합니다. 모든 리소스는 try-with-resources로
관리합니다.

---

## PAIR

1:1 배타적 연결. 라우팅 없음. ([코어 참고](../../03-1-pair.md))

```java
try (Context ctx = Zlink.createContext();
     PairSocket server = ctx.createPairSocket();
     PairSocket client = ctx.createPairSocket()) {

    server.bind("tcp://127.0.0.1:5560");
    client.connect("tcp://127.0.0.1:5560");

    // 전송
    try (Message msg = Message.from("hello")) {
        client.send().message(msg).submit();
    }

    // 수신
    try (Received received = new Received()) {
        server.recv(received, RecvFlags.NONE);
        System.out.println(received.firstPart().toUtf8String());
    }
}
```

---

## DEALER / ROUTER

비동기 요청/응답. DEALER가 클라이언트, ROUTER가 서버.
([코어 DEALER](../../03-3-dealer.md) / [코어 ROUTER](../../03-4-router.md))

### 단순 송수신

```java
try (Context ctx = Zlink.createContext();
     RouterSocket router = ctx.createRouterSocket();
     DealerSocket dealer = ctx.createDealerSocket()) {

    // 라우팅 ID 설정 (설정 안 하면 임의 할당)
    dealer.setRoutingId(RoutingId.from("client-01".getBytes(UTF_8)));

    router.bind("tcp://127.0.0.1:5561");
    dealer.connect("tcp://127.0.0.1:5561");

    // 요청 전송
    try (Message req = Message.from("get-price")) {
        dealer.send().message(req).submit();
    }

    // 서버: 수신 후 회신
    try (Received request = new Received()) {
        router.recv(request, RecvFlags.NONE);

        try (Message reply = Message.from("101.25")) {
            request.send().message(reply).submit(); // Received에서 직접 회신
        }
    }

    // 클라이언트: 응답 수신
    try (Received response = new Received()) {
        dealer.recv(response, RecvFlags.NONE);
        System.out.println(response.firstPart().toUtf8String()); // 101.25
    }
}
```

### 비동기 요청 (submitAsync)

`CompletableFuture`로 응답을 받습니다.

```java
try (Message req = Message.from("get-price")) {
    CompletableFuture<List<Message>> future = dealer.request()
        .message(req)
        .timeout(Duration.ofSeconds(2))
        .submitAsync();

    List<Message> reply = future.get(3, TimeUnit.SECONDS);
    try {
        System.out.println(reply.get(0).toUtf8String());
    } finally {
        Message.closeAll(reply); // 회신 파트는 호출자가 닫아야 합니다
    }
}
```

서버 쪽은 `request.requestSeq()`로 시퀀스를 확인하고 회신합니다:

```java
try (Received request = new Received()) {
    router.recv(request, RecvFlags.NONE);

    if (request.requestSeq().isPresent()) {
        try (Message reply = Message.from("ok")) {
            request.reply().message(reply).submit();
        }
    }
}
```

### 멀티파트 전송

```java
try (Message header = Message.from("cmd:buy");
     Message body = Message.from("{\"qty\":10}")) {
    dealer.send().message(header).message(body).submit();
}
```

수신 쪽에서는 `received.parts()`로 모든 프레임에 접근합니다.

---

## PUB / SUB

토픽 기반 팬아웃. ([코어 참고](../../03-2-pubsub.md))

```java
try (Context ctx = Zlink.createContext();
     PubSocket pub = ctx.createPubSocket();
     SubSocket sub = ctx.createSubSocket()) {

    pub.bind("tcp://127.0.0.1:5562");
    sub.setSubscription("prices");        // 연결 전에 호출 가능
    sub.connect("tcp://127.0.0.1:5562");

    // 발행
    try (Message msg = Message.from("101.25")) {
        pub.publish("prices").message(msg).submit();
    }

    // 수신
    try (TopicMessage topic = new TopicMessage()) {
        if (sub.subscribe(topic, RecvFlags.NONE)) {
            System.out.printf("%s: %s%n",
                topic.topic(),
                topic.singlePartOrThrow().toUtf8String());
        }
    }
}
```

> `subscribe()` 호출은 블로킹입니다. 논블로킹 수신은 `RecvFlags.DONT_WAIT`를 사용합니다.

---

## XPUB / XSUB

구독 이벤트를 직접 처리해야 할 때 사용합니다. ([코어 참고](../../03-2-pubsub.md))

```java
try (XPubSocket xpub = ctx.createXPubSocket();
     SubSocket sub = ctx.createSubSocket()) {

    xpub.bind("tcp://127.0.0.1:5563");
    sub.connect("tcp://127.0.0.1:5563");
    sub.setSubscription("events");

    // 구독 이벤트 수신
    try (SubscriptionEvent event = new SubscriptionEvent()) {
        xpub.receiveSubscriptionEvent(event, RecvFlags.NONE);
        System.out.printf("subscribed=%b topic=%s%n",
            event.subscribed(), event.topic());
    }
}
```

---

## STREAM

원시 TCP 피어와 바이트 스트림을 교환합니다. ([코어 참고](../../03-5-stream.md))

```java
try (Context ctx = Zlink.createContext();
     StreamSocket server = ctx.createStreamSocket()) {

    server.bind("tcp://127.0.0.1:5564");

    // 일반 TCP 클라이언트 연결
    Socket tcpClient = new Socket("127.0.0.1", 5564);
    tcpClient.getOutputStream().write("hello".getBytes());

    // STREAM 소켓 수신 (라우팅 ID = TCP 세션 식별자)
    try (Received received = new Received()) {
        server.recv(received, RecvFlags.NONE);
        System.out.println(received.firstPart().toUtf8String()); // hello

        // 동일 TCP 세션으로 응답
        try (Message reply = Message.from("world")) {
            received.send().message(reply).submit();
        }
    }
}
```

---

## 프록시 (Proxy)

FRONTEND → BACKEND 사이를 중계합니다. 호출 스레드를 블록합니다.

```java
try (XSubSocket frontend = ctx.createXSubSocket();
     XPubSocket backend = ctx.createXPubSocket()) {

    frontend.bind("tcp://127.0.0.1:5565");
    backend.bind("tcp://127.0.0.1:5566");

    new Thread(() -> Zlink.proxy(frontend, backend)).start();
}
```

---

## 논블로킹 수신

`RecvFlags.DONT_WAIT`으로 블로킹 없이 시도합니다. 메시지가 없으면 `false`를 반환합니다.

```java
try (Received received = new Received()) {
    boolean ok = socket.recv(received, RecvFlags.DONT_WAIT);
    if (!ok) { continue; } // 메시지 없음
    // 파트 처리
}
```

---

## 전송 엔드포인트

모든 소켓은 `tcp`, `ipc`, `inproc`, `ws`, `tls+tcp` 트랜스포트를 지원합니다.

```java
socket.bind("tcp://0.0.0.0:5555");
socket.bind("ipc:///tmp/my.sock");
socket.bind("inproc://my-channel");
socket.connect("tcp://10.0.0.1:5555");
socket.disconnect("tcp://10.0.0.1:5555");
socket.unbind("tcp://0.0.0.0:5555");
```
