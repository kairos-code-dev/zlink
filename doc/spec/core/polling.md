[English](polling.md) | [한국어](polling.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Polling, Proxy & Capability

Functions and types for I/O multiplexing across sockets, file descriptors, and
timers, plus message-forwarding proxies and runtime capability queries.

## Types

### zlink_fd_t

Platform-dependent file-descriptor type.

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
```

### zlink_poller_event_mask_t

Bitmask type for polling event flags.

```c
typedef short zlink_poller_event_mask_t;
```

### zlink_poller_source_kind_t

Identifies the kind of source that produced a poller event.

```c
typedef enum zlink_poller_source_kind_t
{
    ZLINK_POLLER_SOURCE_SOCKET = 1,
    ZLINK_POLLER_SOURCE_FD     = 2,
    ZLINK_POLLER_SOURCE_TIMER  = 3
} zlink_poller_source_kind_t;
```

| Value | Meaning |
|-------|---------|
| `ZLINK_POLLER_SOURCE_SOCKET` | Event originated from a zlink socket |
| `ZLINK_POLLER_SOURCE_FD` | Event originated from a native file descriptor |
| `ZLINK_POLLER_SOURCE_TIMER` | Event originated from a timer |

### zlink_pollitem_t

Descriptor used with the legacy `zlink_poll` function.

```c
typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;
```

| Field | Description |
|-------|-------------|
| `socket` | Zlink socket handle, or `NULL` to poll a raw fd |
| `fd` | Native file descriptor (used when `socket` is `NULL`) |
| `events` | Requested event mask (`ZLINK_POLLIN`, `ZLINK_POLLOUT`, etc.) |
| `revents` | Returned event mask filled by `zlink_poll` |

### zlink_poller_event_t

Event structure returned by the modern poller API.

```c
typedef struct zlink_poller_event_t
{
    zlink_poller_source_kind_t source_kind;
    void *socket;
    zlink_fd_t fd;
    void *timer;
    void *user_data;
    short events;
} zlink_poller_event_t;
```

| Field | Description |
|-------|-------------|
| `source_kind` | Which kind of source triggered the event |
| `socket` | Zlink socket handle (valid when `source_kind` is `SOCKET`) |
| `fd` | Native file descriptor (valid when `source_kind` is `FD`) |
| `timer` | Timer handle (valid when `source_kind` is `TIMER`) |
| `user_data` | Opaque pointer supplied when the source was registered |
| `events` | Bitmask of events that occurred |

## Constants

```c
#define ZLINK_POLLIN          1
#define ZLINK_POLLOUT         2
#define ZLINK_POLLERR         4
#define ZLINK_POLLPRI         8
#define ZLINK_POLLITEMS_DFLT 16
#define ZLINK_HAVE_POLLER     1
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_POLLIN` | 1 | Data is available for reading |
| `ZLINK_POLLOUT` | 2 | Writing will not block |
| `ZLINK_POLLERR` | 4 | An error occurred on the descriptor |
| `ZLINK_POLLPRI` | 8 | Urgent / priority data available |
| `ZLINK_POLLITEMS_DFLT` | 16 | Default poll-item array size |
| `ZLINK_HAVE_POLLER` | 1 | Library was compiled with poller support |

## Functions -- Legacy Poll

### zlink_poll

Poll a set of sockets and/or file descriptors for I/O readiness.

```c
int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_);
```

Waits for events on the descriptors listed in `items_`. Each entry specifies
the events of interest in its `events` field; on return, the `revents` field
of each entry indicates which events occurred.

**Parameters:**

| Name | Description |
|------|-------------|
| `items_` | Array of poll items to monitor |
| `nitems_` | Number of entries in the array |
| `timeout_` | Maximum wait time in milliseconds; `0` for immediate return, `-1` for indefinite blocking |

**Returns:** The number of items with events signalled, `0` on timeout, or
`-1` on failure (errno is set).

**Errors:**
- `ETERM` -- the context was terminated.
- `EFAULT` -- the `items_` pointer is invalid.
- `EINTR` -- the call was interrupted by a signal.

**Thread safety:** Each poll item's socket must not be used by another thread
during the call.

**See also:** `zlink_poller_wait`

---

## Functions -- Poller API

The poller is a modern, object-based alternative to `zlink_poll`. It supports
sockets, native file descriptors, and timers in a single event loop.

### zlink_poller_new

Create a new poller instance.

```c
void *zlink_poller_new (void);
```

Allocates and returns an opaque poller handle. Destroy with
`zlink_poller_destroy` when no longer needed.

**Returns:** Poller handle on success, or `NULL` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_poller_destroy`

