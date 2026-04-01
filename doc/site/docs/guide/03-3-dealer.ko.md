# DEALER 소켓

## 1. 개요

DEALER 소켓은 비동기 요청 소켓이다.
여러 피어에 **Round-robin** 분배로 송신하고, **Fair-queue**로 수신한다.
send/recv 순서 강제가 없어 자유로운 비동기 메시징이 가능하다.

**핵심 특성:**
- 송신: Round-robin (`lb_t`) — 연결된 피어에 순환 분배
- 수신: Fair-queue (`fq_t`) — 모든 피어에서 공정하게 수신
- send/recv 순서 강제 없음 (비동기)

**유효한 소켓 조합:** DEALER ↔ ROUTER, DEALER ↔ DEALER

```
1:1 양방향 비동기 (PAIR와 유사, HWM/재연결 지원)

┌──────────┐                    ┌──────────┐
│ DEALER A │◄──────────────────►│ DEALER B │
└──────────┘                    └──────────┘


1:N Round-robin 작업 분배 (PUSH/PULL 대체)

                  ┌──────────┐
        msg 1 ───►│ DEALER 1 │
       /          └──────────┘
┌────────┐        ┌──────────┐
│ DEALER │─msg 2─►│ DEALER 2 │
└────────┘        └──────────┘
       \          ┌──────────┐
        msg 3 ───►│ DEALER 3 │
                  └──────────┘
```

## 2. 기본 사용법

### 생성 및 연결

=== "C"

    ```c
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);

    /* routing_id 설정 (선택, ROUTER에서 식별용) */
    zlink_set_routing_id(dealer, "client-1", 8);

    /* 서버에 연결 */
    zlink_connect(dealer, "tcp://127.0.0.1:5558");
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t dealer(ctx);

    // routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.set_routing_id("client-1");

    // 서버에 연결
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Java"

    ```java
    DealerSocket dealer = new DealerSocket(ctx);

    // routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.setRoutingId("client-1");

    // 서버에 연결
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Python"

    ```python
    dealer = zlink.DealerSocket(ctx)

    # routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.set_routing_id("client-1")

    # 서버에 연결
    dealer.connect("tcp://127.0.0.1:5558")
    ```

=== "Node/TypeScript"

    ```typescript
    const dealer = new zlink.DealerSocket(ctx);

    // routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.setRoutingId("client-1");

    // 서버에 연결
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "C#/.NET"

    ```csharp
    var dealer = new DealerSocket(ctx);

    // routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.SetRoutingId("client-1");

    // 서버에 연결
    dealer.Connect("tcp://127.0.0.1:5558");
    ```

=== "Rust"

    ```rust
    let dealer = ctx.dealer_socket();

    // routing_id 설정 (선택, ROUTER에서 식별용)
    dealer.set_routing_id("client-1");

    // 서버에 연결
    dealer.connect("tcp://127.0.0.1:5558");
    ```

### 메시지 송수신

=== "C"

    ```c
    /* 요청 전송 — 순서 제약 없이 연속 전송 가능 */
    zlink_msg_t msg1, msg2, msg3;
    zlink_msg_init_size(&msg1, 9);
    memcpy(zlink_msg_data(&msg1), "request-1", 9);
    zlink_send(dealer, &msg1, 1, 0);

    zlink_msg_init_size(&msg2, 9);
    memcpy(zlink_msg_data(&msg2), "request-2", 9);
    zlink_send(dealer, &msg2, 1, 0);

    zlink_msg_init_size(&msg3, 9);
    memcpy(zlink_msg_data(&msg3), "request-3", 9);
    zlink_send(dealer, &msg3, 1, 0);

    /* 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨 */
    ```

=== "C++"

    ```cpp
    // 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.send("request-1");
    dealer.send("request-2");
    dealer.send("request-3");

    // 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

=== "Java"

    ```java
    // 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.send("request-1");
    dealer.send("request-2");
    dealer.send("request-3");

    // 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

=== "Python"

    ```python
    # 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.send(b"request-1")
    dealer.send(b"request-2")
    dealer.send(b"request-3")

    # 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

=== "Node/TypeScript"

    ```typescript
    // 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.send(Buffer.from("request-1"));
    dealer.send(Buffer.from("request-2"));
    dealer.send(Buffer.from("request-3"));

    // 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

=== "C#/.NET"

    ```csharp
    // 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.Send("request-1");
    dealer.Send("request-2");
    dealer.Send("request-3");

    // 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

=== "Rust"

    ```rust
    // 요청 전송 — 순서 제약 없이 연속 전송 가능
    dealer.send(b"request-1");
    dealer.send(b"request-2");
    dealer.send(b"request-3");

    // 응답은 생성 시 등록한 핸들러 콜백으로 디스패치됨
    ```

