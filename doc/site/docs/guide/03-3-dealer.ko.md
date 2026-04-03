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

### 메시지 교환

완전한 DEALER/ROUTER 예제: 컨텍스트 생성, ROUTER 서버와 DEALER 클라이언트 설정,
요청 전송, 수신, 응답, 정리까지 전체 흐름.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *router = zlink_socket(ctx, ZLINK_ROUTER);
        zlink_bind(router, "tcp://*:5558");

        void *dealer = zlink_socket(ctx, ZLINK_DEALER);
        zlink_set_routing_id(dealer, "client-1", 8);
        zlink_connect(dealer, "tcp://127.0.0.1:5558");

        /* Dealer가 요청 전송 */
        zlink_msg_t req;
        zlink_msg_init_size(&req, 4);
        memcpy(zlink_msg_data(&req), "ping", 4);
        zlink_send(dealer, &req, 1, 0);

        /* Router가 routing_id와 함께 수신 */
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(router, &source_rid, &parts, &count, 0);
        printf("Router got: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        /* Router가 dealer에게 응답 */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 4);
        memcpy(zlink_msg_data(&reply), "pong", 4);
        zlink_send_rid(router, &source_rid, &reply, 1, 0);

        /* Dealer가 응답 수신 */
        zlink_recv(dealer, &source_rid, &parts, &count, 0);
        printf("Dealer got: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

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

    int main()
    {
        zlink::context_t ctx;

        zlink::router_socket_t router(ctx);
        router.bind("tcp://*:5558");

        zlink::dealer_socket_t dealer(ctx);
        dealer.set_routing_id("client-1");
        dealer.connect("tcp://127.0.0.1:5558");

        // Dealer가 요청 전송
        dealer.send("ping");

        // Router가 routing_id와 함께 수신
        auto [source_rid, parts] = router.recv();
        std::cout << "Router got: " << parts[0].str() << std::endl;

        // Router가 dealer에게 응답
        router.send_rid(source_rid, "pong");

        // Dealer가 응답 수신
        auto [rid2, reply] = dealer.recv();
        std::cout << "Dealer got: " << reply[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class DealerRouterExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            RouterSocket router = new RouterSocket(ctx);
            router.bind("tcp://*:5558");

            DealerSocket dealer = new DealerSocket(ctx);
            dealer.setRoutingId("client-1");
            dealer.connect("tcp://127.0.0.1:5558");

            // Dealer가 요청 전송
            dealer.send("ping");

            // Router가 routing_id와 함께 수신
            Message msg = router.recv();
            System.out.println("Router got: " + msg.partAsString(0));

            // Router가 dealer에게 응답
            router.sendRid(msg.routingId(), "pong");

            // Dealer가 응답 수신
            Message reply = dealer.recv();
            System.out.println("Dealer got: " + reply.partAsString(0));

            dealer.close();
            router.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5558")

    dealer = zlink.DealerSocket(ctx)
    dealer.set_routing_id("client-1")
    dealer.connect("tcp://127.0.0.1:5558")

    # Dealer가 요청 전송
    dealer.send(b"ping")

    # Router가 routing_id와 함께 수신
    source_rid, parts = router.recv()
    print(f"Router got: {parts[0].decode()}")

    # Router가 dealer에게 응답
    router.send_rid(source_rid, b"pong")

    # Dealer가 응답 수신
    _, reply = dealer.recv()
    print(f"Dealer got: {reply[0].decode()}")

    dealer.close()
    router.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const router = new zlink.RouterSocket(ctx);
    router.bind('tcp://*:5558');

    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId('client-1');
    dealer.connect('tcp://127.0.0.1:5558');

    // Dealer가 요청 전송
    dealer.send(Buffer.from('ping'));

    // Router가 routing_id와 함께 수신
    const [sourceRid, parts] = router.receive();
    console.log(`Router got: ${parts[0].toString()}`);

    // Router가 dealer에게 응답
    router.sendRid(sourceRid, Buffer.from('pong'));

    // Dealer가 응답 수신
    const [rid2, reply] = dealer.receive();
    console.log(`Dealer got: ${reply[0].toString()}`);

    dealer.close();
    router.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5558");

    var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("client-1");
    dealer.Connect("tcp://127.0.0.1:5558");

    // Dealer가 요청 전송
    dealer.Send("ping");

    // Router가 routing_id와 함께 수신
    var (sourceRid, parts) = router.Receive();
    Console.WriteLine($"Router got: {parts[0].GetString()}");

    // Router가 dealer에게 응답
    router.SendRid(sourceRid, "pong");

    // Dealer가 응답 수신
    var (rid2, reply) = dealer.Receive();
    Console.WriteLine($"Dealer got: {reply[0].GetString()}");

    dealer.Close();
    router.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let router = ctx.router_socket();
        router.bind("tcp://*:5558")?;

        let dealer = ctx.dealer_socket();
        dealer.set_routing_id("client-1")?;
        dealer.connect("tcp://127.0.0.1:5558")?;

        // Dealer가 요청 전송
        dealer.send(b"ping")?;

        // Router가 routing_id와 함께 수신
        let (source_rid, parts) = router.recv()?;
        println!("Router got: {}",
                 String::from_utf8_lossy(parts[0].data()));

        // Router가 dealer에게 응답
        router.send_rid(&source_rid, b"pong")?;

        // Dealer가 응답 수신
        let (_, reply) = dealer.recv()?;
        println!("Dealer got: {}",
                 String::from_utf8_lossy(reply[0].data()));

        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }
        defer ctx.Close()

        router, err := ctx.RouterSocket()
        if err != nil { log.Fatal(err) }
        defer router.Close()
        router.Bind("tcp://*:5558")

        dealer, err := ctx.DealerSocket()
        if err != nil { log.Fatal(err) }
        defer dealer.Close()
        rid, _ := zlink.NewRoutingID([]byte("client-1"))
        dealer.SetRoutingID(rid)
        dealer.Connect("tcp://127.0.0.1:5558")

        // Dealer가 요청 전송
        dealer.Send(zlink.NewMessage([]byte("ping")))

        // Router가 routing_id와 함께 수신
        received, err := router.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Router got: %s\n", received.Parts[0].Data())

        // Router가 dealer에게 응답
        router.SendTo(received.RoutingID(), zlink.NewMessage([]byte("pong")))
        received.Close()

        // Dealer가 응답 수신
        reply, err := dealer.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Dealer got: %s\n", reply.Parts[0].Data())
        reply.Close()
    }
    ```

> HWM 도달 시 `zlink_send()`는 블록(기본) 또는 `ZLINK_DONTWAIT`로
> `EAGAIN`을 반환한다. 고급 backpressure 패턴은
> [성능 가이드](10-performance.ko.md)를 참고.

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [dealer_router_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/dealer_router_recv_sample.c) |
    | C++ | [dealer_router_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/dealer_router_recv_sample.cpp) |
    | Java | [DealerRouterRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/DealerRouterRecvSample.java) |
    | Python | [dealer_router_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/dealer_router_recv.py) |
    | Node | [dealer_router_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/dealer_router_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/DealerRouterRecv/Program.cs) |
    | Rust | [dealer_router_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/dealer_router_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/dealer_router_recv_sample/main.go) |

??? example "Full Sample Code -- Callback"

    | Language | Source |
    |----------|--------|
    | C | [dealer_router_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/dealer_router_callback_sample.c) |
    | C++ | [dealer_router_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/dealer_router_callback_sample.cpp) |
    | Java | [DealerRouterCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/DealerRouterCallbackSample.java) |
    | Python | [dealer_router_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/dealer_router_callback.py) |
    | Node | [dealer_router_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/dealer_router_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/DealerRouterCallback/Program.cs) |
    | Rust | [dealer_router_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/dealer_router_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/dealer_router_callback_sample/main.go) |

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

=== "Go"

    ```go
    // DEALER ↔ DEALER 멀티파트 전송
    dealer.SendMultipart([]zlink.Message{
        zlink.NewMessage([]byte("header")),
        zlink.NewMessage([]byte("body")),
    })
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

=== "Go"

    ```go
    // bind/connect 전에 설정
    rid, _ := zlink.NewRoutingID([]byte("D1"))
    dealer.SetRoutingID(rid)
    dealer.Connect("tcp://127.0.0.1:5558")
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

=== "Go"

    ```go
    a, _ := ctx.DealerSocket()
    a.Bind("tcp://*:5558")

    b, _ := ctx.DealerSocket()
    b.Connect("tcp://127.0.0.1:5558")

    // 양방향 자유 전송
    a.Send(zlink.NewMessage([]byte("ping")))
    b.Send(zlink.NewMessage([]byte("pong")))
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

=== "Go"

    ```go
    // 분배자
    sender, _ := ctx.DealerSocket()
    sender.Bind("tcp://*:5558")

    // 워커 3대
    w1, _ := ctx.DealerSocket()
    w1.Connect("tcp://127.0.0.1:5558")
    w2, _ := ctx.DealerSocket()
    w2.Connect("tcp://127.0.0.1:5558")
    w3, _ := ctx.DealerSocket()
    w3.Connect("tcp://127.0.0.1:5558")

    // 6개 작업 전송 → w1, w2, w3, w1, w2, w3 (round-robin)
    for i := 0; i < 6; i++ {
        sender.Send(zlink.NewMessage([]byte(fmt.Sprintf("task-%d", i))))
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

=== "Go"

    ```go
    // 피어가 없는 상태에서 전송
    err := dealer.SendDontWait(zlink.NewMessage([]byte("data")))
    if err != nil {
        // HWM 초과 또는 피어 없음 (EAGAIN)
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

=== "Go"

    ```go
    // 올바른 순서
    rid, _ := zlink.NewRoutingID([]byte("D1"))
    dealer.SetRoutingID(rid)
    dealer.Connect(endpoint)  // D1으로 식별
    ```

---
[← PUB/SUB](03-2-pubsub.ko.md) | [ROUTER →](03-4-router.ko.md)
