[English](09-message-api.md) | [한국어](09-message-api.ko.md)

# Message API Reference

> **Normative status: Illustrative — Needs refresh.**
> 이 가이드는 설명 목적의 문서이며, API 명칭/시그니처의 정확한 기준은
> `core/include/zlink.h`와 `bindings/README.md`다.

## 1. Overview

zlink messages are represented by the `zlink_msg_t` structure, which has a fixed size of 64 bytes. Small messages are stored inline (VSM), while large messages are handled via separate allocation (LMSG).

## 2. Message Types

| Type | Condition | Memory | When to Use |
|------|-----------|--------|-------------|
| VSM (Very Small Message) | ≤33B (64-bit) | Inline storage within msg_t | Small data, most frequent |
| LMSG (Large Message) | >33B | malloc'd buffer, reference counted | Large data |
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
zlink_send(socket, &msg, 1, 0);
/* my_free(buf, NULL) is called automatically after sending completes */
```

**When to use:** When you want to avoid copying large data. Delegates buffer deallocation timing to the library.

> Reference: `core/tests/test_msg_ffn.cpp` — Verifies free function callback behavior

### 3.2 Data Access

```c
void *data = zlink_msg_data(&msg);
size_t size = zlink_msg_size(&msg);
```

> **Removed:** `zlink_msg_more()` and `ZLINK_MORE` have been removed from the header.
> With the multipart parts-array API, the `more` flag is no longer needed in
> application code.

### 3.3 Sending

```c
/* Multipart send: pass an array of msg parts */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);

int rc = zlink_send(socket, parts, 2, 0);
if (rc == -1) {
    /* Failure: caller still owns parts */
    for (size_t i = 0; i < 2; i++)
        zlink_msg_close(&parts[i]);
}
```

> **Legacy:** `zlink_msg_send()` is still present in the header but planned for
> removal. Use `zlink_send()` with a parts array instead.

### 3.4 Receiving

Messages are received via handler callbacks attached to the socket or service handle after creation (via `zlink_recv_handler` or `zlink_subscribe_handler`). The callback provides `zlink_msg_t` parts directly:

```c
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
int rc = zlink_send(socket, &part, 1, 0);
if (rc != -1) {
    /* Success: part is now empty. Calling close is safe but unnecessary */
}

/* Pattern 2: Send fails → manual cleanup required */
zlink_msg_t part2;
zlink_msg_init_size(&part2, 5);
memcpy(zlink_msg_data(&part2), "Hello", 5);
rc = zlink_send(socket, &part2, 1, ZLINK_DONTWAIT);
if (rc == -1) {
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

> Reference: `core/tests/test_msg_ffn.cpp` — `ffn()` callback writes "freed" to hint

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

> Reference: `core/tests/test_msg_ffn.cpp` — close/send/copy scenarios

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

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_const()`

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

> Reference: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

```c
/* PUB: [topic][payload] as parts array */
zlink_msg_t pub_parts[2];
zlink_msg_init_size(&pub_parts[0], 7);
memcpy(zlink_msg_data(&pub_parts[0]), "weather", 7);
zlink_msg_init_size(&pub_parts[1], 5);
memcpy(zlink_msg_data(&pub_parts[1]), "sunny", 5);
zlink_send(pub, pub_parts, 2, 0);

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
int refcnt = zlink_msg_refcnt(&copy);
/* refcnt == 2 */

zlink_msg_close(&original);
zlink_msg_close(&copy);  /* Actual memory freed when last reference is released */
```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`: Verifying shared property after copy

### Storage Refcount — zlink_msg_refcnt

The internal reference count is managed with atomic operations.
Copying and closing different `zlink_msg_t` handles that share the same
underlying storage from different threads is safe. `zlink_msg_refcnt()`
returns a point-in-time snapshot, so use it for diagnostics and assertions.
Do not access a single `zlink_msg_t` instance concurrently from multiple
threads.

```c
/* Reference-counted message */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 1024);
int refcnt = zlink_msg_refcnt(&msg);  /* 1: single owner */

zlink_msg_t copy;
zlink_msg_init(&copy);
zlink_msg_copy(&copy, &msg);
refcnt = zlink_msg_refcnt(&copy);  /* 2: shared by msg and copy */

/* Constant data message */
zlink_msg_t const_msg;
zlink_msg_init_data(&const_msg, (void *)"TEST", 5, NULL, NULL);
refcnt = zlink_msg_refcnt(&const_msg);  /* 1: not internally refcounted */
```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: shared property of constant messages

## 8. Error Handling

### Send Failure

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 100);
memcpy(zlink_msg_data(&part), data, 100);

