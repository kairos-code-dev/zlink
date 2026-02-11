[English](09-message-api.md) | [한국어](09-message-api.ko.md)

# Message API Reference

## 1. Overview

zlink messages are represented by the `zlink_msg_t` structure, which has a fixed size of 64 bytes. Small messages are stored inline (VSM), while large messages are handled via separate allocation (LMSG).

## 2. Message Types

| Type | Condition | Memory | When to Use |
|------|-----------|--------|-------------|
| VSM (Very Small Message) | ≤33B (64-bit) | Inline storage within msg_t | Small data, most frequent |
| LMSG (Large Message) | >33B | malloc'd buffer, reference counted | Large data |
| CMSG (Constant Message) | Constant data | External pointer reference (no copy) | `zlink_send_const()` |
| ZCLMSG (Zero-copy Large) | zero-copy | External buffer + free callback | `zlink_msg_init_data()` |

> For internal memory layout details (VSM/LMSG struct internals), see [architecture.md](../internals/architecture.md).

## 3. Message Lifecycle

### 3.1 Initialization — zlink_msg_init vs zlink_msg_init_size vs zlink_msg_init_data

#### zlink_msg_init — Empty Message

Used for receiving messages or initialization purposes. Creates a message without data.

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
/* Then receive data with zlink_msg_recv(), or free with zlink_msg_close() */
```

#### zlink_msg_init_size — Size-Specified (Requires Copy)

Allocates a buffer of the specified size, then fills it directly via `zlink_msg_data()`. This is the pattern for **copying data into the message**.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
memcpy(zlink_msg_data(&msg), source_data, 1024);
zlink_msg_send(&msg, socket, 0);
```

**When to use:** When creating a message from data in your own buffer. Safe to free the original buffer immediately.

#### zlink_msg_init_data — External Buffer Reference (Zero-Copy)

Transfers ownership of an external buffer to the message. Sends without copying. The free callback (ffn) handles buffer cleanup.

```c
void my_free(void *data, void *hint) {
    free(data);
}

void *buf = malloc(4096);
memcpy(buf, source_data, 4096);

zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, 4096, my_free, NULL);
/* buf is now owned by the message. Do not free it directly */
zlink_msg_send(&msg, socket, 0);
/* my_free(buf, NULL) is called automatically after sending completes */
```

**When to use:** When you want to avoid copying large data. Delegates buffer deallocation timing to the library.

> Reference: `core/tests/test_msg_ffn.cpp` — Verifies free function callback behavior

### 3.2 Data Access

```c
void *data = zlink_msg_data(&msg);
size_t size = zlink_msg_size(&msg);
int more = zlink_msg_more(&msg);  /* Whether the next frame exists */
```

### 3.3 Sending

```c
/* On success, msg ownership transfers to the library */
int rc = zlink_msg_send(&msg, socket, 0);
if (rc == -1) {
    /* Failure: caller still owns msg */
    zlink_msg_close(&msg);
}
```

### 3.4 Receiving

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
int rc = zlink_msg_recv(&msg, socket, 0);
if (rc != -1) {
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&msg),
           (char *)zlink_msg_data(&msg));
}
zlink_msg_close(&msg);
```

### 3.5 Deallocation

```c
zlink_msg_close(&msg);
```

## 4. Ownership Rules

| Situation | Ownership | Subsequent Action |
|-----------|-----------|-------------------|
| `zlink_msg_send` succeeds | Transferred to library | msg is empty, must not be accessed |
| `zlink_msg_send` fails | Caller still owns | Must call `zlink_msg_close()` |
| `zlink_msg_recv` succeeds | Library fills msg with data | Must call `zlink_msg_close()` |
| `zlink_msg_close` | Resources freed | msg can be reused (re-initialization required) |

### Ownership Rules in Practice

```c
/* Pattern 1: Send succeeds → msg automatically cleaned up */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "Hello", 5);
int rc = zlink_msg_send(&msg, socket, 0);
if (rc != -1) {
    /* Success: msg is now empty. Calling close is safe but unnecessary */
}

/* Pattern 2: Send fails → manual cleanup required */
rc = zlink_msg_send(&msg, socket, ZLINK_DONTWAIT);
if (rc == -1) {
    /* Failure: msg is still valid. Must close */
    zlink_msg_close(&msg);
}

/* Pattern 3: Accessing msg data after send — dangerous! */
zlink_msg_send(&msg, socket, 0);
/* zlink_msg_data(&msg);  ← undefined behavior! */
```

## 5. Zero-Copy Pattern Details

### Writing Free Function Callbacks

```c
/* Basic free callback */
void simple_free(void *data, void *hint) {
    free(data);
}

/* Callback using hint */
void pool_free(void *data, void *hint) {
    struct memory_pool *pool = (struct memory_pool *)hint;
    pool_return(pool, data);
}

/* Notification callback (does not free the data itself) */
void notify_free(void *data, void *hint) {
    /* Notify that the data is no longer in use */
    memcpy(hint, "freed", 5);
    /* data is managed externally */
}
```

> Reference: `core/tests/test_msg_ffn.cpp` — `ffn()` callback writes "freed" to hint

### When Free Functions Are Called

```c
/* 1. Called on message close */
zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_close(&msg);  /* → my_free(buf, NULL) called */

/* 2. Called after sending completes */
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_send(&msg, socket, 0);
/* my_free(buf, NULL) called when sending completes */

