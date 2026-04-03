# PAIR 소켓

## 1. 개요

PAIR 소켓은 정확히 하나의 피어와 1:1 양방향 독점 연결을 형성한다. 두 번째 피어가 연결하면 첫 번째 연결은 끊어진다.

**핵심 특성:**
- 단일 파이프만 허용 (1:1 독점)
- 양방향 자유 메시징 (send/recv 순서 무관)
- 가장 단순한 소켓 타입

**유효한 소켓 조합:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. 기본 사용법

### 생성 및 연결

=== "C"

    ```c
    void *ctx = zlink_ctx_new();

    /* 서버 측 */
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "tcp://*:5555");

    /* 클라이언트 측 */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "tcp://127.0.0.1:5555");
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;

    // 서버 측
    zlink::pair_socket_t server(ctx);
    server.bind("tcp://*:5555");

    // 클라이언트 측
    zlink::pair_socket_t client(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Java"

    ```java
    Context ctx = new Context();

    // 서버 측
    PairSocket server = new PairSocket(ctx);
    server.bind("tcp://*:5555");

    // 클라이언트 측
    PairSocket client = new PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Python"

    ```python
    ctx = zlink.Context()

    # 서버 측
    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    # 클라이언트 측
    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();

    // 서버 측
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://*:5555");

    // 클라이언트 측
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "C#/.NET"

    ```csharp
    var ctx = new Context();

    // 서버 측
    var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    // 클라이언트 측
    var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");
    ```

=== "Rust"

    ```rust
    let ctx = Context::new();

    // 서버 측
    let server = ctx.pair_socket();
    server.bind("tcp://*:5555");

    // 클라이언트 측
    let client = ctx.pair_socket();
    client.connect("tcp://127.0.0.1:5555");
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { log.Fatal(err) }
    defer ctx.Close()

    // 서버 측
    server, err := ctx.PairSocket()
    if err != nil { log.Fatal(err) }
    server.Bind("tcp://*:5555")

    // 클라이언트 측
    client, err := ctx.PairSocket()
    if err != nil { log.Fatal(err) }
    client.Connect("tcp://127.0.0.1:5555")
    ```

### 메시지 교환

!!! note "C API 콜백 시그니처"
    수신 핸들러는 C 전용 타입(`zlink_routing_id_t`, `zlink_msg_t`)을
    사용한다. 각 바인딩은 자체적인 관용적 콜백/수신 인터페이스를 제공한다.

    ```c
    /* 수신 핸들러 정의 */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C"

    ```c
    /* 서버는 recv 모드를 유지 */
    void *server = zlink_socket(ctx, ZLINK_PAIR);

    /* 클라이언트 (송신 전용) */
    void *client = zlink_socket(ctx, ZLINK_PAIR);

    zlink_bind(server, "tcp://*:5555");
    zlink_connect(client, "tcp://127.0.0.1:5555");

    /* 클라이언트 → 서버 */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 5);
    memcpy(zlink_msg_data(&msg), "Hello", 5);
    zlink_send(client, &msg, 1, 0);
    /* 서버는 zlink_recv() 또는 poller + zlink_recv()로 수신 */

    /* 서버 → 클라이언트 (양방향이지만 클라이언트도 수신하려면 핸들러 필요) */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "World", 5);
    zlink_send(server, &reply, 1, 0);
    ```

=== "C++"

    ```cpp
    // 서버는 recv 모드를 유지
    zlink::pair_socket_t server(ctx);

    // 클라이언트 (송신 전용)
    zlink::pair_socket_t client(ctx);

    server.bind("tcp://*:5555");
    client.connect("tcp://127.0.0.1:5555");

    // 클라이언트 → 서버
    client.send("Hello");

    // 서버 → 클라이언트
    server.send("World");
    ```

=== "Java"

    ```java
    // 서버는 recv 모드를 유지
    PairSocket server = new PairSocket(ctx);

    // 클라이언트 (송신 전용)
    PairSocket client = new PairSocket(ctx);

    server.bind("tcp://*:5555");
    client.connect("tcp://127.0.0.1:5555");

    // 클라이언트 → 서버
    client.send("Hello");

    // 서버 → 클라이언트
    server.send("World");
    ```

