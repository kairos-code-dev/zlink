[English](errno-map.md) | [한국어](errno-map.ko.md)

[Spec Index](../README.md) · [Core Index](README.md)

# Submit Result And Request Completion

This document defines the canonical outcome mapping for send, request, and
reply APIs.

The public C API now returns `zlink_submit_result_t` for send, request
submit, and reply submit. Internal implementation paths still use detailed
`errno` values, and exported API boundaries normalize those values into the
public result enums defined here.

## zlink_submit_result_t

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED = 1,
    ZLINK_SUBMIT_NOT_CONNECTED = 2,
    ZLINK_SUBMIT_NOT_FOUND = 3,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED = 7,
    ZLINK_SUBMIT_INVALID_STATE = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR = 12
} zlink_submit_result_t;
```

## Submit Result Classification

### Normal control-flow result

These are expected outcomes that callers may handle directly.

| Result | Internal errno | Meaning |
|---|---|---|
| `OK` | — | Submit succeeded |
| `BACKPRESSURED` | `EAGAIN` | Send queue full (HWM) or not writable yet |
| `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | Target peer or path is not connected |
| `NOT_FOUND` | `ENOENT` | Target peer, spot, or routed destination was not found |

### Runtime / lifecycle failure

| Result | Internal errno | Meaning |
|---|---|---|
| `TERMINATED` | `ETERM` | Context was terminated |

### Caller contract violation

These indicate a bug in the caller.

| Result | Internal errno | Meaning |
|---|---|---|
| `INVALID_HANDLE` | `EFAULT` | NULL handle or invalid pointer |
| `INVALID_ARGUMENT` | `EINVAL` | Wrong socket type, NULL handler, zero `request_seq`, invalid routing ID |
| `NOT_SUPPORTED` | `ENOTSUP` | Operation not supported for this socket type or invalid flags |
| `INVALID_STATE` | `EFSM` | Socket is in the wrong state |
| `THREAD_VIOLATION` | `EMTHREAD` | Socket accessed from the wrong thread model |

### Internal failure

| Result | Internal errno | Meaning |
|---|---|---|
| `OUT_OF_MEMORY` | `ENOMEM` | Memory allocation failed |
| `SEQ_EXHAUSTED` | `EBUSY` | Request sequence number space exhausted (request only) |
| `INTERNAL_ERROR` | `EPROTO` and other internal submit failures | Internal send/request/reply submit error |

## zlink_request_result_t

```c
typedef enum zlink_request_result_t
{
    /* Reply completed successfully. */
    ZLINK_REQUEST_OK = 0,

    /* Completion failure visible to the requester. */
    ZLINK_REQUEST_TIMED_OUT = 1,
    ZLINK_REQUEST_NOT_FOUND = 2,
    ZLINK_REQUEST_TERMINATED = 3,
    ZLINK_REQUEST_PROTOCOL_ERROR = 4
} zlink_request_result_t;
```

This enum applies only to request completion, not submit.

| Completion | Callback errno | Meaning |
|---|---|---|
| `OK` | `0` | Reply payload was received successfully |
| `TIMED_OUT` | `ETIMEDOUT` | Reply did not arrive within `timeout_ms` |
| `NOT_FOUND` | `ENOENT` | Target was not found and the request completed with an error reply |
| `TERMINATED` | `ETERM` | Reserved until the request path emits explicit termination completion |
| `PROTOCOL_ERROR` | `EPROTO` | Reply envelope or error reply payload was malformed |

## Submit Matrix

`Y` = the submit path can produce this normalized result.
`—` = the submit path never produces this normalized result.

### Send functions

| Result | `zlink_send` | `zlink_send_rid` | `zlink_publish` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | — | Y | — |
| `NOT_FOUND` | — | — | — |
| `TERMINATED` | Y | Y | Y |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | Y | Y | Y |
| `THREAD_VIOLATION` | Y | Y | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | — | — | — |

### Socket request functions

| Result | `zlink_dealer_request` | `zlink_router_request` |
|---|---|---|
| `OK` | Y | Y |
| `BACKPRESSURED` | Y | Y |
| `NOT_CONNECTED` | — | — |
| `NOT_FOUND` | — | — |
| `TERMINATED` | Y | Y |
| `INVALID_HANDLE` | Y | Y |
| `INVALID_ARGUMENT` | Y | Y |
| `NOT_SUPPORTED` | Y | Y |
| `INVALID_STATE` | Y | Y |
| `THREAD_VIOLATION` | Y | Y |
| `OUT_OF_MEMORY` | Y | Y |
| `SEQ_EXHAUSTED` | Y | Y |
| `INTERNAL_ERROR` | Y | Y |

### Socket reply function

| Result | `zlink_router_reply` |
|---|---|
| `OK` | Y |
| `BACKPRESSURED` | Y |
| `NOT_CONNECTED` | — |
| `NOT_FOUND` | — |
| `TERMINATED` | Y |
| `INVALID_HANDLE` | Y |
| `INVALID_ARGUMENT` | Y |
| `NOT_SUPPORTED` | Y |
| `INVALID_STATE` | Y |
| `THREAD_VIOLATION` | Y |
| `OUT_OF_MEMORY` | — |
| `SEQ_EXHAUSTED` | — |
| `INTERNAL_ERROR` | Y |

### SPOT request functions

| Result | `zlink_spot_request_spot` | `zlink_spot_request_router` | `zlink_router_request_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | Y | Y | Y |
| `SEQ_EXHAUSTED` | — | — | Y |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT send functions

| Result | `zlink_spot_send_spot` | `zlink_spot_send_router` | `zlink_router_send_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT reply functions

| Result | `zlink_spot_reply_spot` | `zlink_spot_reply_router` | `zlink_router_reply_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | Y | Y | Y |

## Request Callback Completion

`zlink_reply_handler_fn` receives `errno_` as the first parameter. This is a
request completion result, separate from submit.

Known completion `errno_` values in the current implementation are `0`,
`ETIMEDOUT`, `ENOENT`, and `EPROTO`. `ETERM` is reserved and does not yet
have an explicit completion emission path in the current request code.

## Applicable Functions

`zlink_submit_result_t` applies to submit of these 15 functions:

| Category | Functions |
|---|---|
| Send | `zlink_send`, `zlink_send_rid`, `zlink_publish` |
| Socket request | `zlink_dealer_request`, `zlink_router_request` |
| Socket reply | `zlink_router_reply` |
| SPOT request | `zlink_spot_request_spot`, `zlink_spot_request_router`, `zlink_router_request_spot` |
| SPOT send | `zlink_spot_send_spot`, `zlink_spot_send_router`, `zlink_router_send_spot` |
| SPOT reply | `zlink_spot_reply_spot`, `zlink_spot_reply_router`, `zlink_router_reply_spot` |

`zlink_request_result_t` applies to completion delivered through
`zlink_reply_handler_fn`.
