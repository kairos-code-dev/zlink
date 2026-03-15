# SPOT Thread-Safe Destroy Timeout With Stale Tracked Sockets

## Summary

`SPOT` thread-safe scaling 계약 테스트에서 `pub/sub` child handle을 모두
정상 destroy 한 뒤에도 `spot_node_destroy()`가 `ETIMEDOUT`로 실패한다.

관찰된 shutdown 로그는 반복적으로 아래 형태다.

```text
[spot-shutdown] service=spot node=0x... shutdown=abortive reason=110 live_slots=0 attachments=0 tracked=9
```

핵심 포인트는 다음이다.

- `live_slots == 0`
- `attachments == 0`
- 그런데 `tracked == 9`가 끝까지 남음
- 그 결과 `spot_node_destroy()`가 graceful/abortive shutdown 이후에도
  drain 완료로 수렴하지 못하고 timeout으로 실패함

즉, 실제 runtime slot/attachment는 비워졌는데
`service_runtime_base`가 관리하는 socket lifecycle bookkeeping이 stale state를
남기고 있다고 보는 것이 가장 타당하다.

## Affected Area

- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_runtime.hpp`
- `core/src/services/common/service_runtime_base.hpp`
- 재현 테스트:
  [`core/tests/integration/test_thread_safe_scaling_contract.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/tests/integration/test_thread_safe_scaling_contract.cpp)

## User-Visible Impact

- high-load 또는 multi-handle thread-safe 시나리오에서 `zlink_spot_destroy()`
  자체는 성공하더라도, 뒤이은 `zlink_spot_node_destroy()`가 timeout으로 실패할
  수 있다.
- 결과적으로 SPOT node teardown이 deterministic 하지 않다.
- thread-safe acceptance, perf-contract, stress/TSan lane 전개를 막는다.

## Reproduction

### Command

```bash
cmake --build core/build --target test_thread_safe_scaling_contract -j"$(nproc)"
ZLINK_PERF_MIN_RATIO=0.05 \
  ctest --test-dir core/build --output-on-failure \
  -R '^test_thread_safe_scaling_spot$'
```

`0.05`는 throughput ratio 자체를 보려는 값이 아니라,
현재 teardown 버그가 perf threshold보다 먼저 터지므로 ratio gate를 낮춰
destroy 문제만 관찰하기 위한 값이다.

### Observed Result

반복적으로 아래 둘 중 하나로 실패한다.

1. child handle destroy 단계 timeout

```text
... FAIL: zlink_spot_destroy (&pubs[i]) failed, errno = 110 (Connection timed out)
```

2. node destroy 단계 timeout

```text
... FAIL: zlink_spot_node_destroy (&sub_nodes[i]) failed, errno = 110 (Connection timed out)
```

그리고 장시간 실행 끝에는 아래 로그가 남는다.

```text
[spot-shutdown] service=spot node=0x... shutdown=abortive reason=110 live_slots=0 attachments=0 tracked=9
```

## Expected Result

- explicit child `spot` handle destroy 이후 node destroy가 deterministic 하게
  완료되어야 한다.
- runtime slot과 attachment가 모두 0이면 lifecycle bookkeeping도 0으로
  수렴해야 한다.
- `spot_node_destroy()`는 이 상태에서 timeout으로 실패하면 안 된다.

## What Was Verified

다음은 이번 조사에서 확인한 사실이다.

- 재현은 test fixture 자체의 API misuse 때문이 아니다.
  `node-per-handle`, explicit `spot` handle 생성/파괴, same-handle publish는
  기존 repo 패턴과 일치한다.
- 테스트 쪽 실험성 보정은 모두 원복했다.
- `tracked=9`는 explicit child handle 1개만을 의미하지 않는다.
  `spot` runtime이 추적하는 internal socket 집합 크기와 훨씬 더 잘 맞는다.
- shutdown 로그에서 이미 `live_slots=0`, `attachments=0`이므로,
  문제는 peer 연결이 남았다기보다 tracked socket bookkeeping/drain 불일치다.

## Relevant Code Paths

### 1. Attachment destroy

`spot_pub_t::destroy_internal()` / `spot_sub_t::destroy_internal()`는
`_runtime->destroy_attachment(_attachment_id)`를 호출한다.

관련 경로:

- [`spot_pub.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_pub.cpp)
- [`spot_sub.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_sub.cpp)
- [`spot_node.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_node.cpp)

### 2. Node shutdown

`spot_node_t::destroy()`는 내부적으로:

1. peer disconnect / unbind
2. data plane stop
3. `destroy_handles()`
4. `wait_owned_socket_removals(10000)`
5. 필요 시 `abortive_stop()`
6. `force_wait_remaining(5000)`
7. `wait_owned_socket_removals(5000)`

를 수행한다.

여기서 최종 timeout이 발생한다.

### 3. Lifecycle bookkeeping

`service_runtime_base_t`는 `_owned_sockets`와 `_closing_sockets`를 별도로
추적한다.

관련 파일:

- [`service_runtime_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)

현재 증상은 다음과 해석된다.