---

### zlink_poller_destroy

Destroy a poller and release its resources.

```c
int zlink_poller_destroy (void **poller_p_);
```

Frees the poller handle. The pointer at `*poller_p_` is set to `NULL` after
destruction.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called while another thread is using the same
poller.

**See also:** `zlink_poller_new`

---

### zlink_poller_size

Return the number of sources registered with the poller.

```c
int zlink_poller_size (void *poller_);
```

**Returns:** The current count of registered sockets, file descriptors, and
timers, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with add/remove operations
on the same poller.

---

### zlink_poller_add

Register a zlink socket with the poller.

```c
int zlink_poller_add (void *poller_, void *socket_, void *user_data_, short events_);
```

Adds `socket_` to the poller and monitors it for the events specified in
`events_`. The `user_data_` pointer is stored and returned in
`zlink_poller_event_t` when an event fires.

**Parameters:**

| Name | Description |
|------|-------------|
| `poller_` | Poller handle |
| `socket_` | Zlink socket to monitor |
| `user_data_` | Opaque pointer returned with events |
| `events_` | Event mask (`ZLINK_POLLIN`, `ZLINK_POLLOUT`, etc.) |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_modify`, `zlink_poller_remove`

---

### zlink_poller_modify

Change the monitored events for a registered socket.

```c
int zlink_poller_modify (void *poller_, void *socket_, short events_);
```

Updates the event mask for `socket_` that was previously added with
`zlink_poller_add`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_add`

---

### zlink_poller_remove

Remove a zlink socket from the poller.

```c
int zlink_poller_remove (void *poller_, void *socket_);
```

Unregisters the socket. It will no longer produce events.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_add`

---

### zlink_poller_add_fd

Register a native file descriptor with the poller.

```c
int zlink_poller_add_fd (void *poller_, zlink_fd_t fd_, void *user_data_, short events_);
```

Adds the file descriptor `fd_` and monitors it for the specified events.

**Parameters:**

| Name | Description |
|------|-------------|
| `poller_` | Poller handle |
| `fd_` | Native file descriptor |
| `user_data_` | Opaque pointer returned with events |
| `events_` | Event mask |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_modify_fd`, `zlink_poller_remove_fd`

---

### zlink_poller_add_timer

Register a timer with the poller.

```c
int zlink_poller_add_timer (void *poller_, void *timer_, void *user_data_);
```

Adds the timer handle `timer_` to the poller. When the timer fires, the poller
returns an event with `source_kind` set to `ZLINK_POLLER_SOURCE_TIMER`.

**Parameters:**

| Name | Description |
|------|-------------|
| `poller_` | Poller handle |
| `timer_` | Timer handle (from `zlink_timer_new` or `zlink_spot_timer_new`) |
| `user_data_` | Opaque pointer returned with events |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_remove_timer`

---

### zlink_poller_modify_fd

Change the monitored events for a registered file descriptor.

```c
int zlink_poller_modify_fd (void *poller_, zlink_fd_t fd_, short events_);
```

Updates the event mask for `fd_` that was previously added with
`zlink_poller_add_fd`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_add_fd`

---

### zlink_poller_remove_fd

Remove a file descriptor from the poller.

```c
int zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_);
```

Unregisters the file descriptor. It will no longer produce events.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_add_fd`

---

### zlink_poller_remove_timer

Remove a timer from the poller.

```c
int zlink_poller_remove_timer (void *poller_, void *timer_);
```

Unregisters the timer. It will no longer produce poller events.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_add_timer`

