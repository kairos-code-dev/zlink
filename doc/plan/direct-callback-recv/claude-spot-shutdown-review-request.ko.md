# Spot Shutdown / Teardown 이슈 검토 요청서

## 목적
`spot` direct-callback/runtime 정리 작업 이후에도 일부 `spot` split 회귀에서
종료가 비결정적으로 길어지거나 timeout 되는 문제가 남아 있다.

이 문서는 현재까지의 변경 사항, 재현 증상, 이미 시도한 해결책, 그리고
검토 받고 싶은 질문을 정리한 것이다.

검토 대상은 주로 다음 영역이다.

- `spot` runtime / attachment lifecycle
- `spot node destroy -> ctx_term` 종료 계약
- `TLS/WS peer transport` teardown
- `discovery/registry`가 같이 들어간 `spot introspection` 시나리오

기준 커밋:

- `17e69e79` (`refactor: checkpoint direct callback lifecycle work`)

## 현재 문제 요약
핵심 증상은 두 가지다.

1. `spot` 관련 테스트가 단독 실행에서는 통과하지만,
   split/full 순차 실행에서는 일부 케이스가 `ctest timeout`으로 멈춘다.
2. `spot_node_destroy()`에서 `shutdown=abortive` 로그가 찍혀도,
   그 뒤 `ctx_term()` 또는 테스트 프로세스 종료가 끝까지 수렴하지 않는 경우가 있다.

현재 가장 최근에 명확히 잡힌 blocker:

- `core/tests/spot/test_spot_service_introspection.cpp`
  - `test_spot_tls_settings_lock_after_bind_connect_and_register`
  - `ctest --output-on-failure --stop-on-failure -R '^test_spot_'`
    기준 `60s timeout`

추가로 sequence 재현 중에는 다음도 관찰됐다.

- `test_spot_topology_summary_lifecycle`
  - registry 생성 단계에서 `Expected Non-NULL`
  - 즉 `create_started_registry_with_port_seed(...)`가 NULL 반환

## 재현 방법
### 1. 현재 대표 blocker
```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite/core/build
ctest --output-on-failure --stop-on-failure -R '^test_spot_'
```

최근 관찰 결과:

- 앞선 introspection 케이스들은 통과
- `test_spot_service_introspection_tls_lock`에서 `60s timeout`
- stderr:
  - `service=spot node=0x... shutdown=abortive reason=110 live_slots=0 attachments=0`

### 2. 단독 실행은 자주 통과함
```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
env ZLINK_TEST_CASE=test_spot_tls_settings_lock_after_bind_connect_and_register \
    ZLINK_SPOT_SHUTDOWN_LOG=1 \
    core/build/bin/test_spot_service_introspection
```

### 3. sequence 재현 중 관찰된 이슈
```bash
cd /home/hep7/project/kairos/zlink-direct-callback-rewrite
for tc in \
  test_spot_pub_sub_options_and_routing_ids \
  test_spot_monitors_and_monitor_poller \
  test_spot_node_direct_apis_and_explicit_handles_interop \
  test_spot_topology_summary_lifecycle \
  test_spot_register_null_derivation_and_wildcard_rejection \
  test_spot_tls_settings_lock_after_bind_connect_and_register
do
  echo "CASE:$tc"
  env ZLINK_TEST_CASE=$tc \
      ZLINK_SPOT_SHUTDOWN_LOG=1 \
      ZLINK_TEST_DEBUG=1 \
      core/build/bin/test_spot_service_introspection || break
done
```

이 sequence에서 관찰된 사례:

- `topology_summary`에서 registry 생성이 NULL로 실패
- 또는 `tls_lock`에서 종료가 timeout

## 이미 반영된 구조 변경
### 1. `spot` public handle -> runtime ownership 집중화
다음 방향으로 많이 정리했다.

- `spot_pub_t`, `spot_sub_t`는 직접 socket pointer를 들지 않고
  runtime attachment id를 통해 socket을 조회하도록 변경
- child handle destroy는 detach 위주로 줄이고,
  final drain owner를 `spot_node_destroy()`로 몰아가는 방향으로 정리

