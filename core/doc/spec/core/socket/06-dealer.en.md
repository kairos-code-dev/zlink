[한국어](06-dealer.ko.md) | English

[Specification index](../../README.en.md) · [Core index](../README.en.md) · [Socket overview](README.en.md) · [errno map](../04-errno-map.en.md)

# Socket — DEALER

DEALER is an asynchronous raw socket that fair-queues inbound messages and
sends to connected peers using round-robin or weight-aware selection. The same
socket can process ordinary raw messages and request/reply records.

## 1. Public types

The following numbers are public ABI values.

```c
typedef enum zlink_dealer_option_t {
  ZLINK_DEALER_OPT_PROBE              = 0x3201,
  ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS = 0x3202,
  ZLINK_DEALER_OPT_WEIGHT             = 0x3203
} zlink_dealer_option_t;

typedef enum zlink_dealer_message_type_t {
  ZLINK_DEALER_MESSAGE_RAW         = 0,
  ZLINK_DEALER_MESSAGE_REQUEST     = 1,
  ZLINK_DEALER_MESSAGE_REPLY       = 2,
  ZLINK_DEALER_MESSAGE_ERROR_REPLY = 3
} zlink_dealer_message_type_t;

typedef enum zlink_part_flag_t {
  ZLINK_PART_FINAL = 0,
  ZLINK_PART_MORE  = 1
} zlink_part_flag_t;

typedef void (*zlink_reply_handler_fn)(
  zlink_request_result_t result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

`ZLINK_PART_MORE` means that another part follows in the same multipart
record. `ZLINK_PART_FINAL` means that the current part is the last part of the
record. Receive APIs use the same two values for `has_more_out_`.

## 2. DEALER options

```c
ZLINK_EXPORT zlink_config_result_t zlink_set_dealer_option(
  void *handle_,
  zlink_dealer_option_t option_,
  const void *optval_,
  size_t optvallen_);

ZLINK_EXPORT zlink_config_result_t zlink_get_dealer_option(
  void *handle_,
  zlink_dealer_option_t option_,
  void *optval_,
  size_t *optvallen_);
```

| Constant | Value format | Meaning |
|---|---|---|
| `ZLINK_DEALER_OPT_PROBE` | `int`, `0` or `1` | Sends an empty raw message when a connection is established so the peer can observe the connection and routing ID; the default is `0` |
| `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` | Nonnegative `int`, milliseconds | Selects the default timeout used by a request API when `timeout_ms_ == 0`; the default is `5000` |
| `ZLINK_DEALER_OPT_WEIGHT` | `int`, `0..100` | Advertises this DEALER's weight to connected peers; the default is `100` |

For `zlink_get_dealer_option()`, `*optvallen_` is the input capacity of
`optval_`. On success it is updated to the number of bytes written. HWM,
reconnect and timeout options that are not DEALER-specific use
`zlink_set_option()` and `zlink_get_option()`.

Outbound peers with equal positive weights are selected in round-robin order.
Unequal positive weights affect the selection ratio, while a peer with weight
`0` is excluded. If every known peer has weight `0`, a submit may fail with
`ZLINK_SUBMIT_NOT_ADMITTED`.

## 3. Message record classification

`zlink_dealer_recv_part()` returns a record kind and request sequence together
with each payload part.

| `message_type_out_` value | `request_seq_out_` | Meaning |
|---|---:|---|
| `ZLINK_DEALER_MESSAGE_RAW` (`0`) | `0` | An ordinary raw multipart message without a request/reply envelope |
| `ZLINK_DEALER_MESSAGE_REQUEST` (`1`) | Nonzero | A request received by this DEALER; the returned value is a reply token for `zlink_dealer_reply_part()` |
| `ZLINK_DEALER_MESSAGE_REPLY` (`2`) | Nonzero | A successful reply record |
| `ZLINK_DEALER_MESSAGE_ERROR_REPLY` (`3`) | Nonzero | A failed reply record |

Every part of one multipart record returns the same message type and request
sequence. Replies and terminal failures for work started through a request API
are delivered through the `zlink_reply_handler_fn` completion. The following
API sends an ordinary raw message and does not create a request sequence.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_send_part(
  void *s_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

## 4. Part sequences and ownership

`*_part` send calls form one multipart sequence from `ZLINK_PART_MORE` through
`ZLINK_PART_FINAL`. While a sequence is open, another send-helper family cannot
be interleaved on the same handle.

When `part_` points to a valid initialized message, a send API consumes its
message content on both success and failure. The caller therefore cannot read
the pre-submit payload or submit the same content again after the call,
regardless of its result. Payload needed for a retry must be retained in a
separate message before the call.

Each send-helper family stages successful intermediate parts as one record
until `ZLINK_PART_FINAL` succeeds. If an intermediate or final submit in an
open sequence fails, Core atomically discards the previously staged parts and
the failed part and closes the sequence. No part of that record becomes visible
to the peer. The failed call still consumes `part_`, and the next submit starts
the first part of a new record. A failed request submit creates no request
sequence and invokes no handler. After a reply-sequence failure, the reply
token remains valid until a successful `ZLINK_PART_FINAL` or request-lifecycle termination, so the caller
can resubmit a retained complete reply from its first part.

`part_out_` passed to a receive API must be an initialized `zlink_msg_t`. On
success, ownership of the received part moves to the caller, which closes it
exactly once with `zlink_msg_close()`. No received-part ownership moves on
failure.

## 5. Raw request submit

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_request_part(
  void *dealer_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);
```