---

### zlink_poller_wait

Wait for a single event.

```c
int zlink_poller_wait (void *poller_, zlink_poller_event_t *event_, long timeout_);
```

Blocks until one of the registered sources has an event ready, or the timeout
expires. On success, `event_` is populated with the event details.

**Parameters:**

| Name | Description |
|------|-------------|
| `poller_` | Poller handle |
| `event_` | Pointer to a single event structure to fill |
| `timeout_` | Maximum wait in milliseconds; `0` for immediate, `-1` for indefinite |

**Returns:** `0` if an event was received, `-1` on timeout or failure (errno
is set; `EAGAIN` on timeout).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_wait_all`

---

### zlink_poller_wait_all

Wait for multiple events in a single call.

```c
int zlink_poller_wait_all (void *poller_,
                           zlink_poller_event_t *events_,
                           int n_events_,
                           long timeout_);
```

Blocks until at least one registered source has an event ready, then fills
`events_` with up to `n_events_` events.

**Parameters:**

| Name | Description |
|------|-------------|
| `poller_` | Poller handle |
| `events_` | Array of event structures to fill |
| `n_events_` | Maximum number of events to return |
| `timeout_` | Maximum wait in milliseconds; `0` for immediate, `-1` for indefinite |

**Returns:** The number of events stored in `events_`, or `-1` on failure
(errno is set; `EAGAIN` on timeout).

**Thread safety:** Must not be called concurrently with other operations on
the same poller.

**See also:** `zlink_poller_wait`

---

## Functions -- Proxy

### zlink_proxy

Start a built-in proxy between a frontend and a backend socket.

```c
int zlink_proxy (void *frontend_, void *backend_, void *capture_);
```

Connects a frontend socket to a backend socket, forwarding messages in both
directions. If `capture_` is not `NULL`, all messages are also sent to the
capture socket for logging or inspection. This call blocks forever (until the
context is terminated) and does not return under normal operation.

**Parameters:**

| Name | Description |
|------|-------------|
| `frontend_` | Socket facing clients |
| `backend_` | Socket facing workers / services |
| `capture_` | Optional socket for message capture, or `NULL` |

**Returns:** `-1` when the proxy terminates (errno is set to `ETERM`).

**Errors:**
- `ETERM` -- the context was terminated.

**Thread safety:** The three socket handles must not be used by other threads
while the proxy is running.

**See also:** `zlink_proxy_steerable`

---

### zlink_proxy_steerable

Start a steerable proxy with an additional control socket.

```c
int zlink_proxy_steerable (void *frontend_,
                           void *backend_,
                           void *capture_,
                           void *control_);
```

Behaves like `zlink_proxy` but accepts commands on `control_`. Send the string
`PAUSE` to suspend message forwarding, `RESUME` to continue, or `TERMINATE`
to shut down the proxy and return. If `control_` is `NULL`, this function
behaves identically to `zlink_proxy`.

**Parameters:**

| Name | Description |
|------|-------------|
| `frontend_` | Socket facing clients |
| `backend_` | Socket facing workers / services |
| `capture_` | Optional socket for message capture, or `NULL` |
| `control_` | Control socket (PAIR type), or `NULL` |

**Returns:** `0` when terminated via the control socket, or `-1` on failure
(errno is set).

**Thread safety:** The four socket handles must not be used by other threads
while the proxy is running. The control socket may be written to from any
thread.

**See also:** `zlink_proxy`

---

## Functions -- Capability

### zlink_has

Check whether the library supports a given capability.

```c
int zlink_has (const char *capability_);
```

Queries the library for compile-time or run-time support of a named feature.
Common capability strings include `"ipc"`, `"tls"`, `"ws"`, and `"wss"`.

**Returns:** `1` if the capability is supported, `0` otherwise.

**Thread safety:** Safe to call from any thread at any time.

**See also:** `zlink_version`
