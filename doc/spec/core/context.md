[English](context.md) | [한국어](context.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Context

A context is the top-level container that manages I/O threads and serves as
the foundation for creating sockets. Every application must create at least one
context before using any other zlink API. Contexts are thread-safe and may be
shared across threads.

## Context Option Constants

Options are set and queried with `zlink_ctx_set` and `zlink_ctx_get`.

```c
typedef enum zlink_ctx_option_t
{
    ZLINK_IO_THREADS              = 1,
    ZLINK_MAX_SOCKETS             = 2,
    ZLINK_SOCKET_LIMIT            = 3,
    ZLINK_THREAD_PRIORITY         = 3,
    ZLINK_THREAD_SCHED_POLICY     = 4,
    ZLINK_MAX_MSGSZ               = 5,
    ZLINK_MSG_T_SIZE              = 6,
    ZLINK_THREAD_AFFINITY_CPU_ADD      = 7,
    ZLINK_THREAD_AFFINITY_CPU_REMOVE   = 8,
    ZLINK_THREAD_NAME_PREFIX      = 9,
    ZLINK_CTX_OPT_BLOCKY          = 10,
    ZLINK_SPOT_WORKER_THREADS     = 11,
    ZLINK_CTX_OPT_AUTO_HWM_ENABLE = 12,
    ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB = 13
} zlink_ctx_option_t;
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
| `ZLINK_CTX_OPT_BLOCKY` | 10 | Legacy option for blocking behavior on context termination (`int`; default 1) |
| `ZLINK_SPOT_WORKER_THREADS` | 11 | Worker count for `zlink_spot_dispatch_event_handler()` callbacks (`0` = auto) |
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | 12 | Whether automatic HWM policy is enabled (`0` = disabled, `1` = enabled) |
| `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` | 13 | Total context memory budget in MB used by the automatic HWM policy (`>= 1`) |

## Default Values

```c
#define ZLINK_IO_THREADS_DFLT           1
#define ZLINK_MAX_SOCKETS_DFLT          4095
#define ZLINK_THREAD_PRIORITY_DFLT      -1
#define ZLINK_THREAD_SCHED_POLICY_DFLT  -1
#define ZLINK_SPOT_WORKER_THREADS_DFLT  0
#define ZLINK_CTX_AUTO_HWM_ENABLE_DFLT  1
#define ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT 128
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_IO_THREADS_DFLT` | 1 | Default number of I/O threads |
| `ZLINK_MAX_SOCKETS_DFLT` | 4095 | Default maximum socket count |
| `ZLINK_THREAD_PRIORITY_DFLT` | -1 | Default thread priority (OS default) |
| `ZLINK_THREAD_SCHED_POLICY_DFLT` | -1 | Default scheduling policy (OS default) |
| `ZLINK_SPOT_WORKER_THREADS_DFLT` | 0 | Default Spot worker count (`0` = auto) |
| `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT` | 1 | Automatic HWM policy enabled by default |
| `ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT` | 128 | Default total context memory budget used by automatic HWM policy (MB) |

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
zlink_close_result_t zlink_ctx_term(void *context_);
```

Destroys the context. This call may block until all sockets created within the
context have been closed. Any blocking operations on sockets belonging to the
context will return with `ETERM` after `zlink_ctx_shutdown` is called or when
all sockets are closed. Each context must be terminated exactly once.

**Returns:** `ZLINK_CLOSE_OK` on success; otherwise a `zlink_close_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

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
zlink_close_result_t zlink_ctx_shutdown(void *context_);
```

Signals all blocking operations on sockets belonging to this context to return
immediately with `ETERM`. This is a non-blocking call that initiates shutdown
but does not release resources. `zlink_ctx_term` must still be called
afterwards for final cleanup. Calling shutdown before term avoids deadlocks
when sockets are used across multiple threads.

**Returns:** `ZLINK_CLOSE_OK` on success; otherwise a `zlink_close_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:**
- `EFAULT` -- invalid context handle.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_term`

---

### zlink_ctx_set

Set a context option.

```c
zlink_config_result_t zlink_ctx_set(void *context_, zlink_ctx_option_t option_, int optval_);
```

Configures the context before or after sockets have been created. Some options
(such as `ZLINK_IO_THREADS` and `ZLINK_SPOT_WORKER_THREADS`) must be set before
runtime startup to take effect. `ZLINK_SPOT_WORKER_THREADS` controls the worker
count for `zlink_spot_dispatch_event_handler()` callbacks. A value of `0`
means auto-select, which resolves to `min(visible logical cores, 8)` and falls
back to `1` if the core count cannot be determined. Changing this option after
runtime startup fails with `ZLINK_CONFIG_INVALID_ARGUMENT` (`errno=EINVAL`).
Refer to the option constants table above for valid option names and their
semantics. `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` and
`ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` take effect on existing
sockets immediately, but only for sockets that still use automatic
`SNDHWM` / `RCVHWM` / `SNDBUF` / `RCVBUF` values rather than manual
overrides.

**Returns:** `ZLINK_CONFIG_OK` on success; otherwise a `zlink_config_result_t` value. `zlink_errno()` retains the detailed internal errno for diagnostics.

**Errors:**
- `EINVAL` -- unknown option or invalid value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_get`

---

### zlink_ctx_get

Get a context option.

```c
int zlink_ctx_get(void *context_, zlink_ctx_option_t option_, zlink_config_result_t *error_out_);
```

Retrieves the current value of a context option. Can be used at any time to
inspect the context configuration, including read-only options such as
`ZLINK_SOCKET_LIMIT` and `ZLINK_MSG_T_SIZE`. Writes the configuration result
into `*error_out_` on failure; returns the option value as the primary return
on success.

**Returns:** The option value on success, or `-1` on failure with the
`zlink_config_result_t` written through `*error_out_`. `zlink_errno()` retains
the detailed internal errno for diagnostics.

**Errors:**
- `EINVAL` -- unknown option.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_ctx_set`