관련 파일:

- `core/src/services/spot/spot_runtime.hpp`
- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_node.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_pub.hpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_sub.hpp`
- `core/src/services/spot/spot_data_plane.cpp`

### 2. 공통 lifecycle helper 도입
공통 runtime helper를 추가했다.

- `core/src/services/common/service_runtime_base.hpp`

역할:

- owned socket registry
- closing socket registry
- canonical close path
- `wait_drained()`
- 최근 추가:
  - `force_wait_remaining()`

### 3. socket identity / ctx helper
다음도 추가 또는 정리했다.

- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/core/ctx.hpp`
- `core/src/core/ctx.cpp`

포인트:

- internal `socket_id()` 접근
- `wait_for_socket_removal(...)`
- `close_socket_and_wait(...)`
- `ZLINK_CTX_DEBUG=1`일 때 context socket dump

### 4. abortive fallback 및 종료 로그
현재 `spot node destroy`는 graceful shutdown을 먼저 시도하고,
실패하면 abortive fallback으로 내려간다.

현재 로그 형식:

- graceful:
  - `service=spot node=... shutdown=graceful`
- abortive:
  - `service=spot node=... shutdown=abortive reason=110 live_slots=0 attachments=0`

환경 변수:

- `ZLINK_SPOT_SHUTDOWN_LOG=1`

관련 구현:

- `core/src/services/spot/spot_node.cpp`

## 최근 테스트 관련 수정
### 1. unified `spot` handle API 오사용 정리
`test_spot_pubsub_scenario.cpp`에서 unified `spot` handle인데
standalone `spot_pub/sub` API를 잘못 쓰던 부분을 수정했다.

예:

- `zlink_spot_sub_subscribe(...)` -> `zlink_spot_subscribe(...)`
- `zlink_spot_pub_publish(...)` -> `zlink_spot_publish(...)`
- unified handle destroy에 `zlink_spot_pub_destroy/zlink_spot_sub_destroy`를
  쓰던 부분 제거

파일:

- `core/tests/spot/test_spot_pubsub_scenario.cpp`

### 2. introspection 포트 충돌 완화
테스트에 포트 seed helper를 넣었다.

- `bind_spot_node_with_port_seed(...)`
- `create_started_registry_with_port_seed(...)`

파일:

- `core/tests/spot/test_spot_service_introspection.cpp`

하지만 여전히 sequence 실행에서 registry 생성 또는 teardown 문제가 남는다.

## 현재 관찰상 중요한 사실
### A. abortive fallback이 들어가도 종료가 끝나지 않는 경우가 있음
이건 `spot node`가 추적하는 socket/attachment는 비웠다고 보는데,
그 바깥 `ctx/reaper` 레벨에서는 아직 제거 완료가 안 된 자원이 있다는 뜻으로 보고 있다.

즉 현재 상태는:

- service-level abortive fallback: 있음
- context-level bounded fallback: 없음

### B. 단독 실행은 통과하는데 split/full 순차 실행에서만 깨지는 경우가 많음
이 패턴 때문에 다음 두 가지 가설을 보고 있다.

1. `spot/destroy -> ctx_term` 사이 lifecycle race
2. `spot introspection` 테스트들의 transport/runtime 정리가 다음 프로세스에
   영향을 줄 정도로 늦게 수렴하거나, 특정 transport state가 잔류

### C. `ZLINK_CTX_DEBUG=1`에서 보였던 잔류 socket 패턴
실패 run 또는 느린 run에서 다음 류의 socket이 오래 남았다.

- `inproc://zlink.spot.<id>.ctrl`
- `inproc://zlink.spot.<id>.pub-in`
- `inproc://zlink.spot.<id>.sub-out`
- 일부 `<none>` endpoint socket
- 가끔 transport endpoint socket (`tcp://127.0.0.1:...`)

즉 `spot` internal ctrl/data-plane/facade 관련 socket이
`ctx_term()` 직전까지 남는 경우가 반복 관찰됐다.

