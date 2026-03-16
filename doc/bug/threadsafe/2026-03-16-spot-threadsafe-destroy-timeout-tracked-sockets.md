# SPOT Thread-Safe Destroy Timeout With Stale Tracked Sockets

## Status

- Resolved
- Fix commit: `01a5c663` (`fix: complete thread-safe socket contracts`)

## Summary

`SPOT` thread-safe scaling 계약 테스트에서 `pub/sub` child handle을 모두
destroy 한 뒤에도 `spot_node_destroy()`가 `ETIMEDOUT`로 실패하던 문제를
해결했다.

초기 관찰 로그는 반복적으로 아래 형태였다.

```text
[spot-shutdown] service=spot node=0x... shutdown=abortive reason=110 live_slots=0 attachments=0 tracked=9
```

조사 과정에서 증상은 다음처럼 단계적으로 좁혀졌다.

- 초기: `tracked=9`
- lifecycle/ownership 정리 후: `tracked=2`
- mailbox/pipe termination 보강 후: `tracked=1`
- 최종: `spot_node_destroy()` 정상 수렴

최종적으로 확인된 근본 원인은 단일 문제가 아니라 아래 조합이었다.

- `connect_peer_pub()`가 remote control endpoint를 local `node_id`로 잘못
  파생해, 존재하지 않는 `inproc` control connection을 pending 상태로
  남기던 문제
- `spot` data-plane의 일부 internal socket이 poll 대상이 아니어서
  cross-thread termination command를 제때 소모하지 못하던 문제
- pipe termination 경로가 `waiting_for_delimiter` 상태에서 즉시 ack로
  수렴하지 못하던 edge case
- 64-handle scaling 계약에서 default socket cap이 낮아 fixture 자체가
  불필요한 resource ceiling에 걸리던 문제

## Affected Area

- [`spot_data_plane.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_data_plane.cpp)
- [`spot_node.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_node.cpp)
- [`service_runtime_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)
- [`pipe.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/pipe.cpp)
- [`socket_base.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
- [`socket_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp)
- [`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)
- 재현 테스트:
  [`test_thread_safe_scaling_contract.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/tests/integration/test_thread_safe_scaling_contract.cpp)

## User-Visible Impact

수정 전에는 high-load 또는 multi-handle thread-safe 시나리오에서
`zlink_spot_destroy()` 이후 `zlink_spot_node_destroy()`가
deterministic 하게 끝나지 못하고 timeout으로 실패할 수 있었다.

수정 후에는 `raw/gateway/spot` scaling 계약 테스트가 모두 통과하며,
`spot` teardown이 동일 fixture에서 정상 수렴한다.

## Reproduction

### Command

```bash
cmake --build core/build --target test_thread_safe_scaling_contract -j"$(nproc)"
ZLINK_PERF_MIN_RATIO=0.05 \
  ctest --test-dir core/build --output-on-failure \
  -R '^test_thread_safe_scaling_spot$'
```

초기 조사 단계에서는 `0.05`로 throughput gate를 낮춰 teardown failure를
우선 관찰했다.

### Original Failure

반복적으로 아래 둘 중 하나로 실패했다.

1. child handle destroy 단계 timeout

```text
... FAIL: zlink_spot_destroy (&pubs[i]) failed, errno = 110 (Connection timed out)
```

2. node destroy 단계 timeout

```text
... FAIL: zlink_spot_node_destroy (&sub_nodes[i]) failed, errno = 110 (Connection timed out)
```

그리고 shutdown 로그는 아래 형태로 남았다.

```text
[spot-shutdown] service=spot node=0x... shutdown=abortive reason=110 live_slots=0 attachments=0 tracked=9
```

## Expected Result

- explicit child `spot` handle destroy 이후 node destroy가 deterministic 하게
  완료되어야 한다.
- runtime slot과 attachment가 모두 0이면 lifecycle bookkeeping도 0으로
  수렴해야 한다.
- `spot_node_destroy()`는 thread-safe scaling fixture에서 timeout으로
  실패하면 안 된다.

## Investigation Progress

### Phase 1: Symptom Narrowing

초기에는 `tracked=9`가 남아 `spot` runtime의 tracked socket bookkeeping과
실제 ctx removal 사이에 불일치가 있다고 판단했다.

이 단계에서 확인한 사실:

- `node-per-handle`, explicit `spot` handle 생성/파괴, same-handle publish는
  기존 repo 패턴과 일치했다.
- 테스트 쪽 실험성 변경은 모두 원복했다.
- 문제는 test misuse가 아니라 core teardown/lifecycle 경로였다.

### Phase 2: Ownership and Drain Tightening

`service_runtime_base_t`의 drain 경로와 `spot` attachment/control socket
shutdown 경로를 정리하면서 `tracked=9`는 `tracked=2`까지 줄었다.

이 단계에서 남는 소켓은 반복적으로 아래 둘로 수렴했다.

- `mesh_xsub`
- `peer_ctrl_pub`

즉, 문제 범위가 전체 bookkeeping 불일치에서
특정 internal socket teardown 경로로 좁혀졌다.

### Phase 3: Pipe Termination and Mailbox Pumping

추가 계측 결과 `mesh_xsub` 쪽은 owner-thread mailbox를 안 도는 unpolled
socket 문제가 맞았다. data-plane 루프에서 internal socket command pumping을
보강한 뒤 `tracked=2`는 `tracked=1`로 줄었다.

마지막으로 남는 소켓은 `peer_ctrl_pub` 하나였고,
이 경로를 추적한 결과 remote control endpoint 파생이 잘못되어
실재하지 않는 `inproc` control connection이 pending 상태로 생성되는
문제가 드러났다.

## Final Root Cause

최종 root cause는 아래 조합이다.

### 1. Wrong Remote Control Endpoint Derivation

