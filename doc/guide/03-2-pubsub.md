[English](03-2-pubsub.md) | [한국어](03-2-pubsub.ko.md)

# PUB/SUB/XPUB/XSUB Publish-Subscribe

## 1. Overview

The Publish-Subscribe pattern distributes messages based on topics. zlink provides two levels: basic PUB/SUB and advanced XPUB/XSUB.

| Socket | Role | Characteristics |
|------|------|------|
| **PUB** | Publisher | Broadcasts to all subscribers. Cannot receive. |
| **SUB** | Subscriber | Topic prefix match filtering. Cannot send. |
| **XPUB** | Advanced Publisher | PUB + can receive subscription frames |
| **XSUB** | Advanced Subscriber | SUB + can send subscription frames directly |

**Valid socket combinations:**
- PUB → SUB, PUB → XSUB
- XPUB → SUB, XPUB → XSUB

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

### Publisher (PUB)

```c
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* Publish message -- dropped if there are no subscribers */
zlink_msg_t part;
zlink_msg_init_size(&part, 14);
memcpy(zlink_msg_data(&part), "weather: sunny", 14);
zlink_publish(pub, NULL, &part, 1, 0);
```

### Subscriber (SUB)

```c
void on_topic(const zlink_routing_id_t *source_rid,
              const char *topic, size_t topic_len,
              zlink_msg_t *parts, size_t part_count,
              void *userdata)
{
    printf("Topic: %.*s, Data: %.*s\n",
           (int)topic_len, topic,
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}

void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_subscribe_handler(sub, on_topic, NULL);
zlink_connect(sub, "tcp://127.0.0.1:5556");

/* Subscribe to topic -- set after connect */
zlink_set_subscription(sub, "weather");

/* Messages are dispatched to on_topic callback */
```

> Reference: `core/tests/test_pubsub.cpp` -- empty subscription ("") → receives all messages

### Sending and Receiving Summary

| Socket | Direction | Registration Call | Notes |
|--------|-----------|-------------------|-------|
| PUB | Send only | N/A | Cannot receive; does not accept a handler |
| SUB | Receive only | `zlink_subscribe_handler()` | `topic` + `parts[]` — topic extracted by prefix match |
| XPUB | Bidirectional | `zlink_subscription_event_handler()` | Receives subscription events, not data |
| XSUB | Bidirectional | `zlink_subscribe_handler()` | Sends subscription frames; receives data via fair-queue |

SUB uses `zlink_subscribe_handler()` instead of `zlink_recv_handler()`
because the I/O thread separates the matched topic from the payload before
invoking the callback — the handler receives `topic` and `parts[]` as
distinct parameters.

**Pull mode** is also available for SUB: call `zlink_recv()` without
attaching a handler. The multipart message is received without topic separation.

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

```c
/* Subscribe to multiple topics */
zlink_set_subscription(sub, "weather");
zlink_set_subscription(sub, "sports");

/* Unsubscribe */
zlink_unset_subscription(sub, "sports");
```

### Empty Subscription (All Messages)

```c
/* Subscribe with empty string -- receives all messages */
zlink_set_subscription(sub, "");
```

> Reference: `core/tests/test_pubsub.cpp` -- `zlink_set_subscription(subscriber, "")`

## 4. Message Format

PUB/SUB messages can use two formats.

### Single Frame (Topic Included)

The topic is embedded in the data. Simple but requires parsing.

```c
/* Publish */
zlink_msg_t part;
zlink_msg_init_size(&part, 14);
memcpy(zlink_msg_data(&part), "weather: sunny", 14);
zlink_publish(pub, NULL, &part, 1, 0);

/* SUB handler callback receives:
   topic = "weather: sunny" (full match prefix)
   parts[0] = "weather: sunny" (full data) */
```

### Multipart Frame (Topic + Data Separated)

The topic and data are sent as separate frames. No parsing needed.

