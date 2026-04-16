# Rust binding poller FFI signature mismatch corrupts `zlink_poller_wait`

## Summary

Rust binding의 poller FFI 선언이 `core/include/zlink.h` 와 맞지 않아
`zlink_poller_wait()` / `zlink_poller_wait_all()` 호출 시 마지막
`error_out` 인자가 빠진 상태로 native 함수를 호출하고 있었다.

이 상태에서는 Rust에서 poller를 실제로 사용하는 순간 native stack frame이
깨질 수 있고, perf single raw 패턴에서는 `zlink_poller_wait()` 내부
세그폴트로 바로 드러났다.

문제 범위는 perf 전용이 아니라 Rust binding 전체 poller surface다.

## Reproduction

작업 디렉터리:

```bash
cd /home/hep7/project/kairos/zlink
```

### 1. Rust single perf 재현

```bash
./bindings/rust/perf/run_benchmarks.sh \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1
```

수정 전 관찰:

- runner summary: `binary_exit`
- `gdb` backtrace:
  - `zlink_poller_wait`
  - `zlink::poller::Poller::wait`

### 2. 직접 `gdb` 재현

```bash
env LD_LIBRARY_PATH=/home/hep7/project/kairos/zlink/bindings/rust/native/linux-x86_64 \
gdb -batch -ex 'set debuginfod enabled off' -ex run -ex bt --args \
  bindings/rust/perf/single/target/release/perf_pair \
  --pattern PAIR --transport tcp --msg-size 64 --duration 1
```

수정 전 관찰:

```text
Thread ... received signal SIGSEGV, Segmentation fault.
0x... in zlink_poller_wait() from .../libzlink.so.5
```

## Why this is a binding bug

- core public header의 함수 시그니처는 아래와 같다.

```c
int zlink_poller_wait(void *poller_,
                      zlink_poller_event_t *event_,
                      long timeout_,
                      zlink_config_result_t *error_out_);
```

- Rust FFI 선언은 마지막 `error_out` 인자를 누락하고 있었다.
- 즉 public API 계약을 binding이 잘못 선언한 ABI mismatch 문제다.

## Fix

- `bindings/rust/src/ffi.rs`
  - `zlink_poll`
  - `zlink_poller_wait`
  - `zlink_poller_wait_all`
  에 `error_out` 포인터 인자를 추가했다.
- `bindings/rust/src/poller.rs`
  - 해당 호출부에서 `NULL` error_out을 명시적으로 넘기도록 수정했다.
- 회귀 테스트 추가:
  - `bindings/rust/tests/service_surface_tests.rs`
  - `typed_poller_wait_on_pair_socket_does_not_crash`

## Expected result

- Rust binding poller wait 계열이 세그폴트 없이 정상적으로 event를 반환해야 한다.
- Rust perf는 poller 기반 recv 모델을 다시 사용할 수 있어야 한다.

## Processing result

- 2026-04-16 수정.
- 회귀 테스트 추가 후 Rust poller wait 경로가 더 이상 ABI mismatch로 무너지지 않는다.
