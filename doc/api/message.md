[English](message.md) | [한국어](message.ko.md)

# Message API Reference

The Message API provides functions for creating, sending, and managing zlink
messages. Messages are the fundamental unit of data exchange between sockets
and can carry arbitrary binary payloads, support zero-copy semantics, and
form multipart sequences.

## Types

```c
typedef struct zlink_msg_t
{
    unsigned char _[64];
} zlink_msg_t;
```

`zlink_msg_t` is a 64-byte opaque message structure. The internal layout is
platform-dependent and must not be accessed directly. Every message must be
initialized before use and closed after use.

```c
typedef struct zlink_routing_id_t
{
    uint8_t size;
    uint8_t data[255];
} zlink_routing_id_t;
```

`zlink_routing_id_t` carries a routing identity used by `ROUTER` sockets to
address specific peers. `size` indicates the number of valid bytes in `data`.

```c
typedef void (zlink_free_fn) (void *data_, void *hint_);
```

`zlink_free_fn` is a callback type used with `zlink_msg_init_data()` for
zero-copy message creation. The library invokes this function when the message
data buffer is no longer needed.

## Constants

### String Metadata Properties

The following string metadata keys can be retrieved with `zlink_msg_gets()`:

| Key | Description |
|---|---|
| `"Socket-Type"` | Socket type of the peer |
| `"Identity"` | Peer identity |
| `"Peer-Address"` | Peer network address |

## Functions

### zlink_msg_init

Initialize an empty message.

```c
int zlink_msg_init (zlink_msg_t *msg_);
```

Initializes `msg_` to an empty zero-length message. The message must
eventually be released with `zlink_msg_close()`. Always initialize a
`zlink_msg_t` before passing it to any other message function.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe. Each `zlink_msg_t` must be used from a
single thread at a time.

**See also:** `zlink_msg_init_size`, `zlink_msg_init_data`, `zlink_msg_close`

---

### zlink_msg_init_size

Initialize a message of a given size.

```c
int zlink_msg_init_size (zlink_msg_t *msg_, size_t size_);
```

Allocates an internal buffer of `size_` bytes and initializes `msg_`. The
buffer contents are uninitialized. Use `zlink_msg_data()` to obtain a pointer
to the buffer and populate it before sending.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `ENOMEM` if the allocation fails.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_data`, `zlink_msg_size`

---

### zlink_msg_init_data

Initialize a message from an external data buffer (zero-copy).

```c
int zlink_msg_init_data (
  zlink_msg_t *msg_, void *data_, size_t size_, zlink_free_fn *ffn_, void *hint_);
```

Creates a message that references the caller-provided buffer `data_` of
`size_` bytes without copying it. When the library no longer needs the buffer
(after the message has been sent or closed), it invokes the callback `ffn_`
with `data_` and `hint_` as arguments so the caller can release the buffer.
If `ffn_` is `NULL`, no callback is invoked and the caller is responsible for
ensuring the buffer outlives the message.

This function enables true zero-copy message passing. The caller must not
modify or free `data_` until `ffn_` has been called.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_free_fn`, `zlink_msg_data`

---

### zlink_msg_close

Release message resources.

```c
int zlink_msg_close (zlink_msg_t *msg_);
```

Releases all resources associated with the message. Every initialized message
must be closed exactly once. After closing, the `zlink_msg_t` structure is
invalid and must be re-initialized before reuse.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_init`, `zlink_multipart_close`

---

### zlink_msg_move

Move message content from source to destination.

```c
int zlink_msg_move (zlink_msg_t *dest_, zlink_msg_t *src_);
```

Moves the content of `src_` into `dest_`. After a successful move, `src_`
becomes an empty message (equivalent to a freshly initialized message) and
`dest_` contains the original content. Any previous content of `dest_` is
released.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_copy`

---

### zlink_msg_copy

Copy a message.

```c
int zlink_msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_);
```

Copies the content of `src_` into `dest_`. Both messages share the underlying
data buffer via reference counting. Any previous content of `dest_` is
released. The copy is lightweight and does not duplicate the data payload.

**Returns:** 0 on success, -1 on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_move`

---

### zlink_msg_data

Return a pointer to the message data buffer.

```c
void *zlink_msg_data (zlink_msg_t *msg_);
```

Returns a pointer to the raw data payload of the message. The pointer is valid
until the message is closed, moved, or sent. Returns `NULL` if the message is
uninitialized.

**Returns:** Pointer to the message data buffer.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_size`

---

### zlink_msg_size

Return the message data size in bytes.

```c
size_t zlink_msg_size (const zlink_msg_t *msg_);
```

Returns the size of the message payload in bytes. For empty messages this
returns 0.

**Returns:** Size in bytes.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_data`

---

### zlink_msg_refcnt

Return the message storage reference count.

```c
int zlink_msg_refcnt (const zlink_msg_t *msg_);
```

Returns the current internal reference count for reference-counted large
or zero-copy message storage. Message kinds that are not internally
reference-counted (inline, borrowed-constant) return 1.

The internal reference count is managed with atomic operations:
`zlink_msg_copy()` atomically increments the count, and `zlink_msg_close()`
atomically decrements it — these are safe to call from different threads on
different `zlink_msg_t` handles that share the same underlying storage.

`zlink_msg_refcnt()` itself performs a relaxed read of the atomic counter.
The returned value is a point-in-time snapshot; by the time the caller
inspects it, another thread may have already changed the count via copy or
close. This makes the function suitable for diagnostics and assertions but
not for control decisions.

A single `zlink_msg_t` instance must not be accessed concurrently from
multiple threads. Concurrent access requires separate `zlink_msg_t` handles
created via `zlink_msg_copy()`.

**Returns:** Current storage reference count, or 1 when the message kind is
not internally reference-counted.

**Thread safety:** The underlying reference count is atomic. Reading it
via this function is safe while other threads copy or close *different*
`zlink_msg_t` handles that share the same storage. However, calling this
function and any other `zlink_msg_*` function on the *same* `zlink_msg_t`
instance from multiple threads concurrently is not safe.

**See also:** `zlink_msg_copy`, `zlink_msg_close`

---

### zlink_msg_gets

Get a string message property.

```c
const char *zlink_msg_gets (const zlink_msg_t *msg_, const char *property_);
```

Retrieves a string metadata value from the message by key name. Metadata is
attached by the transport layer and may include keys such as
`"Socket-Type"`, `"Identity"`, and `"Peer-Address"`. The returned pointer
is valid only until the message is closed.

**Returns:** Null-terminated string on success, `NULL` on failure (errno is
set).

**Errors:** `EINVAL` if the property name is not found in the message
metadata.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_refcnt`

---

### zlink_multipart_close

Close all parts in a multipart message array.

```c
void zlink_multipart_close (zlink_msg_t *parts, size_t part_count);
```

Convenience function that calls `zlink_msg_close()` on each element of the
`parts` array. Use this to clean up after receiving or constructing a multipart
message stored as a contiguous array of `zlink_msg_t` structures.

**Returns:** None (void).

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_close`