=== "Python"

    ```python
    # 서버는 recv 모드를 유지
    server = zlink.PairSocket(ctx)

    # 클라이언트 (송신 전용)
    client = zlink.PairSocket(ctx)

    server.bind("tcp://*:5555")
    client.connect("tcp://127.0.0.1:5555")

    # 클라이언트 → 서버
    client.send(b"Hello")

    # 서버 → 클라이언트
    server.send(b"World")
    ```

=== "Node/TypeScript"

    ```typescript
    // 서버는 recv 모드를 유지
    const server = new zlink.PairSocket(ctx);

    // 클라이언트 (송신 전용)
    const client = new zlink.PairSocket(ctx);

    server.bind('tcp://*:5555');
    client.connect('tcp://127.0.0.1:5555');

    // 클라이언트 → 서버
    client.send(Buffer.from("Hello"));

    // 서버 → 클라이언트
    server.send(Buffer.from("World"));
    ```

=== "C#/.NET"

    ```csharp
    // 서버는 recv 모드를 유지
    var server = new PairSocket(ctx);

    // 클라이언트 (송신 전용)
    var client = new PairSocket(ctx);

    server.Bind("tcp://*:5555");
    client.Connect("tcp://127.0.0.1:5555");

    // 클라이언트 → 서버
    client.Send("Hello");

    // 서버 → 클라이언트
    server.Send("World");
    ```

=== "Rust"

    ```rust
    // 서버는 recv 모드를 유지
    let server = ctx.pair_socket();

    // 클라이언트 (송신 전용)
    let client = ctx.pair_socket();

    server.bind("tcp://*:5555")?;
    client.connect("tcp://127.0.0.1:5555")?;

    // 클라이언트 → 서버
    client.send(b"Hello");

    // 서버 → 클라이언트
    server.send(b"World");
    ```

=== "Go"

    ```go
    // 서버는 recv 모드를 유지
    server, _ := ctx.PairSocket()

    // 클라이언트 (송신 전용)
    client, _ := ctx.PairSocket()

    server.Bind("tcp://*:5555")
    client.Connect("tcp://127.0.0.1:5555")

    // 클라이언트 → 서버
    client.Send(zlink.NewMessage([]byte("Hello")))

    // 서버 → 클라이언트
    server.Send(zlink.NewMessage([]byte("World")))
    ```

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_callback_sample.c) |
    | C++ | [pair_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_callback_sample.cpp) |
    | Java | [PairCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairCallbackSample.java) |
    | Python | [pair_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_callback.py) |
    | Node | [pair_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairCallback/Program.cs) |
    | Rust | [pair_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_callback_sample/main.go) |

### 멀티파트 데이터 전송

멀티파트 데이터는 단일 `zlink_send` 호출로 parts 배열을 전송한다.

=== "C"

    ```c
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 3);
    memcpy(zlink_msg_data(&parts[0]), "foo", 3);
    zlink_msg_init_size(&parts[1], 6);
    memcpy(zlink_msg_data(&parts[1]), "foobar", 6);
    zlink_send(server, parts, 2, 0);

    /* 수신 측은 한 번의 zlink_recv() 호출로 두 프레임을 수신:
       parts[0] = "foo", parts[1] = "foobar", part_count = 2 */
    ```

=== "C++"

    ```cpp
    server.send({"foo", "foobar"});

    // 수신 측은 한 번의 recv() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Java"

    ```java
    server.send("foo", "foobar");

    // 수신 측은 한 번의 recv() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Python"

    ```python
    server.send([b"foo", b"foobar"])

    # 수신 측은 한 번의 recv() 호출로 두 프레임을 수신:
    # parts[0] = b"foo", parts[1] = b"foobar"
    ```

