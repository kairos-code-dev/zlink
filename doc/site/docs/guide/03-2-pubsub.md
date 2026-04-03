# PUB/SUB/XPUB/XSUB Publish-Subscribe

## 1. Overview

The Publish-Subscribe pattern distributes messages based on topics. zlink provides two levels: basic PUB/SUB and advanced XPUB/XSUB.

| Socket | Role | Characteristics |
|------|------|------|
| **PUB** | Publisher | Broadcasts to all subscribers. Cannot receive. |
| **SUB** | Subscriber | Topic prefix match filtering. Cannot send. |
| **XPUB** | Advanced Publisher | PUB + can receive subscription events |
| **XSUB** | Advanced Subscriber | Receives all messages without local filtering |

**Valid socket combinations:**
- PUB → SUB, PUB → XSUB
- XPUB → SUB, XPUB → XSUB

### SUB vs XSUB — Key Difference

Both SUB and XSUB send subscription info to the upstream PUB via
`zlink_set_subscription()`. The public API usage is identical.
The difference is **whether the local filter engine is on or off**.

| | SUB (`filter=true`) | XSUB (`filter=false`) |
|---|---|---|
| With subscriptions | Receives only matching messages | **Receives all messages** (no filter check) |
| No subscriptions | **Receives nothing** | **Receives all messages** |
| `""` empty subscription | Receives all (matches every topic) | Already receives all without subscribing |
| Use case | Normal subscriber | Proxy/relay (pass-through) |

Internally, `xsub_t::xrecv()` checks `!options.filter || match(msg)`.
SUB (`filter=true`) evaluates `match()` on every message;
XSUB (`filter=false`) evaluates `!false = true` and skips `match()` entirely.

> **Common confusion:** "If I subscribe SUB with `""`, isn't it the same as XSUB?"
> → Both receive all messages in practice, but
> SUB incurs trie match cost on every message while XSUB skips the check.
> Also, SUB with **no subscriptions** receives nothing,
> while XSUB with no subscriptions still receives everything.

**Why XSUB/XPUB in the proxy pattern:**

```
PUB ──── XSUB ═══ XPUB ──── SUB
          │         │
          │  proxy  │
          └─────────┘
```

- XSUB passes all messages from PUB without subscription state.
- XPUB exposes SUB subscription events via `zlink_subscription_event()`,
  allowing the proxy to inject subscription management logic
  (filtering, logging, authorization, etc.).
- Plain SUB/PUB cannot build this relay structure.

```
              ┌─────┐
         ┌───►│SUB 1│ (topic: "weather")
┌─────┐  │   └─────┘
│ PUB │──┤
└─────┘  │   ┌─────┐
         └───►│SUB 2│ (topic: "sports")
              └─────┘
```

---

# Part I: PUB/SUB

## 2. PUB/SUB Basic Usage

### Message Exchange

A complete XPUB/SUB example: create a context, set up publisher and subscriber,
wait for the subscription to propagate, publish a message, receive it, and clean up.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *pub = zlink_socket(ctx, ZLINK_XPUB);
        zlink_bind(pub, "tcp://*:5556");

        void *sub = zlink_socket(ctx, ZLINK_SUB);
        zlink_connect(sub, "tcp://127.0.0.1:5556");
        zlink_set_subscription(sub, "weather");

        /* Wait for subscription to propagate */
        zlink_routing_id_t rid;
        int subscribed;
        char topic[256];
        size_t topic_len = sizeof(topic);
        zlink_subscription_event(pub, &rid, &subscribed, topic, &topic_len, 0);

        /* Publish */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 5);
        memcpy(zlink_msg_data(&msg), "sunny", 5);
        zlink_publish(pub, "weather", &msg, 1, 0);

        /* Receive */
        zlink_msg_t *parts;
        size_t count;
        char recv_topic[256];
        size_t recv_topic_len = sizeof(recv_topic);
        zlink_subscribe(sub, &rid, &parts, &count,
                        recv_topic, &recv_topic_len, 0);
        printf("Topic: %.*s, Data: %.*s\n",
               (int)recv_topic_len, recv_topic,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(sub);
        zlink_close(pub);
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

        zlink::xpub_socket_t pub(ctx);
        pub.bind("tcp://*:5556");

        zlink::sub_socket_t sub(ctx);
        sub.connect("tcp://127.0.0.1:5556");
        sub.set_subscription("weather");

        // Wait for subscription to propagate
        auto [ev_rid, subscribed, ev_topic] = pub.subscription_event();

        // Publish
        pub.publish("weather", "sunny");

        // Receive
        auto [rid, topic, parts] = sub.subscribe();
        std::cout << "Topic: " << topic
                  << ", Data: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PubSubExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            XPubSocket pub = new XPubSocket(ctx);
            pub.bind("tcp://*:5556");

            SubSocket sub = new SubSocket(ctx);
            sub.connect("tcp://127.0.0.1:5556");
            sub.setSubscription("weather");

            // Wait for subscription to propagate
            pub.subscriptionEvent();

            // Publish
            pub.publish("weather", "sunny");

            // Receive
            SubscribeResult result = sub.subscribe();
            System.out.println("Topic: " + result.topic()
                + ", Data: " + result.partAsString(0));

            sub.close();
            pub.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    pub = zlink.XPubSocket(ctx)
    pub.bind("tcp://*:5556")

    sub = zlink.SubSocket(ctx)
    sub.connect("tcp://127.0.0.1:5556")
    sub.set_subscription("weather")

    # Wait for subscription to propagate
    pub.subscription_event()

    # Publish
    pub.publish("weather", b"sunny")

    # Receive
    source_rid, topic, parts = sub.subscribe()
    print(f"Topic: {topic}, Data: {parts[0].decode()}")

    sub.close()
    pub.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const pub = new zlink.XPubSocket(ctx);
    pub.bind('tcp://*:5556');

    const sub = new zlink.SubSocket(ctx);
    sub.connect('tcp://127.0.0.1:5556');
    sub.setSubscription('weather');

    // Wait for subscription to propagate
    pub.subscriptionEvent();

    // Publish
    pub.publish('weather', Buffer.from('sunny'));

    // Receive
    const [rid, topic, parts] = sub.subscribe();
    console.log(`Topic: ${topic}, Data: ${parts[0].toString()}`);

    sub.close();
    pub.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var pub = new XPubSocket(ctx);
    pub.Bind("tcp://*:5556");

    var sub = new SubSocket(ctx);
    sub.Connect("tcp://127.0.0.1:5556");
    sub.SetSubscription("weather");

    // Wait for subscription to propagate
    pub.SubscriptionEvent();

    // Publish
    pub.Publish("weather", "sunny");

    // Receive
    var (rid, topic, parts) = sub.Subscribe();
    Console.WriteLine($"Topic: {topic}, Data: {parts[0].GetString()}");

    sub.Close();
    pub.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let pub_sock = ctx.xpub_socket();
        pub_sock.bind("tcp://*:5556")?;

        let sub = ctx.sub_socket();
        sub.connect("tcp://127.0.0.1:5556")?;
        sub.set_subscription("weather")?;

        // Wait for subscription to propagate
        pub_sock.subscription_event()?;

        // Publish
        pub_sock.publish("weather", b"sunny")?;

        // Receive
        let (rid, topic, parts) = sub.subscribe()?;
        println!("Topic: {}, Data: {}",
                 topic, String::from_utf8_lossy(parts[0].data()));

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

        pub, err := ctx.XPubSocket()
        if err != nil { log.Fatal(err) }
        defer pub.Close()
        pub.Bind("tcp://*:5556")

        sub, err := ctx.SubSocket()
        if err != nil { log.Fatal(err) }
        defer sub.Close()
        sub.Connect("tcp://127.0.0.1:5556")
        sub.SetSubscription("weather")

        // Wait for subscription to propagate
        pub.SubscriptionEvent()

        // Publish
        pub.Publish("weather", zlink.NewMessage([]byte("sunny")))

        // Receive
        result, err := sub.Subscribe()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Topic: %s, Data: %s\n",
                   result.Topic, result.Parts[0].Data())
        result.Close()
    }
    ```

> Reference: `core/tests/test_pubsub.cpp` -- empty subscription ("") receives all messages

### Sending and Receiving Summary

| Socket | Direction | Receive API | Notes |
|--------|-----------|-------------|-------|
| PUB | Send only | N/A | Cannot receive (`ENOTSUP`) |
| SUB | Receive only | `zlink_subscribe()` / `zlink_subscribe_handler()` | Topic + data separated |
| XPUB | Bidirectional | `zlink_subscription_event()` | Receives subscription events |
| XSUB | Receive only | `zlink_subscribe()` / `zlink_subscribe_handler()` | No local filter; receives all |

> **Note:** `zlink_send()` / `zlink_recv()` return `ENOTSUP` on all 4
> PUB/SUB sockets. Use `zlink_publish()` for publishing and
> `zlink_subscribe()` / `zlink_subscribe_handler()` for receiving.

PUB/SUB sockets support two receive modes:

- **Pull mode** (default): `zlink_subscribe()` returns topic and data separately
- **Callback mode**: `zlink_subscribe_handler()` registers a callback for automatic dispatch

After callback attach, `zlink_subscribe()` and data-plane `ZLINK_POLLIN`
return `EBUSY`.

??? example "Full Sample Code -- Recv"

    | Language | Source |
    |----------|--------|
    | C | [pubsub_recv_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pubsub_recv_sample.c) |
    | C++ | [pubsub_recv_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pubsub_recv_sample.cpp) |
    | Java | [PubSubRecvSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PubSubRecvSample.java) |
    | Python | [pubsub_recv.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pubsub_recv.py) |
    | Node | [pubsub_recv_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pubsub_recv_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PubSubRecv/Program.cs) |
    | Rust | [pubsub_recv_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pubsub_recv_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pubsub_recv_sample/main.go) |

#### Callback Mode

Install the callback with `zlink_subscribe_handler()` to make a one-way
transition from recv mode to callback mode. Incoming messages are then
dispatched automatically through that callback.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    void on_topic_message(const zlink_routing_id_t *source_rid,
                          const char *topic, size_t topic_len,
                          zlink_msg_t *parts, size_t part_count,
                          void *userdata)
    {
        printf("Callback: topic=%.*s data=%.*s\n",
               (int)topic_len, topic,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *pub = zlink_socket(ctx, ZLINK_XPUB);
        zlink_bind(pub, "tcp://*:5556");

        void *sub = zlink_socket(ctx, ZLINK_SUB);
        zlink_connect(sub, "tcp://127.0.0.1:5556");
        zlink_set_subscription(sub, "weather");

        /* Wait for subscription to propagate */
        zlink_routing_id_t rid;
        int subscribed;
        char topic[256];
        size_t topic_len = sizeof(topic);
        zlink_subscription_event(pub, &rid, &subscribed,
                                 topic, &topic_len, 0);

        /* Transition sub to callback mode */
        zlink_subscribe_handler(sub, on_topic_message, NULL);

        /* Publish */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 5);
        memcpy(zlink_msg_data(&msg), "sunny", 5);
        zlink_publish(pub, "weather", &msg, 1, 0);

        zlink_msleep(200);  /* let callback fire */

        zlink_close(sub);
        zlink_close(pub);
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

        zlink::xpub_socket_t pub(ctx);
        pub.bind("tcp://*:5556");

        zlink::sub_socket_t sub(ctx);
        sub.connect("tcp://127.0.0.1:5556");
        sub.set_subscription("weather");

        // Wait for subscription to propagate
        pub.subscription_event();

        // Transition sub to callback mode
        sub.subscribe_handler([](const zlink::routing_id_t& source_rid,
                                 std::string_view topic,
                                 std::span<zlink::msg> parts) {
            std::cout << "Callback: topic=" << topic
                      << " data=" << parts[0].str() << std::endl;
        });

        // Publish
        pub.publish("weather", "sunny");

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PubSubCallbackExample {
        public static void main(String[] args) throws Exception {
            Context ctx = new Context();

            XPubSocket pub = new XPubSocket(ctx);
            pub.bind("tcp://*:5556");

            SubSocket sub = new SubSocket(ctx);
            sub.connect("tcp://127.0.0.1:5556");
            sub.setSubscription("weather");

            // Wait for subscription to propagate
            pub.subscriptionEvent();

            // Transition sub to callback mode
            sub.onSubscribe((sourceRid, topic, parts) -> {
                System.out.println("Callback: topic=" + topic
                    + " data=" + parts[0].dataAsString());
            });

            // Publish
            pub.publish("weather", "sunny");

            Thread.sleep(200);

            sub.close();
            pub.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink
    import time

    ctx = zlink.Context()

    pub = zlink.XPubSocket(ctx)
    pub.bind("tcp://*:5556")

    sub = zlink.SubSocket(ctx)
    sub.connect("tcp://127.0.0.1:5556")
    sub.set_subscription("weather")

    # Wait for subscription to propagate
    pub.subscription_event()

    # Transition sub to callback mode
    def on_topic_message(source_rid, topic, parts):
        print(f"Callback: topic={topic} data={parts[0].decode()}")

    sub.subscribe_handler(on_topic_message)

    # Publish
    pub.publish("weather", b"sunny")

    time.sleep(0.2)

    sub.close()
    pub.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const pub = new zlink.XPubSocket(ctx);
    pub.bind('tcp://*:5556');

    const sub = new zlink.SubSocket(ctx);
    sub.connect('tcp://127.0.0.1:5556');
    sub.setSubscription('weather');

    // Wait for subscription to propagate
    pub.subscriptionEvent();

    // Transition sub to callback mode
    sub.subscribeHandler((sourceRid, topic, parts) => {
        console.log(`Callback: topic=${topic} data=${parts[0].toString()}`);
    });

    // Publish
    pub.publish('weather', Buffer.from('sunny'));

    await new Promise(r => setTimeout(r, 200));

    sub.close();
    pub.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var pub = new XPubSocket(ctx);
    pub.Bind("tcp://*:5556");

    var sub = new SubSocket(ctx);
    sub.Connect("tcp://127.0.0.1:5556");
    sub.SetSubscription("weather");

    // Wait for subscription to propagate
    pub.SubscriptionEvent();

    // Transition sub to callback mode
    sub.SubscribeHandler((sourceRid, topic, parts) => {
        Console.WriteLine($"Callback: topic={topic} data={parts[0].GetString()}");
    });

    // Publish
    pub.Publish("weather", "sunny");

    Thread.Sleep(200);

    sub.Close();
    pub.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;
    use std::thread;
    use std::time::Duration;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let pub_sock = ctx.xpub_socket();
        pub_sock.bind("tcp://*:5556")?;

        let sub = ctx.sub_socket();
        sub.connect("tcp://127.0.0.1:5556")?;
        sub.set_subscription("weather")?;

        // Wait for subscription to propagate
        pub_sock.subscription_event()?;

        // Transition sub to callback mode
        sub.subscribe_handler(|source_rid, topic, parts| {
            println!("Callback: topic={} data={}",
                     topic, String::from_utf8_lossy(parts[0].data()));
        });

        // Publish
        pub_sock.publish("weather", b"sunny")?;

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

        pub, err := ctx.XPubSocket()
        if err != nil { log.Fatal(err) }
        defer pub.Close()
        pub.Bind("tcp://*:5556")

        sub, err := ctx.SubSocket()
        if err != nil { log.Fatal(err) }
        defer sub.Close()
        sub.Connect("tcp://127.0.0.1:5556")
        sub.SetSubscription("weather")

        // Wait for subscription to propagate
        pub.SubscriptionEvent()

        // Transition sub to callback mode
        sub.OnSubscribe(func(sourceRid zlink.RoutingID, topic string, parts []zlink.Message) {
            fmt.Printf("Callback: topic=%s data=%s\n", topic, parts[0].Data())
        })

        // Publish
        pub.Publish("weather", zlink.NewMessage([]byte("sunny")))

        time.Sleep(200 * time.Millisecond)
    }
    ```

