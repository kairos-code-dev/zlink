[English](polling.md) | [한국어](polling.ko.md)

# Polling & Proxy

Functions for multiplexing I/O across multiple sockets and for building
message-forwarding proxies. The polling API lets you wait for events on any
combination of zlink sockets and native file descriptors in a single call.

## Types

```c
#if defined _WIN32
#if defined _WIN64
typedef unsigned __int64 zlink_fd_t;
#else
typedef unsigned int zlink_fd_t;
#endif
#else
typedef int zlink_fd_t;
#endif

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;
```

`zlink_fd_t` is a platform-dependent file-descriptor type: `unsigned __int64`
on 64-bit Windows, `unsigned int` on 32-bit Windows, and `int` on POSIX
systems.

`zlink_pollitem_t` describes one item to poll. Set `socket` to a zlink socket
handle or set `fd` to a native file descriptor (when `socket` is `NULL`).
`events` specifies the events to watch for and `revents` is filled in by
`zlink_poll` with the events that actually occurred.

## Constants

```c
#define ZLINK_POLLIN   1
#define ZLINK_POLLOUT  2
#define ZLINK_POLLERR  4
#define ZLINK_POLLPRI  8

#define ZLINK_POLLITEMS_DFLT  16
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_POLLIN` | 1 | At least one message can be received without blocking |
| `ZLINK_POLLOUT` | 2 | At least one message can be sent without blocking |
| `ZLINK_POLLERR` | 4 | An error condition is present |
| `ZLINK_POLLPRI` | 8 | High-priority data is available (for raw file descriptors) |
| `ZLINK_POLLITEMS_DFLT` | 16 | Suggested default allocation size for poll-item arrays |

## Functions

### zlink_poll

Poll for events on a set of sockets and/or file descriptors.

```c
int zlink_poll(zlink_pollitem_t *items_, int nitems_, long timeout_);
```

Waits until at least one item signals a requested event, or the timeout
expires. Set `timeout_` to `-1` for infinite blocking, `0` for an immediate
non-blocking check, or a positive value for the maximum wait in milliseconds.
On return, each item's `revents` field indicates which events occurred.

**Returns:** The number of items with signalled events, `0` if the timeout
expired with no events, or `-1` on failure (errno is set).

**Errors:**
- `ETERM` -- the context associated with one of the sockets was terminated.
- `EFAULT` -- `items_` is `NULL` while `nitems_` is non-zero.
- `EINTR` -- the call was interrupted by a signal.

**Thread safety:** Each poll item must not be shared with another thread during
the call. Different threads may poll different item sets concurrently.

**See also:** `zlink_proxy`, `zlink_proxy_steerable`

---

### zlink_proxy

Start a built-in proxy between a frontend and a backend socket.

```c
int zlink_proxy(void *frontend_, void *backend_, void *capture_);
```

Connects a frontend socket to a backend socket, forwarding messages in both
directions. If `capture_` is not `NULL`, all messages are also sent to the
capture socket for logging or inspection. This call blocks forever (until the
context is terminated) and does not return under normal operation.

**Returns:** `-1` when the proxy terminates (errno is set to `ETERM`).

**Errors:**
- `ETERM` -- the context was terminated.

**Thread safety:** The three socket handles must not be used by other threads
while the proxy is running.

**See also:** `zlink_proxy_steerable`, `zlink_poll`

---

### zlink_proxy_steerable

Start a steerable proxy with an additional control socket.

```c
int zlink_proxy_steerable(void *frontend_,
                          void *backend_,
                          void *capture_,
                          void *control_);
```

Behaves like `zlink_proxy` but accepts commands on `control_`. Send the string
`PAUSE` to suspend message forwarding, `RESUME` to continue, or `TERMINATE`
to shut down the proxy and return. If `control_` is `NULL`, this function
behaves identically to `zlink_proxy`.

**Returns:** `0` when terminated via the control socket, or `-1` on failure
(errno is set).

**Errors:**
- `ETERM` -- the context was terminated.

**Thread safety:** The four socket handles must not be used by other threads
while the proxy is running. The control socket may be written to from any
thread.

**See also:** `zlink_proxy`, `zlink_poll`

---

### zlink_has

Check whether the library supports a given capability.

```c
int zlink_has(const char *capability_);
```

Queries the library for compile-time or run-time support of a named feature.
Common capability strings include `"ipc"`, `"tls"`, `"ws"`, and `"wss"`.

**Returns:** `1` if the capability is supported, `0` otherwise.

**Thread safety:** Safe to call from any thread at any time.

**See also:** `zlink_version`
