# single PUBSUB blocking publish timeout corrupts sustained `tcp` perf at `io-thread=1`

## Summary

- Scope: `core/perf` single `PUBSUB`, `tcp`, sustained load, especially
  `io-thread=1`.
- Symptom: `64B` may still look acceptable, but `256B+` throughput collapses
  under the standard single blocking-send model.
- This does **not** reproduce as a callback-only issue. A separate `recv`
  benchmark path shows the same collapse.
- Current strongest cause: blocking `zlink_publish()` sends `topic` and
  `payload` as separate frames; if the first frame succeeds and the second
  times out, retrying the publish can resume from an inconsistent multipart
  send state.

## Reproduction

### 1. Direct single `PUBSUB` callback repro

```bash
env \
  PERF_IO_THREADS=1 \
  PERF_RECV_MODE=callback \
  PERF_SINGLE_DURATION_SECONDS=5 \
  PERF_SINGLE_WARMUP_SECONDS=2 \
  PERF_SINGLE_SNDTIMEO_MS=200 \
  PERF_SINGLE_RCVTIMEO_MS=200 \
  core/build/bin/perf_pubsub current tcp 256
```

Observed:

```text
RESULT,current,PUBSUB,tcp,256,throughput,26200.00
RESULT,current,PUBSUB,tcp,256,latency,482.23
RESULT,current,PUBSUB,tcp,256,snd_pending_max,61.00
```

### 2. Separate `recv`-mode comparison

`PUBSUB` was split into a distinct `recv` model to avoid mixing the result with
the callback worker path. The `recv` path still collapsed at `256B+`, which
rules out callback queue/worker overhead as the primary cause.

Representative comparison:

- callback, `tcp`, `io-thread=1`, `64B`: about `775K msg/s`
- recv, `tcp`, `io-thread=1`, `64B`: about `1.23M msg/s`
- callback and recv both drop sharply at `256B+`

### 3. Timeout sensitivity check

```bash
env \
  PERF_IO_THREADS=1 \
  PERF_RECV_MODE=callback \
  PERF_SINGLE_DURATION_SECONDS=5 \
  PERF_SINGLE_WARMUP_SECONDS=2 \
  PERF_SINGLE_SNDTIMEO_MS=1 \
  PERF_SINGLE_RCVTIMEO_MS=200 \
  core/build/bin/perf_pubsub current tcp 256
```

Observed:

```text
RESULT,current,PUBSUB,tcp,256,throughput,867700.00
```

The same workload jumps from `26.2K` to `867.7K` only by shortening
`SNDTIMEO`, which strongly indicates repeated blocking-send timeout stalls.

This also suggests a wake/retry latency problem, not just a raw queue-capacity
problem. In the slow case, the publisher appears to stay asleep close to the
configured timeout instead of promptly resuming when progress becomes possible.

## Findings

### 1. The failure is below the callback surface

`perf_pubsub.cpp` accepts only one payload part and the exact benchmark topic:

