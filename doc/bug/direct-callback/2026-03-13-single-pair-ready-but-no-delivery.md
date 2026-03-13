# 2026-03-13 single PAIR: monitor ready 이후 실제 송수신 불가

## 요약

- `single PAIR` perf가 `socket monitor` event 기반 readiness gate까지는 정상 통과한다.
- 그런데 readiness 이후 실제 warmup/data delivery가 시작되지 않고 binary가 hang한다.
- perf 쪽에서 추가 sleep/retry/buffering으로 숨길 문제는 아니고, `PAIR` core readiness/delivery 또는 timeout contract bug로 봐야 한다.

## 상태 업데이트 (2026-03-13)

- 이 이슈는 `core` 회귀 테스트로 고정했고, 이제 `perf_pair` 흐름을 닮은
  회귀까지 통과하는 상태로 수정했다.
- acceptance는 perf binary 재실행이 아니라 `core/tests` 회귀 테스트다.
- 추가된 회귀:
  - `core/tests/integration/monitoring/test_monitor_enhanced.cpp`
  - `test_pair_monitor_ready_implies_first_bidirectional_delivery`
  - `test_pair_monitor_snapshot_reopen_after_close_preserves_delivery`
- 추가된 perf-like 회귀:
  - `core/tests/integration/monitoring/test_monitor_perf_contract.cpp`
  - `test_pair_perf_like_send_sampling_preserves_oneway_delivery`
  - `test_pair_perf_like_recv_sampling_preserves_oneway_delivery`
  - `test_pair_perf_like_bidirectional_sampling_preserves_oneway_delivery`
- 첫 번째 테스트는 최소 `PAIR` raw contract인
  `CONNECTION_READY -> 첫 양방향 송수신`을 고정한다.
- 두 번째 테스트는 perf queue/snapshot sampling과 같은 형태로
  `temporary raw monitor open -> snapshot -> close -> reopen -> snapshot -> first delivery`
  경로를 직접 검증한다.
- perf-like 회귀는 `connect monitor gate -> monitor close -> active send/recv 중
  temporary monitor open/snapshot/close 반복 -> phase-end force sample` 경로를
  직접 검증한다.
- root cause는 raw monitor handle을 닫을 때 source socket의 monitor teardown이
  같이 정리되지 않아, 이후 temporary monitor reopen에서 이전 monitor state가
  남은 채 block될 수 있었던 점이다.
- 수정 위치:
  - `core/src/api/zlink.cpp`
  - `core/src/sockets/socket_base.cpp`
- 따라서 이 리포트는 닫는다.

## 증상

- direct 실행:

```bash
PERF_SINGLE_DURATION_SECONDS=2 \
PERF_SINGLE_SNDTIMEO_MS=200 \
PERF_SINGLE_RCVTIMEO_MS=200 \
./core/build/bin/perf_pair current tcp 1024
```

- 위 명령은 45초 외부 timeout으로 종료될 때까지 반환하지 않았다.
- wrapper 실행:

```bash
./core/perf/run_benchmarks.sh \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 1024 \
  --runs 1 \
  --duration 2
```

- wrapper는 `PAIR current tcp 1024B: timeout`으로 `partial` 종료한다.
- 최신 산출물:
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/perf/results/single/report/perf_linux_20260313_153216.txt`

## 재검증 결과

- 이 문서의 acceptance는 현재 `core/tests` 회귀다.
- 과거 direct perf timeout 관측은 이슈 최초 발견 근거로만 유지한다.
- 현재는 아래 회귀가 모두 통과하면 이 리포트를 닫는다.

```bash
./build-codex/bin/test_monitor_perf_contract
ctest --test-dir build-codex --output-on-failure \
  -R '^(test_monitor_perf_contract|test_monitor_enhanced|test_monitor_with_handler|test_monitor_stream_contract|test_monitor_service_contract)$'
```

## 재현 코드 경로

- core regression:
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/tests/integration/monitoring/test_monitor_enhanced.cpp`
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/tests/integration/monitoring/test_monitor_perf_contract.cpp`
- core fix:
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/api/zlink.cpp`
  - `/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp`

## 기대 동작

- `PAIR`에서 `CONNECTION_READY` 이후 첫 양방향 송수신은 즉시 가능해야 한다.
- temporary monitor를 열어 snapshot을 읽고 닫은 뒤 다시 열어도,
  source socket은 monitor teardown 때문에 block되면 안 된다.

## 현재 결론

- raw `PAIR` monitor contract는 회귀 테스트로 고정됐다.
- temporary monitor snapshot/reopen 경로도 회귀 테스트로 고정됐다.
- `perf_pair`와 동일한 monitor gate/close/sampling pattern도 회귀 테스트로 고정됐다.
- 현재 이 리포트의 core 원인은 수정 완료 상태다.