```c
/* Publish: [topic][payload] */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 7);
memcpy(zlink_msg_data(&parts[0]), "weather", 7);
zlink_msg_init_size(&parts[1], 5);
memcpy(zlink_msg_data(&parts[1]), "sunny", 5);
zlink_publish(pub, NULL, parts, 2, 0);

/* SUB handler callback receives:
   topic = "weather"
   parts[0] = "sunny" (payload frames only) */
```

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

```c
/* PUB */
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* SUB -- receive all messages */
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_subscribe_handler(sub, on_topic, NULL);
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub, "");

msleep(100);  /* time for subscription to reach PUB */

zlink_msg_t msg;
zlink_msg_init_size(&msg, 4);
memcpy(zlink_msg_data(&msg), "test", 4);
zlink_publish(pub, NULL, &msg, 1, 0);

/* on_topic callback receives "test" asynchronously */
```

> Reference: `core/tests/test_pubsub.cpp` -- `test_tcp()`

### Pattern 2: Multiple SUBs

Multiple SUBs connect to a single PUB. Each SUB receives only its own topics.

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

### Pattern 3: Multiple PUBs → SUB

A SUB can connect to multiple PUBs. It receives messages from all PUBs via fair-queue.

```c
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_set_subscription(sub, "");
zlink_connect(sub, "tcp://pub1:5556");
zlink_connect(sub, "tcp://pub2:5557");
```

## 7. PUB/SUB Caveats

### Slow Subscriber (Drop on HWM Exceeded)

PUB/XPUB operate in **lossy mode** by default. When a slow subscriber's
send queue reaches the HWM, messages to that subscriber are **silently
dropped** (no error returned).

```c
/* Option 1: Increase buffer by adjusting HWM */
int hwm = 100000;
zlink_set_option(pub, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
```

#### XPUB_NODROP — Backpressure Instead of Drop

Setting `ZLINK_PUB_OPT_NODROP` disables lossy mode. When the HWM is
reached, instead of dropping messages, `EAGAIN` is returned so the
caller can handle backpressure directly.

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

| Mode | Behavior on HWM | When to Use |
|------|-----------------|-------------|
| Default (lossy) | Silent drop — no error, message lost | Only latest data matters (sensor, tick) |
| `XPUB_NODROP=1` | Returns `EAGAIN` — caller controls | Message loss is not acceptable |

> `ZLINK_PUB_OPT_NODROP` is an XPUB-only socket option.
> It is not available on regular PUB sockets.

### Late Joiner (Messages Lost Before Subscription)

Messages published before the subscription message from SUB reaches PUB are lost.

```c
/* Time needed for subscription to propagate to PUB */
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_set_subscription(sub, "topic");
msleep(100);  /* wait for subscription propagation */
/* Only messages published after this point can be received */
```

### Direction Constraints

PUB/SUB each have their own dedicated API:

```c
/* PUB: send via zlink_publish(). Cannot attach recv handler */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "sunny", 5);
zlink_publish(pub, "weather", &part, 1, 0);  /* OK */

/* Using zlink_send() on PUB → ENOTSUP */
zlink_send(pub, &part, 1, 0);  /* errno = ENOTSUP */

/* SUB: receive via zlink_subscribe_handler(). Cannot send/publish */
zlink_publish(sub, "weather", &part, 1, 0);  /* errno = ENOTSUP */
zlink_send(sub, &part, 1, 0);               /* errno = ENOTSUP */
```

---

# Part II: XPUB/XSUB

## 8. XPUB/XSUB Overview

XPUB/XSUB are advanced publish-subscribe sockets that allow applications to handle subscription frames directly. They are used for building proxies/brokers, subscription monitoring, and Last-Value Caching.

### SUB vs XSUB — Key Difference

| | SUB | XSUB |
|---|-----|------|
| **Subscribe** | `zlink_set_subscription()` (automatic) | `zlink_set_subscription()` or direct send |
| **Send** | Not allowed (`ENOTSUP`) | Allowed — forwards subscription frames upstream |
| **Recv** | Topic + payload separated | Same |
| **Implementation** | `xsub_t` subclass, `xsend()` blocked | Base class |