`spot` data-plane의 `connect_peer_pub()`가 remote control endpoint를
peer descriptor가 아니라 local `runtime->node_id`로 파생하고 있었다.

그 결과 실제 존재하지 않는 아래 형태의 endpoint로 pending `inproc`
connect가 생길 수 있었다.

```text
inproc://zlink.spot.peer-ctrl.<local-node-id>
```

이 잘못된 pending connection이 `peer_ctrl_pub` teardown을 오염시키고,
destroy 시 tracked socket drain을 끝까지 막았다.

### 2. Unpolled Internal Socket Command Starvation

`mesh_pub`, `peer_ctrl_sub`, `ingress`, `fanout` 등 일부 internal socket은
poll set에 없어서 cross-thread close/term command를 즉시 소모하지 못했다.

이로 인해 pipe termination이 mailbox scheduling에 묶여 지연될 수 있었고,
특히 scaling fixture에서 shutdown completion이 deterministic 하지 않았다.

### 3. Pipe Termination Edge Case

pipe termination 경로가 `waiting_for_delimiter` 상태에서 즉시 ack로
정리되어야 하는 경우에도, 실제 구현은 pending read/ack 처리 타이밍에 따라
불필요하게 지연될 수 있었다.

이 문제는 `set_nodelay()`와 `process_pipe_term()` 보강으로 정리했다.

### 4. Default Socket Cap Too Low For Scaling Contract

64-handle scaling 계약에서는 생성되는 internal socket 수가 많아
기존 `ZLINK_MAX_SOCKETS_DFLT` 값으로는 불필요한 ceiling에 걸릴 수 있었다.

이 제한은 teardown timeout의 직접 원인은 아니지만,
thread-safe scaling 계약을 안정적으로 실행하려면 함께 정리해야 했다.

## Fix Applied

### 1. `spot` peer control connection ownership 수정

[`spot_data_plane.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_data_plane.cpp)
에서 `connect_peer_pub()`는 더 이상 local `node_id` 기반으로
`peer_ctrl_pub`를 즉시 connect 하지 않는다.

대신:

- 초기 connect 단계에서는 `mesh_xsub`만 연결
- bootstrap descriptor를 수신한 뒤 실제 remote control endpoint로
  `peer_ctrl_pub`를 연결

이렇게 바꿔 잘못된 pending `inproc` control connection이 생기지 않게 했다.

### 2. internal socket mailbox pumping 보강

동일 파일에서 data-plane 루프가 아래 internal socket들에 대해
주기적으로 command mailbox를 pump 하도록 보강했다.

- `mesh_pub`
- `peer_ctrl_sub`
- `ingress`
- `fanout`

이로써 unpolled socket도 close/term command를 제때 소모하게 했다.

### 3. pipe termination 즉시 ack 경로 보강

[`pipe.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/pipe.cpp)
에서 다음을 수정했다.

- `set_nodelay()`가 이미 `waiting_for_delimiter` 상태인 pipe에도
  즉시 ack를 보낼 수 있도록 보강
- `process_pipe_term()`가 실제 pending read가 없으면 즉시 ack 하도록 보강
- next item이 delimiter면 바로 소비하고 ack 하도록 보강

### 4. inproc pipe erase semantics 정합성 수정

[`socket_base.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
에서 `inproc` endpoint teardown 시 `_inprocs.erase_pipes()`가
지연 종료 성격으로 남지 않도록 `terminate(false)` 기준으로 정리했다.

같은 영역에 internal helper
`set_all_pipes_nodelay()`도 추가했다.

### 5. `spot` attachment/control teardown tightening

[`spot_node.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_node.cpp)
에서 attachment destroy 시:

- endpoint 기억
- `term_endpoint()`
- `set_all_pipes_nodelay()`
- `close_socket_and_wait()`

순으로 수렴하도록 정리했다.

또한 control socket close 경로에서도 필요한 socket은
`close_socket_and_wait()`로 동기화했다.

### 6. lifecycle drain semantics 보강

[`service_runtime_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)
에서 drain 경로를 단일 socket fail-fast 성격에서
전체 closing socket을 polling하는 방식으로 보강했다.

이는 root cause 자체를 해결하는 수정은 아니지만,
teardown diagnostics와 drain completeness를 개선했다.

### 7. default socket cap 상향

[`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)
에서:

```c
#define ZLINK_MAX_SOCKETS_DFLT 4095
```

로 상향해 64-handle scaling 계약을 기본 설정에서도 수행 가능하게 했다.

## Verification

다음 검증을 통과했다.

```bash
cmake --build core/build --target test_thread_safe_scaling_contract -j"$(nproc)"
ZLINK_PERF_MIN_RATIO=0.05 \
  ctest --test-dir core/build --output-on-failure \
  -R '^test_thread_safe_scaling_(raw|gateway|spot)$'
```

결과:

- `test_thread_safe_scaling_raw` pass
- `test_thread_safe_scaling_gateway` pass
- `test_thread_safe_scaling_spot` pass

## Lessons Learned

- teardown timeout을 단순히 `tracked` count mismatch로만 보면 부족하다.
  실제 pending `inproc` connection의 생성 원인까지 추적해야 한다.
- unpolled internal socket이 존재하는 구조에서는 owner-thread mailbox pumping이
  lifecycle correctness의 일부다.
- pipe termination의 delayed ack semantics는 scale fixture에서 쉽게
  증폭되므로, `waiting_for_delimiter` edge case를 방치하면 안 된다.

## Residual Notes

- 조사 중 추가한 pipe/socket/session debug log는 env-gated 형태로 유지했다.
- 본 문서는 최초 작성 시점의 미해결 가설 상태에서,
  최종 원인 확인 및 수정 완료 상태로 갱신되었다.
