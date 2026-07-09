# C++ Worker Prompt: actor_ref_t / spot_ref_t 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

C++ framework public contract에서 메시징 대상 개념을 `actor_ref_t` / `spot_ref_t`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

## Naming 규칙

| 현재 이름 | 최종 이름 |
|-----------|-----------|
| `actor_ref_t` | `actor_ref_t` 유지 |
| `actor_ref_snapshot_t` | `actor_ref_snapshot_t` 유지 |
| `spot_address_t` | `spot_ref_t` |
| `spot_address_resolver_t` | `spot_ref_resolver_t` |

C++ public type은 `zlink::framework` namespace 안에서 snake_case + `_t`를 사용한다. 타입명에
`zlink_` prefix를 다시 붙이지 않는다.

## 제거 대상

```text
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
  actor_client_t::send_to_actor(std::string actor_id, ...)
  actor_client_t::request_to_actor(std::string actor_id, ...)
  actor_client_t::send_to_actor_erased(std::string actor_id, ...)
  actor_client_t::request_to_actor_erased(std::string actor_id, ...)

framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
  route_client_t::send_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)
  route_client_t::request_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)

framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
  request_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
  send_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
```

## 추가/변경 대상

- `spot_address_t`를 `spot_ref_t`로 변경한다.
- `spot_address_resolver_t`를 `spot_ref_resolver_t`로 변경한다.
- `resolve_spot_address`를 `resolve_spot_ref`로 변경한다.
- `resolve_actor_spot_address`를 `resolve_actor_spot_ref`로 변경한다.
- actor messaging API는 `const actor_ref_t&` 또는 값 이동 가능한 `actor_ref_t`를 받는다.
- spot messaging API는 `const spot_ref_t&` 또는 값 이동 가능한 `spot_ref_t`를 받는다.
- node rid와 spot rid를 낱개로 받는 public messaging API는 `spot.hpp`와 `channel.hpp` 양쪽에서
  제거한다.

## 주요 파일

```text
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/resolvers.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/rows.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
framework/languages/cpp/framework/src/runtime/actors/actor_client.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.hpp
framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp
framework/languages/cpp/framework/src/runtime/channels/route_channel_runtime.cpp
framework/languages/cpp/framework/src/runtime/locations/store_location_resolvers.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_internal_dispatcher.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_packets.cpp
framework/languages/cpp/extensions/framework-locations-redis/include/zlink/locations/redis.hpp
```

Sample/e2e 영향 범위:

```text
framework/languages/cpp/e2e/RegistrationCodec
framework/languages/cpp/e2e/SpotService
framework/languages/cpp/e2e/YieldDispatch
framework/languages/cpp/e2e/DeliveryDispatch
framework/languages/cpp/e2e/ToActorMessaging
framework/languages/cpp/samples/Bingo
framework/languages/cpp/samples/TicTacToe
framework/languages/cpp/samples/DeliveryDispatch
framework/languages/cpp/samples/SupportChat
framework/languages/cpp/samples/GameQuest
framework/languages/cpp/samples/ShoppingMall
```

## C++ E2E/sample gap 제거 연계

이 작업은 `framework/doc/plan/framework-cpp-e2e-sample-gap-closure-plan.ko.md`의 C++ 담당 작업과
같이 진행한다. ref 기반 전송 표면을 바꾸면 공통 E2E와 sample의 public API 사용 예시가 함께 바뀌므로,
아래 항목을 같은 작업 범위 안에서 확인한다.

- `framework/doc/framework/common/e2e`의 config-1~9 scenario가 C++에서 `implemented` 상태로 남아
  있는지 확인한다.
- `framework/doc/framework/common/sample`의 sample 요구사항이 C++ sample inventory와 runner evidence에
  반영되어 있는지 확인한다.
- sample 동작은 `.NET` 구현(`framework/languages/dotnet/samples`)을 포팅 기준으로 삼는다.
- `partial` 또는 `gap` 상태는 완료로 보지 않는다. ref target 통일 때문에 새 public API 공백이 드러나면
  private helper, raw frame, 테스트 전용 adapter로 우회하지 않고 spec/guide/draft 검토 항목으로 분리한다.