SUB cannot send, so it **cannot forward** subscription requests from downstream SUBs to upstream PUBs. This is why XSUB is needed.

### PUB vs XPUB — Key Difference

| | PUB | XPUB |
|---|-----|------|
| **Recv** | Not allowed | Receives subscription events via callback |
| **Send** | Same (topic broadcast) | Same |
| **Handler** | N/A | `zlink_subscription_event_handler()` |

XPUB can observe which clients subscribe to or unsubscribe from which topics.

### XSUB/XPUB Roles in a Proxy

```
          data flow ───────────────────────►
          subscription flow ◄──────────────

┌─────┐              Proxy               ┌─────┐
│ PUB │──connect──►┌──────┐◄──connect──  │ SUB │
│     │            │ XSUB │   ┌──────┐   │     │
└─────┘            │  │   │   │ XPUB │   └─────┘
┌─────┐            │  │   │   │      │   ┌─────┐
│ PUB │──connect──►│  ▼   │   │      │◄──│ SUB │
│     │            │ data ├──►│ data │   │     │
└─────┘            │      │   │      │   └─────┘
                   └──────┘   └──┬───┘
                      ▲          │
                      │          ▼
                   subscribe  on_subscription
                   forward    callback received
```

**Subscription propagation flow:**

1. SUB subscribes to `"weather"` topic
2. XPUB's `subscription_event_handler` callback receives
   `(subscribed=1, topic="weather")`
3. Proxy calls `zlink_set_subscription(xsub, "weather")` —
   sends subscription frame `[0x01 "weather"]` upstream to PUBs
4. PUB publishes `"weather"` data
5. XSUB receives data → XPUB delivers to matching SUBs

**This is impossible with a regular SUB** — SUB blocks all sends, so it
cannot forward subscriptions upstream. This is why proxy frontends must
use XSUB.

### Why Use a Proxy?

Direct PUB/SUB connections have structural limitations:

```
Direct (no proxy)                    With proxy
─────────────────                    ──────────

┌─────┐     ┌─────┐                 ┌─────┐     ┌───────────┐     ┌─────┐
│PUB 1│────►│SUB 1│                 │PUB 1│──►  │           │  ──►│SUB 1│
└─────┘     └─────┘                 └─────┘     │   XSUB    │     └─────┘
┌─────┐     ┌─────┐                 ┌─────┐     │     │     │     ┌─────┐
│PUB 2│────►│SUB 2│                 │PUB 2│──►  │     ▼     │  ──►│SUB 2│
└─────┘     └─────┘                 └─────┘     │   XPUB    │     └─────┘
                                                │  (Proxy)  │
 N PUB × M SUB = N×M connections                └───────────┘
 PUB/SUB must know each other's addresses
                                                 N + M connections
                                                 PUB/SUB only need to know the proxy address
```

**Key use cases for a proxy:**

| Use Case | Description |
|----------|-------------|
| **Reduce connections** | N×M direct connections → N+M (PUB→proxy, proxy→SUB) |
| **Address decoupling** | PUB and SUB don't need each other's endpoints — only the proxy address |
| **Dynamic scaling** | PUBs/SUBs can be added or removed independently without affecting each other |
| **Subscription transformation** | XPUB MANUAL mode enables topic remapping and filtering |
| **Network bridging** | Connect PUB/SUB across different network segments (e.g., inproc ↔ tcp) |
| **Monitoring** | Capture socket records all messages passing through |

`zlink_proxy()` is a built-in proxy that automatically forwards messages
and subscriptions bidirectionally between XSUB and XPUB:

```c
/* frontend: PUBs connect here */
void *xsub = zlink_socket(ctx, ZLINK_XSUB);
zlink_bind(xsub, "tcp://*:5556");

/* backend: SUBs connect here */
void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_bind(xpub, "tcp://*:5557");

/* capture (optional): records all messages passing through */
void *capture = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(capture, "tcp://*:5558");

zlink_proxy(xsub, xpub, capture);  /* blocking — run in a separate thread */
```

## 9. Subscription Frame Format

