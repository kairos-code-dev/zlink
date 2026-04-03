# PAIR Socket

## 1. Overview

The PAIR socket forms an exclusive 1:1 bidirectional connection with exactly one peer. If a second peer connects, the first connection is dropped.

**Key characteristics:**
- Only a single pipe is allowed (1:1 exclusive)
- Bidirectional free messaging (send/recv order does not matter)
- The simplest socket type

**Valid socket combinations:** PAIR ↔ PAIR

```
┌────────┐              ┌────────┐
│ PAIR A │◄────────────►│ PAIR B │
└────────┘   양방향     └────────┘
```

## 2. Basic Usage

### Message Exchange

A complete PAIR example: create a context, bind/connect two sockets,
send a message, receive it, and clean up.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://*:5555");

        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "tcp://127.0.0.1:5555");

        /* Send from client to server */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 10);
        memcpy(zlink_msg_data(&msg), "hello-pair", 10);
        zlink_send(client, &msg, 1, 0);

        /* Receive on server */
        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(server, &rid, &parts, &count, 0);
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        /* Send reply back (bidirectional) */
        zlink_msg_t reply;
        zlink_msg_init_size(&reply, 5);
        memcpy(zlink_msg_data(&reply), "World", 5);
        zlink_send(server, &reply, 1, 0);

        zlink_recv(client, &rid, &parts, &count, 0);
        printf("Reply: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(client);
        zlink_close(server);
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

        zlink::pair_socket_t server(ctx);
        server.bind("tcp://*:5555");

        zlink::pair_socket_t client(ctx);
        client.connect("tcp://127.0.0.1:5555");

        // Send from client to server
        client.send("hello-pair");

        // Receive on server
        auto [rid, parts] = server.recv();
        std::cout << "Received: " << parts[0].str() << std::endl;

        // Send reply back (bidirectional)
        server.send("World");

        auto [rid2, reply] = client.recv();
        std::cout << "Reply: " << reply[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PairExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            PairSocket server = new PairSocket(ctx);
            server.bind("tcp://*:5555");

            PairSocket client = new PairSocket(ctx);
            client.connect("tcp://127.0.0.1:5555");

            // Send from client to server
            client.send("hello-pair");

            // Receive on server
            Message msg = server.recv();
            System.out.println("Received: " + msg.partAsString(0));

            // Send reply back (bidirectional)
            server.send("World");

            Message reply = client.recv();
            System.out.println("Reply: " + reply.partAsString(0));

            client.close();
            server.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")

    # Send from client to server
    client.send(b"hello-pair")

    # Receive on server
    source_rid, parts = server.recv()
    print(f"Received: {parts[0].decode()}")

    # Send reply back (bidirectional)
    server.send(b"World")

    source_rid, reply = client.recv()
    print(f"Reply: {reply[0].decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.bind('tcp://*:5555');

    const client = new zlink.PairSocket(ctx);
    client.connect('tcp://127.0.0.1:5555');

    // Send from client to server
    client.send(Buffer.from('hello-pair'));

    // Receive on server
    const [rid, parts] = server.receive();
    console.log(`Received: ${parts[0].toString()}`);

    // Send reply back (bidirectional)
    server.send(Buffer.from('World'));

    const [rid2, reply] = client.receive();
    console.log(`Reply: ${reply[0].toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");

    // Send from client to server
    client.Send("hello-pair");

    // Receive on server
    var (rid, parts) = server.Receive();
    Console.WriteLine($"Received: {parts[0].GetString()}");

    // Send reply back (bidirectional)
    server.Send("World");

    var (rid2, reply) = client.Receive();
    Console.WriteLine($"Reply: {reply[0].GetString()}");

    client.Close();
    server.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let server = ctx.pair_socket();
        server.bind("tcp://*:5555")?;

        let client = ctx.pair_socket();
        client.connect("tcp://127.0.0.1:5555")?;

        // Send from client to server
        client.send(b"hello-pair")?;

        // Receive on server
        let (rid, parts) = server.recv()?;
        println!("Received: {}", String::from_utf8_lossy(parts[0].data()));

        // Send reply back (bidirectional)
        server.send(b"World")?;

        let (rid2, reply) = client.recv()?;
        println!("Reply: {}", String::from_utf8_lossy(reply[0].data()));

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

        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("tcp://*:5555")

        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect("tcp://127.0.0.1:5555")

        // Send from client to server
        client.Send(zlink.NewMessage([]byte("hello-pair")))

        // Receive on server
        received, err := server.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Received: %s\n", received.Parts[0].Data())
        received.Close()

        // Send reply back (bidirectional)
        server.Send(zlink.NewMessage([]byte("World")))

        reply, err := client.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Reply: %s\n", reply.Parts[0].Data())
        reply.Close()
    }
    ```

??? example "Full Sample Code -- Recv"

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

### Callback Mode

Install the callback with `zlink_recv_handler()` to make a one-way
transition from recv mode to callback mode. Incoming messages are then
dispatched automatically through that callback.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_message(const zlink_routing_id_t *source_rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        printf("Callback received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://*:5555");

        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "tcp://127.0.0.1:5555");

        /* Transition server to callback mode */
        zlink_recv_handler(server, on_message, NULL);

        /* Send from client to server */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 10);
        memcpy(zlink_msg_data(&msg), "hello-pair", 10);
        zlink_send(client, &msg, 1, 0);

        zlink_msleep(200);  /* let callback fire */

        zlink_close(client);
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    #include <zlink/socket.hpp>
    #include <iostream>
    #include <thread>
    #include <chrono>

    int main()
    {
        zlink::context_t ctx;

        zlink::pair_socket_t server(ctx);
        server.bind("tcp://*:5555");

        zlink::pair_socket_t client(ctx);
        client.connect("tcp://127.0.0.1:5555");

        // Transition server to callback mode
        server.recv_handler([](const zlink::routing_id_t& source_rid,
                               std::span<zlink::msg> parts) {
            std::cout << "Callback received: " << parts[0].str() << std::endl;
        });

        // Send from client to server
        client.send("hello-pair");

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PairCallbackExample {
        public static void main(String[] args) throws Exception {
            Context ctx = new Context();

            PairSocket server = new PairSocket(ctx);
            server.bind("tcp://*:5555");

            PairSocket client = new PairSocket(ctx);
            client.connect("tcp://127.0.0.1:5555");

            // Transition server to callback mode
            server.onReceive((sourceRid, parts) -> {
                System.out.println("Callback received: "
                    + parts[0].dataAsString());
            });

            // Send from client to server
            client.send("hello-pair");

            Thread.sleep(200);

            client.close();
            server.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink
    import time

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    client = zlink.PairSocket(ctx)
    client.connect("tcp://127.0.0.1:5555")

    # Transition server to callback mode
    def on_message(source_rid, parts):
        print(f"Callback received: {parts[0].decode()}")

    server.on_receive(on_message)

    # Send from client to server
    client.send(b"hello-pair")

    time.sleep(0.2)

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.bind('tcp://*:5555');

    const client = new zlink.PairSocket(ctx);
    client.connect('tcp://127.0.0.1:5555');

    // Transition server to callback mode
    server.recvHandler((sourceRid, parts) => {
        console.log(`Callback received: ${parts[0].toString()}`);
    });

    // Send from client to server
    client.send(Buffer.from('hello-pair'));

    await new Promise(r => setTimeout(r, 200));

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    var client = new PairSocket(ctx);
    client.Connect("tcp://127.0.0.1:5555");

    // Transition server to callback mode
    server.RecvHandler((sourceRid, parts) => {
        Console.WriteLine($"Callback received: {parts[0].GetString()}");
    });

    // Send from client to server
    client.Send("hello-pair");

    Thread.Sleep(200);

    client.Close();
    server.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;
    use std::thread;
    use std::time::Duration;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let server = ctx.pair_socket();
        server.bind("tcp://*:5555")?;

        let client = ctx.pair_socket();
        client.connect("tcp://127.0.0.1:5555")?;

        // Transition server to callback mode
        server.on_receive(|source_rid, parts| {
            println!("Callback received: {}",
                     String::from_utf8_lossy(parts[0].data()));
        });

        // Send from client to server
        client.send(b"hello-pair")?;

        thread::sleep(Duration::from_millis(200));

        Ok(())
    }
    ```

=== "Go"

    ```go
    package main

    import (
        "fmt"
        "log"
        "time"
        "github.com/kairos-code-dev/zlink-go"
    )

    func main() {
        ctx, err := zlink.NewContext()
        if err != nil { log.Fatal(err) }
        defer ctx.Close()

        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("tcp://*:5555")

        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect("tcp://127.0.0.1:5555")

        // Transition server to callback mode
        server.OnMessage(func(sourceRid zlink.RoutingID, parts []zlink.Message) {
            fmt.Printf("Callback received: %s\n", parts[0].Data())
        })

        // Send from client to server
        client.Send(zlink.NewMessage([]byte("hello-pair")))

        time.Sleep(200 * time.Millisecond)
    }
    ```

??? example "Full Sample Code -- Callback"

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

### Sending Multipart Data

Multipart data is sent as a parts array in a single `zlink_send` call.
Both frames arrive together in one `zlink_recv()` call on the receiver.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *sender = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(sender, "tcp://*:5556");

        void *receiver = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(receiver, "tcp://127.0.0.1:5556");

        /* Send two frames as one multipart message */
        zlink_msg_t parts[2];
        zlink_msg_init_size(&parts[0], 6);
        memcpy(zlink_msg_data(&parts[0]), "header", 6);
        zlink_msg_init_size(&parts[1], 7);
        memcpy(zlink_msg_data(&parts[1]), "payload", 7);
        zlink_send(sender, parts, 2, 0);

        /* Receive both frames in one call */
        zlink_routing_id_t rid;
        zlink_msg_t *recv_parts;
        size_t count;
        zlink_recv(receiver, &rid, &recv_parts, &count, 0);
        printf("Frame 0: %.*s\n",
               (int)zlink_msg_size(&recv_parts[0]),
               (char *)zlink_msg_data(&recv_parts[0]));
        printf("Frame 1: %.*s\n",
               (int)zlink_msg_size(&recv_parts[1]),
               (char *)zlink_msg_data(&recv_parts[1]));
        zlink_multipart_close(recv_parts, count);

        zlink_close(receiver);
        zlink_close(sender);
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

        zlink::pair_socket_t sender(ctx);
        sender.bind("tcp://*:5556");

        zlink::pair_socket_t receiver(ctx);
        receiver.connect("tcp://127.0.0.1:5556");

        // Send two frames as one multipart message
        sender.send({"header", "payload"});

        // Receive both frames in one call
        auto [rid, parts] = receiver.recv();
        std::cout << "Frame 0: " << parts[0].str() << std::endl;
        std::cout << "Frame 1: " << parts[1].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PairMultipartExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            PairSocket sender = new PairSocket(ctx);
            sender.bind("tcp://*:5556");

            PairSocket receiver = new PairSocket(ctx);
            receiver.connect("tcp://127.0.0.1:5556");

            // Send two frames as one multipart message
            sender.send("header", "payload");

            // Receive both frames in one call
            Message msg = receiver.recv();
            System.out.println("Frame 0: " + msg.partAsString(0));
            System.out.println("Frame 1: " + msg.partAsString(1));

            receiver.close();
            sender.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    sender = zlink.PairSocket(ctx)
    sender.bind("tcp://*:5556")

    receiver = zlink.PairSocket(ctx)
    receiver.connect("tcp://127.0.0.1:5556")

    # Send two frames as one multipart message
    sender.send([b"header", b"payload"])

    # Receive both frames in one call
    source_rid, parts = receiver.recv()
    print(f"Frame 0: {parts[0].decode()}")
    print(f"Frame 1: {parts[1].decode()}")

    receiver.close()
    sender.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const sender = new zlink.PairSocket(ctx);
    sender.bind('tcp://*:5556');

    const receiver = new zlink.PairSocket(ctx);
    receiver.connect('tcp://127.0.0.1:5556');

    // Send two frames as one multipart message
    sender.send([Buffer.from('header'), Buffer.from('payload')]);

    // Receive both frames in one call
    const [rid, parts] = receiver.receive();
    console.log(`Frame 0: ${parts[0].toString()}`);
    console.log(`Frame 1: ${parts[1].toString()}`);

    receiver.close();
    sender.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var sender = new PairSocket(ctx);
    sender.Bind("tcp://*:5556");

    var receiver = new PairSocket(ctx);
    receiver.Connect("tcp://127.0.0.1:5556");

    // Send two frames as one multipart message
    sender.Send("header", "payload");

    // Receive both frames in one call
    var (rid, parts) = receiver.Receive();
    Console.WriteLine($"Frame 0: {parts[0].GetString()}");
    Console.WriteLine($"Frame 1: {parts[1].GetString()}");

    receiver.Close();
    sender.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let sender = ctx.pair_socket();
        sender.bind("tcp://*:5556")?;

        let receiver = ctx.pair_socket();
        receiver.connect("tcp://127.0.0.1:5556")?;

        // Send two frames as one multipart message
        sender.send(&[b"header", b"payload"])?;

        // Receive both frames in one call
        let (rid, parts) = receiver.recv()?;
        println!("Frame 0: {}", String::from_utf8_lossy(parts[0].data()));
        println!("Frame 1: {}", String::from_utf8_lossy(parts[1].data()));

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

        sender, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer sender.Close()
        sender.Bind("tcp://*:5556")

        receiver, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer receiver.Close()
        receiver.Connect("tcp://127.0.0.1:5556")

        // Send two frames as one multipart message
        sender.SendMultipart([]zlink.Message{
            zlink.NewMessage([]byte("header")),
            zlink.NewMessage([]byte("payload")),
        })

        // Receive both frames in one call
        received, err := receiver.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Frame 0: %s\n", received.Parts[0].Data())
        fmt.Printf("Frame 1: %s\n", received.Parts[1].Data())
        received.Close()
    }
    ```

> Reference: `core/tests/test_pair_inproc.cpp` -- `test_zlink_send_multipart()` test

> When HWM is reached, `zlink_send()` blocks (default) or returns
> `EAGAIN` with `ZLINK_DONTWAIT`. For advanced backpressure patterns,
> see [Performance Guide](10-performance.md).

## 3. Message Format

PAIR socket message frames contain **application data only**.

```
Single frame:     [data]
Multipart frame:  [frame1][frame2]...[frameN]
```

> For `source_rid` and the common receive interface, see
> [Socket Patterns Overview](03-0-socket-patterns.md#7-common-receive-interface).

For a complete multipart send/receive example, see
[Sending Multipart Data](#sending-multipart-data) above.

## 4. Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Maximum number of messages in the send queue |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Maximum number of messages in the receive queue |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time for unsent messages on close (ms), -1=infinite |
| `ZLINK_OPT_SNDTIMEO` | int | -1 | Send timeout (ms), -1=infinite |
| `ZLINK_OPT_RCVTIMEO` | int | -1 | Receive timeout (ms), -1=infinite |

=== "C"

    ```c
    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

    int linger = 0;  /* return immediately on close */
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Java"

    ```java
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Python"

    ```python
    socket.set_option(ZLINK_OPT_SNDHWM, 5000)

    socket.set_option(ZLINK_OPT_LINGER, 0)  # return immediately on close
    ```

=== "Node/TypeScript"

    ```typescript
    socket.setOption(ZLINK_OPT_SNDHWM, 5000);

    socket.setOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(ZLINK_OPT_SNDHWM, 5000);

    socket.SetOption(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Rust"

    ```rust
    socket.set_option(ZLINK_OPT_SNDHWM, 5000);

    socket.set_option(ZLINK_OPT_LINGER, 0);  // return immediately on close
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionSndHWM, 5000)

    socket.SetOption(zlink.OptionLinger, 0)  // return immediately on close
    ```

## 5. Usage Patterns

### Pattern 1: Inter-thread Signaling (inproc)

The most common PAIR use case. Zero-copy communication between threads via the inproc transport. The worker thread signals completion to the main thread.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        /* Main thread side */
        void *main_sock = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(main_sock, "inproc://signal");

        /* Worker thread side (same context) */
        void *worker_sock = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(worker_sock, "inproc://signal");

        /* Worker sends completion signal */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 4);
        memcpy(zlink_msg_data(&msg), "DONE", 4);
        zlink_send(worker_sock, &msg, 1, 0);

        /* Main thread receives signal */
        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(main_sock, &rid, &parts, &count, 0);
        printf("Signal: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(worker_sock);
        zlink_close(main_sock);
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

        // Main thread side
        zlink::pair_socket_t main_sock(ctx);
        main_sock.bind("inproc://signal");

        // Worker thread side (same context)
        zlink::pair_socket_t worker_sock(ctx);
        worker_sock.connect("inproc://signal");

        // Worker sends completion signal
        worker_sock.send("DONE");

        // Main thread receives signal
        auto [rid, parts] = main_sock.recv();
        std::cout << "Signal: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class InprocSignalExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            // Main thread side
            PairSocket mainSock = new PairSocket(ctx);
            mainSock.bind("inproc://signal");

            // Worker thread side (same context)
            PairSocket workerSock = new PairSocket(ctx);
            workerSock.connect("inproc://signal");

            // Worker sends completion signal
            workerSock.send("DONE");

            // Main thread receives signal
            Message msg = mainSock.recv();
            System.out.println("Signal: " + msg.partAsString(0));

            workerSock.close();
            mainSock.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    # Main thread side
    main_sock = zlink.PairSocket(ctx)
    main_sock.bind("inproc://signal")

    # Worker thread side (same context)
    worker_sock = zlink.PairSocket(ctx)
    worker_sock.connect("inproc://signal")

    # Worker sends completion signal
    worker_sock.send(b"DONE")

    # Main thread receives signal
    source_rid, parts = main_sock.recv()
    print(f"Signal: {parts[0].decode()}")

    worker_sock.close()
    main_sock.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    // Main thread side
    const mainSock = new zlink.PairSocket(ctx);
    mainSock.bind('inproc://signal');

    // Worker thread side (same context)
    const workerSock = new zlink.PairSocket(ctx);
    workerSock.connect('inproc://signal');

    // Worker sends completion signal
    workerSock.send(Buffer.from('DONE'));

    // Main thread receives signal
    const [rid, parts] = mainSock.receive();
    console.log(`Signal: ${parts[0].toString()}`);

    workerSock.close();
    mainSock.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    // Main thread side
    var mainSock = new PairSocket(ctx);
    mainSock.Bind("inproc://signal");

    // Worker thread side (same context)
    var workerSock = new PairSocket(ctx);
    workerSock.Connect("inproc://signal");

    // Worker sends completion signal
    workerSock.Send("DONE");

    // Main thread receives signal
    var (rid, parts) = mainSock.Receive();
    Console.WriteLine($"Signal: {parts[0].GetString()}");

    workerSock.Close();
    mainSock.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        // Main thread side
        let main_sock = ctx.pair_socket();
        main_sock.bind("inproc://signal")?;

        // Worker thread side (same context)
        let worker_sock = ctx.pair_socket();
        worker_sock.connect("inproc://signal")?;

        // Worker sends completion signal
        worker_sock.send(b"DONE")?;

        // Main thread receives signal
        let (rid, parts) = main_sock.recv()?;
        println!("Signal: {}", String::from_utf8_lossy(parts[0].data()));

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

        // Main thread side
        mainSock, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer mainSock.Close()
        mainSock.Bind("inproc://signal")

        // Worker thread side (same context)
        workerSock, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer workerSock.Close()
        workerSock.Connect("inproc://signal")

        // Worker sends completion signal
        workerSock.Send(zlink.NewMessage([]byte("DONE")))

        // Main thread receives signal
        received, err := mainSock.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Signal: %s\n", received.Parts[0].Data())
        received.Close()
    }
    ```

> Reference: `core/tests/test_pair_inproc.cpp` -- bind -> connect -> bounce pattern

### Pattern 2: TCP Communication with Wildcard Port

1:1 communication over the network. Wildcard bind lets the OS assign a free port, then the client queries the assigned endpoint to connect.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        /* Server: wildcard port */
        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://127.0.0.1:*");

        /* Query the assigned endpoint */
        char endpoint[256];
        size_t len = sizeof(endpoint);
        zlink_get_option(server, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len);
        printf("Server bound to: %s\n", endpoint);

        /* Client: connect using the queried endpoint */
        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, endpoint);

        /* Exchange a message */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 4);
        memcpy(zlink_msg_data(&msg), "ping", 4);
        zlink_send(client, &msg, 1, 0);

        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(server, &rid, &parts, &count, 0);
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(client);
        zlink_close(server);
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

        // Server: wildcard port
        zlink::pair_socket_t server(ctx);
        server.bind("tcp://127.0.0.1:*");

        // Query the assigned endpoint
        auto endpoint = server.get_option<std::string>(ZLINK_OPT_LAST_ENDPOINT);
        std::cout << "Server bound to: " << endpoint << std::endl;

        // Client: connect using the queried endpoint
        zlink::pair_socket_t client(ctx);
        client.connect(endpoint);

        // Exchange a message
        client.send("ping");

        auto [rid, parts] = server.recv();
        std::cout << "Received: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class TcpWildcardExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            // Server: wildcard port
            PairSocket server = new PairSocket(ctx);
            server.bind("tcp://127.0.0.1:*");

            // Query the assigned endpoint
            String endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);
            System.out.println("Server bound to: " + endpoint);

            // Client: connect using the queried endpoint
            PairSocket client = new PairSocket(ctx);
            client.connect(endpoint);

            // Exchange a message
            client.send("ping");

            Message msg = server.recv();
            System.out.println("Received: " + msg.partAsString(0));

            client.close();
            server.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    # Server: wildcard port
    server = zlink.PairSocket(ctx)
    server.bind("tcp://127.0.0.1:*")

    # Query the assigned endpoint
    endpoint = server.get_option(ZLINK_OPT_LAST_ENDPOINT)
    print(f"Server bound to: {endpoint}")

    # Client: connect using the queried endpoint
    client = zlink.PairSocket(ctx)
    client.connect(endpoint)

    # Exchange a message
    client.send(b"ping")

    source_rid, parts = server.recv()
    print(f"Received: {parts[0].decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    // Server: wildcard port
    const server = new zlink.PairSocket(ctx);
    server.bind('tcp://127.0.0.1:*');

    // Query the assigned endpoint
    const endpoint = server.getOption(ZLINK_OPT_LAST_ENDPOINT);
    console.log(`Server bound to: ${endpoint}`);

    // Client: connect using the queried endpoint
    const client = new zlink.PairSocket(ctx);
    client.connect(endpoint);

    // Exchange a message
    client.send(Buffer.from('ping'));

    const [rid, parts] = server.receive();
    console.log(`Received: ${parts[0].toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    // Server: wildcard port
    var server = new PairSocket(ctx);
    server.Bind("tcp://127.0.0.1:*");

    // Query the assigned endpoint
    var endpoint = server.GetOption(ZLINK_OPT_LAST_ENDPOINT);
    Console.WriteLine($"Server bound to: {endpoint}");

    // Client: connect using the queried endpoint
    var client = new PairSocket(ctx);
    client.Connect(endpoint);

    // Exchange a message
    client.Send("ping");

    var (rid, parts) = server.Receive();
    Console.WriteLine($"Received: {parts[0].GetString()}");

    client.Close();
    server.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        // Server: wildcard port
        let server = ctx.pair_socket();
        server.bind("tcp://127.0.0.1:*")?;

        // Query the assigned endpoint
        let endpoint = server.get_option::<String>(ZLINK_OPT_LAST_ENDPOINT);
        println!("Server bound to: {}", endpoint);

        // Client: connect using the queried endpoint
        let client = ctx.pair_socket();
        client.connect(&endpoint)?;

        // Exchange a message
        client.send(b"ping")?;

        let (rid, parts) = server.recv()?;
        println!("Received: {}", String::from_utf8_lossy(parts[0].data()));

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

        // Server: wildcard port
        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("tcp://127.0.0.1:*")

        // Query the assigned endpoint
        status, _ := server.StatusSnapshot()
        endpoint := status.LocalEndpoint
        fmt.Printf("Server bound to: %s\n", endpoint)

        // Client: connect using the queried endpoint
        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect(endpoint)

        // Exchange a message
        client.Send(zlink.NewMessage([]byte("ping")))

        received, err := server.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Received: %s\n", received.Parts[0].Data())
        received.Close()
    }
    ```

> Reference: `core/tests/test_pair_tcp.cpp` -- `bind_loopback_ipv4()` + wildcard bind

### Pattern 3: Connection by DNS Name

Connect using a hostname instead of an IP address. The server binds on a fixed port and the client resolves `localhost` to connect.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "tcp://*:5555");

        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "tcp://localhost:5555");

        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 4);
        memcpy(zlink_msg_data(&msg), "ping", 4);
        zlink_send(client, &msg, 1, 0);

        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(server, &rid, &parts, &count, 0);
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(client);
        zlink_close(server);
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

        zlink::pair_socket_t server(ctx);
        server.bind("tcp://*:5555");

        zlink::pair_socket_t client(ctx);
        client.connect("tcp://localhost:5555");

        client.send("ping");

        auto [rid, parts] = server.recv();
        std::cout << "Received: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class DnsConnectExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            PairSocket server = new PairSocket(ctx);
            server.bind("tcp://*:5555");

            PairSocket client = new PairSocket(ctx);
            client.connect("tcp://localhost:5555");

            client.send("ping");

            Message msg = server.recv();
            System.out.println("Received: " + msg.partAsString(0));

            client.close();
            server.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.bind("tcp://*:5555")

    client = zlink.PairSocket(ctx)
    client.connect("tcp://localhost:5555")

    client.send(b"ping")

    source_rid, parts = server.recv()
    print(f"Received: {parts[0].decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.bind('tcp://*:5555');

    const client = new zlink.PairSocket(ctx);
    client.connect('tcp://localhost:5555');

    client.send(Buffer.from('ping'));

    const [rid, parts] = server.receive();
    console.log(`Received: ${parts[0].toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var server = new PairSocket(ctx);
    server.Bind("tcp://*:5555");

    var client = new PairSocket(ctx);
    client.Connect("tcp://localhost:5555");

    client.Send("ping");

    var (rid, parts) = server.Receive();
    Console.WriteLine($"Received: {parts[0].GetString()}");

    client.Close();
    server.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let server = ctx.pair_socket();
        server.bind("tcp://*:5555")?;

        let client = ctx.pair_socket();
        client.connect("tcp://localhost:5555")?;

        client.send(b"ping")?;

        let (rid, parts) = server.recv()?;
        println!("Received: {}", String::from_utf8_lossy(parts[0].data()));

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

        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("tcp://*:5555")

        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect("tcp://localhost:5555")

        client.Send(zlink.NewMessage([]byte("ping")))

        received, err := server.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Received: %s\n", received.Parts[0].Data())
        received.Close()
    }
    ```

> Reference: `core/tests/test_pair_tcp.cpp` -- `test_pair_tcp_connect_by_name()`

### Pattern 4: IPC Communication

Inter-process communication on the same machine (Linux/macOS).

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *server = zlink_socket(ctx, ZLINK_PAIR);
        zlink_bind(server, "ipc:///tmp/myapp.ipc");

        void *client = zlink_socket(ctx, ZLINK_PAIR);
        zlink_connect(client, "ipc:///tmp/myapp.ipc");

        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 8);
        memcpy(zlink_msg_data(&msg), "ipc-ping", 8);
        zlink_send(client, &msg, 1, 0);

        zlink_routing_id_t rid;
        zlink_msg_t *parts;
        size_t count;
        zlink_recv(server, &rid, &parts, &count, 0);
        printf("Received: %.*s\n",
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(client);
        zlink_close(server);
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

        zlink::pair_socket_t server(ctx);
        server.bind("ipc:///tmp/myapp.ipc");

        zlink::pair_socket_t client(ctx);
        client.connect("ipc:///tmp/myapp.ipc");

        client.send("ipc-ping");

        auto [rid, parts] = server.recv();
        std::cout << "Received: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class IpcExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            PairSocket server = new PairSocket(ctx);
            server.bind("ipc:///tmp/myapp.ipc");

            PairSocket client = new PairSocket(ctx);
            client.connect("ipc:///tmp/myapp.ipc");

            client.send("ipc-ping");

            Message msg = server.recv();
            System.out.println("Received: " + msg.partAsString(0));

            client.close();
            server.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    server = zlink.PairSocket(ctx)
    server.bind("ipc:///tmp/myapp.ipc")

    client = zlink.PairSocket(ctx)
    client.connect("ipc:///tmp/myapp.ipc")

    client.send(b"ipc-ping")

    source_rid, parts = server.recv()
    print(f"Received: {parts[0].decode()}")

    client.close()
    server.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const server = new zlink.PairSocket(ctx);
    server.bind('ipc:///tmp/myapp.ipc');

    const client = new zlink.PairSocket(ctx);
    client.connect('ipc:///tmp/myapp.ipc');

    client.send(Buffer.from('ipc-ping'));

    const [rid, parts] = server.receive();
    console.log(`Received: ${parts[0].toString()}`);

    client.close();
    server.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var server = new PairSocket(ctx);
    server.Bind("ipc:///tmp/myapp.ipc");

    var client = new PairSocket(ctx);
    client.Connect("ipc:///tmp/myapp.ipc");

    client.Send("ipc-ping");

    var (rid, parts) = server.Receive();
    Console.WriteLine($"Received: {parts[0].GetString()}");

    client.Close();
    server.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let server = ctx.pair_socket();
        server.bind("ipc:///tmp/myapp.ipc")?;

        let client = ctx.pair_socket();
        client.connect("ipc:///tmp/myapp.ipc")?;

        client.send(b"ipc-ping")?;

        let (rid, parts) = server.recv()?;
        println!("Received: {}", String::from_utf8_lossy(parts[0].data()));

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

        server, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer server.Close()
        server.Bind("ipc:///tmp/myapp.ipc")

        client, err := ctx.PairSocket()
        if err != nil { log.Fatal(err) }
        defer client.Close()
        client.Connect("ipc:///tmp/myapp.ipc")

        client.Send(zlink.NewMessage([]byte("ipc-ping")))

        received, err := server.Recv()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Received: %s\n", received.Parts[0].Data())
        received.Close()
    }
    ```

> Reference: `core/tests/test_pair_ipc.cpp` -- includes IPC path length validation

## 6. Caveats

### Only a Single Peer Allowed

A PAIR socket maintains only one connection. If a second peer connects, the first connection is dropped.

```
 Allowed:  PAIR A ↔ PAIR B      (1:1)
 Invalid:  PAIR A ← PAIR B      (N:1 attempt drops existing connection)
               ← PAIR C
```

Use DEALER/ROUTER if N:1 communication is needed.

### inproc bind Order

With the inproc transport, **bind must be called before connect**.

=== "C"

    ```c
    /* Correct order */
    zlink_bind(a, "inproc://signal");     /* 1. bind first */
    zlink_connect(b, "inproc://signal");  /* 2. connect */

    /* Wrong order -- fails */
    zlink_connect(b, "inproc://signal");  /* fails because bind has not been called yet */
    zlink_bind(a, "inproc://signal");
    ```

=== "C++"

    ```cpp
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Java"

    ```java
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Python"

    ```python
    # Correct order
    a.bind("inproc://signal")      # 1. bind first
    b.connect("inproc://signal")   # 2. connect

    # Wrong order -- fails
    b.connect("inproc://signal")   # fails because bind has not been called yet
    a.bind("inproc://signal")
    ```

=== "Node/TypeScript"

    ```typescript
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "C#/.NET"

    ```csharp
    // Correct order
    a.Bind("inproc://signal");     // 1. bind first
    b.Connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.Connect("inproc://signal");  // fails because bind has not been called yet
    a.Bind("inproc://signal");
    ```

=== "Rust"

    ```rust
    // Correct order
    a.bind("inproc://signal");     // 1. bind first
    b.connect("inproc://signal");  // 2. connect

    // Wrong order -- fails
    b.connect("inproc://signal");  // fails because bind has not been called yet
    a.bind("inproc://signal");
    ```

=== "Go"

    ```go
    // Correct order
    a.Bind("inproc://signal")  // 1. bind first
    b.Connect("inproc://signal")  // 2. connect

    // Wrong order -- fails
    b.Connect("inproc://signal")  // fails because bind has not been called yet
    a.Bind("inproc://signal")
    ```

### IPC Path Length

The file path of an IPC endpoint cannot exceed the system limit (typically 108 characters).

=== "C"

    ```c
    /* Path too long → ENAMETOOLONG error */
    zlink_bind(socket, "ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C++"

    ```cpp
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Java"

    ```java
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Python"

    ```python
    # Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

=== "Node/TypeScript"

    ```typescript
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "C#/.NET"

    ```csharp
    // Path too long → ENAMETOOLONG error
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Rust"

    ```rust
    // Path too long → ENAMETOOLONG error
    socket.bind("ipc:///very/long/path/.../endpoint.ipc");
    ```

=== "Go"

    ```go
    // Path too long → ENAMETOOLONG error
    socket.Bind("ipc:///very/long/path/.../endpoint.ipc")
    ```

> Reference: `core/tests/test_pair_ipc.cpp` -- `test_endpoint_too_long()`

### HWM Behavior

When there is no peer or the peer is slow, outgoing messages are queued up to the HWM. When the HWM is exceeded, `zlink_send()` blocks (default) or returns `EAGAIN` (`ZLINK_DONTWAIT`).

### LINGER Setting

When `zlink_close()` is called and there are unsent messages remaining, it waits for the LINGER duration. For tests or when a fast shutdown is needed:

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
[← Socket Patterns](03-0-socket-patterns.md) | [PUB/SUB →](03-2-pubsub.md)