int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
if (rc == -1) {
    if (errno == EAGAIN) {
        /* HWM exceeded: retry later */
    } else if (errno == ENOTSUP) {
        /* Send not supported on this socket (e.g., SUB socket) */
    } else if (errno == ETERM) {
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

> **Legacy:** `zlink_msg_send()` is still present in the header but planned for
> removal. Migrate all call sites to `zlink_send()` with a parts array.

## 10. Request-Reply Envelope

Messages can carry request-reply fields (`msg_type` and `correlation_id`)
that core serializes into the wire envelope automatically. DATA messages
(the default) produce no envelope overhead.

### Setting Request/Reply

```c
/* Send a REQUEST with correlation_id = 1001 */
zlink_msg_t req;
zlink_msg_init_size(&req, 13);
memcpy(zlink_msg_data(&req), "get_portfolio", 13);
zlink_msg_set_request(&req, 1001);
zlink_send(dealer, &req, 1, 0);

/* On the responder side: build a REPLY with the same correlation_id */
zlink_msg_t reply;
zlink_msg_init_size(&reply, 4);
memcpy(zlink_msg_data(&reply), "done", 4);
zlink_msg_set_reply(&reply, 1001);
zlink_send_rid(router, &source_rid, &reply, 1, 0);
```

### Reading Request-Reply Info

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    uint8_t msg_type;
    uint64_t correlation_id;
    zlink_msg_get_request_info(&parts[0], &msg_type, &correlation_id);

    if (msg_type == ZLINK_MSG_TYPE_REQUEST) {
        /* dispatch request with correlation_id */
    } else if (msg_type == ZLINK_MSG_TYPE_REPLY) {
        /* match reply to pending request via correlation_id */
    }
    /* msg_type == ZLINK_MSG_TYPE_DATA: regular message, no envelope */

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### Key Points

- `zlink_msg_init()` initializes msg_type to DATA and correlation_id to 0.
- Calling `set_request` after `set_reply` (or vice-versa) overwrites — last call wins.
- `msg_copy` / `msg_move` preserve request-reply fields.
- `msg_data()` / `msg_size()` return user payload only — envelope is not included.
- DATA messages have zero wire overhead (no envelope generated).

## 11. Per-Message Metadata

Each message can carry application-defined key-value metadata that is
serialized to the wire and restored on recv. This is independent of ZMP
protocol metadata (`zlink_msg_gets`) which is per-connection.

### Setting Metadata

```c
/* Application-defined metadata keys */
enum my_meta {
    META_TRACE_ID  = 0x0100,
    META_PRIORITY  = 0x0101,
    META_TIMESTAMP = 0x0102,
};

zlink_msg_t msg;
zlink_msg_init_size(&msg, 11);
memcpy(zlink_msg_data(&msg), "hello world", 11);

/* Attach trace-id */
uint8_t trace_id[16] = { /* ... */ };
zlink_msg_set_metadata(&msg, META_TRACE_ID, trace_id, 16);

/* Attach priority */
uint8_t priority = 3;
zlink_msg_set_metadata(&msg, META_PRIORITY, &priority, 1);

zlink_send(socket, &msg, 1, 0);
```

### Reading Metadata

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    size_t trace_len;
    const void *trace = zlink_msg_get_metadata(&parts[0],
                                                META_TRACE_ID, &trace_len);
    if (trace) {
        /* use trace_id bytes (trace, trace_len) */
    }

    size_t prio_len;
    const void *prio = zlink_msg_get_metadata(&parts[0],
                                               META_PRIORITY, &prio_len);
    if (prio && prio_len == 1) {
        uint8_t priority = *(const uint8_t *)prio;
        /* use priority */
    }

    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

### Key Points

- Keys `0x0000`--`0x00FF` are reserved for zlink. User keys start at `0x0100`.
- Messages with no metadata have zero wire overhead.
- `msg_copy` deep-copies metadata. `msg_move` transfers metadata (source becomes empty).
- `msg_close` frees the metadata storage.
- `msg_data()` / `msg_size()` return user payload only — metadata header is not included.
- `get_metadata` returns NULL for absent keys (no error).
- Metadata and request-reply fields are independent and can coexist on the same message.

---
[← Routing ID](08-routing-id.md) | [Performance →](10-performance.md)