/* 3. Called when original is freed after copy */
zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
zlink_msg_close(&msg);
zlink_msg_close(&copy);  /* my_free called when last reference is released */
```

> Reference: `core/tests/test_msg_ffn.cpp` — close/send/copy scenarios

### zlink_send_const — Sending Constant Data

Sends constant (literal, static) data without copying. No free function needed.

```c
/* Send a string literal directly */
zlink_send_const(socket, "Hello", 5, 0);

/* Multipart */
zlink_send_const(socket, "foo", 3, ZLINK_SNDMORE);
zlink_send_const(socket, "foobar", 6, 0);
```

> Reference: `core/tests/test_pair_inproc.cpp` — `test_zlink_send_const()`

## 6. Multipart Message Patterns in Practice

Multipart messages send consecutive frames using the `ZLINK_SNDMORE` flag. The receiving side checks whether the next frame exists using `zlink_msg_more()`.

### Pattern 1: Request-Reply (DEALER/ROUTER)

```c
/* DEALER → ROUTER: send single frame */
zlink_send(dealer, "request", 7, 0);

/* ROUTER receive: [routing_id][request] — 2-frame multipart */
zlink_msg_t rid, data;
zlink_msg_init(&rid);
zlink_msg_init(&data);
zlink_msg_recv(&rid, router, 0);   /* more=1 */
zlink_msg_recv(&data, router, 0);  /* more=0 */

/* ROUTER reply: routing_id + data */
zlink_msg_send(&rid, router, ZLINK_SNDMORE);
zlink_send(router, "reply", 5, 0);

zlink_msg_close(&rid);
zlink_msg_close(&data);
```

> Reference: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

```c
/* PUB: [topic][payload] */
zlink_send(pub, "weather", 7, ZLINK_SNDMORE);
zlink_send(pub, "sunny", 5, 0);

/* SUB: receive multipart */
char topic[64], payload[256];
zlink_recv(sub, topic, sizeof(topic), 0);
zlink_recv(sub, payload, sizeof(payload), 0);
```

### Pattern 3: Generic Multipart Receive Loop

```c
do {
    zlink_msg_t frame;
    zlink_msg_init(&frame);
    zlink_msg_recv(&frame, socket, 0);

    printf("Frame[%zu bytes]: %.*s\n",
           zlink_msg_size(&frame),
           (int)zlink_msg_size(&frame),
           (char *)zlink_msg_data(&frame));

    int more = zlink_msg_more(&frame);
    zlink_msg_close(&frame);

    if (!more) break;
} while (1);
```

## 7. Message Copying

### zlink_msg_copy — Reference-Counted Copy

Increments the reference count instead of copying the data. Efficient for large messages.

```c
zlink_msg_t original, copy;
zlink_msg_init_size(&original, 1024);
memcpy(zlink_msg_data(&original), data, 1024);

zlink_msg_init(&copy);
zlink_msg_copy(&copy, &original);

/* Both original and copy reference the same data */
/* ZLINK_SHARED property is set to 1 */
int shared = zlink_msg_get(&copy, ZLINK_SHARED);
/* shared == 1 */

zlink_msg_close(&original);
zlink_msg_close(&copy);  /* Actual memory freed when last reference is released */
```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`: Verifying SHARED property after copy

### ZLINK_SHARED Property

```c
/* Reference-counted message */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int shared = zlink_msg_get(&msg, ZLINK_SHARED);  /* 0: single owner */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
shared = zlink_msg_get(&copy, ZLINK_SHARED);  /* 1: shared */

/* Constant data message */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
shared = zlink_msg_get(&const_msg, ZLINK_SHARED);  /* 1: always shared */
```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: SHARED property of constant messages

## 8. Error Handling

### Send Failure

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 100);
memcpy(zlink_msg_data(&msg), data, 100);

int rc = zlink_msg_send(&msg, socket, ZLINK_DONTWAIT);
if (rc == -1) {
    if (errno == EAGAIN) {
        /* HWM exceeded: retry later */
    } else if (errno == ENOTSUP) {
        /* Send not supported on this socket (e.g., SUB socket) */
    } else if (errno == ETERM) {
        /* Context terminated */
    }
    /* On failure, msg is still valid → must close */
    zlink_msg_close(&msg);
}
```

### Partial Send (Multipart)

If sending a middle frame of a multipart message fails, the previously sent frames are already in the queue. Since sending is not atomic, the receiving side must be prepared to handle incomplete multipart messages.

```c
/* Frame 1 sent successfully */
zlink_send(socket, "header", 6, ZLINK_SNDMORE);

/* Frame 2 send fails (HWM, etc.) */
int rc = zlink_send(socket, "body", 4, ZLINK_DONTWAIT);
if (rc == -1) {
    /* header is already in the queue — incomplete message delivered to receiver */
}
```

## 9. zlink_send vs zlink_msg_send

| | `zlink_send` | `zlink_msg_send` |
|---|---|---|
| **Input** | Buffer pointer + size | zlink_msg_t |
| **Copy** | Creates msg internally + copies | Zero-copy possible |
| **Ownership** | Original buffer retained | msg ownership transferred |
| **When to use** | Small data, simple sends | Large data, zero-copy |

```c
/* Simple send */
zlink_send(socket, "Hello", 5, 0);

/* Zero-copy send */
zlink_msg_t msg;
zlink_msg_init_data(&msg, large_buf, large_size, my_free, NULL);
zlink_msg_send(&msg, socket, 0);
```

---
[← Routing ID](08-routing-id.md) | [Performance →](10-performance.md)
