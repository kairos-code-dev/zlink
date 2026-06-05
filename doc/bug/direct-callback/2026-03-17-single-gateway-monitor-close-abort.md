# 2026-03-17 single GATEWAY monitor close abort

## Summary

- `single/GATEWAY`는 현재 일부 transport/msg-size 조합에서 `FAIL`이 아니라
  프로세스 abort로 종료된다.
- abort 지점은
  [`signaler.cpp`](../../../core/src/runtime/core/signaler.cpp#L279)
  의 `signaler_t::recv()` `errno_assert(sz == sizeof(dummy))` 이다.
- 이건 benchmark가 잘못된 결과를 출력하는 수준이 아니라,
  `core` 라이브러리 lifecycle/monitor close 경로가 process abort를 일으키는
  버그다.
- perf 쪽 우회로 숨기지 않고 `core` bug로 처리해야 한다.

## Affected Scope

- pattern:
  - `single/GATEWAY`
- observed transport/msg-size:
  - `tcp/64B`
  - `tcp/256B`
- 최신 실패 리포트:
  - `/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260317_130702.txt`

## Visible Symptom

대표 재현:

```bash
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
./core/build/bin/perf_gateway current tcp 64
```

또는:

```bash
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
./core/build/bin/perf_gateway current tcp 256
```

실제 결과:

```text
Resource temporarily unavailable (/home/hep7/project/kairos/zlink/core/src/core/signaler.cpp:279)
Aborted (core dumped)
```

즉 benchmark result line이 나오기 전에 process가 죽는다.

## Why This Is A Core Bug

- `single/GATEWAY`가 service monitor / send-ready / close lifecycle을 다루는
  방식이 fragile할 수는 있다.
- 하지만 잘못된 사용이 들어오더라도 library는:
  - 명시적 error return
  - `EINVAL`, `EBUSY`, `EFAULT` 같은 contract failure
  로 끝나야 한다.
- 현재는 `signaler_t::recv()` 내부 assert abort가 발생한다.
- 따라서 분류는
  - benchmark bug only
  가 아니라
  - `core` lifecycle bug
  가 맞다.

## Comparison With direct-callback-rewrite

- `/home/hep7/project/kairos/zlink-direct-callback-rewrite`의 동일 benchmark는
  아래 케이스에서 정상 종료한다.

```bash
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build/bin/perf_gateway current tcp 64

PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build/bin/perf_gateway current tcp 256
```

- 두 케이스 모두 `RESULT,...` line을 출력하고 `EXIT:0`으로 끝난다.
- 따라서 현재 트리의 abort는 환경 문제보다는
  - current `core`
  - current service monitor / gateway lifecycle contract
  쪽 회귀로 봐야 한다.

## Current Hypothesis

- `single/GATEWAY`는 ready gate를 위해 service monitor를 열고 snapshot/close를
  거친다.
- 현재 트리에서는 이 monitor close 또는 gateway destroy와 monitor/socket signaler
  정리 순서가 꼬이면서,
  최종적으로 `signaler_t::recv()`가 `EAGAIN` 상태에서 assert abort를 밟는
  것으로 보인다.
- 즉 문제 축은 data plane보다:
  - service monitor close
  - gateway destroy
  - monitor callback/worker shutdown
  - signaler drain
  lifecycle 경계에 있다.

## Reproduction

### Direct repro

```bash
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
./core/build/bin/perf_gateway current tcp 64
```

```bash
PERF_SINGLE_DURATION_SECONDS=1 \
PERF_SINGLE_WARMUP_SECONDS=1 \
./core/build/bin/perf_gateway current tcp 256
```

### Expected

- benchmark가 정상 종료해야 한다.
- 실패하더라도 result line 또는 명시적 error code로 끝나야 한다.
- process abort는 없어야 한다.

### Actual

- `signaler.cpp:279` assert abort

## Non-Fixes

- perf harness에서 monitor close를 생략하는 식으로 숨기지 않는다.
- ready gate를 임시 sleep/retry로 바꾸지 않는다.
- `single/GATEWAY`만 예외적으로 계약을 약화하지 않는다.

## Required Fix Direction

- `core`에서 service monitor/gateway close lifecycle을 재검토해야 한다.
- 특히 아래 경계를 확인해야 한다.
  - `zlink_gateway_monitor_open()`
  - `zlink_service_monitor_close()`
  - `zlink_gateway_destroy()`
  - monitor callback worker / signaler shutdown ordering
- 수용 기준:
  - 위 direct repro 2개가 abort 없이 종료
  - 필요하면 실패는 명시적 errno/return code로 surface
  - `single` full run에서 `GATEWAY` abort 제거

## Status

- 열림
- perf 우회 적용하지 않음
- 다음 단계는 `core` close/lifecycle 최소 재현 테스트 추가 후 root cause 확정
