
# ROUTER 소켓

## 1. 개요

ROUTER 소켓은 **routing_id 기반 라우팅** 소켓이다.
수신 메시지에 routing_id 프레임을 자동으로 추가하고,
송신 시 첫 번째 프레임의 routing_id로 대상 피어를 지정한다.

**핵심 특성:**
- 수신 시 routing_id 프레임 자동 추가 (메시지 출처 식별)
- 송신 시 첫 프레임으로 대상 피어 지정 (특정 클라이언트에게 응답)
- 다중 피어 관리 가능 (서버/브로커 역할)

**유효한 소켓 조합:** ROUTER ↔ DEALER, ROUTER ↔ ROUTER

```
ROUTER ↔ ROUTER: N개 노드가 routing_id로 상대를 지정 (메시/클러스터)

  ┌──────────┐            ┌──────────┐
  │ ROUTER A │◄──────────►│ ROUTER B │
  │   (RA)   │            │   (RB)   │
  └────┬──▲──┘            └──▲──┬────┘
       │  │                  │  │
       │  │  ┌──────────┐   │  │
       │  └──┤ ROUTER C ├───┘  │
       └────►│   (RC)   │◄────┘
             └──────────┘

  A→RC, B→RA, C→RB … routing_id로 대상 지정
```

```
DEALER → ROUTER: round-robin 부하분산 + routing_id 응답 라우팅

  각 DEALER가 연결된 ROUTER에 round-robin 분배:

                                    ┌──────────┐
                        ┌── send ──►│ ROUTER A │
                        │           └──────────┘
                        │
  ┌──────────┐          │           ┌──────────┐
  │ DEALER 1 │──────────┼── send ──►│ ROUTER B │
  │   (D1)   │          │           └──────────┘
  └──────────┘          │
                        │           ┌──────────┐
                        └── send ──►│ ROUTER C │
                                    └──────────┘

  ┌──────────┐
  │ DEALER 2 │──────── (동일하게 A, B, C에 round-robin)
  │   (D2)   │
  └──────────┘


  ROUTER는 source_rid로 요청한 DEALER를 식별하여 응답:

  ┌──────────┐    ① send         ┌──────────┐
  │ DEALER 1 │──────────────────►│ ROUTER A │
  │   (D1)   │◄──────────────────┤          │
  └──────────┘    ② send_rid     └──────────┘
                   source_rid="D1"

  ┌──────────┐    ① send         ┌──────────┐
  │ DEALER 2 │──────────────────►│ ROUTER B │
  │   (D2)   │◄──────────────────┤          │
  └──────────┘    ② send_rid     └──────────┘
                   source_rid="D2"
```

## 2. 기본 사용법

### 생성 및 바인드

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(router, "tcp://*:5558");
    ```

=== "C++"

    ```cpp
    zlink::context_t ctx;
    zlink::router_socket_t router(ctx);
    router.bind("tcp://*:5558");
    ```

=== "Java"

    ```java
    Context ctx = new Context();
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://*:5558");
    ```

=== "Python"

    ```python
    ctx = zlink.Context()
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5558")
    ```

=== "Node/TypeScript"

    ```typescript
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://*:5558");
    ```

=== "C#/.NET"

    ```csharp
    using var ctx = new Context();
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5558");
    ```

=== "Rust"

    ```rust
    let ctx = zlink::Context::new()?;
    let router = ctx.router_socket()?;
    router.bind("tcp://*:5558")?;
    ```

=== "Go"

    ```go
    ctx, err := zlink.NewContext()
    if err != nil { panic(err) }
    router, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    router.Bind("tcp://*:5558")
    ```

### 메시지 교환

DEALER-ROUTER 전체 예제: DEALER가 연결 후 요청을 전송하고,
ROUTER가 `source_rid`로 송신자를 식별하여 `zlink_send_rid()`로 응답한다.

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
        zlink_set_routing_id(dealer, "D1", 2);
        zlink_connect(dealer, "tcp://127.0.0.1:5558");

        /* DEALER가 요청 전송 */
        zlink_msg_t req;
        zlink_msg_init_size(&req, 5);
        memcpy(zlink_msg_data(&req), "Hello", 5);
        zlink_send(dealer, &req, 1, 0);

        /* ROUTER 수신 -- source_rid로 송신자 식별 */
        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        zlink_recv(router, &source_rid, &parts, &part_count, 0);
        printf("[%.*s]로부터: %.*s\n",
               (int)source_rid.size, source_rid.data,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, part_count);

        /* ROUTER가 송신자에게 응답 */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "World", 5);
        zlink_send_rid(router, &source_rid, &reply, 1, 0);

        /* DEALER가 응답 수신 */
        zlink_recv(dealer, &source_rid, &parts, &part_count, 0);
        printf("Reply: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, part_count);

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
        dealer.set_routing_id("D1");
        dealer.connect("tcp://127.0.0.1:5558");

        // DEALER가 요청 전송
        dealer.send(zlink::message_t("Hello", 5));

        // ROUTER 수신 -- source_rid로 송신자 식별
        auto [source_rid, parts] = router.recv();
        std::cout << "[" << source_rid.to_string() << "]로부터: "
                  << parts[0].to_string() << std::endl;

        // ROUTER가 송신자에게 응답
        router.send_rid(source_rid, zlink::message_t("World", 5));

        // DEALER가 응답 수신
        auto [rid2, reply] = dealer.recv();
        std::cout << "Reply: " << reply[0].to_string() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class RouterExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            RouterSocket router = new RouterSocket(ctx);
            router.bind("tcp://*:5558");

            DealerSocket dealer = new DealerSocket(ctx);
            dealer.setRoutingId("D1");
            dealer.connect("tcp://127.0.0.1:5558");

            // DEALER가 요청 전송
            dealer.send(new Message("Hello".getBytes()));

            // ROUTER 수신 -- sourceRid로 송신자 식별
            RecvResult result = router.recv();
            System.out.println("[" + result.routingId() + "]로부터: "
                + new String(result.parts()[0].data()));

            // ROUTER가 송신자에게 응답
            router.sendRid(result.routingId(), new Message("World".getBytes()));

            // DEALER가 응답 수신
            Message reply = dealer.recv();
            System.out.println("Reply: " + reply.partAsString(0));

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
    dealer.set_routing_id(b"D1")
    dealer.connect("tcp://127.0.0.1:5558")

    # DEALER가 요청 전송
    dealer.send(b"Hello")

    # ROUTER 수신 -- source_rid로 송신자 식별
    source_rid, parts = router.recv()
    print(f"[{source_rid}]로부터: {parts[0].data().decode()}")

    # ROUTER가 송신자에게 응답
    router.send_rid(source_rid, b"World")

    # DEALER가 응답 수신
    _, reply = dealer.recv()
    print(f"Reply: {reply[0].decode()}")

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
    dealer.setRoutingId(Buffer.from('D1'));
    dealer.connect('tcp://127.0.0.1:5558');

    // DEALER가 요청 전송
    dealer.send(Buffer.from('Hello'));

    // ROUTER 수신 -- sourceRid로 송신자 식별
    const { sourceRid, parts } = router.recv();
    console.log(`[${sourceRid}]로부터: ${parts[0].toString()}`);

    // ROUTER가 송신자에게 응답
    router.sendRid(sourceRid, Buffer.from('World'));

    // DEALER가 응답 수신
    const reply = dealer.recv();
    console.log(`Reply: ${reply.parts[0].toString()}`);

    dealer.close();
    router.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5558");

    using var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("D1"u8);
    dealer.Connect("tcp://127.0.0.1:5558");

    // DEALER가 요청 전송
    dealer.Send(new Message("Hello"u8));

    // ROUTER 수신 -- sourceRid로 송신자 식별
    var (sourceRid, parts) = router.Recv();
    Console.WriteLine($"[{sourceRid}]로부터: {parts[0].DataString()}");

    // ROUTER가 송신자에게 응답
    router.SendRid(sourceRid, new Message("World"u8));

    // DEALER가 응답 수신
    var (_, reply) = dealer.Recv();
    Console.WriteLine($"Reply: {reply[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new()?;

        let router = ctx.router_socket()?;
        router.bind("tcp://*:5558")?;

        let dealer = ctx.dealer_socket()?;
        dealer.set_routing_id("D1")?;
        dealer.connect("tcp://127.0.0.1:5558")?;

        // DEALER가 요청 전송
        dealer.send(&zlink::Message::from("Hello"))?;

        // ROUTER 수신 -- source_rid로 송신자 식별
        let (source_rid, parts) = router.recv()?;
        println!("[{}]로부터: {}", source_rid, parts[0].as_str()?);

        // ROUTER가 송신자에게 응답
        router.send_rid(&source_rid, &zlink::Message::from("World"))?;

        // DEALER가 응답 수신
        let (_, reply) = dealer.recv()?;
        println!("Reply: {}", reply[0].as_str()?);

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

        router, _ := ctx.RouterSocket()
        defer router.Close()
        router.Bind("tcp://*:5558")

        dealer, _ := ctx.DealerSocket()
        defer dealer.Close()
        dealer.SetRoutingId("D1")
        dealer.Connect("tcp://127.0.0.1:5558")

        // DEALER가 요청 전송
        dealer.Send(zlink.NewMessage([]byte("Hello")))

        // ROUTER 수신 -- source_rid로 송신자 식별
        sourceRid, parts, _ := router.Recv()
        fmt.Printf("[%v]로부터: %s\n", sourceRid, string(parts[0].Data()))

        // ROUTER가 송신자에게 응답
        router.SendTo(sourceRid, zlink.NewMessage([]byte("World")))

        // DEALER가 응답 수신
        _, reply, _ := dealer.Recv()
        fmt.Printf("Reply: %s\n", string(reply[0].Data()))
    }
    ```