- `core/`는 수정하지 않는다. core 버그가 의심되면 C++에서 우회하지 말고 다른 언어 재현 여부를 확인한 뒤
  별도 버그 리포트로 분리한다.

## 구동 순서와 readiness 규칙

서버 역할끼리는 구동 순서에 의존하면 안 된다. 같은 시나리오 안의 서버 A/B, provider/consumer,
play/session/gateway 같은 역할은 어느 쪽이 먼저 떠도 같은 public contract로 수렴해야 한다.
테스트나 runner가 특정 서버를 먼저 띄워야만 통과한다면 완료로 보지 않는다.

클라이언트와 검증 runner는 서버가 준비될 때까지 기다려야 한다. 필요한 대기는 health, port, Redis
location store 전파, target evidence polling처럼 client/runner 쪽 readiness로 표현한다. 서버끼리
임시 peer endpoint를 직접 연결하거나, sleep으로 특정 서버 시작 순서를 보장하거나, private helper로
위치 전파를 우회하지 않는다. zlink 런타임은 서버 간 구동 순서와 무관하게 동작해야 하므로, 실패가
순서에 따라 달라지면 client 대기 조건 또는 public framework 연결을 먼저 점검한다.

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_actor_gateway.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_locations_redis.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp
```

필수 검증:

- `send_to_actor(actor_ref_t, ...)` / `request_to_actor(actor_ref_t, ...)`가 동작한다.
- `send_to(spot_ref_t, ...)` / `request_to(spot_ref_t, ...)`가 동작한다.
- id-only messaging API가 public headers에 없다.
- ref 기반 전송 중 location resolver/store가 호출되지 않는다.
- stale `spot_ref_t` 실패 분류가 기존 계약과 맞다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 C++ 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/cpp
```

사용자-facing 문서에는 `actor_ref_t` / `spot_ref_t` 기반 전송만 남긴다. 내부 row나 store 설명에서
저장된 위치 정보를 말할 때는 "location row"라고 풀어 쓴다.

## 메시지 핸들러 등록 정책 동시 적용

Ref 대상 통일 작업 중 C++ sample이나 E2E의 handler 등록 표면을 고치면
`framework/doc/framework/common/spec/framework-api.ko.md`의 `Handler 등록 정책`도 같은 범위에서
적용한다. C++은 runtime reflection 기반 scan을 전제로 하지 않으므로 다른 언어와 같은 automatic
registration 모양을 억지로 흉내 내지 않는다. 대신 handler 타입에서 알 수 있는 actor 타입, message
타입, request/send/subscription 종류를 호출부에 반복해서 받는 per-handler API를 늘리지 않는다.

sample 정리는 아래 기준으로 함께 진행한다.

- `TicTacToe` C++ sample은 manual handler registration을 보여 주는 예시로 남긴다.
- `Bingo`, `DeliveryDispatch`, `ShoppingMall`, `SupportChat`, `GameQuest` C++ sample은 handler를
  업무 코드에서 직접 나열하는 per-handler manual registration을 제거하고, C++ public builder 또는
  compile-time module 등록 표면으로 모은다.
- C++에서 다른 언어와 같은 automatic registration을 바로 제공할 수 없으면 sample에 임시 helper를 넣지
  말고 public contract gap으로 분리한다.
- README와 guide는 TicTacToe를 수동 등록 예시로, 나머지 sample을 C++에서 제공하는 표준 등록 예시로
  설명한다.

## connection 복구 책임 경계

C++ framework는 이미 core/binding에 넘긴 connection의 복구를 직접 구현하지 않는다. 연결된
connection의 끊김 감지와 reconnect는 core 또는 binding socket option 책임이다. framework는
initial connection readiness 대기, topology handover, stale location 재조회만 수행할 수 있고,
disconnected monitor event 기반 reconnect loop를 만들면 안 된다. readiness 대기는 요청 timeout 안의
bounded polling이어야 하며, payload decode 실패, handler 오류, protocol 오류를 반복 호출로 가리면
안 된다.

