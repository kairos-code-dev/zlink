# single `PAIR --recv recv` times out under event-only handshake policy

## Summary

현재 single perf에서 `PAIR --recv recv` 는 `tcp` 64B smoke 기준 30초 timeout으로 끝난다.

이 문제는 runner option 오류가 아니라 실제 benchmark 진행이 시작되지 못하는 케이스다.
같은 시점에 아래 경로들은 정상 완료를 확인했다.

- multi `PUBSUB --recv recv`
- multi `PUBSUB --recv callback`
- multi `DEALER_DEALER --recv callback`

즉 전체 perf runner가 멈춘 것이 아니라, single `PAIR` receive path 또는
its start/stop progression contract가 깨진 상태다.

## Why this is a bug

monitoring/perf policy 기준으로 raw socket start gate는 `CONNECTION_READY` 만 사용해야 한다.

`PAIR` 는 가장 기본적인 raw socket 패턴이므로,
event-only gate로 전환한 뒤에도 아래 smoke가 정상 종료해야 한다.

현재는 결과 line 하나도 출력하지 못하고 timeout 되므로
perf bug 또는 underlying raw socket readiness/traffic regression으로 추적해야 한다.

## Reproduction

```bash
timeout 45s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks.sh \
  --pattern PAIR \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1
```

Actual result:

- table row: `FAIL`
- failure: `PAIR current tcp 64B: timeout`
- saved report:
  `core/perf/results/single/report/perf_linux_recv_20260320_084404.txt`

Observed summary:

```text
## Failures
- PAIR current tcp 64B: timeout

## Completion
- status: partial
- expected_result_lines: 5
- actual_result_lines: 0
```

## Expected result

- single `PAIR --recv recv` 가 `CONNECTION_READY` 기반으로 정상적으로 active window에 진입해야 한다.
- throughput / latency result lines 를 출력하고 종료해야 한다.
- perf 쪽 sleep/snapshot fallback 없이도 통과해야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- runner timeout만 늘려서 hanging case를 숨김
- single `PAIR recv`만 예외 처리해서 readiness gate를 건너뜀
- sleep이나 snapshot polling을 다시 넣어 timing dependence를 덮음

## Suspected fix areas

- [`core/perf/single/common/bench_common.hpp`](/home/hep7/project/kairos/zlink/core/perf/single/common/bench_common.hpp)
  의 `setup_connected_pair()` 이후 phase progression
- [`core/perf/single/src/perf_pair.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pair.cpp)
  의 recv-mode loop / shutdown path
- `PAIR` `CONNECTION_READY` 이후 첫 traffic kick / stop signaling contract

## Current repo decision

- 이 문제는 perf workaround로 닫지 않는다.
- single raw `PAIR recv` regression bug로 추적한다.