??? example "Full Sample Code -- Callback"

    | Language | Source |
    |----------|--------|
    | C | [pubsub_callback_sample.c](https://github.com/kairos-code-dev/zlink/blob/main/core/samples/pubsub_callback_sample.c) |
    | C++ | [pubsub_callback_sample.cpp](https://github.com/kairos-code-dev/zlink/blob/main/bindings/cpp/samples/pubsub_callback_sample.cpp) |
    | Java | [PubSubCallbackSample.java](https://github.com/kairos-code-dev/zlink/blob/main/bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/PubSubCallbackSample.java) |
    | Python | [pubsub_callback.py](https://github.com/kairos-code-dev/zlink/blob/main/bindings/python/examples/pubsub_callback.py) |
    | Node | [pubsub_callback_sample.ts](https://github.com/kairos-code-dev/zlink/blob/main/bindings/node/examples/pubsub_callback_sample.ts) |
    | C# | [Program.cs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/dotnet/samples/PubSubCallback/Program.cs) |
    | Rust | [pubsub_callback_sample.rs](https://github.com/kairos-code-dev/zlink/blob/main/bindings/rust/samples/pubsub_callback_sample.rs) |
    | Go | [main.go](https://github.com/kairos-code-dev/zlink/blob/main/bindings/go/samples/pubsub_callback_sample/main.go) |

> When PUB's send queue is full (HWM), messages are **dropped** rather
> than blocking. For details, see
> [Performance Guide](10-performance.md).

## 3. Topic Filtering

Topic filtering in SUB sockets uses **prefix matching**.

| Subscription Topic | Received Message | Match |
|-----------|-------------|:----:|
| `"weather"` | `"weather: sunny"` | O |
| `"weather"` | `"weathering storm"` | O |
| `"weather"` | `"sports: baseball"` | X |
| `""` (empty string) | All messages | O |

### Multiple Topic Subscriptions

=== "C"

    ```c
    /* Subscribe to multiple topics */
    zlink_set_subscription(sub, "weather");
    zlink_set_subscription(sub, "sports");

    /* Unsubscribe */
    zlink_unset_subscription(sub, "sports");
    ```

=== "C++"

    ```cpp
    // Subscribe to multiple topics
    sub.set_subscription("weather");
    sub.set_subscription("sports");

    // Unsubscribe
    sub.unset_subscription("sports");
    ```

=== "Java"

    ```java
    // Subscribe to multiple topics
    sub.setSubscription("weather");
    sub.setSubscription("sports");

    // Unsubscribe
    sub.unsetSubscription("sports");
    ```

=== "Python"

    ```python
    # Subscribe to multiple topics
    sub.set_subscription("weather")
    sub.set_subscription("sports")

    # Unsubscribe
    sub.unset_subscription("sports")
    ```

=== "Node/TypeScript"

    ```typescript
    // Subscribe to multiple topics
    sub.setSubscription("weather");
    sub.setSubscription("sports");

    // Unsubscribe
    sub.unsetSubscription("sports");
    ```

=== "C#/.NET"

    ```csharp
    // Subscribe to multiple topics
    sub.SetSubscription("weather");
    sub.SetSubscription("sports");

    // Unsubscribe
    sub.UnsetSubscription("sports");
    ```

=== "Rust"

    ```rust
    // Subscribe to multiple topics
    sub.set_subscription("weather");
    sub.set_subscription("sports");

    // Unsubscribe
    sub.unset_subscription("sports");
    ```

=== "Go"

    ```go
    // Subscribe to multiple topics
    sub.SetSubscription("weather")
    sub.SetSubscription("sports")

    // Unsubscribe
    sub.UnsetSubscription("sports")
    ```

### Empty Subscription (All Messages)

=== "C"

    ```c
    /* Subscribe with empty string -- receives all messages */
    zlink_set_subscription(sub, "");
    ```

=== "C++"

    ```cpp
    // Subscribe with empty string -- receives all messages
    sub.set_subscription("");
    ```

=== "Java"

    ```java
    // Subscribe with empty string -- receives all messages
    sub.setSubscription("");
    ```

=== "Python"

    ```python
    # Subscribe with empty string -- receives all messages
    sub.set_subscription("")
    ```

=== "Node/TypeScript"

    ```typescript
    // Subscribe with empty string -- receives all messages
    sub.setSubscription("");
    ```

=== "C#/.NET"

    ```csharp
    // Subscribe with empty string -- receives all messages
    sub.SetSubscription("");
    ```

=== "Rust"

    ```rust
    // Subscribe with empty string -- receives all messages
    sub.set_subscription("");
    ```

=== "Go"

    ```go
    // Subscribe with empty string -- receives all messages
    sub.SetSubscription("")
    ```

> Reference: `core/tests/test_pubsub.cpp` -- `zlink_set_subscription(subscriber, "")`

## 4. Message Format

`zlink_publish()` takes a **topic** and a **multipart message** as
separate parameters. Like `zlink_send()` on other sockets, multipart
is the default.

!!! note "C API Function Signature"
    The `zlink_publish` function signature uses C-specific types.
    Each binding wraps this with its own idiomatic API.

    ```c
    int zlink_publish (void *subject,
                       const char *topic_id,      /* topic string */
                       zlink_msg_t *parts,         /* data frame array */
                       size_t part_count,           /* number of frames */
                       zlink_send_flags_t flags);
    ```

=== "C"

    ```c
    /* Publish: topic = "sensor:cpu", payload = 2 frames */
    zlink_msg_t parts[2];
    zlink_msg_init_size(&parts[0], 4);
    memcpy(zlink_msg_data(&parts[0]), "host", 4);
    zlink_msg_init_size(&parts[1], 2);
    memcpy(zlink_msg_data(&parts[1]), "73", 2);
    zlink_publish(pub, "sensor:cpu", parts, 2, 0);

    /* SUB receives (zlink_subscribe or subscribe_handler callback):
       topic     = "sensor:cpu"
       parts[0]  = "host"
       parts[1]  = "73" */
    ```

=== "C++"

    ```cpp
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pub.publish("sensor:cpu", {"host", "73"});

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

=== "Java"

    ```java
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pub.publish("sensor:cpu", "host", "73");

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

=== "Python"

    ```python
    # Publish: topic = "sensor:cpu", payload = 2 frames
    pub.publish("sensor:cpu", [b"host", b"73"])

    # SUB receives:
    #   topic     = "sensor:cpu"
    #   parts[0]  = b"host"
    #   parts[1]  = b"73"
    ```

=== "Node/TypeScript"

    ```typescript
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pub.publish("sensor:cpu", [Buffer.from("host"), Buffer.from("73")]);

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

=== "C#/.NET"

    ```csharp
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pub.Publish("sensor:cpu", "host", "73");

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

=== "Rust"

    ```rust
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pub_sock.publish("sensor:cpu", &[b"host", b"73"]);

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

=== "Go"

    ```go
    // Publish: topic = "sensor:cpu", payload = 2 frames
    pubSock.Publish("sensor:cpu",
        zlink.NewMessage([]byte("host")),
        zlink.NewMessage([]byte("73")))

    // SUB receives:
    //   topic     = "sensor:cpu"
    //   parts[0]  = "host"
    //   parts[1]  = "73"
    ```

The topic is sent on the wire as the first frame. `zlink_subscribe()` /
`zlink_subscribe_handler()` separate the topic from data on the receive
side. Callers never need to assemble topic frames manually.

> **Note:** Passing `NULL` as topic (`zlink_publish(pub, NULL, parts, ...)`)
> activates a legacy path where parts[0] is used as the topic frame.
> This is not recommended. Always pass the `topic_id` parameter explicitly.

## 5. PUB/SUB Socket Options

### SUB-Specific Functions

| Function | Description |
|------|------|
| `zlink_set_subscription()` | Add topic subscription (prefix match) |
| `zlink_unset_subscription()` | Remove topic subscription |

### Common Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 1000 | Send HWM (PUB) |
| `ZLINK_OPT_RCVHWM` | int | 1000 | Receive HWM (SUB) |
| `ZLINK_OPT_LINGER` | int | -1 | Wait time on close (ms) |

## 6. PUB/SUB Usage Patterns

### Pattern 1: Basic PUB/SUB

A complete PUB/SUB example using XPUB so the publisher can detect
when the subscriber is ready before publishing.

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    int main(void)
    {
        void *ctx = zlink_ctx_new();

        void *pub = zlink_socket(ctx, ZLINK_XPUB);
        zlink_bind(pub, "tcp://*:5556");

        void *sub = zlink_socket(ctx, ZLINK_SUB);
        zlink_connect(sub, "tcp://127.0.0.1:5556");
        zlink_set_subscription(sub, "");

        /* Wait for subscription to propagate */
        zlink_routing_id_t rid;
        int subscribed;
        char topic[256];
        size_t topic_len = sizeof(topic);
        zlink_subscription_event(pub, &rid, &subscribed,
                                 topic, &topic_len, 0);

        /* Publish */
        zlink_msg_t msg;
        zlink_msg_init_size(&msg, 4);
        memcpy(zlink_msg_data(&msg), "test", 4);
        zlink_publish(pub, "greeting", &msg, 1, 0);

        /* Receive */
        zlink_msg_t *parts;
        size_t count;
        char recv_topic[256];
        size_t recv_topic_len = sizeof(recv_topic);
        zlink_subscribe(sub, &rid, &parts, &count,
                        recv_topic, &recv_topic_len, 0);
        printf("Topic: %.*s, Data: %.*s\n",
               (int)recv_topic_len, recv_topic,
               (int)zlink_msg_size(&parts[0]),
               (char *)zlink_msg_data(&parts[0]));
        zlink_multipart_close(parts, count);

        zlink_close(sub);
        zlink_close(pub);
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

        zlink::xpub_socket_t pub(ctx);
        pub.bind("tcp://*:5556");

        zlink::sub_socket_t sub(ctx);
        sub.connect("tcp://127.0.0.1:5556");
        sub.set_subscription("");

        // Wait for subscription to propagate
        pub.subscription_event();

        pub.publish("greeting", "test");

        auto [rid, topic, parts] = sub.subscribe();
        std::cout << "Topic: " << topic
                  << ", Data: " << parts[0].str() << std::endl;

        return 0;
    }
    ```

=== "Java"

    ```java
    import dev.kairoscode.zlink.*;

    public class PubSubBasicExample {
        public static void main(String[] args) {
            Context ctx = new Context();

            XPubSocket pub = new XPubSocket(ctx);
            pub.bind("tcp://*:5556");

            SubSocket sub = new SubSocket(ctx);
            sub.connect("tcp://127.0.0.1:5556");
            sub.setSubscription("");

            // Wait for subscription to propagate
            pub.subscriptionEvent();

            pub.publish("greeting", "test");

            SubscribeResult result = sub.subscribe();
            System.out.println("Topic: " + result.topic()
                + ", Data: " + result.partAsString(0));

            sub.close();
            pub.close();
            ctx.close();
        }
    }
    ```

=== "Python"

    ```python
    import zlink

    ctx = zlink.Context()

    pub = zlink.XPubSocket(ctx)
    pub.bind("tcp://*:5556")

    sub = zlink.SubSocket(ctx)
    sub.connect("tcp://127.0.0.1:5556")
    sub.set_subscription("")

    # Wait for subscription to propagate
    pub.subscription_event()

    pub.publish("greeting", b"test")

    source_rid, topic, parts = sub.subscribe()
    print(f"Topic: {topic}, Data: {parts[0].decode()}")

    sub.close()
    pub.close()
    ctx.term()
    ```

=== "Node/TypeScript"

    ```typescript
    import * as zlink from 'zlink';

    const ctx = new zlink.Context();

    const pub = new zlink.XPubSocket(ctx);
    pub.bind('tcp://*:5556');

    const sub = new zlink.SubSocket(ctx);
    sub.connect('tcp://127.0.0.1:5556');
    sub.setSubscription('');

    // Wait for subscription to propagate
    pub.subscriptionEvent();

    pub.publish('greeting', Buffer.from('test'));

    const [rid, topic, parts] = sub.subscribe();
    console.log(`Topic: ${topic}, Data: ${parts[0].toString()}`);

    sub.close();
    pub.close();
    ctx.term();
    ```

=== "C#/.NET"

    ```csharp
    using Zlink;

    var ctx = new Context();

    var pub = new XPubSocket(ctx);
    pub.Bind("tcp://*:5556");

    var sub = new SubSocket(ctx);
    sub.Connect("tcp://127.0.0.1:5556");
    sub.SetSubscription("");

    // Wait for subscription to propagate
    pub.SubscriptionEvent();

    pub.Publish("greeting", "test");

    var (rid, topic, parts) = sub.Subscribe();
    Console.WriteLine($"Topic: {topic}, Data: {parts[0].GetString()}");

    sub.Close();
    pub.Close();
    ctx.Term();
    ```

=== "Rust"

    ```rust
    use zlink::Context;

    fn main() -> Result<(), Box<dyn std::error::Error>> {
        let ctx = Context::new();

        let pub_sock = ctx.xpub_socket();
        pub_sock.bind("tcp://*:5556")?;

        let sub = ctx.sub_socket();
        sub.connect("tcp://127.0.0.1:5556")?;
        sub.set_subscription("")?;

        // Wait for subscription to propagate
        pub_sock.subscription_event()?;

        pub_sock.publish("greeting", b"test")?;

        let (rid, topic, parts) = sub.subscribe()?;
        println!("Topic: {}, Data: {}",
                 topic, String::from_utf8_lossy(parts[0].data()));

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

        pub, err := ctx.XPubSocket()
        if err != nil { log.Fatal(err) }
        defer pub.Close()
        pub.Bind("tcp://*:5556")

        sub, err := ctx.SubSocket()
        if err != nil { log.Fatal(err) }
        defer sub.Close()
        sub.Connect("tcp://127.0.0.1:5556")
        sub.SetSubscription("")

        // Wait for subscription to propagate
        pub.SubscriptionEvent()

        pub.Publish("greeting", zlink.NewMessage([]byte("test")))

        result, err := sub.Subscribe()
        if err != nil { log.Fatal(err) }
        fmt.Printf("Topic: %s, Data: %s\n",
                   result.Topic, result.Parts[0].Data())
        result.Close()
    }
    ```

> Reference: `core/tests/test_pubsub.cpp` -- `test_tcp()`

### Pattern 2: Multiple SUBs

Multiple SUBs connect to a single PUB. Each SUB receives only its own topics.

=== "C"

    ```c
    void *pub = zlink_socket(ctx, ZLINK_PUB);
    zlink_bind(pub, "tcp://*:5556");

    void *sub_weather = zlink_socket(ctx, ZLINK_SUB);
    zlink_connect(sub_weather, "tcp://127.0.0.1:5556");
    zlink_set_subscription(sub_weather, "weather");

    void *sub_sports = zlink_socket(ctx, ZLINK_SUB);
    zlink_connect(sub_sports, "tcp://127.0.0.1:5556");
    zlink_set_subscription(sub_sports, "sports");

    /* Only sub_weather receives weather, only sub_sports receives sports */
    ```

=== "C++"

    ```cpp
    zlink::pub_socket_t pub(ctx);
    pub.bind("tcp://*:5556");

    zlink::sub_socket_t sub_weather(ctx);
    sub_weather.connect("tcp://127.0.0.1:5556");
    sub_weather.set_subscription("weather");

    zlink::sub_socket_t sub_sports(ctx);
    sub_sports.connect("tcp://127.0.0.1:5556");
    sub_sports.set_subscription("sports");

    // Only sub_weather receives weather, only sub_sports receives sports
    ```

=== "Java"

    ```java
    PubSocket pub = new PubSocket(ctx);
    pub.bind("tcp://*:5556");

    SubSocket subWeather = new SubSocket(ctx);
    subWeather.connect("tcp://127.0.0.1:5556");
    subWeather.setSubscription("weather");

    SubSocket subSports = new SubSocket(ctx);
    subSports.connect("tcp://127.0.0.1:5556");
    subSports.setSubscription("sports");

    // Only subWeather receives weather, only subSports receives sports
    ```

=== "Python"

    ```python
    pub = zlink.PubSocket(ctx)
    pub.bind("tcp://*:5556")

    sub_weather = zlink.SubSocket(ctx)
    sub_weather.connect("tcp://127.0.0.1:5556")
    sub_weather.set_subscription("weather")

    sub_sports = zlink.SubSocket(ctx)
    sub_sports.connect("tcp://127.0.0.1:5556")
    sub_sports.set_subscription("sports")

    # Only sub_weather receives weather, only sub_sports receives sports
    ```

=== "Node/TypeScript"

    ```typescript
    const pub = new zlink.PubSocket(ctx);
    pub.bind("tcp://*:5556");

    const subWeather = new zlink.SubSocket(ctx);
    subWeather.connect("tcp://127.0.0.1:5556");
    subWeather.setSubscription("weather");

    const subSports = new zlink.SubSocket(ctx);
    subSports.connect("tcp://127.0.0.1:5556");
    subSports.setSubscription("sports");

    // Only subWeather receives weather, only subSports receives sports
    ```

=== "C#/.NET"

    ```csharp
    var pub = new PubSocket(ctx);
    pub.Bind("tcp://*:5556");

    var subWeather = new SubSocket(ctx);
    subWeather.Connect("tcp://127.0.0.1:5556");
    subWeather.SetSubscription("weather");

    var subSports = new SubSocket(ctx);
    subSports.Connect("tcp://127.0.0.1:5556");
    subSports.SetSubscription("sports");

    // Only subWeather receives weather, only subSports receives sports
    ```

=== "Rust"

    ```rust
    let pub_sock = ctx.pub_socket();
    pub_sock.bind("tcp://*:5556");

    let sub_weather = ctx.sub_socket();
    sub_weather.connect("tcp://127.0.0.1:5556");
    sub_weather.set_subscription("weather");

    let sub_sports = ctx.sub_socket();
    sub_sports.connect("tcp://127.0.0.1:5556");
    sub_sports.set_subscription("sports");

    // Only sub_weather receives weather, only sub_sports receives sports
    ```

=== "Go"

    ```go
    pubSock, _ := ctx.PubSocket()
    pubSock.Bind("tcp://*:5556")

    subWeather, _ := ctx.SubSocket()
    subWeather.Connect("tcp://127.0.0.1:5556")
    subWeather.SetSubscription("weather")

    subSports, _ := ctx.SubSocket()
    subSports.Connect("tcp://127.0.0.1:5556")
    subSports.SetSubscription("sports")

    // Only subWeather receives weather, only subSports receives sports
    ```

### Pattern 3: Multiple PUBs → SUB

A SUB can connect to multiple PUBs. It receives messages from all PUBs via fair-queue.

=== "C"

    ```c
    void *sub = zlink_socket(ctx, ZLINK_SUB);
    zlink_set_subscription(sub, "");
    zlink_connect(sub, "tcp://pub1:5556");
    zlink_connect(sub, "tcp://pub2:5557");
    ```

=== "C++"

    ```cpp
    zlink::sub_socket_t sub(ctx);
    sub.set_subscription("");
    sub.connect("tcp://pub1:5556");
    sub.connect("tcp://pub2:5557");
    ```

=== "Java"

    ```java
    SubSocket sub = new SubSocket(ctx);
    sub.setSubscription("");
    sub.connect("tcp://pub1:5556");
    sub.connect("tcp://pub2:5557");
    ```

=== "Python"

    ```python
    sub = zlink.SubSocket(ctx)
    sub.set_subscription("")
    sub.connect("tcp://pub1:5556")
    sub.connect("tcp://pub2:5557")
    ```

=== "Node/TypeScript"

    ```typescript
    const sub = new zlink.SubSocket(ctx);
    sub.setSubscription("");
    sub.connect("tcp://pub1:5556");
    sub.connect("tcp://pub2:5557");
    ```

=== "C#/.NET"

    ```csharp
    var sub = new SubSocket(ctx);
    sub.SetSubscription("");
    sub.Connect("tcp://pub1:5556");
    sub.Connect("tcp://pub2:5557");
    ```

=== "Rust"

    ```rust
    let sub = ctx.sub_socket();
    sub.set_subscription("");
    sub.connect("tcp://pub1:5556");
    sub.connect("tcp://pub2:5557");
    ```

=== "Go"

    ```go
    sub, _ := ctx.SubSocket()
    sub.SetSubscription("")
    sub.Connect("tcp://pub1:5556")
    sub.Connect("tcp://pub2:5557")
    ```

## 7. PUB/SUB Caveats

### Slow Subscriber (Drop on HWM Exceeded)

PUB/XPUB operate in **lossy mode** by default. When a slow subscriber's
send queue reaches the HWM, messages to that subscriber are **silently
dropped** (no error returned).

=== "C"

    ```c
    /* Option 1: Increase buffer by adjusting HWM */
    int hwm = 100000;
    zlink_set_option(pub, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
    ```

=== "C++"

    ```cpp
    // Option 1: Increase buffer by adjusting HWM
    pub.set_option(ZLINK_OPT_SNDHWM, 100000);
    ```

=== "Java"

    ```java
    // Option 1: Increase buffer by adjusting HWM
    pub.setOption(ZLINK_OPT_SNDHWM, 100000);
    ```

=== "Python"

    ```python
    # Option 1: Increase buffer by adjusting HWM
    pub.set_option(ZLINK_OPT_SNDHWM, 100000)
    ```

=== "Node/TypeScript"

    ```typescript
    // Option 1: Increase buffer by adjusting HWM
    pub.setOption(ZLINK_OPT_SNDHWM, 100000);
    ```

=== "C#/.NET"

    ```csharp
    // Option 1: Increase buffer by adjusting HWM
    pub.SetOption(ZLINK_OPT_SNDHWM, 100000);
    ```

=== "Rust"

    ```rust
    // Option 1: Increase buffer by adjusting HWM
    pub_sock.set_option(ZLINK_OPT_SNDHWM, 100000);
    ```

=== "Go"

    ```go
    // Option 1: Increase buffer by adjusting HWM
    pubSock.SetOption(zlink.OptionSndHWM, 100000)
    ```

#### XPUB_NODROP — Backpressure Instead of Drop

Setting `ZLINK_PUB_OPT_NODROP` disables lossy mode. When the HWM is
reached, instead of dropping messages, `EAGAIN` is returned so the
caller can handle backpressure directly.

=== "C"

    ```c
    /* Enable NODROP on XPUB */
    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    int nodrop = 1;
    zlink_set_pub_option(xpub, ZLINK_PUB_OPT_NODROP, &nodrop, sizeof(nodrop));

    /* On HWM, send returns EAGAIN instead of dropping */
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 5);
    memcpy(zlink_msg_data(&msg), "hello", 5);
    int rc = zlink_publish(xpub, NULL, &msg, 1, ZLINK_DONTWAIT);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        /* HWM reached — retry or apply backpressure logic */
        zlink_msg_close(&msg);
    }
    ```

=== "C++"

    ```cpp
    // Enable NODROP on XPUB
    zlink::xpub_socket_t xpub(ctx);
    xpub.set_pub_option(ZLINK_PUB_OPT_NODROP, 1);

    // On HWM, send returns EAGAIN instead of dropping
    try {
        xpub.publish("", "hello");
    } catch (const zlink::eagain_error& e) {
        // HWM reached — retry or apply backpressure logic
    }
    ```

=== "Java"

    ```java
    // Enable NODROP on XPUB
    XPubSocket xpub = new XPubSocket(ctx);
    xpub.setPubOption(ZLINK_PUB_OPT_NODROP, 1);

    // On HWM, send throws EagainException instead of dropping
    try {
        xpub.publish("", "hello");
    } catch (EagainException e) {
        // HWM reached — retry or apply backpressure logic
    }
    ```

=== "Python"

    ```python
    # Enable NODROP on XPUB
    xpub = zlink.XPubSocket(ctx)
    xpub.set_pub_option(ZLINK_PUB_OPT_NODROP, 1)

    # On HWM, send raises Again instead of dropping
    try:
        xpub.publish("", b"hello")
    except zlink.Again:
        # HWM reached — retry or apply backpressure logic
        pass
    ```

=== "Node/TypeScript"

    ```typescript
    // Enable NODROP on XPUB
    const xpub = new zlink.XPubSocket(ctx);
    xpub.setPubOption(ZLINK_PUB_OPT_NODROP, 1);

    // On HWM, send throws EAGAIN instead of dropping
    try {
        xpub.publish("", Buffer.from("hello"));
    } catch (e) {
        // HWM reached — retry or apply backpressure logic
    }
    ```

=== "C#/.NET"

    ```csharp
    // Enable NODROP on XPUB
    var xpub = new XPubSocket(ctx);
    xpub.SetPubOption(ZLINK_PUB_OPT_NODROP, 1);

    // On HWM, send throws EagainException instead of dropping
    try {
        xpub.Publish("", "hello");
    } catch (EagainException) {
        // HWM reached — retry or apply backpressure logic
    }
    ```

=== "Rust"

    ```rust
    // Enable NODROP on XPUB
    let xpub = ctx.xpub_socket();
    xpub.set_pub_option(ZLINK_PUB_OPT_NODROP, 1);

    // On HWM, send returns Err(Eagain) instead of dropping
    match xpub.publish("", b"hello") {
        Err(ZlinkError::Eagain) => {
            // HWM reached — retry or apply backpressure logic
        }
        _ => {}
    }
    ```

=== "Go"

    ```go
    // Enable NODROP on XPUB
    xpub, _ := ctx.XPubSocket()
    xpub.SetOption(zlink.OptionPubNoDrop, 1)

    // On HWM, send returns error instead of dropping
    err := xpub.Publish("", zlink.NewMessage([]byte("hello")))
    if err != nil {
        // HWM reached — retry or apply backpressure logic
    }
    ```

| Mode | Behavior on HWM | When to Use |
|------|-----------------|-------------|
| Default (lossy) | Silent drop — no error, message lost | Only latest data matters (sensor, tick) |
| `XPUB_NODROP=1` | Returns `EAGAIN` — caller controls | Message loss is not acceptable |

> `ZLINK_PUB_OPT_NODROP` is an XPUB-only socket option.
> It is not available on regular PUB sockets.

### Late Joiner (Messages Lost Before Subscription)

Messages published before the subscription message from SUB reaches PUB are lost.

=== "C"

    ```c
    /* Time needed for subscription to propagate to PUB */
    zlink_connect(sub, "tcp://127.0.0.1:5556");
    zlink_set_subscription(sub, "topic");
    msleep(100);  /* wait for subscription propagation */
    /* Only messages published after this point can be received */
    ```

=== "C++"

    ```cpp
    // Time needed for subscription to propagate to PUB
    sub.connect("tcp://127.0.0.1:5556");
    sub.set_subscription("topic");
    msleep(100);  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

=== "Java"

    ```java
    // Time needed for subscription to propagate to PUB
    sub.connect("tcp://127.0.0.1:5556");
    sub.setSubscription("topic");
    Thread.sleep(100);  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

=== "Python"

    ```python
    # Time needed for subscription to propagate to PUB
    sub.connect("tcp://127.0.0.1:5556")
    sub.set_subscription("topic")
    time.sleep(0.1)  # wait for subscription propagation
    # Only messages published after this point can be received
    ```

=== "Node/TypeScript"

    ```typescript
    // Time needed for subscription to propagate to PUB
    sub.connect("tcp://127.0.0.1:5556");
    sub.setSubscription("topic");
    await sleep(100);  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

=== "C#/.NET"

    ```csharp
    // Time needed for subscription to propagate to PUB
    sub.Connect("tcp://127.0.0.1:5556");
    sub.SetSubscription("topic");
    Thread.Sleep(100);  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

=== "Rust"

    ```rust
    // Time needed for subscription to propagate to PUB
    sub.connect("tcp://127.0.0.1:5556");
    sub.set_subscription("topic");
    thread::sleep(Duration::from_millis(100));  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

=== "Go"

    ```go
    // Time needed for subscription to propagate to PUB
    sub.Connect("tcp://127.0.0.1:5556")
    sub.SetSubscription("topic")
    time.Sleep(100 * time.Millisecond)  // wait for subscription propagation
    // Only messages published after this point can be received
    ```

### Direction Constraints

PUB/SUB each have their own dedicated API:

=== "C"

    ```c
    /* PUB: send via zlink_publish(). Cannot attach recv handler */
    zlink_msg_t part;
    zlink_msg_init_size(&part, 5);
    memcpy(zlink_msg_data(&part), "sunny", 5);
    zlink_publish(pub, "weather", &part, 1, 0);  /* OK */

    /* Using zlink_send() on PUB → ENOTSUP */
    zlink_send(pub, &part, 1, 0);  /* errno = ENOTSUP */

    /* SUB: receive via zlink_subscribe(). Cannot send/publish */
    zlink_publish(sub, "weather", &part, 1, 0);  /* errno = ENOTSUP */
    zlink_send(sub, &part, 1, 0);               /* errno = ENOTSUP */
    ```

=== "C++"

    ```cpp
    // PUB: send via publish(). Cannot attach recv handler
    pub.publish("weather", "sunny");  // OK

    // Using send() on PUB → throws ENOTSUP
    // pub.send("sunny");  // error: ENOTSUP

    // SUB: receive via subscribe(). Cannot send/publish
    // sub.publish("weather", "sunny");  // error: ENOTSUP
    // sub.send("sunny");               // error: ENOTSUP
    ```

=== "Java"

    ```java
    // PUB: send via publish(). Cannot attach recv handler
    pub.publish("weather", "sunny");  // OK

    // Using send() on PUB → throws ENOTSUP
    // pub.send("sunny");  // error: ENOTSUP

    // SUB: receive via subscribe(). Cannot send/publish
    // sub.publish("weather", "sunny");  // error: ENOTSUP
    // sub.send("sunny");               // error: ENOTSUP
    ```

=== "Python"

    ```python
    # PUB: send via publish(). Cannot attach recv handler
    pub.publish("weather", b"sunny")  # OK

    # Using send() on PUB → raises ENOTSUP
    # pub.send(b"sunny")  # error: ENOTSUP

    # SUB: receive via subscribe(). Cannot send/publish
    # sub.publish("weather", b"sunny")  # error: ENOTSUP
    # sub.send(b"sunny")               # error: ENOTSUP
    ```

=== "Node/TypeScript"

    ```typescript
    // PUB: send via publish(). Cannot attach recv handler
    pub.publish("weather", Buffer.from("sunny"));  // OK

    // Using send() on PUB → throws ENOTSUP
    // pub.send(Buffer.from("sunny"));  // error: ENOTSUP

    // SUB: receive via subscribe(). Cannot send/publish
    // sub.publish("weather", Buffer.from("sunny"));  // error: ENOTSUP
    // sub.send(Buffer.from("sunny"));               // error: ENOTSUP
    ```

=== "C#/.NET"

    ```csharp
    // PUB: send via Publish(). Cannot attach recv handler
    pub.Publish("weather", "sunny");  // OK

    // Using Send() on PUB → throws ENOTSUP
    // pub.Send("sunny");  // error: ENOTSUP

    // SUB: receive via Subscribe(). Cannot send/publish
    // sub.Publish("weather", "sunny");  // error: ENOTSUP
    // sub.Send("sunny");               // error: ENOTSUP
    ```

=== "Rust"

    ```rust
    // PUB: send via publish(). Cannot attach recv handler
    pub_sock.publish("weather", b"sunny");  // OK

    // Using send() on PUB → returns Err(ENOTSUP)
    // pub_sock.send(b"sunny");  // error: ENOTSUP

    // SUB: receive via subscribe(). Cannot send/publish
    // sub.publish("weather", b"sunny");  // error: ENOTSUP
    // sub.send(b"sunny");               // error: ENOTSUP
    ```

=== "Go"

    ```go
    // PUB: send via Publish(). Cannot attach recv handler
    pubSock.Publish("weather", zlink.NewMessage([]byte("sunny")))  // OK

    // Using Send() on PUB returns ENOTSUP
    // pubSock.Send(zlink.NewMessage([]byte("sunny")))  // error: ENOTSUP

    // SUB: receive via Subscribe(). Cannot send/publish
    // sub.Publish("weather", zlink.NewMessage([]byte("sunny")))  // error: ENOTSUP
    // sub.Send(zlink.NewMessage([]byte("sunny")))                // error: ENOTSUP
    ```

---

# Part II: XPUB/XSUB

## 8. XPUB/XSUB Overview

XPUB/XSUB are advanced publish-subscribe sockets that allow applications to handle subscription frames directly. They are used for building proxies/brokers, subscription monitoring, and Last-Value Caching.

### SUB vs XSUB — Key Difference

| | SUB | XSUB |
|---|-----|------|
| **Topic registration** | `zlink_set_subscription()` | `zlink_set_subscription()` (same) |
| **Message receive** | `zlink_subscribe()` — filtered | `zlink_subscribe()` — no filter, receives all |
| **Local filter** | **On** — drops non-matching | **Off** — passes all messages |
| **No subscriptions** | Receives nothing | Receives all messages |
| **Implementation** | `xsub_t` subclass (`filter=true`) | Base class (`filter=false`) |

XSUB is needed in proxies because it passes all messages through
without subscription state. Topic registration is sent to upstream
identically via `zlink_set_subscription()` on both.

### PUB vs XPUB — Key Difference

| | PUB | XPUB |
|---|-----|------|
| **Message publish** | `zlink_publish()` | `zlink_publish()` (same) |
| **Subscription events** | Not exposed | `zlink_subscription_event()` |

XPUB can observe which clients subscribe to or unsubscribe from which topics.

### XSUB/XPUB Roles in a Proxy

A proxy has **two separate flows**:

```
Data flow (publish):
  PUB ──► XSUB ══ proxy forward ══► XPUB ──► SUB

Subscription flow (propagation, reverse direction):
  PUB ◄── XSUB ◄── proxy app ◄── XPUB ◄── SUB
```

#### Data Flow

| Step | Actor | Action | Note |
|------|-------|--------|------|
| 1 | PUB | `zlink_publish(pub, topic, ...)` | Publish data |
| 2 | proxy internal | XSUB internal recv → XPUB internal send | Handled by `zlink_proxy()` |
| 3 | SUB | `zlink_subscribe()` or callback | Final consumption |

> **Key point:** The proxy data relay uses `socket_base_t` internal methods
> inside `zlink_proxy(xsub, xpub, NULL)`, not the public
> `zlink_send()`/`zlink_recv()` API.
> Users never need to call XSUB recv → XPUB send directly.

#### Subscription Propagation Flow

| Step | Actor | Action | API |
|------|-------|--------|-----|
| 1 | SUB | Subscribe → arrives at XPUB via wire | `zlink_set_subscription(sub, "weather")` |
| 2 | proxy app | Receive subscription event from XPUB | `zlink_subscription_event(xpub, ...)` |
| 3 | proxy app | Register on XSUB → propagates to PUB via wire | `zlink_set_subscription(xsub, "weather")` |
| 4 | PUB | Publish matching data | `zlink_publish(pub, "weather", ...)` |
| 5 | data flow | XSUB → XPUB → SUB | Handled by `zlink_proxy()` |

> `zlink_set_subscription()` sends subscription info upstream on the wire
> identically for both SUB and XSUB. Calling it on XSUB in a proxy is
> **not because "XSUB can send"** — the proxy app registers subscription
> events received from XPUB onto XSUB to propagate them upstream.

#### Why XSUB/XPUB?

| Question | With SUB/PUB | With XSUB/XPUB |
|----------|-------------|-----------------|
| Data pass-through | SUB local filter on — must register subscriptions | XSUB local filter off — **passes all** |
| Subscription events | PUB does not expose them | XPUB provides `subscription_event()` |
| Proxy suitability | Proxy must manage topics itself | **Relay-only — ideal for proxy** |

### PUB/SUB Socket Public API Summary

| Public API | PUB | SUB | XPUB | XSUB |
|------------|-----|-----|------|------|
| `zlink_publish()` | OK | — | OK | — |
| `zlink_subscribe()` | — | OK | — | OK |
| `zlink_subscribe_handler()` | — | OK | — | OK |
| `zlink_set_subscription()` | — | OK | — | OK |
| `zlink_subscription_event()` | — | — | OK | — |
| Local filter | N/A | **On** | N/A | **Off** |

> `zlink_send()` / `zlink_recv()` return `ENOTSUP` on all 4 PUB/SUB sockets.
> Use `zlink_publish()` for publishing and `zlink_subscribe()` for receiving.

> Proxy patterns (built-in `zlink_proxy()`, manual proxy construction,
> ROUTER/DEALER broker) are covered in the
> [Proxy Guide](03-6-proxy.md).

## 9. Subscription Frame Format

Subscription/unsubscription frames between XPUB/XSUB follow this format:

| Byte | Meaning |
|--------|------|
| `0x01` + topic | Subscription request |
| `0x00` + topic | Unsubscription request |

=== "C"

    ```c
    /* Subscribe from XSUB */
    zlink_set_subscription(xsub, "A");

    /* Unsubscribe from XSUB */
    zlink_unset_subscription(xsub, "A");
    ```

=== "C++"

    ```cpp
    // Subscribe from XSUB
    xsub.set_subscription("A");

    // Unsubscribe from XSUB
    xsub.unset_subscription("A");
    ```

=== "Java"

    ```java
    // Subscribe from XSUB
    xsub.setSubscription("A");

    // Unsubscribe from XSUB
    xsub.unsetSubscription("A");
    ```

=== "Python"

    ```python
    # Subscribe from XSUB
    xsub.set_subscription("A")

    # Unsubscribe from XSUB
    xsub.unset_subscription("A")
    ```

=== "Node/TypeScript"

    ```typescript
    // Subscribe from XSUB
    xsub.setSubscription("A");

    // Unsubscribe from XSUB
    xsub.unsetSubscription("A");
    ```

=== "C#/.NET"

    ```csharp
    // Subscribe from XSUB
    xsub.SetSubscription("A");

    // Unsubscribe from XSUB
    xsub.UnsetSubscription("A");
    ```

=== "Rust"

    ```rust
    // Subscribe from XSUB
    xsub.set_subscription("A");

    // Unsubscribe from XSUB
    xsub.unset_subscription("A");
    ```

=== "Go"

    ```go
    // Subscribe from XSUB
    xsub.SetSubscription("A")

    // Unsubscribe from XSUB
    xsub.UnsetSubscription("A")
    ```

XPUB receives subscription frames with `zlink_subscription_event()`:

=== "C"

    ```c
    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    zlink_bind(xpub, "tcp://*:5557");

    zlink_routing_id_t source_rid;
    int subscribed = 0;
    char topic[256];
    size_t topic_len = sizeof(topic);

    zlink_subscription_event(
      xpub, &source_rid, &subscribed, topic, &topic_len, 0);
    ```

=== "C++"

    ```cpp
    zlink::xpub_socket_t xpub(ctx);
    xpub.bind("tcp://*:5557");

    auto [source_rid, subscribed, topic] = xpub.subscription_event();
    ```

=== "Java"

    ```java
    XPubSocket xpub = new XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    SubscriptionEvent event = xpub.subscriptionEvent();
    // event.isSubscribed(), event.topic()
    ```

=== "Python"

    ```python
    xpub = zlink.XPubSocket(ctx)
    xpub.bind("tcp://*:5557")

    source_rid, subscribed, topic = xpub.subscription_event()
    ```

=== "Node/TypeScript"

    ```typescript
    const xpub = new zlink.XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    const [sourceRid, subscribed, topic] = xpub.subscriptionEvent();
    ```

=== "C#/.NET"

    ```csharp
    var xpub = new XPubSocket(ctx);
    xpub.Bind("tcp://*:5557");

    var (sourceRid, subscribed, topic) = xpub.SubscriptionEvent();
    ```

=== "Rust"

    ```rust
    let xpub = ctx.xpub_socket();
    xpub.bind("tcp://*:5557");

    let (source_rid, subscribed, topic) = xpub.subscription_event();
    ```

=== "Go"

    ```go
    xpub, _ := ctx.XPubSocket()
    xpub.Bind("tcp://*:5557")

    sourceRid, subscribed, topic, _ := xpub.SubscriptionEvent()
    ```

> Reference: `core/tests/test_xpub_manual.cpp` -- `subscription1[] = {1, 'A'}`, `unsubscription1[] = {0, 'A'}`

## 10. XPUB Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_PUB_OPT_MANUAL` | int | 0 | Enable manual subscription management mode |
| `ZLINK_PUB_OPT_VERBOSE` | int | 0 | Forward duplicate subscription messages as well |
| `zlink_set_subscription()` | -- | -- | (MANUAL mode) Add subscription to the current pipe |
| `zlink_unset_subscription()` | -- | -- | (MANUAL mode) Remove subscription from the current pipe |

### XPUB_MANUAL Mode

By default, XPUB processes SUB subscriptions automatically. In MANUAL mode, after receiving a subscription frame, the application explicitly decides the actual subscription using `zlink_set_subscription()` / `zlink_unset_subscription()`.

=== "C"

    ```c
    /* Enable MANUAL mode */
    int manual = 1;
    zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

    /* zlink_subscription_event() returns subscribed=1, topic="A"
       Then apply transformed subscription: */
    zlink_set_subscription(xpub, "XA");

    /* Publish */
    zlink_msg_t msg_a;
    zlink_msg_init_size(&msg_a, 1);
    memcpy(zlink_msg_data(&msg_a), "A", 1);
    zlink_publish(xpub, NULL, &msg_a, 1, 0);   /* does not reach the subscriber */

    zlink_msg_t msg_xa;
    zlink_msg_init_size(&msg_xa, 2);
    memcpy(zlink_msg_data(&msg_xa), "XA", 2);
    zlink_publish(xpub, NULL, &msg_xa, 1, 0);  /* subscriber receives this */
    ```

=== "C++"

    ```cpp
    // Enable MANUAL mode
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1);

    // subscription_event() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.set_subscription("XA");

    // Publish
    xpub.publish("", "A");   // does not reach the subscriber
    xpub.publish("", "XA");  // subscriber receives this
    ```

=== "Java"

    ```java
    // Enable MANUAL mode
    xpub.setPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    // subscriptionEvent() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.setSubscription("XA");

    // Publish
    xpub.publish("", "A");   // does not reach the subscriber
    xpub.publish("", "XA");  // subscriber receives this
    ```

=== "Python"

    ```python
    # Enable MANUAL mode
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1)

    # subscription_event() returns subscribed=True, topic="A"
    # Then apply transformed subscription:
    xpub.set_subscription("XA")

    # Publish
    xpub.publish("", b"A")   # does not reach the subscriber
    xpub.publish("", b"XA")  # subscriber receives this
    ```

=== "Node/TypeScript"

    ```typescript
    // Enable MANUAL mode
    xpub.setPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    // subscriptionEvent() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.setSubscription("XA");

    // Publish
    xpub.publish("", Buffer.from("A"));   // does not reach the subscriber
    xpub.publish("", Buffer.from("XA"));  // subscriber receives this
    ```

=== "C#/.NET"

    ```csharp
    // Enable MANUAL mode
    xpub.SetPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    // SubscriptionEvent() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.SetSubscription("XA");

    // Publish
    xpub.Publish("", "A");   // does not reach the subscriber
    xpub.Publish("", "XA");  // subscriber receives this
    ```

=== "Rust"

    ```rust
    // Enable MANUAL mode
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1);

    // subscription_event() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.set_subscription("XA");

    // Publish
    xpub.publish("", b"A");   // does not reach the subscriber
    xpub.publish("", b"XA");  // subscriber receives this
    ```

=== "Go"

    ```go
    // Enable MANUAL mode
    xpub.SetOption(zlink.OptionPubManual, 1)

    // SubscriptionEvent() returns subscribed=true, topic="A"
    // Then apply transformed subscription:
    xpub.SetSubscription("XA")

    // Publish
    xpub.Publish("", zlink.NewMessage([]byte("A")))   // does not reach the subscriber
    xpub.Publish("", zlink.NewMessage([]byte("XA")))  // subscriber receives this
    ```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_basic()`: subscription request for A → transformed to B

## 11. XPUB/XSUB Usage Patterns

### Pattern 1: Building a Proxy/Broker

Build a PUB/SUB proxy using XSUB (frontend) + XPUB (backend).

=== "C"

    ```c
    /* Proxy frontend: PUBs connect here */
    void *xsub = zlink_socket(ctx, ZLINK_XSUB);
    zlink_bind(xsub, "tcp://*:5556");

    /* Proxy backend: SUBs connect here */
    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    zlink_bind(xpub, "tcp://*:5557");

    /* Run proxy (forwards messages and subscriptions bidirectionally) */
    zlink_proxy(xsub, xpub, NULL);
    ```

=== "C++"

    ```cpp
    // Proxy frontend: PUBs connect here
    zlink::xsub_socket_t xsub(ctx);
    xsub.bind("tcp://*:5556");

    // Proxy backend: SUBs connect here
    zlink::xpub_socket_t xpub(ctx);
    xpub.bind("tcp://*:5557");

    // Run proxy (forwards messages and subscriptions bidirectionally)
    zlink::proxy(xsub, xpub);
    ```

=== "Java"

    ```java
    // Proxy frontend: PUBs connect here
    XSubSocket xsub = new XSubSocket(ctx);
    xsub.bind("tcp://*:5556");

    // Proxy backend: SUBs connect here
    XPubSocket xpub = new XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    // Run proxy (forwards messages and subscriptions bidirectionally)
    Proxy.run(xsub, xpub);
    ```

=== "Python"

    ```python
    # Proxy frontend: PUBs connect here
    xsub = zlink.XSubSocket(ctx)
    xsub.bind("tcp://*:5556")

    # Proxy backend: SUBs connect here
    xpub = zlink.XPubSocket(ctx)
    xpub.bind("tcp://*:5557")

    # Run proxy (forwards messages and subscriptions bidirectionally)
    zlink.proxy(xsub, xpub)
    ```

=== "Node/TypeScript"

    ```typescript
    // Proxy frontend: PUBs connect here
    const xsub = new zlink.XSubSocket(ctx);
    xsub.bind("tcp://*:5556");

    // Proxy backend: SUBs connect here
    const xpub = new zlink.XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    // Run proxy (forwards messages and subscriptions bidirectionally)
    zlink.proxy(xsub, xpub);
    ```

=== "C#/.NET"

    ```csharp
    // Proxy frontend: PUBs connect here
    var xsub = new XSubSocket(ctx);
    xsub.Bind("tcp://*:5556");

    // Proxy backend: SUBs connect here
    var xpub = new XPubSocket(ctx);
    xpub.Bind("tcp://*:5557");

    // Run proxy (forwards messages and subscriptions bidirectionally)
    Proxy.Run(xsub, xpub);
    ```

=== "Rust"

    ```rust
    // Proxy frontend: PUBs connect here
    let xsub = ctx.xsub_socket();
    xsub.bind("tcp://*:5556");

    // Proxy backend: SUBs connect here
    let xpub = ctx.xpub_socket();
    xpub.bind("tcp://*:5557");

    // Run proxy (forwards messages and subscriptions bidirectionally)
    zlink::proxy(&xsub, &xpub);
    ```

=== "Go"

    ```go
    // Proxy frontend: PUBs connect here
    xsub := ctx.XSubSocket()
    xsub.Bind("tcp://*:5556")

    // Proxy backend: SUBs connect here
    xpub := ctx.XPubSocket()
    xpub.Bind("tcp://*:5557")

    // Run proxy (forwards messages and subscriptions bidirectionally)
    zlink.Proxy(xsub, xpub, nil)
    ```

### Pattern 2: MANUAL Mode Proxy (Subscription Transformation)

An advanced proxy that transforms or filters subscription requests.

=== "C"

    ```c
    int manual = 1;
    zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

    for (;;) {
        zlink_routing_id_t source_rid;
        int subscribed = 0;
        char topic[256];
        size_t topic_len = sizeof(topic);

        zlink_subscription_event(
          xpub, &source_rid, &subscribed, topic, &topic_len, 0);

        if (subscribed) {
            /* Register subscription */
            zlink_set_subscription(xpub, topic);

            /* Propagate subscription upstream (XSUB) */
            zlink_set_subscription(xsub, topic);
        } else {
            /* Unsubscription */
            zlink_unset_subscription(xpub, topic);

            zlink_unset_subscription(xsub, topic);
        }
    }
    ```

=== "C++"

    ```cpp
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1);

    for (;;) {
        auto [source_rid, subscribed, topic] = xpub.subscription_event();

        if (subscribed) {
            // Register subscription
            xpub.set_subscription(topic);

            // Propagate subscription upstream (XSUB)
            xsub.set_subscription(topic);
        } else {
            // Unsubscription
            xpub.unset_subscription(topic);

            xsub.unset_subscription(topic);
        }
    }
    ```

=== "Java"

    ```java
    xpub.setPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    while (true) {
        SubscriptionEvent event = xpub.subscriptionEvent();

        if (event.isSubscribed()) {
            // Register subscription
            xpub.setSubscription(event.topic());

            // Propagate subscription upstream (XSUB)
            xsub.setSubscription(event.topic());
        } else {
            // Unsubscription
            xpub.unsetSubscription(event.topic());

            xsub.unsetSubscription(event.topic());
        }
    }
    ```

=== "Python"

    ```python
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1)

    while True:
        source_rid, subscribed, topic = xpub.subscription_event()

        if subscribed:
            # Register subscription
            xpub.set_subscription(topic)

            # Propagate subscription upstream (XSUB)
            xsub.set_subscription(topic)
        else:
            # Unsubscription
            xpub.unset_subscription(topic)

            xsub.unset_subscription(topic)
    ```

=== "Node/TypeScript"

    ```typescript
    xpub.setPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    while (true) {
        const [sourceRid, subscribed, topic] = xpub.subscriptionEvent();

        if (subscribed) {
            // Register subscription
            xpub.setSubscription(topic);

            // Propagate subscription upstream (XSUB)
            xsub.setSubscription(topic);
        } else {
            // Unsubscription
            xpub.unsetSubscription(topic);

            xsub.unsetSubscription(topic);
        }
    }
    ```

=== "C#/.NET"

    ```csharp
    xpub.SetPubOption(ZLINK_PUB_OPT_MANUAL, 1);

    while (true) {
        var (sourceRid, subscribed, topic) = xpub.SubscriptionEvent();

        if (subscribed) {
            // Register subscription
            xpub.SetSubscription(topic);

            // Propagate subscription upstream (XSUB)
            xsub.SetSubscription(topic);
        } else {
            // Unsubscription
            xpub.UnsetSubscription(topic);

            xsub.UnsetSubscription(topic);
        }
    }
    ```

=== "Rust"

    ```rust
    xpub.set_pub_option(ZLINK_PUB_OPT_MANUAL, 1);

    loop {
        let (source_rid, subscribed, topic) = xpub.subscription_event();

        if subscribed {
            // Register subscription
            xpub.set_subscription(&topic);

            // Propagate subscription upstream (XSUB)
            xsub.set_subscription(&topic);
        } else {
            // Unsubscription
            xpub.unset_subscription(&topic);

            xsub.unset_subscription(&topic);
        }
    }
    ```

=== "Go"

    ```go
    xpub.SetOption(zlink.OptionPubManual, 1)

    for {
        _, subscribed, topic, _ := xpub.SubscriptionEvent()

        if subscribed {
            // Register subscription
            xpub.SetSubscription(topic)

            // Propagate subscription upstream (XSUB)
            xsub.SetSubscription(topic)
        } else {
            // Unsubscription
            xpub.UnsetSubscription(topic)

            xsub.UnsetSubscription(topic)
        }
    }
    ```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_xpub_proxy_unsubscribe_on_disconnect()`

### Pattern 3: Subscription Monitoring

Use XPUB to observe which clients subscribe to which topics.

=== "C"

    ```c
    void *xpub = zlink_socket(ctx, ZLINK_XPUB);
    zlink_bind(xpub, "tcp://*:5557");

    for (;;) {
        zlink_routing_id_t source_rid;
        int subscribed = 0;
        char topic[256];
        size_t topic_len = sizeof(topic);

        zlink_subscription_event(
          xpub, &source_rid, &subscribed, topic, &topic_len, 0);
        printf("%s: %.*s\n", subscribed ? "New subscription" : "Unsubscription",
               (int) topic_len, topic);
    }
    ```

=== "C++"

    ```cpp
    zlink::xpub_socket_t xpub(ctx);
    xpub.bind("tcp://*:5557");

    for (;;) {
        auto [source_rid, subscribed, topic] = xpub.subscription_event();
        std::cout << (subscribed ? "New subscription" : "Unsubscription")
                  << ": " << topic << std::endl;
    }
    ```

=== "Java"

    ```java
    XPubSocket xpub = new XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    while (true) {
        SubscriptionEvent event = xpub.subscriptionEvent();
        System.out.printf("%s: %s%n",
            event.isSubscribed() ? "New subscription" : "Unsubscription",
            event.topic());
    }
    ```

=== "Python"

    ```python
    xpub = zlink.XPubSocket(ctx)
    xpub.bind("tcp://*:5557")

    while True:
        source_rid, subscribed, topic = xpub.subscription_event()
        label = "New subscription" if subscribed else "Unsubscription"
        print(f"{label}: {topic}")
    ```

=== "Node/TypeScript"

    ```typescript
    const xpub = new zlink.XPubSocket(ctx);
    xpub.bind("tcp://*:5557");

    while (true) {
        const [sourceRid, subscribed, topic] = xpub.subscriptionEvent();
        console.log(`${subscribed ? "New subscription" : "Unsubscription"}: ${topic}`);
    }
    ```

=== "C#/.NET"

    ```csharp
    var xpub = new XPubSocket(ctx);
    xpub.Bind("tcp://*:5557");

    while (true) {
        var (sourceRid, subscribed, topic) = xpub.SubscriptionEvent();
        Console.WriteLine($"{(subscribed ? "New subscription" : "Unsubscription")}: {topic}");
    }
    ```

=== "Rust"

    ```rust
    let xpub = ctx.xpub_socket();
    xpub.bind("tcp://*:5557");

    loop {
        let (source_rid, subscribed, topic) = xpub.subscription_event();
        println!("{}: {}",
            if subscribed { "New subscription" } else { "Unsubscription" },
            topic);
    }
    ```

=== "Go"

    ```go
    xpub, _ := ctx.XPubSocket()
    xpub.Bind("tcp://*:5557")

    for {
        _, subscribed, topic, _ := xpub.SubscriptionEvent()
        label := "Unsubscription"
        if subscribed {
            label = "New subscription"
        }
        fmt.Printf("%s: %s\n", label, topic)
    }
    ```

### Pattern 4: Automatic Unsubscribe on Subscriber Disconnect

When a SUB disconnects, an unsubscribe frame is automatically delivered to XPUB.

=== "C"

    ```c
    /* After SUB disconnects */
    zlink_close(sub);

    /* The next zlink_subscription_event() returns
       subscribed=0 and the previously subscribed topic */
    ```

=== "C++"

    ```cpp
    // After SUB disconnects
    sub.close();

    // The next subscription_event() returns
    // subscribed=false and the previously subscribed topic
    ```

=== "Java"

    ```java
    // After SUB disconnects
    sub.close();

    // The next subscriptionEvent() returns
    // subscribed=false and the previously subscribed topic
    ```

=== "Python"

    ```python
    # After SUB disconnects
    sub.close()

    # The next subscription_event() returns
    # subscribed=False and the previously subscribed topic
    ```

=== "Node/TypeScript"

    ```typescript
    // After SUB disconnects
    sub.close();

    // The next subscriptionEvent() returns
    // subscribed=false and the previously subscribed topic
    ```

=== "C#/.NET"

    ```csharp
    // After SUB disconnects
    sub.Close();

    // The next SubscriptionEvent() returns
    // subscribed=false and the previously subscribed topic
    ```

=== "Rust"

    ```rust
    // After SUB disconnects
    sub.close();

    // The next subscription_event() returns
    // subscribed=false and the previously subscribed topic
    ```

=== "Go"

    ```go
    // After SUB disconnects
    sub.Close()

    // The next subscription_event() returns
    // subscribed=false and the previously subscribed topic
    ```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_xpub_proxy_unsubscribe_on_disconnect()`

## 12. Caveats

### Subscription Propagation Timing

Subscription messages are propagated asynchronously. Messages published immediately after subscribing may not be received.

=== "C"

    ```c
    zlink_connect(sub, endpoint);
    zlink_set_subscription(sub, "topic");
    /* Publishing a "topic" message at this point may result in loss */
    msleep(100);  /* wait for subscription propagation */
    /* Messages published after this point can be received */
    ```

=== "C++"

    ```cpp
    sub.connect(endpoint);
    sub.set_subscription("topic");
    // Publishing a "topic" message at this point may result in loss
    msleep(100);  // wait for subscription propagation
    // Messages published after this point can be received
    ```

=== "Java"

    ```java
    sub.connect(endpoint);
    sub.setSubscription("topic");
    // Publishing a "topic" message at this point may result in loss
    Thread.sleep(100);  // wait for subscription propagation
    // Messages published after this point can be received
    ```

=== "Python"

    ```python
    sub.connect(endpoint)
    sub.set_subscription("topic")
    # Publishing a "topic" message at this point may result in loss
    time.sleep(0.1)  # wait for subscription propagation
    # Messages published after this point can be received
    ```

=== "Node/TypeScript"

    ```typescript
    sub.connect(endpoint);
    sub.setSubscription("topic");
    // Publishing a "topic" message at this point may result in loss
    await sleep(100);  // wait for subscription propagation
    // Messages published after this point can be received
    ```

=== "C#/.NET"

    ```csharp
    sub.Connect(endpoint);
    sub.SetSubscription("topic");
    // Publishing a "topic" message at this point may result in loss
    Thread.Sleep(100);  // wait for subscription propagation
    // Messages published after this point can be received
    ```

=== "Rust"

    ```rust
    sub.connect(endpoint);
    sub.set_subscription("topic");
    // Publishing a "topic" message at this point may result in loss
    thread::sleep(Duration::from_millis(100));  // wait for subscription propagation
    // Messages published after this point can be received
    ```

=== "Go"

    ```go
    sub.Connect(endpoint)
    sub.SetSubscription("topic")
    // Publishing a "topic" message at this point may result in loss
    time.Sleep(100 * time.Millisecond)  // wait for subscription propagation
    // Messages published after this point can be received
    ```

### Subscription Management in XPUB MANUAL Mode

In MANUAL mode, if `zlink_set_subscription()` is not called after receiving a subscription frame, that subscription is not registered. Subscriptions must be explicitly processed.

### Multiple Subscribers → Single XPUB

When multiple SUBs subscribe to the same topic, the XPUB subscription is maintained until all SUBs have unsubscribed.

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_missing_subscriptions()`: processing two subscribers sequentially to prevent omissions

---
[← PAIR](03-1-pair.md) | [DEALER →](03-3-dealer.md)
