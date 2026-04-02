
# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체로, I/O thread pool과 socket을 관리한다.

=== "C"

    ```c
    /* 생성 */
    void *ctx = zlink_ctx_new();

    /* 설정 — 다중 연결 서버에서는 I/O thread를 늘린다 */
    zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* 기본 1; 연결이 많으면 4가 최적 */

    /* 조회 */
    int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS);

    /* 종료 */
    zlink_ctx_term(ctx);  /* 모든 socket이 닫힌 후 반환 */
    ```

=== "C++"

    ```cpp
    // 생성
    zlink::context_t ctx;
    ctx.set_io_threads(4);       // 기본 1; 연결이 많으면 4가 최적
    int io_threads = ctx.io_threads();

    // 종료 — RAII 또는 명시적
    ctx.close();
    ```

=== "Java"

    ```java
    // 생성
    Context ctx = new Context();
    ctx.setIoThreads(4);         // 기본 1; 연결이 많으면 4가 최적
    int ioThreads = ctx.getIoThreads();

    // 종료
    ctx.close();  // 또는 try-with-resources
    ```

=== "Python"

    ```python
    # 생성
    ctx = zlink.Context()
    ctx.set_io_threads(4)        # 기본 1; 연결이 많으면 4가 최적
    io_threads = ctx.io_threads

    # 종료
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    // 생성
    const ctx = new zlink.Context();
    ctx.setIoThreads(4);         // 기본 1; 연결이 많으면 4가 최적
    const ioThreads = ctx.ioThreads;

    // 종료
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    // 생성
    using var ctx = new Context();
    ctx.IoThreads = 4;           // 기본 1; 연결이 많으면 4가 최적
    int ioThreads = ctx.IoThreads;

    // 종료 — Dispose 패턴
    ```

=== "Rust"

    ```rust
    // 생성
    let ctx = zlink::Context::new()?;
    ctx.set_io_threads(4)?;      // 기본 1; 연결이 많으면 4가 최적
    let io_threads = ctx.io_threads()?;

    // 종료 — Drop trait
    ```

=== "Go"

    ```go
    // 생성
    ctx := zlink.NewContext()
    ctx.SetIoThreads(4) // 기본 1; 연결이 많으면 4가 최적
    ioThreads := ctx.IoThreads()

    // 종료 — Drop trait
    ```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | 1 | I/O thread 수 |
| `ZLINK_MAX_SOCKETS` | 4095 | 최대 socket 수 |
| `ZLINK_MAX_MSGSZ` | -1 | 최대 message 크기 (-1: 무제한) |

## 2. Socket API

공개 socket handle API는 기본적으로 thread-safe다. 여러 thread에서
같은 socket handle을 공유하여 send/recv/bind/connect 등을 호출할 수 있다.

> 세부 threading 규칙은 [Thread Safety 가이드](11-thread-safety.ko.md)를 참고.

### 2.1 Socket 생성 및 닫기

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_DEALER);
    /* ... 사용 ... */
    zlink_close(socket);
    ```

=== "C++"

    ```cpp
    zlink::dealer_socket_t socket(ctx);
    // ... 사용 ...
    socket.close();  // 또는 RAII
    ```

=== "Java"

    ```java
    DealerSocket socket = new DealerSocket(ctx);
    // ... 사용 ...
    socket.close();
    ```

=== "Python"

    ```python
    socket = zlink.DealerSocket(ctx)
    # ... 사용 ...
    socket.close()
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.DealerSocket(ctx);
    // ... 사용 ...
    socket.close();
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new DealerSocket(ctx);
    // ... 사용 ...
    ```

=== "Rust"

    ```rust
    let socket = ctx.dealer_socket()?;
    // ... 사용 ...
    // Drop trait이 close 처리
    ```

=== "Go"

    ```go
    socket := ctx.DealerSocket()
    // ... 사용 ...
    // Drop trait이 close 처리
    ```

### 2.2 Socket Type 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_PAIR` | 0x1001 | 1:1 bidirectional pair 소켓 |
| `ZLINK_PUB` | 0x1002 | Publisher 소켓 |
| `ZLINK_SUB` | 0x1003 | Subscriber 소켓 |
| `ZLINK_DEALER` | 0x1004 | Async dealer 소켓 |
| `ZLINK_ROUTER` | 0x1005 | Router 소켓 |
| `ZLINK_XPUB` | 0x1006 | Extended publisher 소켓 |
| `ZLINK_XSUB` | 0x1007 | Extended subscriber 소켓 |
| `ZLINK_STREAM` | 0x1008 | Raw 소켓 |

