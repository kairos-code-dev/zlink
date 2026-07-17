[English](09-message-api.md) | [한국어](09-message-api.ko.md)

<!-- zlink-nav:start -->
[← Routing ID](08-routing-id.md) | [Performance →](10-performance.md)
<!-- zlink-nav:end -->

# Message API Reference

> **Normative status: Illustrative.**
> This guide is for explanation purposes. The authoritative source for
> API names and signatures is `core/include/zlink.h` and `bindings/README.md`.

## 1. Overview

zlink messages are represented by the `zlink_msg_t` structure, which has a fixed size of 64 bytes. Small messages are stored inline (VSM), while large messages are handled via separate allocation (LMSG).

## 2. Message Types

| Type | Condition | Memory | When to Use |
|------|-----------|--------|-------------|
| VSM (Very Small Message) | ≤41B (64-bit) | Inline storage within msg_t | Small data, most frequent |
| LMSG (Large Message) | >41B | malloc'd buffer, reference counted | Large data |
| CMSG (Constant Message) | Constant data | External pointer reference (no copy) | `zlink_msg_init_data(..., NULL, NULL)` |
| ZCLMSG (Zero-copy Large) | zero-copy | External buffer + free callback | `zlink_msg_init_data()` |

> For internal memory layout details (VSM/LMSG struct internals), see [architecture.md](../internals/architecture.md).

## 3. Message Lifecycle

### 3.1 Initialization — zlink_msg_init vs zlink_msg_init_size vs zlink_msg_init_data

#### zlink_msg_init — Empty Message

Used for receiving messages or initialization purposes. Creates a message without data.

```c
zlink_msg_t msg;
zlink_msg_init(&msg);
/* Used for initialization or as a target for zlink_msg_copy(). Free with zlink_msg_close() */
```

#### zlink_msg_init_size — Size-Specified (Requires Copy)

Allocates a buffer of the specified size, then fills it directly via `zlink_msg_data()`. This is the pattern for **copying data into the message**.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
memcpy(zlink_msg_data(&msg), source_data, 1024);
zlink_send(socket, &msg, 1, 0);
```

**When to use:** When creating a message from data in your own buffer. Safe to free the original buffer immediately.

#### zlink_msg_init_data — External Buffer Reference (Zero-Copy)

Initializes a message from an external buffer without copying. When the free
callback (`ffn`) is non-NULL, the message owns the buffer and releases it
through that callback once the last owning message is freed; when `ffn` is
NULL, the buffer is **borrowed** and never freed by the message (such messages
report as shared).

```c
void my_free(void *data, void *hint) {
    free(data);
}

void *buf = malloc(4096);
memcpy(buf, source_data, 4096);

zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, 4096, my_free, NULL);
/* buf is now owned by the message. Do not free it directly */
zlink_send(socket, &msg, 1, 0);
/* my_free(buf, NULL) is called automatically after sending completes */
```

**When to use:** When you want to avoid copying large data. Delegates buffer deallocation timing to the library.

> Reference: `core/tests/integration/test_msg_ffn.cpp` — Verifies free function callback behavior

#### zlink_msg_adopt — Adopt Without Init+Move

Transfers ownership from `src_` into `dest_` in a single call. Unlike
`zlink_msg_move`, `dest_` must **not** currently own an initialized message.
After the call `src_` is an empty message and `dest_` holds the content.

```c
zlink_msg_t src, dest;
zlink_msg_init_size(&src, 32);
memcpy(zlink_msg_data(&src), "data", 4);

/* dest need not be initialized first */
zlink_msg_adopt(&dest, &src);
/* src is now empty; dest holds the 32-byte buffer */
zlink_msg_close(&dest);
```

**When to use:** Binding implementations that construct messages in a temporary
and then hand them to a caller-owned slot without an extra `zlink_msg_init` step.

### 3.2 Data Access

```c
void *data = zlink_msg_data(&msg);
size_t size = zlink_msg_size(&msg);
```

> **Note:** The header does not expose `zlink_msg_more()` or
> `ZLINK_MORE`. Application code uses the multipart parts-array API
> instead of a per-message `more` flag.

### 3.3 Sending

```c
/* Multipart send: pass an array of msg parts */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);