감사 대상:

```text
framework/languages/cpp/framework/src/runtime/messaging/submit_queue.cpp
framework/languages/cpp/framework/src/runtime/messaging/submit_queue.hpp
framework/languages/cpp/framework/src/runtime/actors/actor_client.cpp
framework/languages/cpp/framework/include/zlink/framework/contracts/configuration/zlink_builder.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
bindings/cpp/include/
```

처리 기준:

1. `submit_queue_t`가 established connection reconnect를 수행하지 않는지 확인한다. pending submit
   queue가 core/binding `submit_retry`, poller, ready notification과 중복이면 framework 정책을 줄이고
   core/binding 옵션을 전달하는 형태로 바꾼다.
2. `actor_client.cpp`의 stale actor re-resolve retry는 location stale 처리인지 connection reconnect인지
   분리한다. stale location 재조회는 허용되지만, disconnected connection 복구를 framework가 대신하면
   안 된다.
3. `on_retry` hook이 connection reconnect를 framework extension point로 노출하고 있으면 의미를
   재검토한다. channel reliability event 관찰이면 유지 가능하지만, framework reconnect 정책 설정이면
   core/binding 옵션으로 내린다.
4. disconnected monitor event 기반 reconnect loop가 framework runtime에 있으면 제거한다.
5. C++만 별도 reconnect/backpressure semantics를 갖지 않도록 Node/.NET/Java와 비교한다.

완료 보고에는 submit queue 유지 여부, `on_retry` 의미, stale actor retry가 connection recovery가
아니라는 근거 또는 분리한 버그를 포함한다.

## 완료 게이트

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure

rg -n "spot_address_t|resolve_spot_address|resolve_actor_spot_address|send_to_actor\\s*\\([^)]*actor_id|request_to_actor\\s*\\([^)]*actor_id|send_to\\s*\\([^)]*spot_rid|request_to\\s*\\([^)]*spot_rid" \
  framework/languages/cpp \
  -S -g '!**/build/**'

rg -n "spot_address_t|SpotAddress|spot address|SpotRemoteAddress|spot remote address|send_to_actor\\s*\\([^)]*actor_id|request_to_actor\\s*\\([^)]*actor_id|send_to\\s*\\([^)]*spot_rid|request_to\\s*\\([^)]*spot_rid" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/cpp \
  -S -g '!framework/doc/plan/**' -g '!framework/doc/**/draft/**'

rg -n "\\b(partial|gap)\\b|public contract gap" \
  framework/languages/cpp/e2e framework/languages/cpp/samples \
  -g 'feature-map.ko.md' -g 'porting-inventory.ko.md' -g 'sample-porting-inventory.ko.md'

rg -n "reconnect|retry|backoff|submit_queue|on_retry|disconnect.*connect|connect.*disconnect" \
  framework/languages/cpp/framework bindings/cpp/include \
  -S -g '!**/build/**'