- [perf_pubsub.cpp](../../../bindings/c/perf/single/src/perf_pubsub.cpp#L113)
- [perf_pubsub.cpp](../../../bindings/c/perf/single/src/perf_pubsub.cpp#L121)

Even after adding a separate blocking `recv` benchmark path, `256B+` still
collapsed. That means the problem is not the callback worker or queue
accounting.

### 2. `zlink_publish()` is not atomic at the message level

`zlink_publish()` sends the topic frame first, then each payload frame:

- [zlink.cpp](../../../core/src/api/core/zlink.cpp#L6388)
- [zlink.cpp](../../../core/src/api/core/zlink.cpp#L6411)
- [zlink.cpp](../../../core/src/api/core/zlink.cpp#L6427)

So a blocking publish of a one-topic, one-payload message is still two send
calls internally.

### 3. `XPUB` keeps multipart send state after a successful first frame

`xpub_t::xsend()` only recomputes matching pipes when `_more_send` is false,
and sets `_more_send = msg_more` after a successful frame send:

- [xpub.cpp](../../../core/src/runtime/sockets/pubsub/xpub.cpp#L368)
- [xpub.cpp](../../../core/src/runtime/sockets/pubsub/xpub.cpp#L395)

If the topic frame succeeds with `SNDMORE`, `_more_send` becomes true.

### 4. Blocking send retries happen at the per-frame level

`socket_base_t::send()` retries the specific frame until timeout:

- [socket_base.cpp](../../../core/src/runtime/sockets/common/socket_base.cpp#L1448)
- [socket_base.cpp](../../../core/src/runtime/sockets/common/socket_base.cpp#L1454)

If the payload frame times out and returns `EAGAIN`, the application-level
benchmark retries the whole publish from the start. But the socket may already
be in the middle of a multipart send because the topic frame was committed
earlier.

## Suspected Root Cause

The strongest current hypothesis is:

1. `zlink_publish()` sends `topic` successfully with `SNDMORE`
2. `payload` send blocks and times out
3. `XPUB` multipart state remains in-progress (`_more_send == true`)
4. the benchmark retries the same publish from the start
5. subsequent frames can be interpreted as continuation frames instead of a new
   `topic + payload` message boundary
6. subscriber-side decode rejects many messages because the received shape is
   no longer `topic + exactly one payload`

This fits all observed symptoms:

- `callback` and `recv` both fail
- `SNDTIMEO` dominates the performance collapse
- `64B` can survive longer while `256B+` collapses quickly
- `snd_pending_max` stays unexpectedly low because time is spent inside long
  blocking send waits instead of filling the queue continuously

## Additional Hypothesis: slow wake after backpressure release

The current data also supports a second, closely related bug candidate:

1. blocking `PUB` hits the `NODROP`/HWM limit
2. downstream progress later makes space available again
3. the blocked sender does not wake and retry quickly enough
4. most of the benchmark time is consumed by long blocking waits close to
   `SNDTIMEO`

Why this matters:

- if the problem were only "queue stays full", `snd_pending_max` should remain
  near the configured HWM for longer periods
- instead, the bad `256B` case showed `snd_pending_max` around `61`, which is
  far below the single default HWM `1000`
- this means the publisher is not spending most of its time saturating the
  queue; it is spending most of its time blocked and waiting to retry

This does not rule out multipart-state corruption. The two mechanisms may both
be present:

- primary symptom amplifier: sender wakes or retries too late after
  backpressure is relieved
- secondary corruption risk: retrying a multi-frame `publish` after timeout may
  continue from an unsafe multipart state

## Why this looks like a runtime/core bug

- The user reports that older versions did not show this `io-thread=1`
  collapse.
- The collapse remains after separating callback and recv benchmark surfaces.
- The hot path crosses public `zlink_publish()` and `core/src/sockets/xpub.cpp`,
  not just perf-local helper logic.
- The failure mode depends on multipart send state and timeout interaction,
  which is owned by `core`.

## Impact

- `single PUBSUB` cannot currently use the same blocking-send model as the
  other single patterns without severe regression at `io-thread=1`.
- Current single perf numbers for `PUBSUB` under standard blocking settings are
  not trustworthy as a transport-only comparison.

## Expected

- A blocking single `PUBSUB` benchmark should either:
  - complete the whole `topic + payload` publish atomically, or
  - recover cleanly from timeout without leaving the socket in a half-sent
    multipart state visible to the next publish attempt.

## Actual

- Sustained blocking `PUBSUB` publish under timeout pressure can leave the send
  path in an inconsistent multipart state and cause dramatic throughput
  collapse.

## Recommended Next Step

- Add a `core/tests/` regression that reproduces:
  - blocking `PUB`/`SUB`
  - `tcp`
  - `io-thread=1`
  - `topic + payload` multipart publish under sustained load and timeout
- Make the regression assert both:
  - progress resumes promptly after backpressure is relieved
  - a timed-out publish does not corrupt the next `topic + payload` boundary
- Then fix the `core` publish path so blocking publish retries are safe across
  multipart boundaries.