zlink_submit_result_t rc = zlink_send(socket, parts, 2, 0);
if (rc != ZLINK_SUBMIT_OK) {
    /* Failure: caller still owns parts */
    for (size_t i = 0; i < 2; i++)
        zlink_msg_close(&parts[i]);
}
```


### 3.4 Receiving

Messages are pulled with `zlink_recv()` / `zlink_subscribe()` /
`zlink_router_recv()` (typically inside a poller loop). `zlink_recv_handler()`
is kept only for raw `STREAM`. The recv functions return `zlink_msg_t`
parts directly:

```c
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
if (zlink_recv(socket, &source_rid, &parts, &part_count, 0) == ZLINK_RECV_OK) {
    printf("Received: %.*s\n",
           (int)zlink_msg_size(&parts[0]),
           (char *)zlink_msg_data(&parts[0]));

    zlink_multipart_close(parts, part_count);
}
```

### 3.5 Deallocation

```c
zlink_msg_close(&msg);
```

## 4. Ownership Rules

| Situation | Ownership | Subsequent Action |
|-----------|-----------|-------------------|
| `zlink_send` succeeds | Transferred to library | msg parts are empty, must not be accessed |
| `zlink_send` fails | Caller still owns | Must call `zlink_msg_close()` per part |
| Handler callback delivers msg | Library provides msg parts | Must call `zlink_msg_close()` per part |
| `zlink_msg_close` | Resources freed | msg can be reused (re-initialization required) |

### Ownership Rules in Practice

```c
/* Pattern 1: Send succeeds → msg parts automatically cleaned up */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
if (rc == ZLINK_SUBMIT_OK) {
    /* Success: part is now empty. Calling close is safe but unnecessary */
}

/* Pattern 2: Send fails → manual cleanup required */
zlink_msg_t part2;
zlink_msg_init_size(&part2, 5);
memcpy(zlink_msg_data(&part2), "Hello", 5);
rc = zlink_send(socket, &part2, 1, ZLINK_DONTWAIT);
if (rc != ZLINK_SUBMIT_OK) {
    /* Failure: part2 is still valid. Must close */
    zlink_msg_close(&part2);
}

/* Pattern 3: Accessing msg data after send — dangerous! */
zlink_send(socket, &part, 1, 0);
/* zlink_msg_data(&part);  ← undefined behavior! */
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

> Reference: `core/tests/integration/test_msg_ffn.cpp` — `ffn()` callback writes "freed" to hint

### When Free Functions Are Called

```c
/* 1. Called on message close */
zlink_msg_t msg;
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_msg_close(&msg);  /* → my_free(buf, NULL) called */

/* 2. Called after sending completes */
zlink_msg_init_data(&msg, buf, size, my_free, NULL);
zlink_send(socket, &msg, 1, 0);
/* my_free(buf, NULL) called when sending completes */

/* 3. Called when original is freed after copy */
zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
zlink_msg_close(&msg);
zlink_msg_close(&copy);  /* my_free called when last reference is released */
```

> Reference: `core/tests/integration/test_msg_ffn.cpp` — close/send/copy scenarios

### Constant Data with zlink_msg_init_data

Constant (literal, static) data can be sent without copying by using
`zlink_msg_init_data()` with `ffn=NULL`.

```c
/* Single frame */
zlink_msg_t msg;
zlink_msg_init_data(&msg, (void *)"Hello", 5, NULL, NULL);
zlink_send(socket, &msg, 1, 0);

/* Multipart — parts array */
zlink_msg_t parts[2];
zlink_msg_init_data(&parts[0], (void *)"foo", 3, NULL, NULL);
zlink_msg_init_data(&parts[1], (void *)"foobar", 6, NULL, NULL);
zlink_send(socket, parts, 2, 0);
```

> Reference: `core/tests/integration/test_msg_flags.cpp` — `test_shared_const()`

## 6. Multipart Message Patterns in Practice

Multipart messages are sent as a parts array in a single `zlink_send()` call.

### Pattern 1: Request-Reply (DEALER/ROUTER)

```c
/* DEALER → ROUTER: send single frame */
zlink_msg_t req;
zlink_msg_init_size(&req, 7);
memcpy(zlink_msg_data(&req), "request", 7);
zlink_send(dealer, &req, 1, 0);

/* ROUTER handler callback receives: source_rid + parts */
void on_request(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    /* parts[0] = "request", source_rid = DEALER's routing_id */

    /* ROUTER reply: directed send via zlink_send_rid */
    zlink_msg_t reply;
    zlink_msg_init_size(&reply, 5);
    memcpy(zlink_msg_data(&reply), "reply", 5);
    zlink_send_rid(router, source_rid, &reply, 1, 0);

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

> Reference: `core/tests/integration/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