```

마지막 `rg`는 완료 전 누락 확인용이다. 결과가 나오면 해당 항목을 구현하거나, public contract 설계가
필요한 별도 항목으로 분리한 뒤 이 worker prompt를 완료 처리하지 않는다.

## 2026-07-08 진행 기록

- C++ public location row/resolver 이름은 `spot_ref_t`, `spot_ref_resolver_t`,
  `resolve_spot_ref`, `resolve_actor_spot_ref`로 바뀌었다.
- C++ actor 전송 public API는 `actor_ref_t`를 받는다. 전송 전에 actor id 문자열만 있을 때는
  public `actor_directory_t`로 ref를 찾는다.
- C++ spot 전송 public API는 `spot_ref_t`를 받는다. route channel 호출부는 node rid와 spot rid를
  낱개로 넘기지 않는다.
- `SpotService`, `YieldDispatch`, `ToActorMessaging`, `DeliveryDispatch`, `GameQuest`,
  `ShoppingMall`, `TicTacToe`의 영향을 받은 C++ 빌드 타깃은 `nice -n 10 -j1`로 통과했다.
- public header grep gate와 `git diff --check`는 통과했다.
- `test_cpp_framework_channel_messaging`의 `hosted-nested` nested request 실패는 수정했다. 이 변경은
  일반 client/server channel request 경로에만 적용한다. route request는 요청마다 dealer/native client를
  만들지 않고 기존 route channel transport를 계속 사용한다.
- `nice -n 10 ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  'test_cpp_framework_channel_messaging$' -j1`는 통과했다.
- 관련 CTest 묶음 `nice -n 10 ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  'test_cpp_framework_(contract_headers|channel_messaging|spot_runtime)|test_cpp_framework_ActorGateway_actor_session_relay'
  -j1`는 통과했다.
- reconnect/retry grep 감사 결과, 이번 작업은 framework-level reconnect loop를 추가하지 않았다.
  `submit_queue_t`는 pending submit 큐이고, `on_retry`는 pending operation hook이며,
  `actor_client.cpp`의 두 번째 submit은 stale actor location 재조회 처리다. route channel의 reconnect
  interval은 zlink socket option 전달로 남긴다.
- ref target 통일 자체의 grep gate, 관련 CTest 묶음, reconnect/retry 감사는 통과했다. 다만 상위
  C++ E2E/sample gap closure 목표의 전체 runner 검증이 남아 있으므로, 상위 계획은 아직 완료가
  아니다.
- 2026-07-08 ResilienceLifecycle 재검증 중 `RL-B5` in-flight drain 실패와 consumer cleanup abort를
  수정했다. weight-only topology 변경은 endpoint removal이 아니므로 client auto connection의 weight만
  갱신하고 native client를 닫지 않는다. endpoint 또는 owner identity가 바뀔 때만 endpoint native
  client를 닫는다. 이 변경은 framework reconnect 기능이 아니라 stale endpoint handover cleanup이다.
- channel server host는 이미 받은 request의 reply를 `routing_id + request_seq`로 flush하고, stop 시
  run thread join 뒤 socket/context를 닫는다. provider cleanup 시 보이던 memory corruption은 socket
  close와 run loop/worker reply가 겹치는 shutdown 순서 문제로 분리했다.
- `RL-A5` flapping down probe는 provider B stop 직후 바로 시작하지 않고 `api-b Ready 0` topology 대기와
  settle 이후 실행한다. server 간 구동 순서를 고정하지 않고 client 검증 시작 전 readiness만 기다린다.
- 검증: `ctest --test-dir framework/languages/cpp/build-redis-vcpkg -R
  '^test_cpp_framework_channel_messaging$' --output-on-failure` 통과.
- 검증: `ZLINK_CPP_E2E_LOCAL_READINESS_TIMEOUT_SECONDS=30 ZLINK_CPP_E2E_SKIP_BUILD=1
  ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build-redis-vcpkg
  ZLINK_REDIS_E2E_ENDPOINT=127.0.0.1:27313
  ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER=zlink-cpp-rl-redis-manual-947211 timeout 900s
  framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh` 통과. 로그:
  `framework/languages/cpp/e2e/ResilienceLifecycle/logs/20260708-210132-952354`.
- 2026-07-08 TicTacToe C++ cleanup crash 조사 중 route client request-reply는 전역 coroutine
  executor를 쓰지 않고 route client state가 소유한 offload executor에서 blocking route wait를 수행하도록
  바꿨다. 요청마다 dealer/native client를 만들지 않고, 기존 route channel/native transport 수명 위에서
  async task 경계만 유지한다.
- `channel_outbound_exchange.cpp`의 submit 오류 처리는 reconnect loop가 아니다. endpoint row가 없거나
  zlink dealer connect 직후 submit 준비가 끝나지 않은 경우만 요청 timeout 안에서 readiness polling을
  수행하고, 그 밖의 submit 오류는 즉시 framework 오류로 반환한다.
- TicTacToe C++ runner는 observer가 반드시 play-b에 붙는다고 가정하지 않는다. client scenario가
  `non_owner_node_rid(room)`로 이미 검증한 `observer-win-milestone=verified` marker를 기준으로 삼아,
  서버 A/B 선택 순서에 따라 실패하지 않게 했다.
- 검증: `ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging
  --output-on-failure` 통과.
- 검증: `TICTACTOE_CPP_KEEP_RUN_DIR=1 TICTACTOE_CPP_REDIS_ENDPOINT=127.0.0.1:60667 timeout 180s
  framework/languages/cpp/samples/TicTacToe/run_sample.sh` 통과. 로그: `/tmp/tmp.eHIt9Ddy5f`.
- 2026-07-08 ShoppingMall cleanup/startup segfault 조사 중 HTTP listener가 Boost.Asio acceptor와
  thread_pool을 stop 시점에 닫으면서 worker thread와 경합할 수 있던 경로를 제거했다. listener transport는
  process가 소유한 fd 기반 synchronous stream으로 바꾸고, HTTP route handler 실행은 listener가 소유한
  lazy offload executor로 보낸다. stop은 open fd를 shutdown해서 blocked read를 깨운 뒤 worker를 drain한다.
  framework는 재연결 loop를 구현하지 않고 zlink socket의 연결 수명 위에서 shutdown 순서만 정리한다.
- Bingo C++ runner는 client 시작 전에 API/Session process가 Play route peer를 발견했다는 evidence를
  기다린다. 이는 request retry가 아니라 client readiness gate이며, server 간 구동 순서는 고정하지 않는다.
- 검증: `cmake --build framework/languages/cpp/build --target
  sample_cpp_framework_bingo_api sample_cpp_framework_bingo_play sample_cpp_framework_bingo_session
  sample_cpp_framework_bingo_client sample_cpp_framework_shoppingmall_order_workflow
  sample_cpp_framework_shoppingmall_commerce_api sample_cpp_framework_shoppingmall_client
  test_cpp_framework_channel_messaging -j 12` 통과.
- 검증: `ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging
  --output-on-failure` 통과.
- 검증: `BINGO_KEEP_RUN_DIR=1 timeout 300s framework/languages/cpp/samples/Bingo/run_sample.sh`
  통과. 로그: `/tmp/tmp.MMH4HMGQ9p`.
- 검증: `SHOPPINGMALL_KEEP_RUN_DIR=1 timeout 300s
  framework/languages/cpp/samples/ShoppingMall/run_sample.sh` 통과. 로그: `/tmp/tmp.WNWtY4s8Bp`.
- 검증: `for i in $(seq 1 5); do SHOPPINGMALL_KEEP_RUN_DIR=1 timeout 300s
  framework/languages/cpp/samples/ShoppingMall/run_sample.sh; done` 5회 연속 통과.
- 검증: `timeout 900s env -u ZLINK_CPP_AUTO_CONNECT_TRACE -u ZLINK_CPP_CHANNEL_TRACE
  CMAKE_BUILD_PARALLEL_LEVEL=12 framework/languages/cpp/samples/run_samples.sh` 통과. 출력은
  `PASS TicTacToe.Cpp`, `bingo full client/server self-check completed`,
  `deliverydispatch sample result=passed`, `PASS SupportChat.Cpp`,
  `PASS GameQuest.Cpp`, `PASS ShoppingMall.Cpp`를 포함한다.
- 검증: aggregate 통과 뒤 `dmesg -T | tail -35`에서 새 `sample_cpp_framework_shoppingmall_*`
  segfault가 보이지 않았다. 직전 실패의 `spot-data` segfault는 aggregate 재실행 통과로 재현되지 않았다.
- 검증: 이번 작업 파일 범위의 `git diff --check` 통과. 전체 worktree에는 다른 작업의 dirty file이 많아
  전체 status는 완료 판단 범위로 사용하지 않는다.
- 2026-07-09 SpotService `SM-B6` stream auth timeout 원인은 retry budget 부족이 아니라 framework
  stream dispatch의 수명 버그였다. `stream_dispatch_context_t`를 handler coroutine에 임시 객체
  reference로 넘겨서, handler가 route request를 `co_await`한 뒤 `dispatch.can_reply()`가 dangling
  reference를 읽었다. framework stream runtime은 dispatch context와 payload를 coroutine frame이 소유하도록
  바꿨고, SpotService session handler는 stream reply `submit()` 결과를 검사한다.
- 검증: `cmake --build framework/languages/cpp/build --target zlink_framework
  zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client -j 20` 통과.
- 검증: `timeout 900s framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B6` 통과. 원래 3초
  stream request timeout으로 되돌린 뒤에도 다시 통과했다.
- 검증: `timeout 900s env -u ZLINK_CPP_AUTO_CONNECT_TRACE -u ZLINK_CPP_CHANNEL_TRACE
  CMAKE_BUILD_PARALLEL_LEVEL=20 framework/languages/cpp/samples/run_samples.sh` 통과. 출력은
  `PASS TicTacToe.Cpp`, `bingo full client/server self-check completed`,
  `deliverydispatch sample result=passed`, `PASS SupportChat.Cpp`, `PASS GameQuest.Cpp`,
  `PASS ShoppingMall.Cpp`를 포함한다.
- 2026-07-09 SpotService full child sweep 재현 중 `SM-B6` 사전 readiness ping이 `errno=113`으로
  실패했다. session node는 play node를 peer-ready로 봤지만 실제 route request가 play node에 도착하지
  않았다. 원인은 route mesh server가 router row와 endpoint 없는 dealer row를 동시에 publish해 공통
  단방향 pairwise initiator 규칙을 깨고 중복 연결과 stale peer-ready를 만들 수 있던 것이다. C++
  auto-connect host는 bind endpoint가 있는 route mesh server에 dealer loop를 만들지 않고, endpoint
  없는 순수 route client만 dealer loop를 쓰도록 수정했다. 이는 retry, sleep, framework reconnect가
  아니라 자동 연결 desired set을 공통 규칙에 맞춘 수정이다.
- 검증: `ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  '^test_cpp_framework_store_location_resolvers$' -j1` 통과. route mesh server가 dealer row를 publish하지
  않는 회귀를 포함한다.
- 검증: `timeout 900s env ZLINK_CPP_E2E_SKIP_BUILD=1
  framework/languages/cpp/e2e/SpotService/run_e2e.sh` 통과
  (`framework/languages/cpp/e2e/SpotService/logs/20260709-092450-3052019`, child 44개).
- 검증: `timeout 900s env ZLINK_CPP_E2E_SKIP_BUILD=1 E2E_START_ORDER=reverse
  framework/languages/cpp/e2e/SpotService/run_e2e.sh` 통과
  (`framework/languages/cpp/e2e/SpotService/logs/20260709-093059-3078532`, child 44개).
- 2026-07-09 TicTacToe bound-session notify 재검증에서 route request reply가 peer 준비 전에 submit되면
  대상 node는 route handler에서 reply를 만들었더라도 호출 node가 reply를 받지 못해 join 또는
  first-move가 timeout될 수 있음을 확인했다. C++ route channel reply path는 새 retry를 만들지 않고
  reply submit 직전에 connected/peer-ready 상태를 요청 timeout 안에서 확인한다.
- 검증: `cmake --build framework/languages/cpp/build --target zlink_framework
  sample_cpp_framework_tictactoe_play sample_cpp_framework_tictactoe_client
  test_cpp_framework_channel_messaging test_cpp_framework_ActorGateway_actor_session_relay -j 20`
  통과.
- 검증: `ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  'test_cpp_framework_(channel_messaging|ActorGateway_actor_session_relay)$' -j1` 통과.
- 검증: `for i in 1 2 3; do TICTACTOE_CPP_STARTUP_SETTLE_SECONDS=0 timeout 300s
  framework/languages/cpp/samples/TicTacToe/run_sample.sh; done` 3회 연속 통과.
