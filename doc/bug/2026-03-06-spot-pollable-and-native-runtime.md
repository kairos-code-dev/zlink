# C++ Perf Bug Report

Date: 2026-03-06
Area: `bindings/cpp/perf`, `core spot`, `bindings/cpp/native`

## 1. SPOT pollable SUB recv crash

### Summary

`SPOT`를 pollable mode로 사용하면 `spot_node_t::sub_socket_handle()`에서 받은 raw SUB 소켓의 recv 경로에서 core가 죽습니다.

- expected: `poller.wait + recv(dontwait)`로 정상 수신/종료
- actual: `SIGFPE` 또는 abort

### Why this matters

현재 정책상 `SPOT` perf는 facade `spot_t`가 아니라 pollable transport mode로 구현해야 합니다.

- use:
  - `spot_node_t::pub_socket_handle()`
  - `spot_node_t::sub_socket_handle()`
- do not mix:
  - `spot_t::publish()`
  - `spot_t::recv()`
  - `spot_t::subscribe()`

즉 이 경로는 perf에서 우회할 수 없는 정식 사용 경로입니다.

### Repro

Server:

```bash
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=1 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=100 \
LD_PRELOAD=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64/libzlink.so \
LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64 \
/tmp/perf_spot_server_cpp tcp 64
```

Client:

```bash
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=1 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=100 \
LD_PRELOAD=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64/libzlink.so \
LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64 \
/tmp/perf_spot_client_cpp tcp 64 --endpoint <READY endpoint>
```

### Observed result

- server: 정상 종료
- client: abort/core dump

Valgrind stack:

```text
Process terminating with default action of signal 8 (SIGFPE)
Integer divide by zero
at zlink::fq_t::recvpipe(zlink::msg_t*, zlink::pipe_t**)
by zlink::xsub_t::xrecv(zlink::msg_t*)
by zlink::socket_base_t::recv(zlink::msg_t*, int)
by zlink_msg_recv
by perf_spot_client.cpp run_recv_phase(...)
```

### Notes

- perf client/server는 facade `spot_t`를 제거하고 raw `PUB/SUB` socket만 사용한 상태에서 재현됨
- topic/payload multipart misalignment는 수정 후 재현
- 작은 raw smoke는 성공함:
  - single message raw pollable send/recv: success
  - 1000 message raw pollable loop: success
- 즉 `SPOT pollable mode` 자체가 완전히 불가능한 것은 아니고, perf 수준 흐름에서 core 내부 `xsub/fq` 경로가 깨지는 것으로 보임

### Suspected area

- `core/src/services/spot/spot_node.cpp`
- `core/src/sockets/xsub.cpp`
- `zlink::fq_t::recvpipe()`

### Expected

- raw pollable SUB recv 경로에서 `SIGFPE` 없이 정상 종료
- facade와 혼용하지 않아도 crash가 나지 않아야 함

## 2. bindings/cpp/native runtime packaging mismatch

### Summary

`bindings/cpp/native/linux-x86_64`의 `libzlink.so`와 `libzlink.so.5`가 서로 다른 심볼 집합을 가집니다.

- link time: `libzlink.so` 사용
- runtime: `libzlink.so.5` 로드
- 결과: poller 심볼 누락으로 실행 시작 단계에서 실패

### Repro

```bash
LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/cpp/native/linux-x86_64 \
/tmp/perf_spot_server_cpp tcp 64
```

### Observed result

```text
symbol lookup error: undefined symbol: zlink_poller_destroy
```

### Evidence

Present in `libzlink.so`:

```bash
nm -D bindings/cpp/native/linux-x86_64/libzlink.so | rg zlink_poller_destroy
```

Missing in `libzlink.so.5`:

```bash
nm -D bindings/cpp/native/linux-x86_64/libzlink.so.5 | rg zlink_poller_destroy
```

Binary needs `libzlink.so.5`:

```bash
ldd /tmp/perf_spot_server_cpp
```

### Expected

`libzlink.so` and `libzlink.so.5` should be ABI-compatible outputs of the same build.

### Actual

runtime `.so.5` is missing symbols that exist in `.so`

## Conclusion

Current blockers for C++ `SPOT` perf:

1. core bug: pollable SUB recv path can crash in `zlink::fq_t::recvpipe()`
2. packaging bug: `bindings/cpp/native/linux-x86_64/libzlink.so.5` is inconsistent with `libzlink.so`

## Re-review on current tree

Date: 2026-03-06

### Reassessment

- Item 2 (`bindings/cpp/native` packaging mismatch) is still a real bug.
- Item 1 should not be treated as confirmed on the current tree without a
  tighter repro.

### What was re-tested

Using the current workspace after the `spot_node_t::process_sub()` change
that stops internal `_sub` draining in pollable mode:

- core targeted test:
  - `ctest --test-dir build_mode --output-on-failure -R 'test_spot_mode_split$'`
  - result: pass
- minimal C API repro:
  - `spot_sub_subscribe()`
  - `zlink_spot_node_sub_socket()`
  - raw `zlink_msg_recv()` topic/payload
  - destroy `pub/sub/node/ctx`
  - result: pass, clean exit
- minimal C# repro with the .NET binding against the same local core:
  - `Spot.Subscribe("bench")`
  - `SpotNode.GetSubSocket()`
  - raw `Socket.Receive()` topic/payload
  - dispose `Spot/SpotNode/Context`
  - result: pass, clean exit

### Interpretation

The broad statement "pollable SUB recv path is a core bug" is not supported
by the current tree anymore.

What is supported by current evidence:

- there was a real core conflict between `SpotNode` internal `_sub` draining
  and application-owned pollable SUB recv; this was addressed by skipping
  `process_sub()` when `_sub_pollable_mode != 0`
- packaging mismatch in `bindings/cpp/native` is independently valid
- if `bindings/cpp/perf` still aborts after the above core fix, the remaining
  cause is likely one of:
  - a stale runtime binary (`libzlink.so` vs `libzlink.so.5`)
  - a perf-only lifetime/state bug
  - a narrower core bug triggered only by the exact perf flow, not by pollable
    SUB recv in general

### Updated recommendation

- keep Item 2 as a confirmed bug
- downgrade Item 1 from "confirmed core bug" to "needs updated repro on current
  tree"
- if C++ perf still crashes, capture a repro against:
  - current core build artifacts
  - current `bindings/cpp/perf` sources
  - matched runtime library files
  and only then reopen Item 1 as a current core bug
