[English](context.md) | [한국어](context.ko.md)

# Context

A context is the top-level container that manages I/O threads and serves as
the foundation for creating sockets. Every application must create at least one
context before using any other zlink API. Contexts are thread-safe and may be
shared across threads.

## Context Option Constants

Options are set and queried with `zlink_ctx_set` and `zlink_ctx_get`.

```c
#define ZLINK_IO_THREADS              1
#define ZLINK_MAX_SOCKETS             2
#define ZLINK_SOCKET_LIMIT            3
#define ZLINK_THREAD_PRIORITY         3
#define ZLINK_THREAD_SCHED_POLICY     4
#define ZLINK_MAX_MSGSZ               5
#define ZLINK_MSG_T_SIZE              6
#define ZLINK_THREAD_AFFINITY_CPU_ADD      7
#define ZLINK_THREAD_AFFINITY_CPU_REMOVE   8
#define ZLINK_THREAD_NAME_PREFIX      9
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_IO_THREADS` | 1 | Number of I/O threads in the context |
| `ZLINK_MAX_SOCKETS` | 2 | Maximum number of sockets allowed |
| `ZLINK_SOCKET_LIMIT` | 3 | Hard upper limit on socket count (read-only) |
| `ZLINK_THREAD_PRIORITY` | 3 | I/O thread scheduling priority |
| `ZLINK_THREAD_SCHED_POLICY` | 4 | I/O thread scheduling policy |
| `ZLINK_MAX_MSGSZ` | 5 | Maximum message size in bytes (-1 = unlimited) |
| `ZLINK_MSG_T_SIZE` | 6 | Size of `zlink_msg_t` in bytes (read-only) |
| `ZLINK_THREAD_AFFINITY_CPU_ADD` | 7 | Add a CPU to the I/O thread affinity set |
| `ZLINK_THREAD_AFFINITY_CPU_REMOVE` | 8 | Remove a CPU from the I/O thread affinity set |
| `ZLINK_THREAD_NAME_PREFIX` | 9 | Prefix for I/O thread names |

## Default Values

```c
#define ZLINK_IO_THREADS_DFLT           2
#define ZLINK_MAX_SOCKETS_DFLT          1023
#define ZLINK_THREAD_PRIORITY_DFLT      -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT  -1
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_IO_THREADS_DFLT` | 2 | Default number of I/O threads |
| `ZLINK_MAX_SOCKETS_DFLT` | 1023 | Default maximum socket count |
| `ZLINK_THREAD_PRIORITY_DFLT` | -1 | Default thread priority (OS default) |
| `ZLINK_THREAD_SCHED_POLICY_DFLT` | -1 | Default scheduling policy (OS default) |

## Functions

### zlink_ctx_new

Create a new zlink context.

```c
void *zlink_ctx_new(void);
```

Allocates and initializes a new context with default option values. The context
manages a pool of I/O threads and serves as the foundation for creating
sockets. Every socket must be associated with a context. When the context is no
longer needed, release it with `zlink_ctx_term`.

**Returns:** A context handle on success, or `NULL` on failure (errno is set).

**Thread safety:** Safe to call from any thread. The returned context handle
may be shared across threads.

**See also:** `zlink_ctx_term`, `zlink_ctx_set`

---

### zlink_ctx_term

Terminate the context and release all associated resources.

```c
int zlink_ctx_term(void *context_);
```

Destroys the context. This call may block until all sockets created within the
context have been closed. Any blocking operations on sockets belonging to the
context will return with `ETERM` after `zlink_ctx_shutdown` is called or when
all sockets are closed. Each context must be terminated exactly once.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EFAULT` -- invalid context handle.
- `EINTR` -- termination was interrupted by a signal; may be retried.

**Thread safety:** Safe to call from any thread, but must be called exactly
once per context. Do not use the context handle after this call returns.

**See also:** `zlink_ctx_new`, `zlink_ctx_shutdown`

---

### zlink_ctx_shutdown

Shut down the context immediately.

```c
int zlink_ctx_shutdown(void *context_);
```

Signals all blocking operations on sockets belonging to this context to return
immediately with `ETERM`. This is a non-blocking call that initiates shutdown
but does not release resources. `zlink_ctx_term` must still be called
afterwards for final cleanup. Calling shutdown before term avoids deadlocks
when sockets are used across multiple threads.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EFAULT` -- invalid context handle.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_term`

---

### zlink_ctx_set

Set a context option.

```c
int zlink_ctx_set(void *context_, int option_, int optval_);
```

Configures the context before or after sockets have been created. Some options
(such as `ZLINK_IO_THREADS`) must be set before creating any sockets to take
effect. Refer to the option constants table above for valid option names and
their semantics.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown option or invalid value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_get`

---

### zlink_ctx_get

Get a context option.

```c
int zlink_ctx_get(void *context_, int option_);
```

Retrieves the current value of a context option. Can be used at any time to
inspect the context configuration, including read-only options such as
`ZLINK_SOCKET_LIMIT` and `ZLINK_MSG_T_SIZE`.

**Returns:** The option value on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- unknown option.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_set`