```c
/* SPOT topic publish — a plain PUB send (zlink_send) returns ENOTSUP on PUB/SUB.
   Use the topic publish API (topic_id = "weather"). */
zlink_msg_t payload;
zlink_msg_init_size(&payload, 5);
memcpy(zlink_msg_data(&payload), "sunny", 5);
zlink_spot_publish(spot, "weather", &payload, 1, 0);
zlink_msg_close(&payload);

/* SUB handler callback receives topic and payload separately */
void on_spot(const zlink_routing_id_t *source_rid,
             const char *topic, size_t topic_len,
             zlink_msg_t *parts, size_t part_count,
             void *userdata)
{
    /* topic = "weather", parts[0] = "sunny" */
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### Pattern 3: Multipart Processing in Handler Callback

```c
/* Handler callback receives all frames as parts array */
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame[%zu bytes]: %.*s\n",
               zlink_msg_size(&parts[i]),
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}
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
/* storage refcount is now 2 */
zlink_config_result_t err = ZLINK_CONFIG_OK;
int refcnt = zlink_msg_refcnt(&copy, &err);
/* refcnt == 2 */

zlink_msg_close(&original);
zlink_msg_close(&copy);  /* Actual memory freed when last reference is released */
```

> Reference: `core/tests/integration/test_msg_flags.cpp` — `test_shared_refcounted()`: Verifying shared property after copy

### Storage Refcount — zlink_msg_refcnt

The internal reference count is managed with atomic operations.
Copying and closing different `zlink_msg_t` handles that share the same
underlying storage from different threads is safe. `zlink_msg_refcnt()`
returns a point-in-time snapshot, so use it for diagnostics and assertions.
Do not access a one `zlink_msg_t` instance concurrently from multiple
threads.

```c
/* Reference-counted message */
zlink_config_result_t err = ZLINK_CONFIG_OK;
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int refcnt = zlink_msg_refcnt(&msg, &err);  /* 1: single owner */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
refcnt = zlink_msg_refcnt(&copy, &err);  /* 2: shared by msg and copy */

/* Constant data message */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
refcnt = zlink_msg_refcnt(&const_msg, &err);  /* 1: not internally refcounted */
```

> Reference: `core/tests/integration/test_msg_flags.cpp` — `test_shared_const()`: shared property of constant messages

## 8. Error Handling

### Send Failure

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 100);
memcpy(zlink_msg_data(&part), data, 100);

zlink_submit_result_t rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc != ZLINK_SUBMIT_OK) {
    if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
        /* HWM exceeded: retry later */
    } else if (rc == ZLINK_SUBMIT_NOT_SUPPORTED) {
        /* Send not supported on this socket (e.g., SUB socket) */
    } else if (rc == ZLINK_SUBMIT_TERMINATED) {
        /* Context terminated */
    }
    /* On failure, part is still valid -> must close */
    zlink_msg_close(&part);
}
```

## 9. zlink_send (Multipart Msg-Based)

`zlink_send()` now takes a `zlink_msg_t` parts array and a part count:

```c
int zlink_send(void *s_, zlink_msg_t *parts_, size_t part_count_, zlink_send_flags_t flags_);
```

```c
/* Single-part send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Zero-copy send */
zlink_msg_t zcmsg;
zlink_msg_init_data(&zcmsg, large_buf, large_size, my_free, NULL);
zlink_send(socket, &zcmsg, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

For ROUTER directed sends, use `zlink_send_rid()`:

```c
zlink_send_rid(router, &target_rid, parts, part_count, 0);
```


## 10. Request-Reply and Metadata

`zlink_msg_t` is a payload part container. It does not carry
request-reply or per-message metadata fields.

Request-reply uses dedicated typed API surfaces:

- **DEALER/ROUTER**: `zlink_dealer_request()`, `zlink_router_request()`,
  `zlink_router_reply()`, `zlink_router_recv()`
  -- see [DEALER Guide](03-3-dealer.md), [ROUTER Guide](03-4-router.md)
- **SPOT routed request-reply**: `zlink_spot_reply_spot()`,
  `zlink_spot_reply_router()`,
  the MeshNode ready handler and claim receive batches
  -- see [SPOT Guide](07-3-spot.md)
- **SPOT channel request-reply**: `zlink_spot_request_channel()`,
  `zlink_spot_send_channel()` -- see [SPOT Guide](07-3-spot.md)

From the message API perspective, the key points are:

- Payload is built with `zlink_msg_t` (init, send, close).
- Request-reply context lives in wire control parts, not message fields.
- Message metadata serialization is not a public contract.
- If application-level metadata is needed (trace-id, priority, etc.),
  encode it as a multipart payload frame.

---
<!-- zlink-nav:bottom:start -->
[← Routing ID](08-routing-id.md) | [Performance →](10-performance.md)
<!-- zlink-nav:bottom:end -->