### 2.3 연결 관리

=== "C"

    ```c
    /* Bind (server) */
    zlink_bind(socket, "tcp://*:5555");

    /* Connect (client) */
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    /* Unbind / Disconnect */
    zlink_unbind(socket, "tcp://*:5555");
    zlink_disconnect(socket, "tcp://127.0.0.1:5555");
    ```

=== "C++"

    ```cpp
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "Java"

    ```java
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "Python"

    ```python
    socket.bind("tcp://*:5555")
    socket.connect("tcp://127.0.0.1:5555")
    socket.unbind("tcp://*:5555")
    socket.disconnect("tcp://127.0.0.1:5555")
    ```

=== "Node/TypeScript"

    ```typescript
    socket.bind("tcp://*:5555");
    socket.connect("tcp://127.0.0.1:5555");
    socket.unbind("tcp://*:5555");
    socket.disconnect("tcp://127.0.0.1:5555");
    ```

=== "C#/.NET"

    ```csharp
    socket.Bind("tcp://*:5555");
    socket.Connect("tcp://127.0.0.1:5555");
    socket.Unbind("tcp://*:5555");
    socket.Disconnect("tcp://127.0.0.1:5555");
    ```

=== "Rust"

    ```rust
    socket.bind("tcp://*:5555")?;
    socket.connect("tcp://127.0.0.1:5555")?;
    socket.unbind("tcp://*:5555")?;
    socket.disconnect("tcp://127.0.0.1:5555")?;
    ```

=== "Go"

    ```go
    socket.Bind("tcp://*:5555")
    socket.Connect("tcp://127.0.0.1:5555")
    socket.Unbind("tcp://*:5555")
    socket.Disconnect("tcp://127.0.0.1:5555")
    ```

### 2.4 Socket Option

=== "C"

    ```c
    /* 옵션 설정 */
    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

    /* 옵션 조회 */
    int value;
    size_t len = sizeof(value);
    zlink_get_option(socket, ZLINK_OPT_SNDHWM, &value, &len);
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::opt::sndhwm, 5000);
    int value = socket.get_option<int>(zlink::opt::sndhwm);
    ```

=== "Java"

    ```java
    socket.setSndHwm(5000);
    int value = socket.getSndHwm();
    ```

=== "Python"

    ```python
    socket.set_option(zlink.OPT_SNDHWM, 5000)
    value = socket.get_option(zlink.OPT_SNDHWM)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(zlink.OPT_SNDHWM, 5000);
    const value = socket.getOption(zlink.OPT_SNDHWM);
    ```

=== "C#/.NET"

    ```csharp
    socket.SndHwm = 5000;
    int value = socket.SndHwm;
    ```

=== "Rust"

    ```rust
    socket.set_sndhwm(5000)?;
    let value = socket.sndhwm()?;
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionSndHwm, 5000)
    value := socket.GetOption(zlink.OptionSndHwm)
    ```

주요 옵션:

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Send High Water Mark |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Recv High Water Mark |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms, -1: 무제한) |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Recv timeout (ms, -1: 무제한) |
| `ZLINK_OPT_LINGER` | int | -1 | Socket close 시 linger (ms) |

