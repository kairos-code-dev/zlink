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
zlink_send(pub, "weather: sunny", 14, 0);
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
zlink_recv_spot_handler(sub, on_topic, NULL);
zlink_connect(sub, "tcp://127.0.0.1:5556");

/* Subscribe to topic -- set after connect */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "weather", 7);

/* Messages are dispatched to on_topic callback */
```

> Reference: `core/tests/test_pubsub.cpp` -- empty subscription ("") → receives all messages

### Sending and Receiving Summary

| Socket | Direction | Registration Call | Notes |
|--------|-----------|-------------------|-------|
| PUB | Send only | N/A | Cannot receive; does not accept a handler |
| SUB | Receive only | `zlink_recv_spot_handler()` | `topic` + `parts[]` — topic extracted by prefix match |
| XPUB | Bidirectional | `zlink_recv_xpub_handler()` | Receives subscription events, not data |
| XSUB | Bidirectional | `zlink_recv_spot_handler()` | Sends subscription frames; receives data via fair-queue |

SUB uses `zlink_recv_spot_handler()` instead of `zlink_recv_handler()`
because the I/O thread separates the matched topic from the payload before
invoking the callback — the handler receives `topic` and `parts[]` as
distinct parameters.

**Pull mode** is also available for SUB: call `zlink_recv()` without
attaching a handler. The raw frame is received without topic separation.

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
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "weather", 7);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "sports", 6);

/* Unsubscribe */
zlink_setsockopt(sub, ZLINK_UNSUBSCRIBE, "sports", 6);
```

### Empty Subscription (All Messages)

```c
/* Subscribe with empty string -- receives all messages */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
```

> Reference: `core/tests/test_pubsub.cpp` -- `zlink_setsockopt(subscriber, ZLINK_SUBSCRIBE, "", 0)`

## 4. Message Format

PUB/SUB messages can use two formats.

### Single Frame (Topic Included)

The topic is embedded in the data. Simple but requires parsing.

```c
/* Publish */
zlink_send(pub, "weather: sunny", 14, 0);

/* SUB handler callback receives:
   topic = "weather: sunny" (full match prefix)
   parts[0] = "weather: sunny" (full data) */
```

### Multipart Frame (Topic + Data Separated)

The topic and data are sent as separate frames. No parsing needed.

```c
/* Publish: [topic][payload] */
zlink_send(pub, "weather", 7, ZLINK_SNDMORE);
zlink_send(pub, "sunny", 5, 0);

/* SUB handler callback receives:
   topic = "weather"
   parts[0] = "sunny" (payload frames only) */
```

## 5. PUB/SUB Socket Options

### SUB-Specific Options

| Option | Type | Description |
|------|------|------|
| `ZLINK_SUBSCRIBE` | binary | Add topic subscription (prefix match) |
| `ZLINK_UNSUBSCRIBE` | binary | Remove topic subscription |

### Common Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_SNDHWM` | int | 1000 | Send HWM (PUB) |
| `ZLINK_RCVHWM` | int | 1000 | Receive HWM (SUB) |
| `ZLINK_LINGER` | int | -1 | Wait time on close (ms) |

## 6. PUB/SUB Usage Patterns

### Pattern 1: Basic PUB/SUB

```c
/* PUB */
void *pub = zlink_socket(ctx, ZLINK_PUB);
zlink_bind(pub, "tcp://*:5556");

/* SUB -- receive all messages */
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_recv_spot_handler(sub, on_topic, NULL);
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);

msleep(100);  /* time for subscription to reach PUB */

zlink_send(pub, "test", 4, 0);

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
zlink_setsockopt(sub_weather, ZLINK_SUBSCRIBE, "weather", 7);

void *sub_sports = zlink_socket(ctx, ZLINK_SUB);
zlink_connect(sub_sports, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub_sports, ZLINK_SUBSCRIBE, "sports", 6);

/* Only sub_weather receives weather, only sub_sports receives sports */
```

### Pattern 3: Multiple PUBs → SUB

A SUB can connect to multiple PUBs. It receives messages from all PUBs via fair-queue.

```c
void *sub = zlink_socket(ctx, ZLINK_SUB);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
zlink_connect(sub, "tcp://pub1:5556");
zlink_connect(sub, "tcp://pub2:5557");
```

## 7. PUB/SUB Caveats

### Slow Subscriber (Drop on HWM Exceeded)

PUB drops messages to slow subscribers. If the receive rate is slower than the publish rate, messages are lost once the HWM is reached.

```c
/* Increase buffer by adjusting HWM */
int hwm = 100000;
zlink_setsockopt(pub, ZLINK_SNDHWM, &hwm, sizeof(hwm));
```

### Late Joiner (Messages Lost Before Subscription)

Messages published before the subscription message from SUB reaches PUB are lost.

```c
/* Time needed for subscription to propagate to PUB */
zlink_connect(sub, "tcp://127.0.0.1:5556");
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "topic", 5);
msleep(100);  /* wait for subscription propagation */
/* Only messages published after this point can be received */
```

### Direction Constraints

```c
/* PUB cannot receive → ENOTSUP */
/* PUB socket does not accept a handler at creation */

/* SUB cannot send → ENOTSUP */
zlink_send(sub, "data", 4, 0);  /* errno = ENOTSUP */
```

---

# Part II: XPUB/XSUB

## 8. XPUB/XSUB Overview

XPUB/XSUB are advanced publish-subscribe sockets that allow applications to handle subscription frames directly. They are used for building proxies/brokers, subscription monitoring, and Last-Value Caching.