- runtime slot은 이미 NULL 처리되어 `live_slots == 0`
- attachment map도 이미 비워져 `attachments == 0`
- 그러나 `_owned_sockets + _closing_sockets`는 9개를 계속 유지
- 따라서 `wait_drained()` / `force_wait_remaining()`가 끝까지 0으로 수렴하지 않음

## Root-Cause Hypothesis

가장 유력한 가설은 아래 둘 중 하나다.

### Hypothesis A

`spot` internal/runtime socket 또는 attachment socket 중 일부가
`close()`는 호출되지만 `ctx` reaper removal까지 완료되지 않아서
`service_runtime_base`의 `_closing_sockets`에 stale entry로 남는다.

### Hypothesis B

`spot` shutdown 과정에서 runtime socket은 실제로 해제되지만,
특정 경로에서 `register_socket()`에 대응하는 drain/erase가 완결되지 않아
`tracked` count만 남는다.

현재 로그 기준으로는 `A + bookkeeping mismatch` 조합일 가능성이 높다.

## Core-Only Fix Attempts Tried During Investigation

아래 수정은 모두 core 쪽에서만 시도했고, 버그를 완전히 해결하지 못했다.

### Attempt 1

`spot_attachment_t`가 endpoint를 기억하도록 하고,
`destroy_attachment()`에서 `term_endpoint()`를 먼저 수행.

의도:

- attachment inproc endpoint를 명시적으로 끊어서 lingering pipe를 줄이기 위함

결과:

- child destroy failure 시점이 약간 변했지만 최종 timeout은 여전히 재현

### Attempt 2

`destroy_attachment()`를 `close_socket_and_wait()`로 바꿔
attachment close를 동기화.

의도:

- explicit `zlink_spot_destroy()` 반환 전에 attachment removal까지 보장

결과:

- child destroy 단계에서 더 일찍 `ETIMEDOUT`가 발생
- 근본 해결은 아니었음

### Attempt 3

`service_runtime_base_t::force_wait_remaining()`에서 이미 `closing` 상태인
소켓에도 다시 `stop()/close()`를 걸고 `wait_for_socket_removal()`하도록 변경.

의도:

- accepted-close만 걸린 stale closing socket을 abortive path에서 강제로 수렴

결과:

- 증상 변화는 있었지만 최종적으로 `tracked=9` timeout은 남음

### Attempt 4

`spot_runtime_t::close_control_sockets()`와 `abortive_stop()`에서 internal
runtime sockets를 `close_socket_and_wait()`로 동기화.

의도:

- runtime internal sockets 8개가 lifecycle에 남는 문제를 직접 제거

결과:

- test duration만 길어졌고 최종 timeout은 계속 재현

### Attempt 5

`spot_node_t::destroy()`의 abortive path에서
`live_slots == 0 && attachments == 0 && final_error == ETIMEDOUT`이면
성공으로 간주하는 완화 로직 시도.

의도:

- runtime이 사실상 정리된 경우 stale tracked count만으로 hard failure 하지 않기 위함

결과:

- 근본 원인을 덮는 방향이고, 실제로는 전체 실행이 hang/timeout으로 이동
- 적절한 해결책이 아님

## Current Best Reading

버그의 본질은 테스트가 아니라 core teardown bookkeeping 문제다.

정확히는:

- `spot` explicit handle destroy와 node destroy 사이에서
  internal socket tracking이 실제 ctx removal과 동기화되지 않는다.
- 그 결과 `spot_node_destroy()`는 runtime이 거의 다 정리된 후에도
  `_lifecycle` 기준으로는 socket이 남아 있는 것으로 판단한다.
- `tracked=9`는 이 불일치를 가장 직접적으로 보여주는 신호다.

## Recommended Next Debug Steps

1. `service_runtime_base_t`에 debug-only logging을 넣어
   `_owned_sockets`와 `_closing_sockets`의 socket id/type 변화를 추적한다.
2. `spot_node_t::destroy()` 진입 직전과
   `destroy_handles()`, `stop_and_join()`, `abortive_stop()` 직후에
   tracked socket id 목록을 덤프한다.
3. `ctx_t::wait_for_socket_removal()`이 끝까지 false를 반환하는 socket이
   어떤 type인지 확인한다.
4. 특히 아래 socket들을 구분해서 본다.
   - `data_ctrl_front/back`
   - `mesh_pub`
   - `mesh_xsub`
   - `peer_ctrl_pub/sub`
   - `local_pub_ingress_sub`
   - `local_fanout_xpub`
   - explicit `spot` attachment pub/sub
5. `close_socket()`와 `close_socket_and_wait()`를 섞어 쓰는 현재 구조가
   `spot` shutdown에 맞는지 재검토한다.

## Status

- 버그 재현: 확정
- 테스트 오남용 여부: 아님
- core root cause: 미확정
- core-only 완전 해결: 미완료

## Notes

- 조사 중 넣었던 test-side 보정은 원복했다.
- 현재 workspace에는 미해결 core 실험 수정이 남아 있을 수 있으므로,
  실제 fix 작업 전에는 이 문서의 “Attempt” 항목과 현재 diff를 같이 보고
  정리하는 것이 좋다.