=== "Node/TypeScript"

    ```typescript
    server.send([Buffer.from("foo"), Buffer.from("foobar")]);

    // 수신 측은 한 번의 receive() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "C#/.NET"

    ```csharp
    server.Send("foo", "foobar");

    // 수신 측은 한 번의 Receive() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Rust"

    ```rust
    server.send(&[b"foo", b"foobar"]);

    // 수신 측은 한 번의 recv() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

=== "Go"

    ```go
    server.SendMultipart([]zlink.Message{
        zlink.NewMessage([]byte("foo")),
        zlink.NewMessage([]byte("foobar")),
    })

    // 수신 측은 한 번의 recv() 호출로 두 프레임을 수신:
    // parts[0] = "foo", parts[1] = "foobar"
    ```

> 참고: `core/tests/test_pair_inproc.cpp` — `test_zlink_send_multipart()` 테스트

### 수신 모드

PAIR의 public API는 recv/poller-only다.
`zlink_recv()`로 동기 수신한다.

=== "C"

    ```c
    void *pair = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(pair, "tcp://*:5556");

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(pair, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        /* parts[0..part_count-1] 처리 */
        zlink_multipart_close(parts, part_count);
        free(parts);
    }
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t pair(ctx);
    pair.bind("tcp://*:5556");

    auto [source_rid, parts] = pair.recv();
    // parts[0..N-1] 처리
    ```

=== "Java"

    ```java
    PairSocket pair = new PairSocket(ctx);
    pair.bind("tcp://*:5556");

    Message msg = pair.recv();
    // msg.parts() 처리
    ```

=== "Python"

    ```python
    pair = zlink.PairSocket(ctx)
    pair.bind("tcp://*:5556")

    source_rid, parts = pair.recv()
    # parts[0..N-1] 처리
    ```

=== "Node/TypeScript"

    ```typescript
    const pair = new zlink.PairSocket(ctx);
    pair.bind("tcp://*:5556");

    const [sourceRid, parts] = pair.receive();
    // parts[0..N-1] 처리
    ```

=== "C#/.NET"

    ```csharp
    var pair = new PairSocket(ctx);
    pair.Bind("tcp://*:5556");

    var (sourceRid, parts) = pair.Receive();
    // parts[0..N-1] 처리
    ```

=== "Rust"

    ```rust
    let pair = ctx.pair_socket();
    pair.bind("tcp://*:5556");

    let (source_rid, parts) = pair.recv();
    // parts[0..N-1] 처리
    ```

=== "Go"

    ```go
    pair, _ := ctx.PairSocket()
    pair.Bind("tcp://*:5556")

    received, err := pair.Recv()
    if err != nil { log.Fatal(err) }
    defer received.Close()
    // received parts 처리
    ```

> HWM 도달 시 `zlink_send()`는 블록(기본) 또는 `ZLINK_DONTWAIT`로
> `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

??? example "Full Sample Code"

    | Language | Source |
    |----------|--------|
    | C | [pair_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pair_recv_sample.c) |
    | C++ | [pair_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pair_recv_sample.cpp) |
    | Java | [PairRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PairRecvSample.java) |
    | Python | [pair_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pair_recv.py) |
    | Node | [pair_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pair_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PairRecv/Program.cs) |
    | Rust | [pair_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pair_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pair_recv_sample/main.go) |

## 3. 메시지 형식

PAIR 소켓의 메시지 프레임에는 **애플리케이션 데이터만** 포함된다.

```
단일 프레임:     [데이터]
멀티파트 프레임:  [프레임1][프레임2]...[프레임N]
```