Routing ID는 전용 함수로 설정/조회한다:
`zlink_set_routing_id()` / `zlink_get_routing_id()`.
구독 관리는 `zlink_set_subscription()`을 사용한다.

`ZLINK_OPT_EVENTS`, `ZLINK_OPT_LAST_ENDPOINT` 같은 option은
runtime 중간에도 보통 사용한다. 반면 HWM, timeout, TLS 같은 대부분의
tuning option은 초기 설정 단계에서 사용한다.

### 2.5 Option 소유권 카테고리

내부적으로 option은 세 카테고리로 분류되어 각 도메인 소유자가 validation/apply를
담당한다. 공개 API surface(`zlink_set_option` / `zlink_get_option`)는 변경 없이
유지되지만, 새 option 추가 시 아래 기준으로 소유권을 결정한다.

| 카테고리 | 대표 option | 설명 |
|----------|-------------|------|
| **Core Socket** | `SNDHWM`, `RCVHWM`, `LINGER`, `ROUTING_ID` 등 | 소켓 핵심 동작 |
| **Transport/Network** | `SNDBUF`, `RCVBUF`, `TOS`, `MULTICAST_*` 등 | 네트워크/transport 계층 정책 |
| **Protocol/Metadata** | ZMP 프로토콜 메타데이터 관련 | 프로토콜 수준 메타데이터 |

이 분류는 transport option 변경이 socket/service 코드에 영향을 주지 않게 하고,
option 하나를 수정할 때 어떤 모듈이 owner인지 바로 파악할 수 있게 한다.

## 3. Message Send/Recv

### 3.1 Send

=== "C"

    ```c
    /* Simple send */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "Hello", 5);
    zlink_send(socket, &part, 1, 0);

    /* Multipart send */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 6);
    memcpy(zlink_msg_data(&parts[0]), "header", 6);
    zlink_msg_init_size(&parts[1], 4);
    memcpy(zlink_msg_data(&parts[1]), "body", 4);
    zlink_send(socket, parts, 2, 0);
    ```

=== "C++"

    ```cpp
    // Simple send
    socket.send(zlink::message_t("Hello", 5));

    // Multipart send
    std::vector<zlink::message_t> parts;
    parts.emplace_back("header", 6);
    parts.emplace_back("body", 4);
    socket.send(parts);
    ```

=== "Java"

    ```java
    // Simple send
    socket.send(new Message("Hello".getBytes()));

    // Multipart send
    socket.send(new Message("header".getBytes()),
                new Message("body".getBytes()));
    ```

=== "Python"

    ```python
    # Simple send
    socket.send(b"Hello")

    # Multipart send
    socket.send_multipart([b"header", b"body"])
    ```

=== "Node/TypeScript"

    ```typescript
    // Simple send
    socket.send(Buffer.from("Hello"));

    // Multipart send
    socket.send([Buffer.from("header"), Buffer.from("body")]);
    ```

=== "C#/.NET"

    ```csharp
    // Simple send
    socket.Send(new Message("Hello"u8));

    // Multipart send
    socket.Send(new Message("header"u8), new Message("body"u8));
    ```

=== "Rust"

    ```rust
    // Simple send
    socket.send(&zlink::Message::from("Hello"))?;

    // Multipart send
    socket.send_multipart(&[
        zlink::Message::from("header"),
        zlink::Message::from("body"),
    ])?;
    ```

=== "Go"

    ```go
    // Simple send
    socket.Send(zlink.NewMessage([]byte("Hello")))

    // Multipart send
    socket.SendMultipart([]zlink.Message{zlink.NewMessage([]byte("header")), zlink.NewMessage([]byte("body"))})
    ```

기본적으로 `zlink_send()`는 send queue가 가득 차면(HWM 도달) blocking한다.
`ZLINK_DONTWAIT` flag를 사용하면 blocking 대신 즉시 `EAGAIN`을 반환한다.
고급 backpressure pattern은
[Performance 가이드](10-performance.ko.md)를 참고.

#### Logical Multipart Send

