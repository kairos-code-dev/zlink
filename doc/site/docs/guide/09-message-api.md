# Message API Reference

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

=== "C"

    ```c
    zlink_msg_t msg;
    zlink_msg_init(&msg);
    /* Used for initialization or as a target for zlink_msg_copy(). Free with zlink_msg_close() */
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

#### zlink_msg_init_size — Size-Specified (Requires Copy)

Allocates a buffer of the specified size, then fills it directly via `zlink_msg_data()`. This is the pattern for **copying data into the message**.

=== "C"

    ```c
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, 1024);
    memcpy(zlink_msg_data(&msg), source_data, 1024);
    zlink_send(socket, &msg, 1, 0);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

**When to use:** When creating a message from data in your own buffer. Safe to free the original buffer immediately.

#### zlink_msg_init_data — External Buffer Reference (Zero-Copy)

Transfers ownership of an external buffer to the message. Sends without copying. The free callback (ffn) handles buffer cleanup.

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

**When to use:** When you want to avoid copying large data. Delegates buffer deallocation timing to the library.

> Reference: `core/tests/test_msg_ffn.cpp` — Verifies free function callback behavior

### 3.2 Data Access

=== "C"

    ```c
    void *data = zlink_msg_data(&msg);
    size_t size = zlink_msg_size(&msg);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> **Removed:** `zlink_msg_more()` and `ZLINK_MORE` have been removed from the header.
> With the multipart parts-array API, the `more` flag is no longer needed in
> application code.

### 3.3 Sending

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> **Legacy:** `zlink_msg_send()` is still present in the header but planned for
> removal. Use `zlink_send()` with a parts array instead.

### 3.4 Receiving

Messages are received via handler callbacks registered at socket creation time. The callback provides `zlink_msg_t` parts directly:

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### 3.5 Deallocation

=== "C"

    ```c
    zlink_msg_close(&msg);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 4. Ownership Rules

| Situation | Ownership | Subsequent Action |
|-----------|-----------|-------------------|
| `zlink_send` succeeds | Transferred to library | msg parts are empty, must not be accessed |
| `zlink_send` fails | Caller still owns | Must call `zlink_msg_close()` per part |
| Handler callback delivers msg | Library provides msg parts | Must call `zlink_msg_close()` per part |
| `zlink_msg_close` | Resources freed | msg can be reused (re-initialization required) |

### Ownership Rules in Practice

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 5. Zero-Copy Pattern Details

### Writing Free Function Callbacks

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_ffn.cpp` — `ffn()` callback writes "freed" to hint

### When Free Functions Are Called

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_ffn.cpp` — close/send/copy scenarios

### Constant Data with zlink_msg_init_data

Constant (literal, static) data can be sent without copying by using
`zlink_msg_init_data()` with `ffn=NULL`.

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_const()`

## 6. Multipart Message Patterns in Practice

Multipart messages are sent as a parts array in a single `zlink_send()` call.

### Pattern 1: Request-Reply (DEALER/ROUTER)

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_flags.cpp` — `test_more()`: DEALER→ROUTER multipart

### Pattern 2: Topic + Data (PUB/SUB)

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

### Pattern 3: Multipart Processing in Handler Callback

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 7. Message Copying

### zlink_msg_copy — Reference-Counted Copy

Increments the reference count instead of copying the data. Efficient for large messages.

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_refcounted()`: Verifying shared property after copy

### Storage Refcount — zlink_msg_refcnt

The internal reference count is managed with atomic operations.
Copying and closing different `zlink_msg_t` handles that share the same
underlying storage from different threads is safe. `zlink_msg_refcnt()`
returns a point-in-time snapshot, so use it for diagnostics and assertions.
Do not access a single `zlink_msg_t` instance concurrently from multiple
threads.

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> Reference: `core/tests/test_msg_flags.cpp` — `test_shared_const()`: shared property of constant messages

## 8. Error Handling

### Send Failure

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

## 9. zlink_send (Multipart Msg-Based)

`zlink_send()` now takes a `zlink_msg_t` parts array and a part count:

!!! note "C API signature -- each binding wraps this into its idiomatic method."

    ```c
    int zlink_send(void *s_, zlink_msg_t *parts_, size_t part_count_, zlink_send_flags_t flags_);
    ```

=== "C"

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

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

For ROUTER directed sends, use `zlink_send_rid()`:

=== "C"

    ```c
    zlink_send_rid(router, &target_rid, parts, part_count, 0);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

> **Legacy:** `zlink_msg_send()` is still present in the header but planned for
> removal. Migrate all call sites to `zlink_send()` with a parts array.

---
[← Routing ID](08-routing-id.md) | [Performance →](10-performance.md)