Subscription/unsubscription frames between XPUB/XSUB follow this format:

| Byte | Meaning |
|--------|------|
| `0x01` + topic | Subscription request |
| `0x00` + topic | Unsubscription request |

```c
/* Subscribe from XSUB */
zlink_set_subscription(xsub, "A");

/* Unsubscribe from XSUB */
zlink_unset_subscription(xsub, "A");
```

XPUB receives subscription frames via a callback handler:

```c
void on_subscription(const zlink_routing_id_t *source_rid,
                     int subscribed, const char *topic, size_t topic_len,
                     void *userdata)
{
    if (subscribed)
        printf("Subscribe: %.*s\n", (int)topic_len, topic);
    else
        printf("Unsubscribe: %.*s\n", (int)topic_len, topic);
}

void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_subscription_event_handler(xpub, on_subscription, NULL);
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

```c
/* Enable MANUAL mode */
int manual = 1;
zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

/* on_subscription callback fires with subscribed=1, topic="A"
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

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_basic()`: subscription request for A → transformed to B

## 11. XPUB/XSUB Usage Patterns

### Pattern 1: Building a Proxy/Broker

Build a PUB/SUB proxy using XSUB (frontend) + XPUB (backend).

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

### Pattern 2: MANUAL Mode Proxy (Subscription Transformation)

An advanced proxy that transforms or filters subscription requests.

```c
int manual = 1;
zlink_set_pub_option(xpub, ZLINK_PUB_OPT_MANUAL, &manual, sizeof(manual));

/* on_subscription callback processes subscription requests */
void on_sub(const zlink_routing_id_t *source_rid,
            int subscribed, const char *topic, size_t topic_len,
            void *userdata)
{
    if (subscribed) {
        /* Register subscription */
        zlink_set_subscription(xpub, topic);

        /* Propagate subscription upstream (XSUB) */
        zlink_subscribe(xsub, topic);
    } else {
        /* Unsubscription */
        zlink_unset_subscription(xpub, topic);

        zlink_unset_subscription(xsub, topic);
    }
}
```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_xpub_proxy_unsubscribe_on_disconnect()`

### Pattern 3: Subscription Monitoring

Use XPUB to observe which clients subscribe to which topics.

```c
void on_subscription(const zlink_routing_id_t *source_rid,
                     int subscribed, const char *topic, size_t topic_len,
                     void *userdata)
{
    if (subscribed)
        printf("New subscription: %.*s\n", (int)topic_len, topic);
    else
        printf("Unsubscription: %.*s\n", (int)topic_len, topic);
}

void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_subscription_event_handler(xpub, on_subscription, NULL);
zlink_bind(xpub, "tcp://*:5557");

/* Subscription events are dispatched to on_subscription callback */
```

### Pattern 4: Automatic Unsubscribe on Subscriber Disconnect

When a SUB disconnects, an unsubscribe frame is automatically delivered to XPUB.

```c
/* After SUB disconnects */
zlink_close(sub);

/* XPUB's on_subscription callback receives unsubscribe event
   (subscribed=0, topic=subscribed topic) */
```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_xpub_proxy_unsubscribe_on_disconnect()`

## 12. Caveats

### Subscription Propagation Timing

Subscription messages are propagated asynchronously. Messages published immediately after subscribing may not be received.

```c
zlink_connect(sub, endpoint);
zlink_set_subscription(sub, "topic");
/* Publishing a "topic" message at this point may result in loss */
msleep(100);  /* wait for subscription propagation */
/* Messages published after this point can be received */
```

### Subscription Management in XPUB MANUAL Mode

In MANUAL mode, if `zlink_set_subscription()` is not called after receiving a subscription frame, that subscription is not registered. Subscriptions must be explicitly processed.

### Multiple Subscribers → Single XPUB

When multiple SUBs subscribe to the same topic, the XPUB subscription is maintained until all SUBs have unsubscribed.

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_missing_subscriptions()`: processing two subscribers sequentially to prevent omissions

---
[← PAIR](03-1-pair.md) | [DEALER →](03-3-dealer.md)