> `source_rid` 등 공통 수신 인터페이스는
> [소켓 패턴 개요](03-0-socket-patterns.ko.md#7-공통-수신-인터페이스)를 참고.

멀티파트 전송:

=== "C"

    ```c
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(server, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    server.send({"header", "body"});
    ```

=== "Java"

    ```java
    server.send("header", "body");
    ```

=== "Python"

    ```python
    server.send([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    server.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    server.Send("header", "body");
    ```

=== "Rust"

    ```rust
    server.send(&[b"header", b"body"]);
    ```

=== "Go"

    ```go
    server.SendMultipart([]zlink.Message{
        zlink.NewMessage([]byte("header")),
        zlink.NewMessage([]byte("body")),
    })
    ```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 큐 최대 메시지 수 |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 큐 최대 메시지 수 |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 미전송 메시지 대기 시간 (ms), -1=무한 |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | 송신 타임아웃 (ms), -1=무한 |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | 수신 타임아웃 (ms), -1=무한 |

=== "C"

    ```c
    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

    int linger = 0;  /* close 즉시 반환 */
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // close 즉시 반환
    ```

=== "Java"

    ```java
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // close 즉시 반환
    ```

=== "Python"

    ```python
    socket.set_option(ZLINK_OPT_SNDHWM, 5000)

    socket.set_option(ZLINK_OPT_LINGER, 0)  # close 즉시 반환
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // close 즉시 반환
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(ZLINK_OPT_SNDHWM, 5000);

    socket.SetOption(ZLINK_OPT_LINGER, 0);  // close 즉시 반환
    ```

=== "Rust"

    ```rust
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // close 즉시 반환
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionSndHWM, 5000)

    socket.SetOption(zlink.OptionLinger, 0)  // close 즉시 반환
    ```

## 5. 사용 패턴

### 패턴 1: 스레드 간 시그널링 (inproc)

가장 일반적인 PAIR 사용 사례. inproc transport로 스레드 간 zero-copy 통신.

=== "C"

    ```c
    /* 메인 스레드 */
    void *signal = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(signal, "inproc://signal");

    /* 워커 스레드 */
    void *worker_signal = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(worker_signal, "inproc://signal");

    /* 워커 → 메인: 작업 완료 시그널 */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "DONE", 4);
    zlink_send(worker_signal, &msg, 1, 0);

    /* 메인: on_signal 콜백이 "DONE"을 비동기로 수신 */
    ```

=== "C++"

    ```cpp
    // 메인 스레드
    zlink::pair_socket_t signal(ctx);
    signal.bind("inproc://signal");

    // 워커 스레드
    zlink::pair_socket_t worker_signal(ctx);
    worker_signal.connect("inproc://signal");

    // 워커 → 메인: 작업 완료 시그널
    worker_signal.send("DONE");
    ```

=== "Java"

    ```java
    // 메인 스레드
    PairSocket signal = new PairSocket(ctx);
    signal.bind("inproc://signal");

    // 워커 스레드
    PairSocket workerSignal = new PairSocket(ctx);
    workerSignal.connect("inproc://signal");

    // 워커 → 메인: 작업 완료 시그널
    workerSignal.send("DONE");
    ```

=== "Python"

    ```python
    # 메인 스레드
    signal = zlink.PairSocket(ctx)
    signal.bind("inproc://signal")

    # 워커 스레드
    worker_signal = zlink.PairSocket(ctx)
    worker_signal.connect("inproc://signal")

    # 워커 → 메인: 작업 완료 시그널
    worker_signal.send(b"DONE")
    ```

=== "Node/TypeScript"

    ```typescript
    // 메인 스레드
    const signal = new zlink.PairSocket(ctx);
    signal.bind("inproc://signal");

    // 워커 스레드
    const workerSignal = new zlink.PairSocket(ctx);
    workerSignal.connect("inproc://signal");

    // 워커 → 메인: 작업 완료 시그널
    workerSignal.send(Buffer.from("DONE"));
    ```

=== "C#/.NET"

    ```csharp
    // 메인 스레드
    var signal = new PairSocket(ctx);
    signal.Bind("inproc://signal");

    // 워커 스레드
    var workerSignal = new PairSocket(ctx);
    workerSignal.Connect("inproc://signal");

    // 워커 → 메인: 작업 완료 시그널
    workerSignal.Send("DONE");
    ```

=== "Rust"

    ```rust
    // 메인 스레드
    let signal = ctx.pair_socket();
    signal.bind("inproc://signal");

    // 워커 스레드
    let worker_signal = ctx.pair_socket();
    worker_signal.connect("inproc://signal");

    // 워커 → 메인: 작업 완료 시그널
    worker_signal.send(b"DONE");
    ```

=== "Go"

    ```go
    // 메인 스레드
    signal, _ := ctx.PairSocket()
    signal.Bind("inproc://signal")

    // 워커 스레드
    workerSignal, _ := ctx.PairSocket()
    workerSignal.Connect("inproc://signal")

    // 워커 → 메인: 작업 완료 시그널
    workerSignal.Send(zlink.NewMessage([]byte("DONE")))
    ```

> 참고: `core/tests/test_pair_inproc.cpp` — bind → connect → bounce 패턴

### 패턴 2: TCP 통신

네트워크를 통한 1:1 통신. 와일드카드 바인드로 포트 자동 할당 가능.

=== "C"

    ```c
    /* 서버: 와일드카드 포트 */
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "tcp://127.0.0.1:*");

    /* 할당된 엔드포인트 조회 */
    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* 클라이언트: 조회된 엔드포인트로 연결 */
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, endpoint);
    ```

=== "C++"

    ```cpp
    // 서버: 와일드카드 포트
    zlink::pair_socket_t server(ctx);
    server.bind("tcp://127.0.0.1:*");

    // 할당된 엔드포인트 조회
    auto endpoint = server.get_option<std::string>(ZLINK_OPT_LAST_ENDPOINT);

    // 클라이언트: 조회된 엔드포인트로 연결
    zlink::pair_socket_t client(ctx);
    client.connect(endpoint);
    ```

=== "Java"

    ```java
    // 서버: 와일드카드 포트
    PairSocket server = new PairSocket(ctx);
    server.bind("tcp://127.0.0.1:*");

    // 할당된 엔드포인트 조회
    String endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);

    // 클라이언트: 조회된 엔드포인트로 연결
    PairSocket client = new PairSocket(ctx);
    client.connect(endpoint);
    ```

=== "Python"

    ```python
    # 서버: 와일드카드 포트
    server = zlink.PairSocket(ctx)
    server.bind("tcp://127.0.0.1:*")

    # 할당된 엔드포인트 조회
    endpoint = server.get_option(ZLINK_OPT_LAST_ENDPOINT)

    # 클라이언트: 조회된 엔드포인트로 연결
    client = zlink.PairSocket(ctx)
    client.connect(endpoint)
    ```

=== "Node/TypeScript"

    ```typescript
    // 서버: 와일드카드 포트
    const server = new zlink.PairSocket(ctx);
    server.bind("tcp://127.0.0.1:*");

    // 할당된 엔드포인트 조회
    const endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);

    // 클라이언트: 조회된 엔드포인트로 연결
    const client = new zlink.PairSocket(ctx);
    client.connect(endpoint);
    ```

=== "C#/.NET"

    ```csharp
    // 서버: 와일드카드 포트
    var server = new PairSocket(ctx);
    server.Bind("tcp://127.0.0.1:*");

    // 할당된 엔드포인트 조회
    var endpoint = server.GetOption(ZLINK_OPT_LAST_ENDPOINT);

    // 클라이언트: 조회된 엔드포인트로 연결
    var client = new PairSocket(ctx);
    client.Connect(endpoint);
    ```

=== "Rust"

    ```rust
    // 서버: 와일드카드 포트
    let server = ctx.pair_socket();
    server.bind("tcp://127.0.0.1:*");

    // 할당된 엔드포인트 조회
    let endpoint = server.get_option::<String>(ZLINK_OPT_LAST_ENDPOINT);

    // 클라이언트: 조회된 엔드포인트로 연결
    let client = ctx.pair_socket();
    client.connect(&endpoint);
    ```

=== "Go"

    ```go
    // 서버: 와일드카드 포트
    server, _ := ctx.PairSocket()
    server.Bind("tcp://127.0.0.1:*")

    // 할당된 엔드포인트 조회
    status, _ := server.StatusSnapshot()
    endpoint := status.LocalEndpoint

    // 클라이언트: 조회된 엔드포인트로 연결
    client, _ := ctx.PairSocket()
    client.Connect(endpoint)
    ```

> 참고: `core/tests/test_pair_tcp.cpp` — `bind_loopback_ipv4()` + 와일드카드 바인드

### 패턴 3: DNS 이름 연결

호스트명으로도 연결 가능하다.

=== "C"

    ```c
    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "tcp://localhost:5555");
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t client(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "Java"

    ```java
    PairSocket client = new PairSocket(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "Python"

    ```python
    client = zlink.PairSocket(ctx)
    client.connect("tcp://localhost:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    const client = new zlink.PairSocket(ctx);
    client.connect("tcp://localhost:5555");
    ```

=== "C#/.NET"

    ```csharp
    var client = new PairSocket(ctx);
    client.Connect("tcp://localhost:5555");
    ```

=== "Rust"

    ```rust
    let client = ctx.pair_socket();
    client.connect("tcp://localhost:5555");
    ```

=== "Go"

    ```go
    client, _ := ctx.PairSocket()
    client.Connect("tcp://localhost:5555")
    ```

> 참고: `core/tests/test_pair_tcp.cpp` — `test_pair_tcp_connect_by_name()`

### 패턴 4: IPC 통신

같은 머신의 프로세스 간 통신 (Linux/macOS).

=== "C"

    ```c
    void *server = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(server, "ipc:///tmp/myapp.ipc");

    void *client = zlink_socket(ctx, ZLINK_PAIR);
    zlink_connect(client, "ipc:///tmp/myapp.ipc");
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t server(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    zlink::pair_socket_t client(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Java"

    ```java
    PairSocket server = new PairSocket(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    PairSocket client = new PairSocket(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Python"

    ```python
    server = zlink.PairSocket(ctx)
    server.bind("ipc:///tmp/myapp.ipc")

    client = zlink.PairSocket(ctx)
    client.connect("ipc:///tmp/myapp.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    const server = new zlink.PairSocket(ctx);
    server.bind("ipc:///tmp/myapp.ipc");

    const client = new zlink.PairSocket(ctx);
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "C#/.NET"

    ```csharp
    var server = new PairSocket(ctx);
    server.Bind("ipc:///tmp/myapp.ipc");

    var client = new PairSocket(ctx);
    client.Connect("ipc:///tmp/myapp.ipc");
    ```

=== "Rust"

    ```rust
    let server = ctx.pair_socket();
    server.bind("ipc:///tmp/myapp.ipc");

    let client = ctx.pair_socket();
    client.connect("ipc:///tmp/myapp.ipc");
    ```

=== "Go"

    ```go
    server, _ := ctx.PairSocket()
    server.Bind("ipc:///tmp/myapp.ipc")

    client, _ := ctx.PairSocket()
    client.Connect("ipc:///tmp/myapp.ipc")
    ```

> 참고: `core/tests/test_pair_ipc.cpp` — IPC 경로 길이 검증 포함

## 6. 주의사항

### 단일 피어만 허용

PAIR 소켓은 하나의 연결만 유지한다. 두 번째 피어가 connect하면 첫 번째 연결이 끊어진다.

```
 허용:  PAIR A ↔ PAIR B      (1:1)
 불가:  PAIR A ← PAIR B      (N:1 시도 시 기존 연결 끊김)
               ← PAIR C
```

N:1 통신이 필요하면 DEALER/ROUTER를 사용한다.

### inproc bind 순서

inproc transport는 **반드시 bind가 connect보다 먼저** 호출되어야 한다.

=== "C"

    ```c
    /* 올바른 순서 */
    zlink_bind(a, "inproc://signal");     /* 1. bind 먼저 */
    zlink_connect(b, "inproc://signal");  /* 2. connect */

    /* 잘못된 순서 — 실패 */
    zlink_connect(b, "inproc://signal");  /* bind가 아직 없으므로 실패 */
    zlink_bind(a, "inproc://signal");
    ```

=== "C++"

    ```cpp
    // 올바른 순서
    a.bind("inproc://signal");     // 1. bind 먼저
    b.connect("inproc://signal");  // 2. connect

    // 잘못된 순서 — 실패
    b.connect("inproc://signal");  // bind가 아직 없으므로 실패
    a.bind("inproc://signal");
    ```

=== "Java"

    ```java
    // 올바른 순서
    a.bind("inproc://signal");     // 1. bind 먼저
    b.connect("inproc://signal");  // 2. connect

    // 잘못된 순서 — 실패
    b.connect("inproc://signal");  // bind가 아직 없으므로 실패
    a.bind("inproc://signal");
    ```

=== "Python"

    ```python
    # 올바른 순서
    a.bind("inproc://signal")      # 1. bind 먼저
    b.connect("inproc://signal")   # 2. connect

    # 잘못된 순서 — 실패
    b.connect("inproc://signal")   # bind가 아직 없으므로 실패
    a.bind("inproc://signal")
    ```

=== "Node/TypeScript"

    ```typescript
    // 올바른 순서
    a.bind("inproc://signal");     // 1. bind 먼저
    b.connect("inproc://signal");  // 2. connect

    // 잘못된 순서 — 실패
    b.connect("inproc://signal");  // bind가 아직 없으므로 실패
    a.bind("inproc://signal");
    ```

=== "C#/.NET"

    ```csharp
    // 올바른 순서
    a.Bind("inproc://signal");     // 1. bind 먼저
    b.Connect("inproc://signal");  // 2. connect

    // 잘못된 순서 — 실패
    b.Connect("inproc://signal");  // bind가 아직 없으므로 실패
    a.Bind("inproc://signal");
    ```

=== "Rust"

    ```rust
    // 올바른 순서
    a.bind("inproc://signal");     // 1. bind 먼저
    b.connect("inproc://signal");  // 2. connect

    // 잘못된 순서 — 실패
    b.connect("inproc://signal");  // bind가 아직 없으므로 실패
    a.bind("inproc://signal");
    ```

=== "Go"

    ```go
    // 올바른 순서
    a.Bind("inproc://signal")  // 1. bind 먼저
    b.Connect("inproc://signal")  // 2. connect

    // 잘못된 순서 — 실패
    b.Connect("inproc://signal")  // bind가 아직 없으므로 실패
    a.Bind("inproc://signal")
    ```

### IPC 경로 길이

IPC 엔드포인트의 파일 경로는 시스템 제한(보통 108자)을 초과할 수 없다.

=== "C"

    ```c
    /* 너무 긴 경로 → ENAMETOOLONG 에러 */
    zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C++"

    ```cpp
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Java"

    ```java
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Python"

    ```python
    # 너무 긴 경로 → ENAMETOOLONG 에러
    socket.bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C#/.NET"

    ```csharp
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Rust"

    ```rust
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Go"

    ```go
    // 너무 긴 경로 → ENAMETOOLONG 에러
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

> 참고: `core/tests/test_pair_ipc.cpp` — `test_endpoint_too_long()`

### HWM 동작

피어가 없거나 느릴 때, 송신 메시지는 HWM까지 큐잉된다. HWM 초과 시 `zlink_send()`가 블록(기본) 또는 `EAGAIN` 반환(`ZLINK_DONTWAIT`).

### LINGER 설정

`zlink_close()` 호출 시 미전송 메시지가 남아 있으면 LINGER 시간만큼 대기한다. 테스트나 빠른 종료가 필요한 경우:

=== "C"

    ```c
    int linger = 0;
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(ZLINK_OPT_LINGER, 0);
    ```

=== "Java"

    ```java
    socket.setOption(ZLINK_OPT_LINGER, 0);
    ```

=== "Python"

    ```python
    socket.set_option(ZLINK_OPT_LINGER, 0)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(ZLINK_OPT_LINGER, 0);
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(ZLINK_OPT_LINGER, 0);
    ```

=== "Rust"

    ```rust
    socket.set_option(ZLINK_OPT_LINGER, 0);
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionLinger, 0)
    ```

---
[← 소켓 패턴](03-0-socket-patterns.ko.md) | [PUB/SUB →](03-2-pubsub.ko.md)