> 피어별 송신 큐가 가득 차면(HWM) `ROUTER_MANDATORY` 활성 시
> `EHOSTUNREACH`를 반환하고, 그렇지 않으면 메시지를 조용히 드롭한다.
> 고급 backpressure 패턴은 [성능 가이드](10-performance.ko.md)를 참고.

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

### 콜백 모드

ROUTER가 `zlink_recv_handler()`로 수신 메시지를 콜백으로 처리한다.
DEALER는 요청을 전송하고 블로킹 `zlink_recv()`로 응답을 수신한다.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_request(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t count, void *userdata)
    {
        printf("Router callback: %.*s from peer\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));

        /* source routing id를 사용하여 응답 */
        void *router = (void *)userdata;
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 4);
        memcpy(zlink_msg_data(&reply), "pong", 4);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        zlink_multipart_close(parts, count);
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();
        void *router = zlink_socket(ctx, ZLINK_ROUTER);
        void *dealer = zlink_socket(ctx, ZLINK_DEALER);

        zlink_bind(router, "tcp://*:5557");
        zlink_set_routing_id(dealer, "CLIENT", 6);
        zlink_connect(dealer, "tcp://127.0.0.1:5557");

        /* Router가 콜백으로 수신 및 응답 */
        zlink_recv_handler(router, on_request, router);

        /* Dealer가 요청 전송 */
        zlink_msg_t req;
        zlink_msg_init_size(&req, 4);
        memcpy(zlink_msg_data(&req), "ping", 4);
        zlink_send(dealer, &req, 1, 0);

        /* Dealer가 응답 수신 (블로킹 recv) */
        zlink_routing_id_t src;
        zlink_msg_t *parts;
        size_t cnt;
        zlink_recv(dealer, &src, &parts, &cnt, 0);
        printf("Reply: %.*s\n", (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, cnt);

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
        router.bind("tcp://*:5557");

        zlink::dealer_socket_t dealer(ctx);
        dealer.set_routing_id("CLIENT");
        dealer.connect("tcp://127.0.0.1:5557");

        // Router가 콜백으로 수신 및 응답
        router.on_receive([&](const zlink::routing_id_t& source_rid,
                              std::span<zlink::message_t> parts) {
            std::cout << "Router callback: " << parts[0].str() << std::endl;
            router.send_rid(source_rid, zlink::message_t("pong", 4));
        });

        // Dealer가 요청 전송
        dealer.send(zlink::message_t("ping", 4));

        // Dealer가 응답 수신 (블로킹 recv)
        auto [rid, reply] = dealer.recv();
        std::cout << "Reply: " << reply[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class RouterCallbackExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            RouterSocket router = new RouterSocket(ctx);
            router.bind("tcp://*:5557");

            DealerSocket dealer = new DealerSocket(ctx);
            dealer.setRoutingId("CLIENT");
            dealer.connect("tcp://127.0.0.1:5557");

            // Router가 콜백으로 수신 및 응답
            router.onReceive(received -> {
                System.out.println("Router callback: "
                    + new String(received.parts()[0].data()));
                router.sendRid(received.routingId(),
                    new Message("pong".getBytes()));
            });

            // Dealer가 요청 전송
            dealer.send(new Message("ping".getBytes()));

            // Dealer가 응답 수신 (블로킹 recv)
            Message reply = dealer.recv();
            System.out.println("Reply: " + reply.partAsString(0));

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
    router.bind("tcp://*:5557")

    dealer = zlink.DealerSocket(ctx)
    dealer.set_routing_id("CLIENT")
    dealer.connect("tcp://127.0.0.1:5557")

    # Router가 콜백으로 수신 및 응답
    def on_request(source_rid, parts):
        print(f"Router callback: {parts[0].decode()}")
        router.send_rid(source_rid, b"pong")

    router.on_receive(on_request)

    # Dealer가 요청 전송
    dealer.send(b"ping")

    # Dealer가 응답 수신 (블로킹 recv)
    _, reply = dealer.recv()
    print(f"Reply: {reply[0].decode()}")

    dealer.close()
    router.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const router = new zlink.RouterSocket(ctx);
    router.bind('tcp://*:5557');

    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId('CLIENT');
    dealer.connect('tcp://127.0.0.1:5557');

    // Router가 콜백으로 수신 및 응답
    router.recvHandler((sourceRid: Buffer, parts: Buffer[]) => {
        console.log(`Router callback: ${parts[0].toString()}`);
        router.sendRid(sourceRid, Buffer.from('pong'));
    });

    // Dealer가 요청 전송
    dealer.send(Buffer.from('ping'));

    // Dealer가 응답 수신 (블로킹 recv)
    const [rid, reply] = dealer.receive();
    console.log(`Reply: ${reply[0].toString()}`);

    dealer.close();
    router.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    using var ctx = new Context();

    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5557");

    using var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("CLIENT");
    dealer.Connect("tcp://127.0.0.1:5557");

    // Router가 콜백으로 수신 및 응답
    router.RecvHandler((sourceRid, parts) => {
        Console.WriteLine($"Router callback: {parts[0].GetString()}");
        router.SendRid(sourceRid, new Message("pong"u8));
    });

    // Dealer가 요청 전송
    dealer.Send(new Message("ping"u8));

    // Dealer가 응답 수신 (블로킹 recv)
    var (rid, reply) = dealer.Recv();
    Console.WriteLine($"Reply: {reply[0].DataString()}");
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let router = ctx.router_socket();
        router.bind("tcp://*:5557")?;

        let dealer = ctx.dealer_socket();
        dealer.set_routing_id("CLIENT")?;
        dealer.connect("tcp://127.0.0.1:5557")?;

        // Router가 콜백으로 수신 및 응답
        let send_handle = router.send_handle();
        router.on_receive(move |source_rid, parts| {
            println!("Router callback: {}",
                     String::from_utf8_lossy(parts[0].data()));
            send_handle.send_rid(source_rid, b"pong")?;
            Ok(())
        })?;

        // Dealer가 요청 전송
        dealer.send(b"ping")?;

        // Dealer가 응답 수신 (블로킹 recv)
        let (_, reply) = dealer.recv()?;
        println!("Reply: {}",
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
        router.Bind("tcp://*:5557")

        dealer, err := ctx.DealerSocket()
        if err != nil { log.Fatal(err) }
        defer dealer.Close()
        rid, _ := zlink.NewRoutingID([]byte("CLIENT"))
        dealer.SetRoutingID(rid)
        dealer.Connect("tcp://127.0.0.1:5557")

        // Router가 콜백으로 수신 및 응답
        router.OnMessage(func(sourceRid zlink.RoutingID, parts []zlink.Message) {
            fmt.Printf("Router callback: %s\n", string(parts[0].Data()))
            router.SendTo(sourceRid, zlink.NewMessage([]byte("pong")))
        })

        // Dealer가 요청 전송
        dealer.Send(zlink.NewMessage([]byte("ping")))

        // Dealer가 응답 수신 (블로킹 recv)
        reply, err := dealer.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Reply: %s\n", reply.Parts[0].Data())
        reply.Close()
    }
    ```

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

ROUTER는 `zlink_send_rid()`로 특정 피어에 전송하고,
`zlink_recv()`의 `source_rid`로 송신자를 식별한다.

### 콜백을 사용한 수신/응답

=== "C"

    ```c
    /* 수신: 핸들러 콜백이 routing_id와 데이터를 제공 */
    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* 응답: zlink_send_rid로 원본 피어에게 전송 */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "reply", 5);
        zlink_send_rid(router, source_rid, &reply, 1, 0);

        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // recv() + send_rid()로 수신/응답
    auto [source_rid, parts] = router.recv();
    zlink::message_t reply("reply", 5);
    router.send_rid(source_rid, reply);
    ```

=== "Java"

    ```java
    // recv() + sendRid()로 수신/응답
    RecvResult result = router.recv();
    Message reply = new Message("reply".getBytes());
    router.sendRid(result.routingId(), reply);
    ```

=== "Python"

    ```python
    # recv() + send_rid()로 수신/응답
    source_rid, parts = router.recv()
    router.send_rid(source_rid, b"reply")
    ```

=== "Node/TypeScript"

    ```typescript
    // recv() + sendRid()로 수신/응답
    const { sourceRid, parts } = router.recv();
    router.sendRid(sourceRid, Buffer.from("reply"));
    ```

=== "C#/.NET"

    ```csharp
    // Recv() + SendRid()로 수신/응답
    var (sourceRid, parts) = router.Recv();
    router.SendRid(sourceRid, new Message("reply"u8));
    ```

=== "Rust"

    ```rust
    // recv() + send_rid()로 수신/응답
    let (source_rid, parts) = router.recv()?;
    router.send_rid(&source_rid, &zlink::Message::from("reply"))?;
    ```

=== "Go"

    ```go
    // recv() + send_rid()로 수신/응답
    source_rid, parts, err := router.Recv()
    if err != nil { panic(err) }
    router.SendTo(source_rid, zlink.NewMessage([]byte("reply")))
    ```

## 4. 소켓 옵션

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_ROUTER_OPT_MANDATORY` | int | 0 | 미도달 시 `EHOSTUNREACH` 반환 |
| `ZLINK_ROUTER_HANDOVER` | int | 0 | routing_id 충돌 시 기존 연결 대체 |
| `zlink_set_routing_id()` | binary | 자동(UUID) | ROUTER 자신의 routing_id (전용 함수) |
| `ZLINK_OPT_SNDHWM` | int | 1000 | 송신 HWM |
| `ZLINK_OPT_RCVHWM` | int | 1000 | 수신 HWM |
| `ZLINK_OPT_LINGER` | int | -1 | close 시 대기 시간 (ms) |

### ROUTER_MANDATORY

기본적으로 ROUTER는 대상을 찾을 수 없는 메시지를 **조용히 드롭**한다. `ROUTER_MANDATORY`를 활성화하면 `EHOSTUNREACH` 에러를 반환한다.

=== "C"

    ```c
    int mandatory = 1;
    zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof(mandatory));

    /* 존재하지 않는 대상에게 전송 시도 */
    zlink_routing_id_t target_rid = { .data = "UNKNOWN", .size = 7 };
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "data", 4);
    int rc = zlink_send_rid(router, &target_rid, &msg, 1, 0);
    /* rc == -1, errno == EHOSTUNREACH */
    ```

=== "C++"

    ```cpp
    router.set_router_mandatory(true);

    // 존재하지 않는 대상에게 전송 시도
    zlink::routing_id_t target_rid("UNKNOWN", 7);
    zlink::message_t msg("data", 4);
    try {
        router.send_rid(target_rid, msg);
    } catch (const zlink::error_t& e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Java"

    ```java
    router.setRouterMandatory(true);

    // 존재하지 않는 대상에게 전송 시도
    RoutingId targetRid = new RoutingId("UNKNOWN");
    Message msg = new Message("data".getBytes());
    try {
        router.sendRid(targetRid, msg);
    } catch (ZlinkException e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Python"

    ```python
    router.set_router_mandatory(True)

    # 존재하지 않는 대상에게 전송 시도
    try:
        router.send_rid(b"UNKNOWN", b"data")
    except zlink.ZlinkError as e:
        # EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    router.setRouterMandatory(true);

    // 존재하지 않는 대상에게 전송 시도
    try {
        router.sendRid(Buffer.from("UNKNOWN"), Buffer.from("data"));
    } catch (e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "C#/.NET"

    ```csharp
    router.SetRouterMandatory(true);

    // 존재하지 않는 대상에게 전송 시도
    var targetRid = new RoutingId("UNKNOWN"u8);
    try {
        router.SendRid(targetRid, new Message("data"u8));
    } catch (ZlinkException) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Rust"

    ```rust
    router.set_router_mandatory(true)?;

    // 존재하지 않는 대상에게 전송 시도
    let target_rid = zlink::RoutingId::from("UNKNOWN");
    let msg = zlink::Message::from("data");
    match router.send_rid(&target_rid, &msg) {
        Err(e) if e.kind() == zlink::ErrorKind::HostUnreachable => {
            // 대상 "UNKNOWN" 찾을 수 없음
        }
        other => other?,
    }
    ```

=== "Go"

    ```go
    router.SetRouterMandatory(true)

    // 존재하지 않는 대상에게 전송 시도
    target_rid := zlink.NewRoutingID("UNKNOWN")
    msg := zlink.NewMessage([]byte("data"))
    err := router.SendTo(target_rid, msg)
        if err != nil { // HostUnreachable
            // 대상 "UNKNOWN" 찾을 수 없음
        }
    }
    ```

> 참고: `core/tests/test_router_mandatory.cpp` — `test_basic()`

## 5. 사용 패턴

### 패턴 1: ROUTER ↔ ROUTER 메시/클러스터

ROUTER의 핵심 패턴. N개 노드가 각각 상대의 routing_id를 지정하여 특정 노드에 전송한다.
1:1이면 DEALER로 충분하므로, ROUTER ↔ ROUTER는 N개 노드 간 통신에서 의미가 있다.

```
  ┌──────────┐    ① send_rid       ┌───────────┐
  │ ROUTER A │─────────────────────►│           │
  │   (RA)   │    target="HUB"     │    HUB    │
  └──────────┘◄────────────────────┤  (ROUTER) │
                  ③ send_rid       │           │
                  target="RA"      └─────┬─────┘
                                         │
                                         │ ② send_rid
                                         │    target="RB"
                                         ▼
                                   ┌──────────┐
                                   │ ROUTER B │
                                   │   (RB)   │
                                   └──────────┘
```

=== "C"

    ```c
    /* 허브 ROUTER: bind */
    void *hub = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(hub, "HUB", 3);
    zlink_bind(hub, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(hub, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* 노드 A, B: connect */
    void *ra = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(ra, "RA", 2);
    zlink_connect(ra, endpoint);

    void *rb = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(rb, "RB", 2);
    zlink_connect(rb, endpoint);

    /* ① A → HUB: routing_id "HUB"를 지정하여 전송 */
    zlink_routing_id_t target_hub = { .data = "HUB", .size = 3 };
    zlink_msg_t msg_a;
    zlink_msg_init_size(&msg_a, 7);
    memcpy(zlink_msg_data(&msg_a), "from_RA", 7);
    zlink_send_rid(ra, &target_hub, &msg_a, 1, 0);

    /* ② HUB 수신: source_rid = "RA" → HUB가 "RB"에게 전달 */
    zlink_routing_id_t target_rb = { .data = "RB", .size = 2 };
    zlink_msg_t forward;
    zlink_msg_init_size(&forward, 10);
    memcpy(zlink_msg_data(&forward), "forwarded", 10);
    zlink_send_rid(hub, &target_rb, &forward, 1, 0);

    /* ③ HUB → RA 응답 */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 3);
    memcpy(zlink_msg_data(&reply), "ack", 3);
    zlink_send_rid(hub, source_rid, &reply, 1, 0);  /* source_rid = "RA" */
    ```

=== "C++"

    ```cpp
    // 허브 ROUTER: bind
    zlink::router_socket_t hub(ctx);
    hub.set_routing_id("HUB");
    hub.bind("tcp://127.0.0.1:*");
    auto endpoint = hub.last_endpoint();

    // 노드 A, B: connect
    zlink::router_socket_t ra(ctx);
    ra.set_routing_id("RA");
    ra.connect(endpoint);

    zlink::router_socket_t rb(ctx);
    rb.set_routing_id("RB");
    rb.connect(endpoint);

    // ① A → HUB: routing_id "HUB"를 지정하여 전송
    ra.send_rid(zlink::routing_id_t("HUB", 3),
                zlink::message_t("from_RA", 7));

    // ② HUB 수신 → "RB"에게 전달
    auto [src_rid, parts] = hub.recv();
    hub.send_rid(zlink::routing_id_t("RB", 2),
                 zlink::message_t("forwarded", 9));

    // ③ HUB → RA 응답
    hub.send_rid(src_rid, zlink::message_t("ack", 3));
    ```

=== "Java"

    ```java
    // 허브 ROUTER: bind
    RouterSocket hub = new RouterSocket(ctx);
    hub.setRoutingId("HUB");
    hub.bind("tcp://127.0.0.1:*");
    String endpoint = hub.lastEndpoint();

    // 노드 A, B: connect
    RouterSocket ra = new RouterSocket(ctx);
    ra.setRoutingId("RA");
    ra.connect(endpoint);

    RouterSocket rb = new RouterSocket(ctx);
    rb.setRoutingId("RB");
    rb.connect(endpoint);

    // ① A → HUB
    ra.sendRid(new RoutingId("HUB"), new Message("from_RA".getBytes()));

    // ② HUB 수신 → "RB"에게 전달
    RecvResult r = hub.recv();
    hub.sendRid(new RoutingId("RB"), new Message("forwarded".getBytes()));

    // ③ HUB → RA 응답
    hub.sendRid(r.routingId(), new Message("ack".getBytes()));
    ```

=== "Python"

    ```python
    # 허브 ROUTER: bind
    hub = zlink.RouterSocket(ctx)
    hub.set_routing_id(b"HUB")
    hub.bind("tcp://127.0.0.1:*")
    endpoint = hub.last_endpoint()

    # 노드 A, B: connect
    ra = zlink.RouterSocket(ctx)
    ra.set_routing_id(b"RA")
    ra.connect(endpoint)

    rb = zlink.RouterSocket(ctx)
    rb.set_routing_id(b"RB")
    rb.connect(endpoint)

    # ① A → HUB
    ra.send_rid(b"HUB", b"from_RA")

    # ② HUB 수신 → "RB"에게 전달
    src_rid, parts = hub.recv()
    hub.send_rid(b"RB", b"forwarded")

    # ③ HUB → RA 응답
    hub.send_rid(src_rid, b"ack")
    ```

=== "Node/TypeScript"

    ```typescript
    // 허브 ROUTER: bind
    const hub = new zlink.RouterSocket(ctx);
    hub.setRoutingId(Buffer.from("HUB"));
    hub.bind("tcp://127.0.0.1:*");
    const endpoint = hub.lastEndpoint();

    // 노드 A, B: connect
    const ra = new zlink.RouterSocket(ctx);
    ra.setRoutingId(Buffer.from("RA"));
    ra.connect(endpoint);

    const rb = new zlink.RouterSocket(ctx);
    rb.setRoutingId(Buffer.from("RB"));
    rb.connect(endpoint);

    // ① A → HUB
    ra.sendRid(Buffer.from("HUB"), Buffer.from("from_RA"));

    // ② HUB 수신 → "RB"에게 전달
    const { sourceRid: srcRid, parts } = hub.recv();
    hub.sendRid(Buffer.from("RB"), Buffer.from("forwarded"));

    // ③ HUB → RA 응답
    hub.sendRid(srcRid, Buffer.from("ack"));
    ```

=== "C#/.NET"

    ```csharp
    // 허브 ROUTER: bind
    using var hub = new RouterSocket(ctx);
    hub.SetRoutingId("HUB"u8);
    hub.Bind("tcp://127.0.0.1:*");
    var endpoint = hub.LastEndpoint;

    // 노드 A, B: connect
    using var ra = new RouterSocket(ctx);
    ra.SetRoutingId("RA"u8);
    ra.Connect(endpoint);

    using var rb = new RouterSocket(ctx);
    rb.SetRoutingId("RB"u8);
    rb.Connect(endpoint);

    // ① A → HUB
    ra.SendRid(new RoutingId("HUB"u8), new Message("from_RA"u8));

    // ② HUB 수신 → "RB"에게 전달
    var (srcRid, parts) = hub.Recv();
    hub.SendRid(new RoutingId("RB"u8), new Message("forwarded"u8));

    // ③ HUB → RA 응답
    hub.SendRid(srcRid, new Message("ack"u8));
    ```

=== "Rust"

    ```rust
    // 허브 ROUTER: bind
    let hub = ctx.router_socket()?;
    hub.set_routing_id("HUB")?;
    hub.bind("tcp://127.0.0.1:*")?;
    let endpoint = hub.last_endpoint()?;

    // 노드 A, B: connect
    let ra = ctx.router_socket()?;
    ra.set_routing_id("RA")?;
    ra.connect(&endpoint)?;

    let rb = ctx.router_socket()?;
    rb.set_routing_id("RB")?;
    rb.connect(&endpoint)?;

    // ① A → HUB
    ra.send_rid(&zlink::RoutingId::from("HUB"),
                &zlink::Message::from("from_RA"))?;

    // ② HUB 수신 → "RB"에게 전달
    let (src_rid, parts) = hub.recv()?;
    hub.send_rid(&zlink::RoutingId::from("RB"),
                 &zlink::Message::from("forwarded"))?;

    // ③ HUB → RA 응답
    hub.send_rid(&src_rid, &zlink::Message::from("ack"))?;
    ```

=== "Go"

    ```go
    // 허브 ROUTER: bind
    hub, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    hub.SetRoutingId("HUB")
    hub.Bind("tcp://127.0.0.1:*")
    endpoint, _ := hub.GetOption(zlink.OptionLastEndpoint)

    // 노드 A, B: connect
    ra, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    ra.SetRoutingId("RA")
    ra.Connect(endpoint)

    rb, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    rb.SetRoutingId("RB")
    rb.Connect(endpoint)

    // ① A → HUB
    ra.SendTo(zlink.NewRoutingID("HUB"),
                    zlink.NewMessage([]byte("from_RA")))

    // ② HUB 수신 → "RB"에게 전달
    src_rid, parts, err := hub.Recv()
    if err != nil { panic(err) }
    hub.SendTo(zlink.NewRoutingID("RB"),
                     zlink.NewMessage([]byte("forwarded")))

    // ③ HUB → RA 응답
    hub.SendTo(src_rid, zlink.NewMessage([]byte("ack")))
    ```

> ROUTER ↔ ROUTER는 브로커, 클러스터 노드 간 메시 통신에 적합하다.
> 모든 노드가 능동적으로 대상을 routing_id로 지정할 수 있다.

### 패턴 2: DEALER → ROUTER 로드밸런싱 요청-응답

DEALER ↔ ROUTER 조합의 핵심 장점:
- **DEALER 측**: round-robin으로 여러 ROUTER 중 하나를 자동 선택 → 부하 분산
- **ROUTER 측**: routing_id로 요청을 보낸 DEALER를 정확히 식별 → 응답 라우팅

```
                                    ┌──────────┐
                        ┌── send ──►│ ROUTER A │
                        │           └──────────┘
  ┌──────────┐          │           ┌──────────┐
  │ DEALER 1 │──────────┼── send ──►│ ROUTER B │    round-robin
  │   (D1)   │          │           └──────────┘    순환 분배
  └──────────┘          │           ┌──────────┐
                        └── send ──►│ ROUTER C │
                                    └──────────┘

  ┌──────────┐
  │ DEALER 2 │──────── (동일하게 A, B, C에 round-robin)
  │   (D2)   │
  └──────────┘


  ROUTER가 요청한 DEALER에 응답:

  ┌──────────┐    ① send         ┌──────────┐
  │ DEALER 1 │──────────────────►│ ROUTER B │
  │   (D1)   │◄──────────────────┤          │
  └──────────┘    ② send_rid     └──────────┘
                   source_rid="D1"
```

=== "C"

    ```c
    /* ROUTER 서버 3대 */
    void *ra = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(ra, "tcp://127.0.0.1:5560");
    void *rb = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(rb, "tcp://127.0.0.1:5561");
    void *rc = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(rc, "tcp://127.0.0.1:5562");

    /* DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배 */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer, "D1", 2);
    zlink_connect(dealer, "tcp://127.0.0.1:5560");
    zlink_connect(dealer, "tcp://127.0.0.1:5561");
    zlink_connect(dealer, "tcp://127.0.0.1:5562");

    /* 3개 요청 전송 → req1→RA, req2→RB, req3→RC (round-robin) */
    for (int i = 0; i < 3; i++) {
        zlink_msg_t req;
        zlink_msg_init_size(&req, 5);
        memcpy(zlink_msg_data(&req), "Hello", 5);
        zlink_send(dealer, &req, 1, 0);
    }

    /* 각 ROUTER는 source_rid = "D1"로 요청 DEALER를 식별하여 응답 */
    ```

=== "C++"

    ```cpp
    // ROUTER 서버 3대
    zlink::router_socket_t ra(ctx), rb(ctx), rc(ctx);
    ra.bind("tcp://127.0.0.1:5560");
    rb.bind("tcp://127.0.0.1:5561");
    rc.bind("tcp://127.0.0.1:5562");

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    zlink::dealer_socket_t dealer(ctx);
    dealer.set_routing_id("D1");
    dealer.connect("tcp://127.0.0.1:5560");
    dealer.connect("tcp://127.0.0.1:5561");
    dealer.connect("tcp://127.0.0.1:5562");

    // 3개 요청 전송 → round-robin
    for (int i = 0; i < 3; i++)
        dealer.send(zlink::message_t("Hello", 5));
    ```

=== "Java"

    ```java
    // ROUTER 서버 3대
    RouterSocket ra = new RouterSocket(ctx);
    ra.bind("tcp://127.0.0.1:5560");
    RouterSocket rb = new RouterSocket(ctx);
    rb.bind("tcp://127.0.0.1:5561");
    RouterSocket rc = new RouterSocket(ctx);
    rc.bind("tcp://127.0.0.1:5562");

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    DealerSocket dealer = new DealerSocket(ctx);
    dealer.setRoutingId("D1");
    dealer.connect("tcp://127.0.0.1:5560");
    dealer.connect("tcp://127.0.0.1:5561");
    dealer.connect("tcp://127.0.0.1:5562");

    // 3개 요청 전송 → round-robin
    for (int i = 0; i < 3; i++)
        dealer.send(new Message("Hello".getBytes()));
    ```

=== "Python"

    ```python
    # ROUTER 서버 3대
    ra = zlink.RouterSocket(ctx)
    ra.bind("tcp://127.0.0.1:5560")
    rb = zlink.RouterSocket(ctx)
    rb.bind("tcp://127.0.0.1:5561")
    rc = zlink.RouterSocket(ctx)
    rc.bind("tcp://127.0.0.1:5562")

    # DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    dealer = zlink.DealerSocket(ctx)
    dealer.set_routing_id(b"D1")
    dealer.connect("tcp://127.0.0.1:5560")
    dealer.connect("tcp://127.0.0.1:5561")
    dealer.connect("tcp://127.0.0.1:5562")

    # 3개 요청 전송 → round-robin
    for i in range(3):
        dealer.send(b"Hello")
    ```

=== "Node/TypeScript"

    ```typescript
    // ROUTER 서버 3대
    const ra = new zlink.RouterSocket(ctx);
    ra.bind("tcp://127.0.0.1:5560");
    const rb = new zlink.RouterSocket(ctx);
    rb.bind("tcp://127.0.0.1:5561");
    const rc = new zlink.RouterSocket(ctx);
    rc.bind("tcp://127.0.0.1:5562");

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId(Buffer.from("D1"));
    dealer.connect("tcp://127.0.0.1:5560");
    dealer.connect("tcp://127.0.0.1:5561");
    dealer.connect("tcp://127.0.0.1:5562");

    // 3개 요청 전송 → round-robin
    for (let i = 0; i < 3; i++)
        dealer.send(Buffer.from("Hello"));
    ```

=== "C#/.NET"

    ```csharp
    // ROUTER 서버 3대
    using var ra = new RouterSocket(ctx);
    ra.Bind("tcp://127.0.0.1:5560");
    using var rb = new RouterSocket(ctx);
    rb.Bind("tcp://127.0.0.1:5561");
    using var rc = new RouterSocket(ctx);
    rc.Bind("tcp://127.0.0.1:5562");

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    using var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("D1"u8);
    dealer.Connect("tcp://127.0.0.1:5560");
    dealer.Connect("tcp://127.0.0.1:5561");
    dealer.Connect("tcp://127.0.0.1:5562");

    // 3개 요청 전송 → round-robin
    for (int i = 0; i < 3; i++)
        dealer.Send(new Message("Hello"u8));
    ```

=== "Rust"

    ```rust
    // ROUTER 서버 3대
    let ra = ctx.router_socket()?;
    ra.bind("tcp://127.0.0.1:5560")?;
    let rb = ctx.router_socket()?;
    rb.bind("tcp://127.0.0.1:5561")?;
    let rc = ctx.router_socket()?;
    rc.bind("tcp://127.0.0.1:5562")?;

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    let dealer = ctx.dealer_socket()?;
    dealer.set_routing_id("D1")?;
    dealer.connect("tcp://127.0.0.1:5560")?;
    dealer.connect("tcp://127.0.0.1:5561")?;
    dealer.connect("tcp://127.0.0.1:5562")?;

    // 3개 요청 전송 → round-robin
    for _ in 0..3 {
        dealer.send(&zlink::Message::from("Hello"))?;
    }
    ```

=== "Go"

    ```go
    // ROUTER 서버 3대
    ra, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    ra.Bind("tcp://127.0.0.1:5560")
    rb, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    rb.Bind("tcp://127.0.0.1:5561")
    rc, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    rc.Bind("tcp://127.0.0.1:5562")

    // DEALER 클라이언트: 3개 ROUTER에 연결 → round-robin 분배
    dealer, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    dealer.SetRoutingId("D1")
    dealer.Connect("tcp://127.0.0.1:5560")
    dealer.Connect("tcp://127.0.0.1:5561")
    dealer.Connect("tcp://127.0.0.1:5562")

    // 3개 요청 전송 → round-robin
    for i := 0; i < 3; i++ {
        dealer.Send(zlink.NewMessage([]byte("Hello")))
    }
    ```

### 패턴 3: ROUTER ↔ ROUTER 가중치 라우팅

DEALER → ROUTER는 round-robin이 고정되어 분배 비율을 제어할 수 없다.
가중치, 우선순위, 조건부 라우팅이 필요하면 ROUTER ↔ ROUTER로 구성하고
애플리케이션이 routing_id를 직접 선택한다.

```
  DEALER → ROUTER (round-robin 고정, 균등 분배):

  ┌──────────┐     1/3      ┌──────────┐
  │          │──────────────►│ ROUTER A │
  │  DEALER  │     1/3      ┌──────────┐
  │          │──────────────►│ ROUTER B │    변경 불가
  │          │     1/3      ┌──────────┐
  │          │──────────────►│ ROUTER C │
  └──────────┘              └──────────┘


  ROUTER ↔ ROUTER (애플리케이션이 대상 직접 선택):

  ┌──────────┐     50%      ┌──────────┐
  │          │──────────────►│ ROUTER A │
  │  ROUTER  │     30%      ┌──────────┐
  │ (client) │──────────────►│ ROUTER B │    자유롭게 제어
  │          │     20%      ┌──────────┐
  │          │──────────────►│ ROUTER C │
  └──────────┘              └──────────┘
```

=== "C"

    ```c
    /* 서버 3대 */
    void *sa = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(sa, "SA", 2);
    zlink_bind(sa, "tcp://127.0.0.1:5560");
    void *sb = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(sb, "SB", 2);
    zlink_bind(sb, "tcp://127.0.0.1:5561");
    void *sc = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(sc, "SC", 2);
    zlink_bind(sc, "tcp://127.0.0.1:5562");

    /* 클라이언트 ROUTER: 3개 서버에 connect */
    void *client = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_set_routing_id(client, "C1", 2);
    zlink_connect(client, "tcp://127.0.0.1:5560");
    zlink_connect(client, "tcp://127.0.0.1:5561");
    zlink_connect(client, "tcp://127.0.0.1:5562");

    /* 가중치 테이블: SA=50%, SB=30%, SC=20% */
    typedef struct { const char *rid; size_t len; int weight; } route_t;
    route_t routes[] = {
        { "SA", 2, 50 }, { "SB", 2, 30 }, { "SC", 2, 20 }
    };

    /* 가중치 기반 대상 선택 */
    int roll = rand() % 100;
    route_t *target = (roll < 50)  ? &routes[0]
                   : (roll < 80) ? &routes[1]
                   :                &routes[2];

    zlink_routing_id_t rid = { .data = target->rid, .size = target->len };
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 7);
    memcpy(zlink_msg_data(&msg), "request", 7);
    zlink_send_rid(client, &rid, &msg, 1, 0);

    /* 서버 응답: source_rid = "C1"로 클라이언트 식별 가능 */
    ```

=== "C++"

    ```cpp
    // 서버 3대
    zlink::router_socket_t sa(ctx), sb(ctx), sc(ctx);
    sa.set_routing_id("SA"); sa.bind("tcp://127.0.0.1:5560");
    sb.set_routing_id("SB"); sb.bind("tcp://127.0.0.1:5561");
    sc.set_routing_id("SC"); sc.bind("tcp://127.0.0.1:5562");

    // 클라이언트 ROUTER
    zlink::router_socket_t client(ctx);
    client.set_routing_id("C1");
    client.connect("tcp://127.0.0.1:5560");
    client.connect("tcp://127.0.0.1:5561");
    client.connect("tcp://127.0.0.1:5562");

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    int roll = std::rand() % 100;
    std::string target = (roll < 50) ? "SA" : (roll < 80) ? "SB" : "SC";
    client.send_rid(zlink::routing_id_t(target),
                    zlink::message_t("request", 7));
    ```

=== "Java"

    ```java
    // 서버 3대
    RouterSocket sa = new RouterSocket(ctx);
    sa.setRoutingId("SA"); sa.bind("tcp://127.0.0.1:5560");
    RouterSocket sb = new RouterSocket(ctx);
    sb.setRoutingId("SB"); sb.bind("tcp://127.0.0.1:5561");
    RouterSocket sc = new RouterSocket(ctx);
    sc.setRoutingId("SC"); sc.bind("tcp://127.0.0.1:5562");

    // 클라이언트 ROUTER
    RouterSocket client = new RouterSocket(ctx);
    client.setRoutingId("C1");
    client.connect("tcp://127.0.0.1:5560");
    client.connect("tcp://127.0.0.1:5561");
    client.connect("tcp://127.0.0.1:5562");

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    int roll = new Random().nextInt(100);
    String target = (roll < 50) ? "SA" : (roll < 80) ? "SB" : "SC";
    client.sendRid(new RoutingId(target), new Message("request".getBytes()));
    ```

=== "Python"

    ```python
    # 서버 3대
    sa = zlink.RouterSocket(ctx); sa.set_routing_id(b"SA"); sa.bind("tcp://127.0.0.1:5560")
    sb = zlink.RouterSocket(ctx); sb.set_routing_id(b"SB"); sb.bind("tcp://127.0.0.1:5561")
    sc = zlink.RouterSocket(ctx); sc.set_routing_id(b"SC"); sc.bind("tcp://127.0.0.1:5562")

    # 클라이언트 ROUTER
    client = zlink.RouterSocket(ctx)
    client.set_routing_id(b"C1")
    client.connect("tcp://127.0.0.1:5560")
    client.connect("tcp://127.0.0.1:5561")
    client.connect("tcp://127.0.0.1:5562")

    # 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    import random
    roll = random.randint(0, 99)
    target = b"SA" if roll < 50 else b"SB" if roll < 80 else b"SC"
    client.send_rid(target, b"request")
    ```

=== "Node/TypeScript"

    ```typescript
    // 서버 3대
    const sa = new zlink.RouterSocket(ctx);
    sa.setRoutingId(Buffer.from("SA")); sa.bind("tcp://127.0.0.1:5560");
    const sb = new zlink.RouterSocket(ctx);
    sb.setRoutingId(Buffer.from("SB")); sb.bind("tcp://127.0.0.1:5561");
    const sc = new zlink.RouterSocket(ctx);
    sc.setRoutingId(Buffer.from("SC")); sc.bind("tcp://127.0.0.1:5562");

    // 클라이언트 ROUTER
    const client = new zlink.RouterSocket(ctx);
    client.setRoutingId(Buffer.from("C1"));
    client.connect("tcp://127.0.0.1:5560");
    client.connect("tcp://127.0.0.1:5561");
    client.connect("tcp://127.0.0.1:5562");

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    const roll = Math.floor(Math.random() * 100);
    const target = roll < 50 ? "SA" : roll < 80 ? "SB" : "SC";
    client.sendRid(Buffer.from(target), Buffer.from("request"));
    ```

=== "C#/.NET"

    ```csharp
    // 서버 3대
    using var sa = new RouterSocket(ctx);
    sa.SetRoutingId("SA"u8); sa.Bind("tcp://127.0.0.1:5560");
    using var sb = new RouterSocket(ctx);
    sb.SetRoutingId("SB"u8); sb.Bind("tcp://127.0.0.1:5561");
    using var sc = new RouterSocket(ctx);
    sc.SetRoutingId("SC"u8); sc.Bind("tcp://127.0.0.1:5562");

    // 클라이언트 ROUTER
    using var client = new RouterSocket(ctx);
    client.SetRoutingId("C1"u8);
    client.Connect("tcp://127.0.0.1:5560");
    client.Connect("tcp://127.0.0.1:5561");
    client.Connect("tcp://127.0.0.1:5562");

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    int roll = Random.Shared.Next(100);
    string target = roll < 50 ? "SA" : roll < 80 ? "SB" : "SC";
    client.SendRid(new RoutingId(Encoding.UTF8.GetBytes(target)),
                   new Message("request"u8));
    ```

=== "Rust"

    ```rust
    // 서버 3대
    let sa = ctx.router_socket()?;
    sa.set_routing_id("SA")?; sa.bind("tcp://127.0.0.1:5560")?;
    let sb = ctx.router_socket()?;
    sb.set_routing_id("SB")?; sb.bind("tcp://127.0.0.1:5561")?;
    let sc = ctx.router_socket()?;
    sc.set_routing_id("SC")?; sc.bind("tcp://127.0.0.1:5562")?;

    // 클라이언트 ROUTER
    let client = ctx.router_socket()?;
    client.set_routing_id("C1")?;
    client.connect("tcp://127.0.0.1:5560")?;
    client.connect("tcp://127.0.0.1:5561")?;
    client.connect("tcp://127.0.0.1:5562")?;

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    let roll = rand::random::<u32>() % 100;
    let target = if roll < 50 { "SA" } else if roll < 80 { "SB" } else { "SC" };
    client.send_rid(&zlink::RoutingId::from(target),
                    &zlink::Message::from("request"))?;
    ```

=== "Go"

    ```go
    // 서버 3대
    sa, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    sa.SetRoutingID(zlink.NewRoutingID([]byte("SA")))
    sa.Bind("tcp://127.0.0.1:5560")
    sb, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    sb.SetRoutingID(zlink.NewRoutingID([]byte("SB")))
    sb.Bind("tcp://127.0.0.1:5561")
    sc, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    sc.SetRoutingID(zlink.NewRoutingID([]byte("SC")))
    sc.Bind("tcp://127.0.0.1:5562")

    // 클라이언트 ROUTER
    client, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    client.SetRoutingID(zlink.NewRoutingID([]byte("C1")))
    client.Connect("tcp://127.0.0.1:5560")
    client.Connect("tcp://127.0.0.1:5561")
    client.Connect("tcp://127.0.0.1:5562")

    // 가중치 기반 대상 선택: SA=50%, SB=30%, SC=20%
    roll := rand.Intn(100)
    var target string
    if roll < 50 { target = "SA" } else if roll < 80 { target = "SB" } else { target = "SC" }
    client.SendTo(zlink.NewRoutingID([]byte(target)),
        zlink.NewMessage([]byte("request")))
    ```

> DEALER → ROUTER의 round-robin이 충분하면 DEALER를 사용하고,
> 분배 로직을 제어해야 하면 ROUTER ↔ ROUTER로 전환한다.

### 패턴 4: 다중 DEALER 서버

여러 DEALER가 하나의 ROUTER에 연결. ROUTER가 각 DEALER를 routing_id로 구분.

```
  ┌──────────┐
  │ DEALER 1 │── send ──┐
  │   (D1)   │          │
  └──────────┘          │     ┌──────────┐
                        ├────►│  ROUTER  │
  ┌──────────┐          │     └──────────┘
  │ DEALER 2 │── send ──┘         │
  │   (D2)   │              source_rid로
  └──────────┘              D1, D2 구분
```

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(router, "tcp://127.0.0.1:*");

    char endpoint[256];
    size_t len = sizeof(endpoint);
    zlink_get_option(router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);

    /* 클라이언트 1 */
    void *d1 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(d1, "D1", 2);
    zlink_connect(d1, endpoint);

    /* 클라이언트 2 */
    void *d2 = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(d2, "D2", 2);
    zlink_connect(d2, endpoint);

    /* 각 클라이언트가 메시지 전송 — ROUTER가 source_rid로 구분 */
    zlink_msg_t m1;
    zlink_msg_init_size(&m1, 7);
    memcpy(zlink_msg_data(&m1), "from_d1", 7);
    zlink_send(d1, &m1, 1, 0);

    zlink_msg_t m2;
    zlink_msg_init_size(&m2, 7);
    memcpy(zlink_msg_data(&m2), "from_d2", 7);
    zlink_send(d2, &m2, 1, 0);
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t router(ctx);
    router.bind("tcp://127.0.0.1:*");
    auto endpoint = router.last_endpoint();

    zlink::dealer_socket_t d1(ctx);
    d1.set_routing_id("D1");
    d1.connect(endpoint);

    zlink::dealer_socket_t d2(ctx);
    d2.set_routing_id("D2");
    d2.connect(endpoint);

    d1.send(zlink::message_t("from_d1", 7));
    d2.send(zlink::message_t("from_d2", 7));
    ```

=== "Java"

    ```java
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:*");
    String endpoint = router.lastEndpoint();

    DealerSocket d1 = new DealerSocket(ctx);
    d1.setRoutingId("D1");
    d1.connect(endpoint);

    DealerSocket d2 = new DealerSocket(ctx);
    d2.setRoutingId("D2");
    d2.connect(endpoint);

    d1.send(new Message("from_d1".getBytes()));
    d2.send(new Message("from_d2".getBytes()));
    ```

=== "Python"

    ```python
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://127.0.0.1:*")
    endpoint = router.last_endpoint()

    d1 = zlink.DealerSocket(ctx)
    d1.set_routing_id(b"D1")
    d1.connect(endpoint)

    d2 = zlink.DealerSocket(ctx)
    d2.set_routing_id(b"D2")
    d2.connect(endpoint)

    d1.send(b"from_d1")
    d2.send(b"from_d2")
    ```

=== "Node/TypeScript"

    ```typescript
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:*");
    const endpoint = router.lastEndpoint();

    const d1 = new zlink.DealerSocket(ctx);
    d1.setRoutingId(Buffer.from("D1"));
    d1.connect(endpoint);

    const d2 = new zlink.DealerSocket(ctx);
    d2.setRoutingId(Buffer.from("D2"));
    d2.connect(endpoint);

    d1.send(Buffer.from("from_d1"));
    d2.send(Buffer.from("from_d2"));
    ```

=== "C#/.NET"

    ```csharp
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://127.0.0.1:*");
    var endpoint = router.LastEndpoint;

    using var d1 = new DealerSocket(ctx);
    d1.SetRoutingId("D1"u8);
    d1.Connect(endpoint);

    using var d2 = new DealerSocket(ctx);
    d2.SetRoutingId("D2"u8);
    d2.Connect(endpoint);

    d1.Send(new Message("from_d1"u8));
    d2.Send(new Message("from_d2"u8));
    ```

=== "Rust"

    ```rust
    let router = ctx.router_socket()?;
    router.bind("tcp://127.0.0.1:*")?;
    let endpoint = router.last_endpoint()?;

    let d1 = ctx.dealer_socket()?;
    d1.set_routing_id("D1")?;
    d1.connect(&endpoint)?;

    let d2 = ctx.dealer_socket()?;
    d2.set_routing_id("D2")?;
    d2.connect(&endpoint)?;

    d1.send(&zlink::Message::from("from_d1"))?;
    d2.send(&zlink::Message::from("from_d2"))?;
    ```

=== "Go"

    ```go
    router, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    router.Bind("tcp://127.0.0.1:*")
    endpoint, _ := router.GetOption(zlink.OptionLastEndpoint)

    d1, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    d1.SetRoutingId("D1")
    d1.Connect(endpoint)

    d2, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    d2.SetRoutingId("D2")
    d2.Connect(endpoint)

    d1.Send(zlink.NewMessage([]byte("from_d1")))
    d2.Send(zlink.NewMessage([]byte("from_d2")))
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 3가지 transport

### 패턴 5: 프록시 패턴 (ROUTER-DEALER)

ROUTER(프론트엔드) + DEALER(백엔드)로 멀티스레드 서버 구축.

```
  ┌──────────┐                                       ┌──────────┐
  │ CLIENT 1 │──┐                               ┌───►│ WORKER 1 │
  │ (DEALER) │  │    ┌──────────┐  ┌──────────┐ │    │ (DEALER) │
  └──────────┘  ├───►│  ROUTER  ├──►  DEALER  ├─┤    └──────────┘
  ┌──────────┐  │    │(frontend)│  │(backend) │ │    ┌──────────┐
  │ CLIENT 2 │──┘    └──────────┘  └──────────┘ └───►│ WORKER 2 │
  │ (DEALER) │          proxy()                      │ (DEALER) │
  └──────────┘                                       └──────────┘
```

=== "C"

    ```c
    /* 프론트엔드: 클라이언트가 연결 */
    void *frontend = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(frontend, "tcp://*:5558");

    /* 백엔드: 워커 스레드가 연결 */
    void *backend = zlink_socket(ctx, ZLINK_DEALER);
    zlink_bind(backend, "inproc://backend");

    /* 워커 스레드 시작 후 프록시 실행 */
    zlink_proxy(frontend, backend, NULL);
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t frontend(ctx);
    frontend.bind("tcp://*:5558");

    zlink::dealer_socket_t backend(ctx);
    backend.bind("inproc://backend");

    // 워커 스레드 시작 후 프록시 실행
    zlink::proxy(frontend, backend);
    ```

=== "Java"

    ```java
    RouterSocket frontend = new RouterSocket(ctx);
    frontend.bind("tcp://*:5558");

    DealerSocket backend = new DealerSocket(ctx);
    backend.bind("inproc://backend");

    // 워커 스레드 시작 후 프록시 실행
    Proxy.start(frontend, backend);
    ```

=== "Python"

    ```python
    frontend = zlink.RouterSocket(ctx)
    frontend.bind("tcp://*:5558")

    backend = zlink.DealerSocket(ctx)
    backend.bind("inproc://backend")

    # 워커 스레드 시작 후 프록시 실행
    zlink.proxy(frontend, backend)
    ```

=== "Node/TypeScript"

    ```typescript
    const frontend = new zlink.RouterSocket(ctx);
    frontend.bind("tcp://*:5558");

    const backend = new zlink.DealerSocket(ctx);
    backend.bind("inproc://backend");

    // 워커 스레드 시작 후 프록시 실행
    zlink.proxy(frontend, backend);
    ```

=== "C#/.NET"

    ```csharp
    using var frontend = new RouterSocket(ctx);
    frontend.Bind("tcp://*:5558");

    using var backend = new DealerSocket(ctx);
    backend.Bind("inproc://backend");

    // 워커 스레드 시작 후 프록시 실행
    Proxy.Start(frontend, backend);
    ```

=== "Rust"

    ```rust
    let frontend = ctx.router_socket()?;
    frontend.bind("tcp://*:5558")?;

    let backend = ctx.dealer_socket()?;
    backend.bind("inproc://backend")?;

    // 워커 스레드 시작 후 프록시 실행
    zlink::proxy(&frontend, &backend, None)?;
    ```

=== "Go"

    ```go
    frontend, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    frontend.Bind("tcp://*:5558")

    backend, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    backend.Bind("inproc://backend")

    // 워커 스레드 시작 후 프록시 실행
    zlink.Proxy(frontend, backend, nil)
    ```

=== "C"

    ```c
    /* 워커 스레드 */
    void worker_thread(void *arg) {
        void on_work(const zlink_routing_id_t *source_rid,
                     zlink_msg_t *parts, size_t part_count,
                     void *userdata)
        {
            /* 처리 후 동일 routing_id로 응답 */
            zlink_send_rid(worker, source_rid, parts, part_count, 0);
        }

        void *worker = zlink_socket(ctx, ZLINK_DEALER);
        /* zlink_recv()로 작업 수신 */
        zlink_connect(worker, "inproc://backend");

        /* 소켓이 닫힐 때까지 워커 유지 */
    }
    ```

=== "C++"

    ```cpp
    // 워커 스레드
    void worker_thread() {
        zlink::dealer_socket_t worker(ctx);
        worker.connect("inproc://backend");

        while (true) {
            auto [rid, parts] = worker.recv();
            worker.send_rid(rid, parts);
        }
    }
    ```

=== "Java"

    ```java
    // 워커 스레드
    class WorkerThread implements Runnable {
        public void run() {
            DealerSocket worker = new DealerSocket(ctx);
            worker.connect("inproc://backend");
            while (true) {
                RecvResult r = worker.recv();
                worker.sendRid(r.routingId(), r.parts());
            }
        }
    }
    ```

=== "Python"

    ```python
    # 워커 스레드
    def worker_thread():
        worker = zlink.DealerSocket(ctx)
        worker.connect("inproc://backend")
        while True:
            rid, parts = worker.recv()
            worker.send_rid(rid, parts)
    ```

=== "Node/TypeScript"

    ```typescript
    // 워커 스레드
    const worker = new zlink.DealerSocket(ctx);
    worker.connect("inproc://backend");
    while (true) {
        const { sourceRid, parts } = worker.recv();
        worker.sendRid(sourceRid, parts);
    }
    ```

=== "C#/.NET"

    ```csharp
    // 워커 스레드
    using var worker = new DealerSocket(ctx);
    worker.Connect("inproc://backend");
    while (true) {
        var (rid, parts) = worker.Recv();
        worker.SendRid(rid, parts);
    }
    ```

=== "Rust"

    ```rust
    // 워커 스레드
    let worker = ctx.dealer_socket()?;
    worker.connect("inproc://backend")?;
    loop {
        let (rid, parts) = worker.recv()?;
        worker.send_rid(&rid, &parts)?;
    }
    ```

=== "Go"

    ```go
    // 워커 스레드
    worker, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    worker.Connect("inproc://backend")
    for {
        rid, parts, err := worker.Recv()
        if err != nil { panic(err) }
        worker.SendTo(rid, parts)
    }
    ```

> 참고: `core/tests/test_proxy.cpp` — ROUTER(frontend) + DEALER(backend) + 워커 풀

### 패턴 6: ROUTER_MANDATORY로 전송 실패 감지

```
  MANDATORY = 0 (기본):

  ┌──────────┐   send_rid      ┌ ─ ─ ─ ─ ─ ┐
  │  ROUTER  ├────────────────►  "UNKNOWN"       조용히 드롭
  └──────────┘   target=       └ ─ ─ ─ ─ ─ ┘    (에러 없음)
                 "UNKNOWN"

  MANDATORY = 1:

  ┌──────────┐   send_rid      ┌ ─ ─ ─ ─ ─ ┐
  │  ROUTER  ├───────X─────────  "UNKNOWN"       rc = -1
  └──────────┘   target=       └ ─ ─ ─ ─ ─ ┘    errno = EHOSTUNREACH
                 "UNKNOWN"
```

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);
    zlink_bind(router, "tcp://*:5558");

    /* 기본 동작: 미도달 메시지 조용히 드롭 */
    zlink_routing_id_t bad_rid = { .data = "UNKNOWN", .size = 7 };
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 4);
    memcpy(zlink_msg_data(&msg), "DATA", 4);
    zlink_send_rid(router, &bad_rid, &msg, 1, 0);
    /* 에러 없음, 메시지 소실 */

    /* MANDATORY 모드 활성화 */
    int mandatory = 1;
    zlink_set_router_option(router, ZLINK_ROUTER_OPT_MANDATORY, &mandatory, sizeof(mandatory));

    /* 이제 미도달 시 에러 반환 */
    zlink_msg_t msg2;
    zlink_msg_init_size(&msg2, 4);
    memcpy(zlink_msg_data(&msg2), "DATA", 4);
    int rc = zlink_send_rid(router, &bad_rid, &msg2, 1, 0);
    if (rc == -1 && errno == EHOSTUNREACH) {
        /* 대상 "UNKNOWN"을 찾을 수 없음 */
    }
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t router(ctx);
    router.bind("tcp://*:5558");

    // 기본 동작: 미도달 메시지 조용히 드롭
    zlink::routing_id_t bad_rid("UNKNOWN", 7);
    router.send_rid(bad_rid, zlink::message_t("DATA", 4));

    // MANDATORY 모드 활성화
    router.set_router_mandatory(true);

    try {
        router.send_rid(bad_rid, zlink::message_t("DATA", 4));
    } catch (const zlink::error_t& e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Java"

    ```java
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://*:5558");

    // 기본 동작: 미도달 메시지 조용히 드롭
    RoutingId badRid = new RoutingId("UNKNOWN");
    router.sendRid(badRid, new Message("DATA".getBytes()));

    // MANDATORY 모드 활성화
    router.setRouterMandatory(true);

    try {
        router.sendRid(badRid, new Message("DATA".getBytes()));
    } catch (ZlinkException e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Python"

    ```python
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://*:5558")

    # 기본 동작: 미도달 메시지 조용히 드롭
    router.send_rid(b"UNKNOWN", b"DATA")

    # MANDATORY 모드 활성화
    router.set_router_mandatory(True)

    try:
        router.send_rid(b"UNKNOWN", b"DATA")
    except zlink.ZlinkError:
        # EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://*:5558");

    // 기본 동작: 미도달 메시지 조용히 드롭
    router.sendRid(Buffer.from("UNKNOWN"), Buffer.from("DATA"));

    // MANDATORY 모드 활성화
    router.setRouterMandatory(true);

    try {
        router.sendRid(Buffer.from("UNKNOWN"), Buffer.from("DATA"));
    } catch (e) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "C#/.NET"

    ```csharp
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://*:5558");

    // 기본 동작: 미도달 메시지 조용히 드롭
    var badRid = new RoutingId("UNKNOWN"u8);
    router.SendRid(badRid, new Message("DATA"u8));

    // MANDATORY 모드 활성화
    router.SetRouterMandatory(true);

    try {
        router.SendRid(badRid, new Message("DATA"u8));
    } catch (ZlinkException) {
        // EHOSTUNREACH — 대상 "UNKNOWN" 찾을 수 없음
    }
    ```

=== "Rust"

    ```rust
    let router = ctx.router_socket()?;
    router.bind("tcp://*:5558")?;

    // 기본 동작: 미도달 메시지 조용히 드롭
    let bad_rid = zlink::RoutingId::from("UNKNOWN");
    router.send_rid(&bad_rid, &zlink::Message::from("DATA"))?;

    // MANDATORY 모드 활성화
    router.set_router_mandatory(true)?;

    match router.send_rid(&bad_rid, &zlink::Message::from("DATA")) {
        Err(e) if e.kind() == zlink::ErrorKind::HostUnreachable => {
            // 대상 "UNKNOWN" 찾을 수 없음
        }
        other => other?,
    }
    ```

=== "Go"

    ```go
    router, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    router.Bind("tcp://*:5558")

    // 기본 동작: 미도달 메시지 조용히 드롭
    bad_rid := zlink.NewRoutingID("UNKNOWN")
    router.SendTo(bad_rid, zlink.NewMessage([]byte("DATA")))

    // MANDATORY 모드 활성화
    router.SetRouterMandatory(true)

    err := router.SendTo(bad_rid, zlink.NewMessage([]byte("DATA")))
        if err != nil { // HostUnreachable
            // 대상 "UNKNOWN" 찾을 수 없음
        }
    }
    ```

> 참고: `core/tests/test_router_mandatory.cpp` — 기본 드롭 vs MANDATORY 에러

### 패턴 7: 연결 확인 후 전송

DEALER가 먼저 메시지를 전송하여 ROUTER에 연결을 알린 후, ROUTER가 응답.

```
  ┌──────────┐    ① "Hello"       ┌──────────┐
  │  DEALER  │───────────────────►│  ROUTER  │
  │   (X)    │◄───────────────────┤          │
  └──────────┘    ② "Welcome"     └──────────┘
                   source_rid="X"
```

=== "C"

    ```c
    /* ROUTER 핸들러: DEALER의 초기 메시지로 연결 확인 */
    void on_connect(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* source_rid->data = "X" — 이제 "X"로 안전하게 전송 가능 */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 7);
        memcpy(zlink_msg_data(&reply), "Welcome", 7);
        zlink_send_rid(router, source_rid, &reply, 1, 0);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    /* DEALER 연결 및 초기 메시지 전송 */
    void *dealer = zlink_socket(ctx, ZLINK_DEALER);
    zlink_set_routing_id(dealer, "X", 1);
    zlink_connect(dealer, endpoint);
    zlink_msg_t hello;
    zlink_msg_init_size(&hello, 5);
    memcpy(zlink_msg_data(&hello), "Hello", 5);
    zlink_send(dealer, &hello, 1, 0);

    /* on_connect 수신: source_rid = "X", parts[0] = "Hello"
       "Welcome"으로 응답 */
    ```

=== "C++"

    ```cpp
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    auto [source_rid, parts] = router.recv();
    // source_rid = "X" — 이제 "X"로 안전하게 전송 가능
    router.send_rid(source_rid, zlink::message_t("Welcome", 7));

    // DEALER 연결 및 초기 메시지 전송
    zlink::dealer_socket_t dealer(ctx);
    dealer.set_routing_id("X");
    dealer.connect(endpoint);
    dealer.send(zlink::message_t("Hello", 5));
    ```

=== "Java"

    ```java
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    RecvResult result = router.recv();
    // sourceRid = "X" — 이제 "X"로 안전하게 전송 가능
    router.sendRid(result.routingId(), new Message("Welcome".getBytes()));

    // DEALER 연결 및 초기 메시지 전송
    DealerSocket dealer = new DealerSocket(ctx);
    dealer.setRoutingId("X");
    dealer.connect(endpoint);
    dealer.send(new Message("Hello".getBytes()));
    ```

=== "Python"

    ```python
    # ROUTER: DEALER의 초기 메시지로 연결 확인
    source_rid, parts = router.recv()
    # source_rid = "X" — 이제 "X"로 안전하게 전송 가능
    router.send_rid(source_rid, b"Welcome")

    # DEALER 연결 및 초기 메시지 전송
    dealer = zlink.DealerSocket(ctx)
    dealer.set_routing_id(b"X")
    dealer.connect(endpoint)
    dealer.send(b"Hello")
    ```

=== "Node/TypeScript"

    ```typescript
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    const { sourceRid, parts } = router.recv();
    // sourceRid = "X" — 이제 "X"로 안전하게 전송 가능
    router.sendRid(sourceRid, Buffer.from("Welcome"));

    // DEALER 연결 및 초기 메시지 전송
    const dealer = new zlink.DealerSocket(ctx);
    dealer.setRoutingId(Buffer.from("X"));
    dealer.connect(endpoint);
    dealer.send(Buffer.from("Hello"));
    ```

=== "C#/.NET"

    ```csharp
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    var (sourceRid, parts) = router.Recv();
    // sourceRid = "X" — 이제 "X"로 안전하게 전송 가능
    router.SendRid(sourceRid, new Message("Welcome"u8));

    // DEALER 연결 및 초기 메시지 전송
    using var dealer = new DealerSocket(ctx);
    dealer.SetRoutingId("X"u8);
    dealer.Connect(endpoint);
    dealer.Send(new Message("Hello"u8));
    ```

=== "Rust"

    ```rust
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    let (source_rid, parts) = router.recv()?;
    // source_rid = "X" — 이제 "X"로 안전하게 전송 가능
    router.send_rid(&source_rid, &zlink::Message::from("Welcome"))?;

    // DEALER 연결 및 초기 메시지 전송
    let dealer = ctx.dealer_socket()?;
    dealer.set_routing_id("X")?;
    dealer.connect(&endpoint)?;
    dealer.send(&zlink::Message::from("Hello"))?;
    ```

=== "Go"

    ```go
    // ROUTER: DEALER의 초기 메시지로 연결 확인
    source_rid, parts, err := router.Recv()
    if err != nil { panic(err) }
    // source_rid = "X" — 이제 "X"로 안전하게 전송 가능
    router.SendTo(source_rid, zlink.NewMessage([]byte("Welcome")))

    // DEALER 연결 및 초기 메시지 전송
    dealer, err := ctx.DealerSocket()
    if err != nil { panic(err) }
    dealer.SetRoutingId("X")
    dealer.Connect(endpoint)
    dealer.Send(zlink.NewMessage([]byte("Hello")))
    ```

> 참고: `core/tests/test_router_mandatory.cpp` — DEALER 연결 → 메시지 → ROUTER 응답

### 패턴 8: 다중 Transport

같은 ROUTER에 다양한 transport로 연결 가능. routing_id로 통합 관리.

```
  ┌──────────┐                       ┌──────────┐
  │ DEALER 1 │── tcp://  ───────────►│          │
  └──────────┘                       │          │
  ┌──────────┐                       │  ROUTER  │
  │ DEALER 2 │── ipc://  ───────────►│          │
  └──────────┘                       │          │
  ┌──────────┐                       │          │
  │ DEALER 3 │── inproc://  ────────►│          │
  └──────────┘                       └──────────┘

  transport가 달라도 routing_id로 동일하게 식별
```

=== "C"

    ```c
    void *router = zlink_socket(ctx, ZLINK_ROUTER);

    /* TCP */
    zlink_bind(router, "tcp://127.0.0.1:5558");

    /* IPC (Linux/macOS) */
    zlink_bind(router, "ipc:///tmp/router.ipc");

    /* inproc (동일 프로세스) */
    zlink_bind(router, "inproc://router");

    /* 각 transport의 DEALER가 연결 — ROUTER는 routing_id로 통합 관리 */
    ```

=== "C++"

    ```cpp
    zlink::router_socket_t router(ctx);
    router.bind("tcp://127.0.0.1:5558");
    router.bind("ipc:///tmp/router.ipc");     // IPC (Linux/macOS)
    router.bind("inproc://router");            // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "Java"

    ```java
    RouterSocket router = new RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:5558");
    router.bind("ipc:///tmp/router.ipc");     // IPC (Linux/macOS)
    router.bind("inproc://router");            // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "Python"

    ```python
    router = zlink.RouterSocket(ctx)
    router.bind("tcp://127.0.0.1:5558")
    router.bind("ipc:///tmp/router.ipc")      # IPC (Linux/macOS)
    router.bind("inproc://router")             # inproc (동일 프로세스)
    # 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "Node/TypeScript"

    ```typescript
    const router = new zlink.RouterSocket(ctx);
    router.bind("tcp://127.0.0.1:5558");
    router.bind("ipc:///tmp/router.ipc");     // IPC (Linux/macOS)
    router.bind("inproc://router");            // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "C#/.NET"

    ```csharp
    using var router = new RouterSocket(ctx);
    router.Bind("tcp://127.0.0.1:5558");
    router.Bind("ipc:///tmp/router.ipc");     // IPC (Linux/macOS)
    router.Bind("inproc://router");            // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "Rust"

    ```rust
    let router = ctx.router_socket()?;
    router.bind("tcp://127.0.0.1:5558")?;
    router.bind("ipc:///tmp/router.ipc")?;    // IPC (Linux/macOS)
    router.bind("inproc://router")?;           // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

=== "Go"

    ```go
    router, err := ctx.RouterSocket()
    if err != nil { panic(err) }
    router.Bind("tcp://127.0.0.1:5558")
    router.Bind("ipc:///tmp/router.ipc")  // IPC (Linux/macOS)
    router.Bind("inproc://router")  // inproc (동일 프로세스)
    // 각 transport의 DEALER가 연결 — routing_id로 통합 관리
    ```

> 참고: `core/tests/test_router_multiple_dealers.cpp` — TCP/IPC/inproc 테스트

## 6. 주의사항

### 기본 드롭 동작

`ROUTER_MANDATORY`를 설정하지 않으면, 존재하지 않는 routing_id로 전송 시 메시지가 **조용히 드롭**된다. 프로덕션에서는 `ROUTER_MANDATORY` 활성화를 권장한다.

### 재연결 시 routing_id 변경

DEALER가 재연결하면 자동 생성된 routing_id가 변경될 수 있다. 안정적인 통신을 위해 명시적 routing_id 설정을 권장한다.

=== "C"

    ```c
    /* 명시적 routing_id — 재연결 시에도 동일 */
    zlink_set_routing_id(dealer, "stable-id", 9);
    ```

=== "C++"

    ```cpp
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.set_routing_id("stable-id");
    ```

=== "Java"

    ```java
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.setRoutingId("stable-id");
    ```

=== "Python"

    ```python
    # 명시적 routing_id — 재연결 시에도 동일
    dealer.set_routing_id(b"stable-id")
    ```

=== "Node/TypeScript"

    ```typescript
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.setRoutingId(Buffer.from("stable-id"));
    ```

=== "C#/.NET"

    ```csharp
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.SetRoutingId("stable-id"u8);
    ```

=== "Rust"

    ```rust
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.set_routing_id("stable-id")?;
    ```

=== "Go"

    ```go
    // 명시적 routing_id — 재연결 시에도 동일
    dealer.SetRoutingId("stable-id")
    ```

### routing_id 충돌

같은 routing_id를 가진 두 DEALER가 동시에 연결되면, 기본적으로 두 번째 연결이 거부된다. `ROUTER_HANDOVER`를 활성화하면 기존 연결을 대체한다.

> routing_id의 상세 개념은 [08-routing-id.ko.md](08-routing-id.ko.md)를 참고.

---
[← DEALER](03-3-dealer.ko.md) | [STREAM →](03-5-stream.ko.md)
