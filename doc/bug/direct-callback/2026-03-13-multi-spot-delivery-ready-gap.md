# Multi SPOT delivery-ready event gap

## Summary

`core/perf`를 monitor-event only 정책으로 맞춘 뒤, `MULTI_SPOT`는 `tcp`에서도
startup delivery가 보장되지 않는다.

- client는 sub monitor event만으로 준비를 판정한다.
- server는 pub monitor event만으로 준비를 판정한다.
- 그 뒤에도 warmup 첫 메시지가 오지 않아 benchmark가 바로 실패한다.

이건 perf가 sleep/polling/handshake로 보정할 문제가 아니라, core monitor
contract가 server-visible delivery-ready를 충분히 표현하지 못하는 문제다.

기존 single `SPOT` wss gap 문서와는 별개다.

## Status Update (2026-03-13)

- single pub/sub 최소 contract 회귀 테스트는 추가했고 통과한다.
  - `core/tests/integration/monitoring/test_monitor_service_contract.cpp`
  - `test_spot_delivery_ready_changed_implies_first_publish_delivery`
- 즉 문제는 single minimal delivery-ready contract 전체보다는
  multi perf server/client phase 전환에 더 가깝다.
- direct multi repro는 여전히 재현된다.

Server:

```bash
env PERF_DEBUG=1 PERF_MSG_SIZES=1024 PERF_CLIENTS=16 \
  ./core/build/bin/comp_src_spot_server current tcp
```

Client:

```bash
timeout 45s env PERF_DEBUG=1 PERF_MSG_SIZES=1024 PERF_CLIENTS=16 \
  ./core/build/bin/comp_src_spot_client current tcp 1024 \
    --endpoint tcp://127.0.0.1:37800
```

- 관측 결과:
  - server: `READY,tcp://127.0.0.1:37800`
  - client: `[multi-spot-client] warmup start timeout size=1024`
  - client 종료: `exit=124`
- 따라서 이 리포트도 아직 open 상태다.

파일:
- `doc/bug/direct-callback/2026-03-13-spot-subscription-ready-wss-delivery-gap.md`

## Current perf policy

현재 perf는 아래 제약을 지킨다.

- peer count polling 없음
- startup settle sleep 없음
- synthetic handshake 없음
- readiness는 monitor callback + condition_variable 만 사용

적용된 현재 gate는 다음과 같다.

- client/sub: `ZLINK_SPOT_SUB_FILTER_APPLIED` + `ZLINK_SPOT_SUB_SUBSCRIPTION_READY`
- server/pub: `ZLINK_MONITOR_EVENT_PEER_UP`

즉, perf 쪽 완충이나 재시도 없이 core event 계약만으로 시작한다.

## Reproduction

### Wrapper smoke

```bash
PERF_DEBUG=1 ./core/perf/run_benchmarks_multi.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 1024 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 16
```

Observed:

- result file: `core/perf/results/multi/report/perf_linux_20260313_090516.txt`
- failure: `SPOT current tcp 1024B: non_zero_exit_1_size_1024`

### Direct server/client

Server:

```bash
PERF_DEBUG=1 PERF_MSG_SIZES=1024 PERF_CLIENTS=16 \
./core/build/bin/comp_src_spot_server current tcp
```

Observed:

```text
READY,tcp://127.0.0.1:35736
```

Client:

```bash
PERF_DEBUG=1 PERF_MSG_SIZES=1024 PERF_CLIENTS=16 \
./core/build/bin/comp_src_spot_client current tcp 1024 \
  --endpoint tcp://127.0.0.1:35736
```

Observed:

```text
[multi-spot-client] warmup start timeout size=1024
```

## Expected

아래가 모두 성립하면 첫 warmup publish는 수신 가능해야 한다.

- sub monitor에서 `FILTER_APPLIED`
- sub monitor에서 `SUBSCRIPTION_READY`
- pub monitor에서 `PEER_UP`

즉 perf는 이 시점 이후 별도 sleep/poll/handshake 없이 바로 phase를 시작할 수
있어야 한다.

## Actual

위 event gate를 모두 기반으로 시작해도 client가 warmup 첫 메시지를 받지 못한다.
server는 이미 phase를 진행할 수 있으므로, startup window 전체를 놓치면 client는
아무 phase도 시작하지 못하고 timeout으로 끝난다.

핵심은 `PEER_UP`이 "publisher가 이 subscriber에게 즉시 delivery 가능"을
보장하지 않는다는 점이다. client-side `SUBSCRIPTION_READY`만으로도 wrapper
전체를 안전하게 시작시킬 수 없다. server는 그 상태를 볼 수 있는 event가 없다.

## Why perf cannot fix this

perf에서 가능한 우회는 모두 정책 위반이다.

- fixed sleep 추가: 금지
- peer count polling 복구: 금지
- probe message / handshake barrier 추가: 금지
- 첫 메시지 유실을 perf queue/retry로 가리기: 금지

따라서 core event contract를 고치거나, 필요한 event를 추가해야 한다.

## Requested core action

둘 중 하나가 필요하다.

1. 기존 event semantics 강화

- `ZLINK_SPOT_SUB_SUBSCRIPTION_READY`가 발생하면 publisher 관점에서도 즉시
  delivery 가능한 상태여야 한다.
- 이 경우 `ZLINK_MONITOR_EVENT_PEER_UP`는 그보다 앞서 발행되면 안 된다.

2. 새 pub-visible ready event 추가

예시:

- `ZLINK_SPOT_PUB_DELIVERY_READY`
- `ZLINK_SPOT_PUB_DELIVERY_READY_COUNT_CHANGED`

필요 의미:

- publisher가 특정 subscriber 또는 N개 subscriber에 대해 실제 publish delivery를
  시작해도 되는 시점
- multi perf server가 sleep/poll 없이 phase 시작 여부를 결정할 수 있는 정보

## Affected files

- `core/perf/multi/src/perf_multi_spot_client.cpp`
- `core/perf/multi/src/perf_multi_spot_server.cpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/common/service_monitor.cpp`

## Status

- perf side workaround: 하지 않음
- event-only policy: 유지
- next action needed: core monitor contract fix or new event design
