# multi `PUBSUB` perf still regresses when warmup is non-zero

## Summary

Related classification:

- [`doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md`](/home/hep7/project/kairos/zlink/doc/bug/perf/2026-03-20-socket-matrix-failures-classification.md)

`PUBSUB`는 `warmup=0` smoke에서는 통과하지만,
default-like `warmup=1` 경로에서는 아직 깨진다.

현재 active failure는 callback 쪽으로 좁혀졌다.

- `--recv recv --warmup 1`: fixed
- `--recv callback --warmup 1`: still flaky

즉 이전 fix가 `warmup=0` 경로는 닫았지만,
non-zero warmup / phase sequencing 전체를 아직 완전히 닫지 못한 상태다.

## Reproduction

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER,PUBSUB,GATEWAY,SPOT,STREAM \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Observed:

- `PUBSUB current tcp 64B: non_zero_exit_1_CLIENT_READY,64`
- `PUBSUB current tcp 64B: size case failed size=64`
- repeated callback runs can also surface `malloc(): corrupted top size`

### Direct process repro

Runner를 빼도 같은 실패가 난다.

1. server 실행
2. `READY,<endpoint>` 대기
3. client 실행
4. `CLIENT_READY,64` 대기
5. server stdin 으로 `START,64`
6. client가 `size case failed size=64` 로 종료
7. server stdin 으로 `STOP`
8. server는 `0`으로 정상 종료

Observed on `2026-03-20` before the recv-side fix:

- `CLIENT_RC=1`
- `SERVER_RC=0`
- client stderr:
  - `[multi-pubsub-client] size case failed size=64`
- server stderr:
  - `delivery ready peers=1`
  - `publish ok phase=1 size=64 seq=1..8`

In the same batch these patterns passed:

- `STREAM`
- `DEALER_DEALER`
- `GATEWAY`
- `SPOT`

## Why this is a bug

`PUBSUB`를 `CLIENT_READY -> START,<size>` gate로 고친 뒤에도,
non-zero warmup에서는 recv/callback 모두 다시 깨진다.

그리고 direct process repro에서도 동일하게 깨지므로,
남은 이슈는 runner shell/orchestration이 아니라
`perf_multi_pubsub_server/client`의 phase progression 자체에 있다.

## Expected result

- `PUBSUB --recv recv` 와 `PUBSUB --recv callback` 이 모두
  `warmup=1` 에서 안정적으로 통과해야 한다.
- `CLIENT_READY` / `START,<size>` 이후 warmup, drain, active phase가 일관되게 진행돼야 한다.
- non-zero exit 없이 result line을 출력해야 한다.

## Suspected fix areas

- [`core/perf/multi/src/perf_multi_pubsub_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_server.cpp)
- [`core/perf/multi/src/perf_multi_pubsub_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_client.cpp)
- callback path phase progression under one-way publish pressure
- callback attach/dispatch readiness and active-phase start race
- callback shutdown / message ownership if heap corruption is related
- runner가 아니라 binary-level protocol로도 같은 실패가 나는지

## Current repo decision

- 이 문제는 smoke 파라미터를 `warmup=0` 으로 제한해서 닫지 않는다.
- runner retry, extra sleep, result fallback으로 닫지 않는다.
- multi `PUBSUB` perf phase progression bug로 추적한다.

## 2026-03-20 Investigation Update

이번 턴에서는 `core` direct callback path를 먼저 검증했다.

Added direct regression:

- [`core/tests/integration/test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  - `test_pubsub_callback_remains_active_across_warmup_and_active_phases`

Verification:

```bash
ctest --test-dir core/build --output-on-failure \
  -R '^test_multi_socket_contract_regressions$' -j1
```

Observed:

- pass

이 테스트는 `PUB` + two `SUB` callback path에서 delivery-ready 이후 warmup (`W`),
drain (`D`), active (`A`) phase payload가 모두 callback으로 정상 도착하는지 직접
검증한다. 즉 `core` public contract 기준 warmup 이후 callback dispatch 자체는
정상이다.

Perf re-run on `2026-03-20`:

```bash
timeout 180s env PYTHONDONTWRITEBYTECODE=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern DEALER_DEALER,PUBSUB,GATEWAY,SPOT,STREAM \
  --recv callback \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Observed now:

- `STREAM`: pass
- `DEALER_DEALER`: pass
- `GATEWAY`: pass
- `SPOT`: pass
- `PUBSUB`: still `non_zero_exit_1_CLIENT_READY,64`

Conclusion:

- 남은 문제는 이번 턴 기준 `core` library bug로 재현되지 않았다.
- 현재 남은 failure는 `core/perf/multi/src/perf_multi_pubsub_server.cpp` /
  `core/perf/multi/src/perf_multi_pubsub_client.cpp` 쪽 process/phase sequencing issue로 분류한다.
- repository rule에 따라 이번 턴에서는 perf 코드는 수정하지 않았다.

## 2026-03-20 Direct Reproduction Update

이번 턴에는 runner를 제거하고 server/client binary만으로 다시 재현했다.

Verification:

```bash
python3 - <<'PY'
# 1. comp_src_pubsub_server current tcp 실행
# 2. READY,<endpoint> 대기
# 3. comp_src_pubsub_client current tcp 64 --endpoint <endpoint> 실행
# 4. CLIENT_READY,64 대기
# 5. server stdin 으로 START,64
# 6. client 종료 관찰
# 7. server stdin 으로 STOP
PY
```

Observed:

- `CLIENT_READY,64` 까지는 정상
- client는 여전히 `size case failed size=64` 로 종료
- server는 `STOP`을 받으면 `0`으로 정상 종료
- `PERF_SETTLE_MS=5000` 으로 늘려도 실패
- `PERF_CONNECT_READY_TIMEOUT_MS=60000` 으로 늘려도 실패

Current assessment:

- direct process repro가 있으므로 이 이슈는 runner-only bug가 아니다.
- `zlink_subscribe()` public contract bug로도 현재는 확정되지 않았다.
- 남은 문제는 multi perf `PUBSUB` callback path의 phase progression /
  active-phase start race로 본다.

## 2026-03-20 Recv Fix Update

`recv` path는 이번 턴에서 fix 되었다.

Applied change:

- [`perf_multi_pubsub_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_server.cpp)
  - `ZLINK_PUB_OPT_NODROP` enabled on the server `PUB` socket

Verification:

```bash
./core/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB \
  --recv recv \
  --reuse-build \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

Observed:

- pass

## 2026-03-20 Callback Flake Update

`callback` path는 일부 run에서는 pass 하지만 아직 deterministic 하게 닫히지 않았다.

Applied change:

- [`perf_multi_pubsub_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_client.cpp)
  - local warmup/drain timer와 exact phase match에 의존하지 않고
    first `phase_active` callback을 본 시점부터 measurement를 시작하도록 변경

Observed after that change:

- same runner command may pass on one run and fail on the next
- repeated callback runs also exposed:
  - `callback metrics invalid fatal=0 recv=0 lat=0`
  - `malloc(): corrupted top size`
  - occasional server stop hang during direct repetition

Current classification:

- recv-side warmup regression: fixed
- callback-side warmup regression: still open
- callback path may still contain a message ownership or shutdown race bug