```
┌─────┐     ┌──────────────┐     ┌─────┐
│ PUB │────►│ XSUB ── XPUB │────►│ SUB │
└─────┘     │   (Proxy)    │     └─────┘
┌─────┐     │              │     ┌─────┐
│ PUB │────►│              │────►│ SUB │
└─────┘     └──────────────┘     └─────┘
```

## 9. Subscription Frame Format

Subscription/unsubscription frames between XPUB/XSUB follow this format:

| Byte | Meaning |
|--------|------|
| `0x01` + topic | Subscription request |
| `0x00` + topic | Unsubscription request |

```c
/* Send subscription from XSUB */
const uint8_t subscribe[] = {0x01, 'A'};    /* subscribe to topic "A" */
zlink_send(xsub, subscribe, 2, 0);

/* Send unsubscription from XSUB */
const uint8_t unsubscribe[] = {0x00, 'A'};  /* unsubscribe from topic "A" */
zlink_send(xsub, unsubscribe, 2, 0);
```

XPUB receives subscription frames via a callback handler:

```c
void on_subscription(int subscribed, const uint8_t *topic, size_t topic_len,
                     void *userdata)
{
    if (subscribed)
        printf("Subscribe: %.*s\n", (int)topic_len, (const char *)topic);
    else
        printf("Unsubscribe: %.*s\n", (int)topic_len, (const char *)topic);
}

void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_recv_xpub_handler(xpub, on_subscription, NULL);
```

> Reference: `core/tests/test_xpub_manual.cpp` -- `subscription1[] = {1, 'A'}`, `unsubscription1[] = {0, 'A'}`

## 10. XPUB Socket Options

| Option | Type | Default | Description |
|------|------|--------|------|
| `ZLINK_XPUB_MANUAL` | int | 0 | Enable manual subscription management mode |
| `ZLINK_XPUB_VERBOSE` | int | 0 | Forward duplicate subscription messages as well |
| `ZLINK_SUBSCRIBE` | binary | -- | (MANUAL mode) Add subscription to the current pipe |
| `ZLINK_UNSUBSCRIBE` | binary | -- | (MANUAL mode) Remove subscription from the current pipe |

### XPUB_MANUAL Mode

By default, XPUB processes SUB subscriptions automatically. In MANUAL mode, after receiving a subscription frame, the application explicitly decides the actual subscription using `ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`.

```c
/* Enable MANUAL mode */
int manual = 1;
zlink_setsockopt(xpub, ZLINK_XPUB_MANUAL, &manual, sizeof(manual));

/* on_subscription callback fires with subscribed=1, topic="A"
   Then apply transformed subscription: */
zlink_setsockopt(xpub, ZLINK_SUBSCRIBE, "XA", 2);

/* Publish */
zlink_send(xpub, "A", 1, 0);   /* does not reach the subscriber */
zlink_send(xpub, "XA", 2, 0);  /* subscriber receives this */
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
zlink_setsockopt(xpub, ZLINK_XPUB_MANUAL, &manual, sizeof(manual));

/* on_subscription callback processes subscription requests */
void on_sub(int subscribed, const uint8_t *topic, size_t topic_len,
            void *userdata)
{
    if (subscribed) {
        /* Register subscription */
        zlink_setsockopt(xpub, ZLINK_SUBSCRIBE, topic, topic_len);

        /* Propagate subscription upstream (XSUB) */
        uint8_t sub_frame[256];
        sub_frame[0] = 0x01;
        memcpy(sub_frame + 1, topic, topic_len);
        zlink_send(xsub, sub_frame, 1 + topic_len, 0);
    } else {
        /* Unsubscription */
        zlink_setsockopt(xpub, ZLINK_UNSUBSCRIBE, topic, topic_len);

        uint8_t unsub_frame[256];
        unsub_frame[0] = 0x00;
        memcpy(unsub_frame + 1, topic, topic_len);
        zlink_send(xsub, unsub_frame, 1 + topic_len, 0);
    }
}
```

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_xpub_proxy_unsubscribe_on_disconnect()`

### Pattern 3: Subscription Monitoring

Use XPUB to observe which clients subscribe to which topics.

```c
void on_subscription(int subscribed, const uint8_t *topic, size_t topic_len,
                     void *userdata)
{
    if (subscribed)
        printf("New subscription: %.*s\n", (int)topic_len, (const char *)topic);
    else
        printf("Unsubscription: %.*s\n", (int)topic_len, (const char *)topic);
}

void *xpub = zlink_socket(ctx, ZLINK_XPUB);
zlink_recv_xpub_handler(xpub, on_subscription, NULL);
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
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "topic", 5);
/* Publishing a "topic" message at this point may result in loss */
msleep(100);  /* wait for subscription propagation */
/* Messages published after this point can be received */
```

### Subscription Management in XPUB MANUAL Mode

In MANUAL mode, if `ZLINK_SUBSCRIBE` is not called after receiving a subscription frame, that subscription is not registered. Subscriptions must be explicitly processed.

### Multiple Subscribers → Single XPUB

When multiple SUBs subscribe to the same topic, the XPUB subscription is maintained until all SUBs have unsubscribed.

> Reference: `core/tests/test_xpub_manual.cpp` -- `test_missing_subscriptions()`: processing two subscribers sequentially to prevent omissions

---
[← PAIR](03-1-pair.md) | [DEALER →](03-3-dealer.md)