## 현재 코드에서 특히 봐줬으면 하는 포인트
### 1. `spot_node_destroy()`의 종료 계약이 올바른가
파일:

- `core/src/services/spot/spot_node.cpp`

보고 싶은 것:

- `destroy_handles()`
- `_runtime->stop_and_join()`
- `wait_owned_socket_removals(...)`
- abortive fallback 이후 `force_wait_remaining(...)`

질문:

- 지금 구조가 `destroy()` 반환 전에 enough drain을 보장하는가?
- 아니면 `ctx_term()`에 최종 강제 수렴 책임을 일부 넘겨야 하는가?

### 2. `service_runtime_base_t`의 모델이 맞는가
파일:

- `core/src/services/common/service_runtime_base.hpp`

보고 싶은 것:

- `_owned_sockets`
- `_closing_sockets`
- `close_socket(...)`
- `wait_drained(...)`
- `force_wait_remaining(...)`

질문:

- tracked socket 모델이 충분한가?
- 아니면 이미 closing된 socket의 상태를 더 풍부하게 들고 있어야 하는가?
- `force_wait_remaining()`이 abortive path로 충분한가?

### 3. `spot` attachment/runtime ownership 모델이 실제로 race를 줄이는 방향인가
파일:

- `core/src/services/spot/spot_runtime.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_data_plane.cpp`

질문:

- attachment/runtime 구조는 맞는 방향인가?
- 아직 public handle 또는 node 계층에 lifecycle 책임이 남아 있는가?
- `TLS/WS peer transport` teardown이 runtime stop과 직렬화되지 않는 경로가 있는가?

### 4. `ctx_term()`에 bounded abortive fallback이 필요한가
파일:

- `core/src/core/ctx.cpp`

현재는 `terminate()`가 reaper `done`을 사실상 무기한 기다린다.

질문:

- 지금 남은 증상 기준으로
  `ctx_term()`에도 bounded fallback을 넣는 것이 맞는가?
- 아니면 그 전에 service-level teardown만 더 고쳐야 하는가?
- `ctx`가 최종 강제 종료를 맡는다면 어떤 invariant가 필요할까?

### 5. test isolation 문제인지, runtime bug인지
특히 다음 두 테스트를 sequence 관점에서 봐주면 좋겠다.

- `test_spot_topology_summary_lifecycle`
- `test_spot_tls_settings_lock_after_bind_connect_and_register`

파일:

- `core/tests/spot/test_spot_service_introspection.cpp`

질문:

- 이 증상이 core runtime bug에 더 가깝나?
- 아니면 test helper/port seed/transport settle 부족 같은 isolation defect가 큰가?
- 둘 다라면 경계는 어디인가?

## Claude에게 원하는 답변 형식
가능하면 아래 형식으로 답변 받으면 좋겠다.

1. 현재 증상의 root cause 가설 2~3개
2. 각 가설의 근거
3. 가장 가능성 높은 1순위 원인
4. 구조적으로 맞는 해결책
5. 지금 코드에 바로 적용 가능한 최소 수정안
6. 장기적으로 더 나은 구조 개선안
7. 테스트 관점에서 꼭 보강해야 할 회귀

## 참고 파일 목록
직접 읽어보면 좋은 파일:

- `core/src/services/common/service_runtime_base.hpp`
- `core/src/services/spot/spot_runtime.hpp`
- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_data_plane.cpp`
- `core/src/core/ctx.cpp`
- `core/tests/spot/test_spot_pubsub_scenario.cpp`
- `core/tests/spot/test_spot_service_introspection.cpp`

필요하면 문서도 참고:

- `doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md`
- `doc/plan/direct-callback-recv/spot-node-direct-facade-plan.ko.md`
- `doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md`

## 한 줄 요약
`spot` runtime/attachment/lifecycle를 많이 정리했지만,
아직 `spot introspection` split sequence, 특히 `tls_lock`에서
`destroy -> ctx_term` 수렴이 비결정적이다.
abortive 로그는 찍히지만 프로세스 종료까지는 아직 완전히 보장되지 않는다.
