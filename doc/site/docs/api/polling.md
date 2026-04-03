
# Proxy & Utilities

Functions for building message-forwarding proxies and querying library
capabilities.

With the callback-only receive model, the polling API (`zlink_poll`,
`zlink_pollitem_t`, `ZLINK_POLLIN`) has been removed. All message receiving is
now handled through handler callbacks. The proxy and capability-check functions
remain.

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
```

`zlink_fd_t` is a platform-dependent file-descriptor type.

## Functions

### zlink_proxy

Start a built-in proxy between a frontend and a backend socket.

```c
int zlink_proxy (void *frontend_, void *backend_, void *capture_);
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

**Returns:** `0` when terminated via the control socket, or `-1` on failure
(errno is set).

**Thread safety:** The four socket handles must not be used by other threads
while the proxy is running. The control socket may be written to from any
thread.

**See also:** `zlink_proxy`

---

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