`zlink_send()`, `zlink_publish()` 등 공개/서비스 surface의
멀티파트 송신은 내부적으로 공통 **logical multipart send** 모듈을 사용한다.
이 모듈은 다음 의미를 공통으로 보장한다.

- **nonblocking**: one-shot 시도 후 실패 시 partial local state rollback
- **blocking**: `sndtimeo` deadline까지 whole-message 단위 재시도
- **재시도 대상**: `EAGAIN`, `EINTR`만 재시도, 그 외 오류는 즉시 실패
- **whole-message 보장**: 멀티파트 메시지는 전체가 성공하거나 전체가 실패한다

이 설계는 `libzmq`의 `pipe/router/xpub/dist` lower layer가 제공하는
complete-message 기준 accounting과 rollback 메커니즘을 기반으로 한다.

### 3.2 Recv

zlink socket은 두 가지 recv mode를 지원한다:

#### Pull Mode (synchronous)

Handler를 부착하지 않으면 `zlink_recv()`로 직접 message를 받을 수 있다.
Socket은 기본적으로 pull mode로 시작한다.

=== "C"

    ```c
    void *socket = zlink_socket(ctx, ZLINK_PAIR);
    zlink_bind(socket, "tcp://*:5556");

    /* Blocking recv */
    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    int rc = zlink_recv(socket, &source_rid, &parts, &part_count, 0);
    if (rc == 0) {
        for (size_t i = 0; i < part_count; i++) {
            printf("frame %zu: %.*s\n", i,
                   (int)zlink_msg_size(&parts[i]),
                   (char *)zlink_msg_data(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
        free(parts);
    }

    /* Non-blocking recv */
    rc = zlink_recv(socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        /* 현재 사용 가능한 message 없음 */
    }
    ```

=== "C++"

    ```cpp
    zlink::pair_socket_t socket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    auto [source_rid, parts] = socket.recv();
    for (size_t i = 0; i < parts.size(); i++)
        std::cout << "frame " << i << ": " << parts[i].to_string() << "\n";

    // Non-blocking recv
    auto result = socket.recv(zlink::dontwait);
    if (!result)  // EAGAIN
        ;
    ```

=== "Java"

    ```java
    PairSocket socket = new PairSocket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    RecvResult result = socket.recv();
    for (int i = 0; i < result.parts().length; i++)
        System.out.println("frame " + i + ": "
            + new String(result.parts()[i].data()));

    // Non-blocking recv
    RecvResult r = socket.recv(DONTWAIT);
    if (r == null)  // EAGAIN
        ;
    ```

=== "Python"

    ```python
    socket = zlink.PairSocket(ctx)
    socket.bind("tcp://*:5556")

    # Blocking recv
    source_rid, parts = socket.recv()
    for i, part in enumerate(parts):
        print(f"frame {i}: {part.data().decode()}")

    # Non-blocking recv
    try:
        source_rid, parts = socket.recv(dontwait=True)
    except zlink.Again:
        pass  # 현재 사용 가능한 message 없음
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.PairSocket(ctx);
    socket.bind("tcp://*:5556");

    // Blocking recv
    const { sourceRid, parts } = socket.recv();
    parts.forEach((part, i) =>
        console.log(`frame ${i}: ${part.data().toString()}`));

    // Non-blocking recv
    const result = socket.recv({ dontwait: true });
    if (!result)  // EAGAIN
        ;
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new PairSocket(ctx);
    socket.Bind("tcp://*:5556");

    // Blocking recv
    var (sourceRid, parts) = socket.Recv();
    for (int i = 0; i < parts.Length; i++)
        Console.WriteLine($"frame {i}: {parts[i].DataString()}");

    // Non-blocking recv
    var result = socket.TryRecv();
    if (result == null)  // EAGAIN
        ;
    ```

