[English](message.md) | [한국어](message.ko.md)

# Message API Reference

The Message API provides functions for creating, sending, receiving, and
managing zlink messages. Messages are the fundamental unit of data exchange
between sockets and can carry arbitrary binary payloads, support zero-copy
semantics, and form multipart sequences.

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

### Message Flags

| Constant | Value | Description |
|---|---|---|
| `ZLINK_MORE` | 1 | Indicates more parts follow in a multipart message |
| `ZLINK_SHARED` | 3 | Message data is shared (reference-counted) |

### Message Properties

The following property identifiers are used with `zlink_msg_get()`,
`zlink_msg_set()`, and `zlink_msg_gets()`:

| Function | Property | Description |
|---|---|---|
| `zlink_msg_more()` / `zlink_msg_get()` | `ZLINK_MORE` | Whether more parts follow |
| `zlink_msg_get()` | `ZLINK_SHARED` | Whether the message is shared |
| `zlink_msg_gets()` | String key | Retrieve metadata by key name (e.g. `"Socket-Type"`, `"Identity"`, `"Peer-Address"`) |

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

### zlink_msg_send

Send a message on a socket.

```c
int zlink_msg_send (zlink_msg_t *msg_, void *s_, int flags_);
```

Sends the message `msg_` on socket `s_`. On success, ownership of the message
transfers to the library and `msg_` becomes an empty message (as if
`zlink_msg_init()` had been called on it). The caller must not access the
original message data after a successful send. On failure the message is
unchanged and the caller retains ownership.

`flags_` may be 0, `ZLINK_DONTWAIT`, `ZLINK_SNDMORE`, or a bitwise OR of
these values. `ZLINK_SNDMORE` indicates that more parts will follow in a
multipart message.

**Returns:** Number of bytes in the message on success, -1 on failure (errno
is set).

**Errors:** `EAGAIN` if the socket cannot send immediately and
`ZLINK_DONTWAIT` was set. `ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_msg_recv`, `zlink_send`

---

### zlink_msg_recv

Receive a message from a socket.

```c
int zlink_msg_recv (zlink_msg_t *msg_, void *s_, int flags_);
```

Receives a message from socket `s_` and stores it in `msg_`. Any previous
content of `msg_` is properly released before storing the new message. The
caller owns the received message and must close it with `zlink_msg_close()`
when finished.

**Returns:** Number of bytes in the received message on success, -1 on failure
(errno is set).

**Errors:** `EAGAIN` if no message is available and `ZLINK_DONTWAIT` was set.
`ETERM` if the context was terminated.

**Thread safety:** Not thread-safe on the same socket.

**See also:** `zlink_msg_send`, `zlink_recv`

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

**See also:** `zlink_msg_init`, `zlink_msgv_close`

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

### zlink_msg_more

Check if more parts follow in a multipart message.

```c
int zlink_msg_more (const zlink_msg_t *msg_);
```

Queries the `ZLINK_MORE` flag on the message. Returns 1 if the message is part
of a multipart sequence and more parts follow, 0 otherwise. Typically called
after `zlink_msg_recv()` to determine whether to continue receiving.

**Returns:** 1 if more parts follow, 0 otherwise.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_get`, `ZLINK_MORE`

---

### zlink_msg_get

Get an integer message property.

```c
int zlink_msg_get (const zlink_msg_t *msg_, int property_);
```

Retrieves the value of an integer property from the message. Valid properties
include `ZLINK_MORE` and `ZLINK_SHARED`.

**Returns:** Property value on success, -1 on failure (errno is set to
`EINVAL` for an unknown property).

**Errors:** `EINVAL` if the property is not recognized.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_set`, `zlink_msg_gets`

---

### zlink_msg_set

Set an integer message property.

```c
int zlink_msg_set (zlink_msg_t *msg_, int property_, int optval_);
```

Sets the value of an integer property on the message. The set of writable
properties is implementation-defined.

**Returns:** 0 on success, -1 on failure (errno is set).

**Errors:** `EINVAL` if the property is not recognized or not writable.

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_get`

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

**See also:** `zlink_msg_get`

---

### zlink_msgv_close

Close all parts in a multipart message array.

```c
void zlink_msgv_close (zlink_msg_t *parts, size_t part_count);
```

Convenience function that calls `zlink_msg_close()` on each element of the
`parts` array. Use this to clean up after receiving or constructing a multipart
message stored as a contiguous array of `zlink_msg_t` structures.

**Returns:** None (void).

**Thread safety:** Not thread-safe.

**See also:** `zlink_msg_close`
