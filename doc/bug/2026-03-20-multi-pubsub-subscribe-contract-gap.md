# multi `PUBSUB` warmup run exposed a `zlink_subscribe()` contract-gap hypothesis

## Summary

이 문서는 최초 관찰 시점의 `core` bug hypothesis를 기록한다.

현재 multi perf에서 남은 실패는 `PUBSUB` 뿐이다.

- `--recv recv --warmup 1`
- `--recv callback --warmup 1`

perf process/phase sequencing 쪽 start gate 버그 일부를 정리한 뒤에도,
failing run에서는 client-side `zlink_subscribe()` 관찰 shape가
기존 public contract 및 integration test 기대와 맞지 않는다.

관찰된 증상:

- `topic_len=64, part_count=0`
- 또는 `topic=="bench"` 인데 payload header decode 실패

반면 기존 integration test는 `zlink_subscribe()`가 아래 shape를 제공한다고
검증한다.

- `topic=="bench"`
- `part_count==1`
- `parts[0] == published payload`

초기 관찰만 보면 perf sequencing만이 아니라,
multi-process warmup path에서의 `zlink_subscribe()` direct recv contract bug
가능성이 높아 보였다.

## Why this looked like a bug

현재 저장소에는 `zlink_subscribe()` public contract를 전제로 한 테스트가 이미 있다.

- [`test_monitor_perf_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_perf_contract.cpp#L374)
- [`test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp#L792)

이 테스트들은 `zlink_publish(pub, "topic", &part, 1, 0)` 이후
`zlink_subscribe(sub, ...)`가

- `topic_len == strlen(topic)`
- `part_count == 1`
- `parts[0] == payload`

형태를 돌려준다고 검증한다.

그런데 multi perf `PUBSUB` warmup run에서는 이와 다른 shape가 관찰된다.
즉 perf 쪽 파라미터 차이로 설명되지 않는 contract mismatch다.

## Reproduction

### 1. Runner smoke

```bash
timeout 25s env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 PERF_DEBUG_TRANSITIONS=1 \
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

```bash
timeout 25s env PYTHONDONTWRITEBYTECODE=1 PERF_DEBUG=1 PERF_DEBUG_TRANSITIONS=1 \
  ./core/perf/run_benchmarks_multi.sh \
  --pattern PUBSUB \
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

Actual result:

- both runs fail
- failure suffix often collapses to `non_zero_exit_1_CLIENT_READY,64`
  or `size case failed size=64`

### 2. Additional debug from current perf client path

During the failing recv run, the client observed:

```text
[multi-pubsub-client] header mismatch decoded=0 magic=1297106481 run=1 phase=1 size=64 expected_size=64
[multi-pubsub-client] recv shape mismatch topic_len=64 part_count=0
```

These two lines mean the direct recv path is not consistently seeing the
expected `(topic="bench", part_count=1, payload frame)` shape.

## Expected result

For `PUBSUB` direct recv in multi-process warmup runs:

- `topic_id_out == "bench"`
- `topic_id_len_out == 5`
- `part_count_out == 1`
- `parts_out[0]` contains the published payload bytes

For callback mode:

- callback handler should receive the same logical message shape
- warmup/drain/active phases should not corrupt topic/payload framing

## Non-goals

아래는 해결로 인정하지 않는다.

- perf client에서 malformed receive shape를 무시하고 계속 진행
- warmup을 0으로 고정해서 문제를 숨김
- `PUBSUB`만 다른 ad-hoc decode rule을 추가해 contract mismatch를 덮음

## Suspected fix areas

- [`core/src/api/zlink.cpp`](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)
  `recv_socket_subscribe_parts()`
- `PUB/SUB` multipart framing around `zlink_publish()` + `zlink_subscribe()`
- multi-process warmup/drain transitions if they expose an ordering bug that
  reaches `zlink_subscribe()` with partial or malformed frame state

## Current repo decision at filing time

- 이 문제는 perf workaround로 닫지 않는다.
- `zlink_subscribe()` public contract bug candidate로 추적한다.

## 2026-03-20 Validation Update

`core/tests/` direct regression으로 다시 확인한 결과, 현재 workspace에서는
이 문서가 주장한 `zlink_subscribe()` public contract 위반을 재현하지 못했다.

추가한 회귀 테스트:

- [`test_multi_socket_contract_regressions.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_multi_socket_contract_regressions.cpp)
  - `test_pubsub_subscribe_preserves_topic_and_payload_shape_across_warmup`
  - `test_pubsub_subscribe_dontwait_preserves_perf_contract_during_burst`

검증 내용:

- `ZLINK_PUB` + `ZLINK_SUB` x2
- `topic=="bench"`
- `payload size == 64`
- warmup/drain/active 흐름 및 nonblocking burst drain
- 모든 direct `zlink_subscribe()` 수신에서
  - `topic_len == 5`
  - `topic == "bench"`
  - `part_count == 1`
  - `parts[0] == published payload`

검증 결과:

- direct core regression은 모두 통과
- 따라서 현재까지는 이 문서의 `topic_len=64, part_count=0` 관찰을
  `core` public contract bug로 확정할 수 없음

같은 시점에 perf multi runner를 다시 실행하면 `PUBSUB --recv recv`는
여전히 실패한다. 다만 현재 관찰된 실패는 아래와 같았다.

- 반복 수신 다수는 `topic_ok=1`, `part_count=1`, payload header decode 성공
- 이후 특정 시점에만 payload header decode 실패로 종료
- 이번 재실행에서는 `topic_len=64, part_count=0` shape mismatch는 재관찰되지 않음

현재 판단:

- 남은 이슈는 `core/tests/` direct contract regression으로는 재현되지 않는다.
- 따라서 현재 저장소 기준으로는 `core` subscribe contract bug보다
  perf multi `PUBSUB` orchestration 또는 perf-side payload validation path
  이슈로 보는 쪽이 더 타당하다.
- 저장소 규칙에 따라 perf 코드는 수정하지 않았다.

## Current status

이 문서는 지금 시점에서 confirmed bug report가 아니라
초기 hypothesis 기록으로 취급한다.

현재 active bug 분류는 아래 문서를 따른다.

- [`2026-03-20-multi-pubsub-warmup-regression.md`](/home/hep7/project/kairos/zlink/doc/bug/2026-03-20-multi-pubsub-warmup-regression.md)