=== "Rust"

    ```rust
    let socket = ctx.pair_socket()?;
    socket.bind("tcp://*:5556")?;

    // Blocking recv
    let (source_rid, parts) = socket.recv()?;
    for (i, part) in parts.iter().enumerate() {
        println!("frame {}: {}", i, part.as_str()?);
    }

    // Non-blocking recv
    match socket.recv_dontwait() {
        Ok((rid, parts)) => { /* 처리 */ }
        Err(e) if e.kind() == zlink::ErrorKind::Again => {}
        Err(e) => return Err(e),
    }
    ```

=== "Go"

    ```go
    socket := ctx.PairSocket()
    socket.Bind("tcp://*:5556")

    // Blocking recv
    source_rid, parts, _ := socket.Recv()
    for (i, part) in parts.iter().enumerate() {
        fmt.Printf("frame {}: %v\n", i, part.as_str()?)
    }

    // Non-blocking recv
    received, err := socket.RecvDontWait()
        Ok((rid, parts)) => { /* 처리 */ }
        Err(e) if e.kind() == zlink::ErrorKind::Again => {}
        Err(e) => return Err(e),
    }
    ```

#### Callback Mode

Socket 생성 후 handler callback을 부착하면 message 도착 시 I/O thread에서
비동기로 호출된다. 한번 부착하면 socket 수명 동안 해제할 수 없다.
Handler가 부착된 상태에서 `zlink_recv()` 호출 시 `EBUSY`를 반환한다.

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        for (size_t i = 0; i < part_count; i++) {
            printf("frame %zu: %.*s\n", i,
                   (int)zlink_msg_size(&parts[i]),
                   (char *)zlink_msg_data(&parts[i]));
            zlink_msg_close(&parts[i]);
        }
    }

    void *socket = zlink_socket(ctx, ZLINK_STREAM);
    zlink_recv_handler(socket, on_message, NULL);
    ```

=== "C++"

    ```cpp
    zlink::stream_socket_t socket(ctx);
    socket.on_message([](const zlink::routing_id_t& rid,
                         std::span<zlink::message_t> parts) {
        for (size_t i = 0; i < parts.size(); i++)
            std::cout << "frame " << i << ": "
                      << parts[i].to_string() << "\n";
    });
    ```

=== "Java"

    ```java
    StreamSocket socket = new StreamSocket(ctx);
    socket.onMessage((rid, parts) -> {
        for (int i = 0; i < parts.length; i++)
            System.out.println("frame " + i + ": "
                + new String(parts[i].data()));
    });
    ```

=== "Python"

    ```python
    socket = zlink.StreamSocket(ctx)

    def on_message(rid, parts):
        for i, part in enumerate(parts):
            print(f"frame {i}: {part.data().decode()}")

    socket.on_message(on_message)
    ```

=== "Node/TypeScript"

    ```typescript
    const socket = new zlink.StreamSocket(ctx);
    socket.onMessage((rid, parts) => {
        parts.forEach((part, i) =>
            console.log(`frame ${i}: ${part.data().toString()}`));
    });
    ```

=== "C#/.NET"

    ```csharp
    using var socket = new StreamSocket(ctx);
    socket.OnMessage((rid, parts) => {
        for (int i = 0; i < parts.Length; i++)
            Console.WriteLine($"frame {i}: {parts[i].DataString()}");
    });
    ```

=== "Rust"

    ```rust
    let socket = ctx.stream_socket()?;
    socket.on_message(|rid, parts| {
        for (i, part) in parts.iter().enumerate() {
            println!("frame {}: {}", i, part.as_str()?);
        }
        Ok(())
    })?;
    ```

=== "Go"

    ```go
    socket := ctx.StreamSocket()
    socket.on_message(|rid, parts| {
        for (i, part) in parts.iter().enumerate() {
            fmt.Printf("frame {}: %v\n", i, part.as_str()?)
        }

    })
    ```

> 두 mode의 비교와 고급 pattern은
> [Performance 가이드](10-performance.ko.md)를 참고.

### 3.3 Send Flag

| Flag | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | Non-blocking mode (send/recv 불가 시 즉시 EAGAIN 반환) |

## 4. Handler Type

각 socket type은 전용 등록 함수를 사용한다:

| Socket Type | 등록 호출 | Callback Signature |
|---|---|---|
| STREAM | `zlink_recv_handler()` | `fn(rid, parts, count, userdata)` |
| spot, spot_node | `zlink_subscribe_handler()` | `fn(rid, topic, topic_len, parts, count, userdata)` |
| PUB | N/A | Send-only socket |

상세 시그니처:

- **STREAM**: `void fn(const zlink_routing_id_t *rid, zlink_msg_t *parts, size_t count, void *userdata)`
- **spot/spot_node**: `void fn(const zlink_routing_id_t *rid, const char *topic, size_t topic_len, zlink_msg_t *parts, size_t count, void *userdata)`

Callback은 I/O thread에서 호출된다. Callback 내부에서 blocking 작업을 피해야 한다.
느린 처리가 필요하면 user queue에 넣고 별도 thread에서 처리한다.

## 5. Error 처리

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    int rc = zlink_send(socket, &part, 1, 0);
    if (rc == -1) {
        int err = zlink_errno();
        printf("error: %s\n", zlink_strerror(err));
    }
    ```