This submits one asynchronous request payload one part at a time. For an
intermediate part, pass `ZLINK_PART_MORE`, `timeout_ms_ == 0`, `handler_ ==
NULL`, and `userdata_ == NULL`. The last part uses `ZLINK_PART_FINAL` and a
non-null `handler_`. A final call with `timeout_ms_ == 0` uses the
`ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` default. `flags_` is
`ZLINK_SEND_FLAGS_NONE` or `ZLINK_SEND_FLAGS_DONTWAIT`.

If the final submit returns `ZLINK_SUBMIT_OK`, exactly one completion is
delivered to `handler_`. A failed submit does not invoke the handler. Ownership
of callback `parts_` and every message moves to the callback, which releases
them exactly once. On timeout and other terminal outcomes,
`zlink_request_result_t` identifies the result.

## 6. Raw record receive

```c
ZLINK_EXPORT zlink_recv_result_t zlink_dealer_recv_part(
  void *dealer_,
  uint8_t *message_type_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);
```

This returns one part from a complete record. Every output pointer is required.
The C type of `message_type_out_` is `uint8_t`; its value is one of the numbers
defined by `zlink_dealer_message_type_t`. `flags_` is
`ZLINK_RECV_FLAGS_NONE` or `ZLINK_RECV_FLAGS_DONTWAIT`. A non-blocking call with
no available record returns `ZLINK_RECV_NO_DATA` with `EAGAIN`.

When `has_more_out_ == ZLINK_PART_MORE`, the next call receives the next part
of the same record. `ZLINK_PART_FINAL` completes that record's receive
sequence.

## 7. Raw reply submit

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_reply_part(
  void *dealer_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);
```

This sends a reply part for a `ZLINK_DEALER_MESSAGE_REQUEST` record.
`request_seq_` must be the nonzero reply token returned for that request by
`zlink_dealer_recv_part()` on the same socket. A multipart reply uses the same
token for every part. A successful `ZLINK_PART_FINAL` completes the reply for
that token, which cannot then be reused.

## 8. Results and readiness

Submit APIs return `zlink_submit_result_t`, receive APIs return
`zlink_recv_result_t`, and option APIs return `zlink_config_result_t`. The
[errno map](../04-errno-map.en.md) defines the mapping between each result and
`zlink_errno()`.

DEALER `ZLINK_POLLIN` means that a raw or request/reply record can be received.
`ZLINK_POLLOUT` and `zlink_send_ready_handler()` indicate that retrying a
backpressured submit is worthwhile; they do not guarantee that the next submit
will succeed.