### 수신 모드

DEALER는 `zlink_recv()`로 동기 수신한다.

=== "C"

    ```c
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(dealer, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        /* parts[0..part_count-1] 처리 */
        zlink_multipart_close(parts, part_count);
        free(parts);
    }
    ```

=== "C++"

    ```cpp
    auto [source_rid, parts] = dealer.recv();
    // parts[0..N-1] 처리
    ```

=== "Java"

    ```java
    Message msg = dealer.recv();
    // msg.parts() 처리
    ```

=== "Python"

    ```python
    source_rid, parts = dealer.recv()
    # parts[0..N-1] 처리
    ```

=== "Node/TypeScript"

    ```typescript
    const [sourceRid, parts] = dealer.receive();
    // parts[0..N-1] 처리
    ```

=== "C#/.NET"

    ```csharp
    var (sourceRid, parts) = dealer.Receive();
    // parts[0..N-1] 처리
    ```

=== "Rust"

    ```rust
    let (source_rid, parts) = dealer.recv();
    // parts[0..N-1] 처리
    ```

> HWM 도달 시 `zlink_send()`는 블록(기본) 또는 `ZLINK_DONTWAIT`로
> `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

## 3. 사용 예제

=== "C"

    ```c
    /* DEALER ↔ DEALER 멀티파트 전송 */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(dealer, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.send({"header", "body"});
    ```

=== "Java"

    ```java
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.send("header", "body");
    ```

=== "Python"

    ```python
    # DEALER ↔ DEALER 멀티파트 전송
    dealer.send([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.Send("header", "body");
    ```

=== "Rust"

    ```rust
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.send(&[b"header", b"body"]);
    ```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `zlink_set_routing_id()` | binary | 자동(UUID) | ROUTER에서 식별할 ID (전용 함수) |
| `ZLINK_PROBE_ROUTER` | int | 0 | 연결 시 빈 메시지 전송 (연결 알림) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 큐 최대 메시지 수 |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 대기 시간 (ms) |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms) |
| `ZLINK_CONNECT_ROUTING_ID` | binary | — | 다음 connect에 적용할 alias |

### routing_id 설정

ROUTER가 DEALER를 식별하려면 명시적으로 routing_id를 설정한다.

=== "C"

    ```c
    /* bind/connect 전에 설정 */
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, "tcp://127.0.0.1:5558");
    ```

=== "C++"

    ```cpp
    // bind/connect 전에 설정
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Java"

    ```java
    // bind/connect 전에 설정
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "Python"

    ```python
    # bind/connect 전에 설정
    dealer.set_routing_id("D1")
    dealer.connect("tcp://127.0.0.1:5558")
    ```

=== "Node/TypeScript"

    ```typescript
    // bind/connect 전에 설정
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

=== "C#/.NET"

    ```csharp
    // bind/connect 전에 설정
    dealer.SetRoutingId("D1");
    dealer.Connect("tcp://127.0.0.1:5558");
    ```

=== "Rust"

    ```rust
    // bind/connect 전에 설정
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5558");
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — `zlink_set_routing_id(dealer1, "D1", 2)`

## 5. 사용 패턴

### 패턴 1: 1:1 양방향 비동기

PAIR와 유사하지만 HWM과 자동 재연결을 지원한다. 응답이 필요한 경우 반드시 1:1로 구성해야 한다.
(routing_id가 없으므로 1:N에서는 어떤 피어가 응답했는지 구분할 수 없다.)

=== "C"

    ```c
    void *a = zlink_socket(ctx, ZLINK_DEALER);
    zlink_bind(a, "tcp://*:5558");

    void *b = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(b, "tcp://127.0.0.1:5558");

    /* 양방향 자유 전송 */
    zlink_msg_t ping;
    zlink_msg_init_size(&ping, 4);
    memcpy(zlink_msg_data(&ping), "ping", 4);
    zlink_send(a, &ping, 1, 0);

    zlink_msg_t pong;
    zlink_msg_init_size(&pong, 4);
    memcpy(zlink_msg_data(&pong), "pong", 4);
    zlink_send(b, &pong, 1, 0);

    /* b가 "ping" 수신, a가 "pong" 수신 */
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t a(ctx);
    a.bind("tcp://*:5558");

    zlink::dealer_socket_t b(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // 양방향 자유 전송
    a.send("ping");
    b.send("pong");
    ```

=== "Java"

    ```java
    DealerSocket a = new DealerSocket(ctx);
    a.bind("tcp://*:5558");

    DealerSocket b = new DealerSocket(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // 양방향 자유 전송
    a.send("ping");
    b.send("pong");
    ```

=== "Python"

    ```python
    a = zlink.DealerSocket(ctx)
    a.bind("tcp://*:5558")

    b = zlink.DealerSocket(ctx)
    b.connect("tcp://127.0.0.1:5558")

    # 양방향 자유 전송
    a.send(b"ping")
    b.send(b"pong")
    ```

=== "Node/TypeScript"

    ```typescript
    const a = new zlink.DealerSocket(ctx);
    a.bind("tcp://*:5558");

    const b = new zlink.DealerSocket(ctx);
    b.connect("tcp://127.0.0.1:5558");

    // 양방향 자유 전송
    a.send(Buffer.from("ping"));
    b.send(Buffer.from("pong"));
    ```

=== "C#/.NET"

    ```csharp
    var a = new DealerSocket(ctx);
    a.Bind("tcp://*:5558");

    var b = new DealerSocket(ctx);
    b.Connect("tcp://127.0.0.1:5558");

    // 양방향 자유 전송
    a.Send("ping");
    b.Send("pong");
    ```

=== "Rust"

    ```rust
    let a = ctx.dealer_socket();
    a.bind("tcp://*:5558");

    let b = ctx.dealer_socket();
    b.connect("tcp://127.0.0.1:5558");

    // 양방향 자유 전송
    a.send(b"ping");
    b.send(b"pong");
    ```

### 패턴 2: 1:N Round-robin 작업 분배

PUSH/PULL 없이 작업을 N개 워커에 순환 분배하는 패턴.
응답이 필요 없는 작업 분배 또는 파이프라인 단계 간 전달에 사용한다.

=== "C"

    ```c
    /* 분배자 */
    void *sender = zlink_socket(ctx, ZLINK_DEALER);
    zlink_bind(sender, "tcp://*:5558");

    /* 워커 3대 */
    void *w1 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(w1, "tcp://127.0.0.1:5558");
    void *w2 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(w2, "tcp://127.0.0.1:5558");
    void *w3 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_connect(w3, "tcp://127.0.0.1:5558");

    /* 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin) */
    for (int i = 0; i < 6; i++) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "task-%d", i);
        zlink_msg_t task;
        zlink_msg_init_size(&task, len);
        memcpy(zlink_msg_data(&task), buf, len);
        zlink_send(sender, &task, 1, 0);
    }
    ```

=== "C++"

    ```cpp
    // 분배자
    zlink::dealer_socket_t sender(ctx);
    sender.bind("tcp://*:5558");

    // 워커 3대
    zlink::dealer_socket_t w1(ctx), w2(ctx), w3(ctx);
    w1.connect("tcp://127.0.0.1:5558");
    w2.connect("tcp://127.0.0.1:5558");
    w3.connect("tcp://127.0.0.1:5558");

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for (int i = 0; i < 6; i++) {
        sender.send("task-" + std::to_string(i));
    }
    ```

=== "Java"

    ```java
    // 분배자
    DealerSocket sender = new DealerSocket(ctx);
    sender.bind("tcp://*:5558");

    // 워커 3대
    DealerSocket w1 = new DealerSocket(ctx);
    w1.connect("tcp://127.0.0.1:5558");
    DealerSocket w2 = new DealerSocket(ctx);
    w2.connect("tcp://127.0.0.1:5558");
    DealerSocket w3 = new DealerSocket(ctx);
    w3.connect("tcp://127.0.0.1:5558");

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for (int i = 0; i < 6; i++) {
        sender.send("task-" + i);
    }
    ```

=== "Python"

    ```python
    # 분배자
    sender = zlink.DealerSocket(ctx)
    sender.bind("tcp://*:5558")

    # 워커 3대
    w1 = zlink.DealerSocket(ctx)
    w1.connect("tcp://127.0.0.1:5558")
    w2 = zlink.DealerSocket(ctx)
    w2.connect("tcp://127.0.0.1:5558")
    w3 = zlink.DealerSocket(ctx)
    w3.connect("tcp://127.0.0.1:5558")

    # 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for i in range(6):
        sender.send(f"task-{i}".encode())
    ```

=== "Node/TypeScript"

    ```typescript
    // 분배자
    const sender = new zlink.DealerSocket(ctx);
    sender.bind("tcp://*:5558");

    // 워커 3대
    const w1 = new zlink.DealerSocket(ctx);
    w1.connect("tcp://127.0.0.1:5558");
    const w2 = new zlink.DealerSocket(ctx);
    w2.connect("tcp://127.0.0.1:5558");
    const w3 = new zlink.DealerSocket(ctx);
    w3.connect("tcp://127.0.0.1:5558");

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for (let i = 0; i < 6; i++) {
        sender.send(Buffer.from(`task-${i}`));
    }
    ```

=== "C#/.NET"

    ```csharp
    // 분배자
    var sender = new DealerSocket(ctx);
    sender.Bind("tcp://*:5558");

    // 워커 3대
    var w1 = new DealerSocket(ctx);
    w1.Connect("tcp://127.0.0.1:5558");
    var w2 = new DealerSocket(ctx);
    w2.Connect("tcp://127.0.0.1:5558");
    var w3 = new DealerSocket(ctx);
    w3.Connect("tcp://127.0.0.1:5558");

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for (int i = 0; i < 6; i++) {
        sender.Send($"task-{i}");
    }
    ```

=== "Rust"

    ```rust
    // 분배자
    let sender = ctx.dealer_socket();
    sender.bind("tcp://*:5558");

    // 워커 3대
    let w1 = ctx.dealer_socket();
    w1.connect("tcp://127.0.0.1:5558");
    let w2 = ctx.dealer_socket();
    w2.connect("tcp://127.0.0.1:5558");
    let w3 = ctx.dealer_socket();
    w3.connect("tcp://127.0.0.1:5558");

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for i in 0..6 {
        sender.send(format!("task-{}", i).as_bytes());
    }
    ```

> DEALER ↔ ROUTER 조합(로드밸런싱 + 응답 라우팅, 프록시 등)은
> [ROUTER 소켓](03-4-router.ko.md)을 참고.

## 6. 주의사항

### 피어 없으면 큐잉

연결된 피어가 없으면 메시지는 송신 큐에 쌓인다. HWM 초과 시 블록(기본) 또는 `EAGAIN` 반환(`ZLINK_DONTWAIT`).

=== "C"

    ```c
    /* 피어가 없는 상태에서 전송 */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "data", 4);
    int rc = zlink_send(dealer, &msg, 1, ZLINK_DONTWAIT);
    if (rc == -1 && errno == EAGAIN) {
        /* HWM 초과 또는 피어 없음 */
    }
    ```

=== "C++"

    ```cpp
    // 피어가 없는 상태에서 전송
    try {
        dealer.send("data", zlink::dontwait);
    } catch (const zlink::eagain_error& e) {
        // HWM 초과 또는 피어 없음
    }
    ```

=== "Java"

    ```java
    // 피어가 없는 상태에서 전송
    try {
        dealer.send("data", SendFlags.DONTWAIT);
    } catch (EagainException e) {
        // HWM 초과 또는 피어 없음
    }
    ```

=== "Python"

    ```python
    # 피어가 없는 상태에서 전송
    try:
        dealer.send(b"data", flags=zlink.DONTWAIT)
    except zlink.Again:
        # HWM 초과 또는 피어 없음
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    // 피어가 없는 상태에서 전송
    try {
        dealer.send(Buffer.from("data"), { dontwait: true });
    } catch (e) {
        // HWM 초과 또는 피어 없음
    }
    ```

=== "C#/.NET"

    ```csharp
    // 피어가 없는 상태에서 전송
    try {
        dealer.Send("data", SendFlags.DontWait);
    } catch (EagainException) {
        // HWM 초과 또는 피어 없음
    }
    ```

=== "Rust"

    ```rust
    // 피어가 없는 상태에서 전송
    match dealer.send_dontwait(b"data") {
        Err(ZlinkError::Eagain) => {
            // HWM 초과 또는 피어 없음
        }
        _ => {}
    }
    ```

### Round-robin 분배

여러 피어가 연결된 경우 메시지는 순환적으로 분배된다. 특정 피어에게만 전송하려면 ROUTER를 사용한다.

### routing_id는 connect 전에 설정

`zlink_set_routing_id()`는 `zlink_connect()` 호출 전에 호출해야 한다. 연결 후 변경은 적용되지 않는다.

=== "C"

    ```c
    /* 올바른 순서 */
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, endpoint);  /* D1으로 식별 */
    ```

=== "C++"

    ```cpp
    // 올바른 순서
    dealer.set_routing_id("D1");
    dealer.connect(endpoint);  // D1으로 식별
    ```

=== "Java"

    ```java
    // 올바른 순서
    dealer.setRoutingId("D1");
    dealer.connect(endpoint);  // D1으로 식별
    ```

=== "Python"

    ```python
    # 올바른 순서
    dealer.set_routing_id("D1")
    dealer.connect(endpoint)  # D1으로 식별
    ```

=== "Node/TypeScript"

    ```typescript
    // 올바른 순서
    dealer.setRoutingId("D1");
    dealer.connect(endpoint);  // D1으로 식별
    ```

=== "C#/.NET"

    ```csharp
    // 올바른 순서
    dealer.SetRoutingId("D1");
    dealer.Connect(endpoint);  // D1으로 식별
    ```

=== "Rust"

    ```rust
    // 올바른 순서
    dealer.set_routing_id("D1");
    dealer.connect(endpoint);  // D1으로 식별
    ```

---
[← PUB/SUB](03-2-pubsub.ko.md) | [ROUTER →](03-4-router.ko.md)
