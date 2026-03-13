# Single SPOT `DELIVERY_READY_CHANGED` flake

## Summary

`SPOT` single perf는 기존 `SUBSCRIPTION_READY` 대신
`ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED(value=1)`만 기다리도록 바꾼 뒤에도
동일한 env에서 fail/success가 섞여 나온다.

즉 현재 `DELIVERY_READY_CHANGED`도 perf가 기대하는
"이 이벤트를 본 뒤 바로 첫 publish 가능" 계약을 아직 안정적으로 보장하지 못한다.

## Status Update (2026-03-13)

- 최소 service contract 회귀 테스트는 추가했고 통과한다.
  - `core/tests/integration/monitoring/test_monitor_service_contract.cpp`
  - `test_spot_delivery_ready_changed_implies_first_publish_delivery`
- 즉 single pub/sub 최소 경로에서는
  `*_DELIVERY_READY_CHANGED -> 첫 publish/receive` contract가 성립한다.
- 그러나 direct perf tcp 재실행에서는 flake가 여전히 남아 있다.

```bash
for i in 1 2 3 4 5; do
  timeout 30s env PERF_SINGLE_DURATION_SECONDS=2 \
    PERF_SINGLE_SNDTIMEO_MS=200 PERF_SINGLE_RCVTIMEO_MS=200 \
    ./core/build/bin/perf_spot current tcp 1024
done
```

- 관측 결과: `exit=0, 0, 0, 0, 1`
- 따라서 이 리포트는 아직 open 상태다.
- 현재 범위는 "minimal single spot contract 전체가 깨진다"보다,
  `perf_spot` startup/phase 경로에서만 드러나는 race 또는 timing gap일 가능성이
  더 크다.

## Current perf gate

현재 [perf_spot.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/single/src/perf_spot.cpp)
는 아래만 사용한다.

1. sub monitor open
   - `ZLINK_SPOT_SUB_FILTER_APPLIED`
   - `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED`
2. bind / connect / subscribe
3. `FILTER_APPLIED(topic=bench)`
4. `DELIVERY_READY_CHANGED(endpoint=<bound endpoint>, topic=bench, value>=1)`
5. 바로 warmup publish 시작

추가 settle sleep, peer polling, startup probe는 없다.

## Reproduction

직접 실행:

```bash
PERF_SINGLE_DURATION_SECONDS=2 \
PERF_SINGLE_SNDTIMEO_MS=200 \
PERF_SINGLE_RCVTIMEO_MS=200 \
./core/build/bin/perf_spot current tcp 1024
```

동일 env에서 연속 실행했을 때 관측:

실패:

```text
RESULT,current,SPOT,tcp,1024,throughput,0.00
RESULT,current,SPOT,tcp,1024,bandwidth,0.00
RESULT,current,SPOT,tcp,1024,latency,0.00
RESULT,current,SPOT,tcp,1024,latency_p95,0.00
RESULT,current,SPOT,tcp,1024,latency_p99,0.00
RESULT,current,SPOT,tcp,1024,snd_pending_max,7.00
RESULT,current,SPOT,tcp,1024,rcv_pending_max,0.00
RESULT,current,SPOT,tcp,1024,rcv_pending_end,0.00
exit=1
```

성공:

```text
RESULT,current,SPOT,tcp,1024,throughput,21667.00
RESULT,current,SPOT,tcp,1024,bandwidth,22.19
RESULT,current,SPOT,tcp,1024,latency,92.38
RESULT,current,SPOT,tcp,1024,latency_p95,168.00
RESULT,current,SPOT,tcp,1024,latency_p99,277.00
RESULT,current,SPOT,tcp,1024,snd_pending_max,598.00
RESULT,current,SPOT,tcp,1024,rcv_pending_max,53.00
RESULT,current,SPOT,tcp,1024,rcv_pending_end,53.00
exit=0
```

wrapper 재현도 동일하다.

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 1024 \
  --runs 1 \
  --duration 2
```

- 실패 예: [perf_linux_20260313_101525.txt](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/results/single/report/perf_linux_20260313_101525.txt)
- 성공 예: [perf_linux_20260313_101646.txt](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/results/single/report/perf_linux_20260313_101646.txt)

## Suspect area

[spot_sub.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp)
에서 subject ready를 기록하는 즉시 아래를 같이 내보낸다.

- `ZLINK_SPOT_SUB_SUBSCRIPTION_READY`
- `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED(value=1)`

관련 지점:

- [spot_sub.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L560)
- [spot_sub.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp#L482)

현재 관측상 `ready_subject_keys` 삽입 시점이 실제 first-delivery 가능 시점보다
앞설 가능성이 있다.

## Expected

`DELIVERY_READY_CHANGED(value>=1)`를 받은 뒤 첫 `zlink_spot_publish()`는
transport/timing에 상관없이 안정적으로 subscriber에 도달해야 한다.

## Actual

같은 env에서도 첫 delivery가 0-receive로 끝나는 경우가 남아 있다.
즉 이벤트가 여전히 너무 이르거나, ready 상태가 monotonic하지 않다.

## Requested core action

아래 중 하나가 필요하다.

1. `ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED(value>=1)` 의미를 더 엄격하게 만들어
   실제 first-delivery 보장 시점에만 emit
2. ready state가 transient하게 1로 올라갔다가 아직 delivery 불가한 경우가 있는지
   ordering/forwarding 경로 점검
3. 현재 이벤트를 유지해야 한다면, perf가 기다릴 수 있는 더 강한 single-sub
   delivery-ready event 추가

## Status

- perf side workaround: 하지 않음
- retry/sleep/polling: 추가하지 않음
- next action needed: core readiness contract 재점검
