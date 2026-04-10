# 2026-04-02 multi `SPOT` ready gate contract mismatch

## Summary

`core/perf` 의 multi `SPOT` start gate를 정리하던 중, perf policy가 raw
PUB/SUB의 exact ready-count gate를 SPOT service에도 그대로 일반화하고
있었다는 점이 드러났다.

public guide/api 문서는 SPOT publisher의 start gate를 일관되게
`ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED(value>=1)` 로
설명한다. 반면 당시 perf policy/implementation은
`ZLINK_SPOT_MONITOR_EVENT_PUB_DELIVERY_READY_CHANGED(value>=expected_clients)`
를 publisher start gate로 사용하려고 했고, 그 exact-count gate를 적용하자
multi `SPOT` benchmark가 시작하지 못했다.

현재 증거만으로는 이것을 곧바로 core bug로 단정할 수 없다. 우선
`doc/perf` policy를 public contract와 맞추는 것이 선행되어야 한다.

## Public contract

근거 문서:

- `doc/guide/06-monitoring.ko.md`
- `doc/spec/core/events.md`

핵심 내용:

- SPOT sub start gate: `SUB_DELIVERY_READY_CHANGED`
- SPOT pub start gate:
  `PUB_FIRST_DELIVERY_READY_CHANGED(value>=1)`
- `SPOT_PUB_DELIVERY_READY_CHANGED` 는 service monitor surface에 존재하지만,
  public contract 문서에서는 publisher start gate로 규정되어 있지 않다

즉 multi `SPOT` publisher의 공식 start gate는 현재 public 문서 기준으로는
`PUB_FIRST_DELIVERY_READY_CHANGED` 다.

## What perf was doing

server 쪽:

- `PERF_MULTI_SPOT_PUB_READY_QUORUM_PERCENT` 기본 90
- `PUB_DELIVERY_READY_CHANGED` 기반 quorum 완화 gate

client 쪽:

- `PERF_MULTI_SPOT_PHASE_QUORUM_PERCENT` 기본 90
- phase start 관찰을 slot quorum으로 완화

따라서 “quorum workaround를 전부 제거한 순수 exact-count path”라고 말할 수는
없다. server 쪽 완화만 제거해도 client 쪽에는 여전히 quorum 기반 phase gate가
남아 있다.

## Reproduction that exposed the mismatch

빌드:

```bash
cmake --build core/build --target perf_spot comp_src_spot_server comp_src_spot_client -j$(nproc)
```

single `SPOT` 검증:

```bash
./core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1
```

결과: success

multi `SPOT` exact-count experiment:

```bash
PERF_DEBUG=1 ./core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --warmup 1 \
  --duration 1 \
  --clients 2
```

동일 실험을 `--recv callback` 으로도 수행했으며 결과는 동일했다.

관찰된 증상:

- client는 `CLIENT_READY,64` 까지는 출력
- 이후 client가 `[multi-spot-client] size start timeout size=64` 로 종료
- 최종적으로 RESULT line이 생성되지 않아 partial/fail

이 결과는 “multi `SPOT` 에서 exact-count publisher gate를 적용하면 현재 perf
흐름이 시작되지 않는다”는 점까지만 보여 준다.

이 재현만으로는 아래를 아직 분리하지 못했다.

1. `PUB_DELIVERY_READY_CHANGED.value` 가 expected clients 수까지 실제로
   올라오지 않는지
2. server가 pub-ready timeout 으로 종료했는지
3. server는 진행했지만 client phase gate가 별도 이유로 멈췄는지

## Current conclusion

- **확정된 것**
  - 기존 `doc/perf` multi `SPOT` policy는 public contract보다 강한 gate를
    요구하고 있었다
  - multi `SPOT` path에는 server/client 양쪽에 완화용 quorum 성격의 코드가
    남아 있었다

- **아직 확정되지 않은 것**
  - core monitoring/runtime bug 여부
  - `SPOT_PUB_DELIVERY_READY_CHANGED` 의 exact-count semantics가 깨졌는지 여부

## Requested follow-up

1. `doc/perf` multi `SPOT` start gate를 public contract와 맞춘다.
   publisher start gate는 `PUB_FIRST_DELIVERY_READY_CHANGED(value>=1)` 로 둔다.
2. multi `SPOT` client/server에 남아 있는 quorum 완화 코드를 별도 항목으로
   추적한다.
3. exact-count semantics를 product contract로 삼고 싶다면, 먼저 guide/api에
   그 계약을 명시한 뒤 `core/tests/` 회귀로 고정한다.
4. 이후에도 exact-count path가 필요하면 monitor trace와 server/client 종료
   원인을 분리 수집한 뒤 core bug 여부를 다시 판정한다.