=== "C++"

    ```cpp
    try {
        socket.send(zlink::message_t(data, size));
    } catch (const zlink::error_t& e) {
        std::cerr << "error: " << e.what() << "\n";
    }
    ```

=== "Java"

    ```java
    try {
        socket.send(new Message(data));
    } catch (ZlinkException e) {
        System.err.println("error: " + e.getMessage());
    }
    ```

=== "Python"

    ```python
    try:
        socket.send(data)
    except zlink.ZlinkError as e:
        print(f"error: {e}")
    ```

=== "Node/TypeScript"

    ```typescript
    try {
        socket.send(data);
    } catch (e) {
        console.error(`error: ${e}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    try {
        socket.Send(new Message(data));
    } catch (ZlinkException e) {
        Console.Error.WriteLine($"error: {e.Message}");
    }
    ```

=== "Rust"

    ```rust
    match socket.send(&zlink::Message::from(data)) {
        Ok(()) => {}
        Err(e) => eprintln!("error: {}", e),
    }
    ```

=== "Go"

    ```go
    err := socket.Send(zlink.NewMessage(data))
         => {}
        Err(e) => eprintln!("error: {}", e),
    }
    ```

주요 error code:

| Error | 값 | 설명 |
|-------|-----|------|
| `EAGAIN` | POSIX | Non-blocking mode에서 즉시 완료 불가 |
| `EINTR` | POSIX | Signal에 의해 interrupt됨 |
| `ENOTSOCK` | `HAUSNUMERO + 9` | 유효하지 않은 socket |
| `EHOSTUNREACH` | `HAUSNUMERO + 17` | Host 도달 불가 |
| `EFSM` | `HAUSNUMERO + 51` | 현재 state에서 허용되지 않는 연산 |
| `ETERM` | `HAUSNUMERO + 53` | Context terminated |

> `ZLINK_HAUSNUMERO` = 156384712. POSIX errno와 충돌하지 않는 zlink 전용 base 값이다.

## 6. DEALER/ROUTER 예제

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_router_message(const zlink_routing_id_t *source_rid,
                           zlink_msg_t *parts, size_t part_count,
                           void *userdata)
    {
        printf("[%.*s] recv: %.*s\n",
               (int)source_rid->size, source_rid->data,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    void on_dealer_message(const zlink_routing_id_t *source_rid,
                           zlink_msg_t *parts, size_t part_count,
                           void *userdata)
    {
        printf("reply: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    int main(void) {
        void *ctx = zlink_ctx_new();

        /* ROUTER (server) */
        void *router = zlink_socket(ctx, ZLINK_ROUTER);
        /* zlink_recv()로 수신 */
        zlink_bind(router, "tcp://*:5555");

        /* DEALER (client) */
        void *dealer = zlink_socket(ctx, ZLINK_DEALER);
        /* zlink_recv()로 수신 */
        zlink_connect(dealer, "tcp://127.0.0.1:5555");

        /* DEALER → ROUTER */
        zlink_msg_t req;
        zlink_msg_init_size(&req, 7);
        memcpy(zlink_msg_data(&req), "request", 7);
        zlink_send(dealer, &req, 1, 0);

        /* Handler callback이 비동기로 message를 처리 */
        msleep(100);

        zlink_close(dealer);
        zlink_close(router);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>

    int main() {
        zlink::context_t ctx;

        // ROUTER (server)
        zlink::router_socket_t router(ctx);
        router.bind("tcp://*:5555");

        // DEALER (client)
        zlink::dealer_socket_t dealer(ctx);
        dealer.connect("tcp://127.0.0.1:5555");

        // DEALER → ROUTER
        dealer.send(zlink::message_t("request", 7));

        // 수신 및 출력
        auto [rid, parts] = router.recv();
        std::cout << "[" << rid.to_string()
                  << "] recv: " << parts[0].to_string() << "\n";

        dealer.close();
        router.close();
        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class DealerRouterExample {
        public static void main(String[] args) throws Exception {
            try (Context ctx = new Context()) {
                RouterSocket router = new RouterSocket(ctx);
                router.bind("tcp://*:5555");

                DealerSocket dealer = new DealerSocket(ctx);
                dealer.connect("tcp://127.0.0.1:5555");

                dealer.send(new Message("request".getBytes()));

                RecvResult result = router.recv();
                System.out.println("[" + result.routingId() + "] recv: "
                    + new String(result.parts()[0].data()));

                dealer.close();
                router.close();
            }
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5555")

    dealer = zlink.DealerSocket(ctx)
    dealer.connect("tcp://127.0.0.1:5555")

    dealer.send(b"request")

    rid, parts = router.recv()
    print(f"[{rid}] recv: {parts[0].data().decode()}")

    dealer.close()
    router.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from "zlink";

    const ctx = new zlink.Context();

    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://*:5555");

    const dealer = new zlink.DealerSocket(ctx);
    dealer.connect("tcp://127.0.0.1:5555");

    dealer.send(Buffer.from("request"));

    const { sourceRid, parts } = router.recv();
    console.log(`[${sourceRid}] recv: ${parts[0].data().toString()}`);

    dealer.close();
    router.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5555");

    using var dealer = new DealerSocket(ctx);
    dealer.Connect("tcp://127.0.0.1:5555");

    dealer.Send(new Message("request"u8));

    var (rid, parts) = router.Recv();
    Console.WriteLine($"[{rid}] recv: {parts[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::{Context, SocketType};

    fn main() -> zlink::Result<()> {
        let ctx = Context::new()?;

        let router = ctx.router_socket()?;
        router.bind("tcp://*:5555")?;

        let dealer = ctx.dealer_socket()?;
        dealer.connect("tcp://127.0.0.1:5555")?;

        dealer.send(&zlink::Message::from("request"))?;

        let (rid, parts) = router.recv()?;
        println!("[{}] recv: {}", rid, parts[0].as_str()?);

        Ok(())
    }
    ```

=== "Go"

    ```go
    func main() {
        ctx := zlink.NewContext()

        router := ctx.RouterSocket()
        router.Bind("tcp://*:5555")

        dealer := ctx.DealerSocket()
        dealer.Connect("tcp://127.0.0.1:5555")

        dealer.Send(zlink.NewMessage([]byte("request")))

        rid, parts, _ := router.Recv()
        fmt.Printf("[{}] recv: %v\n", rid, parts[0].as_str()?)


    }
    ```

---
[← 개요](01-overview.ko.md) | [Socket Pattern →](03-0-socket-patterns.ko.md)
