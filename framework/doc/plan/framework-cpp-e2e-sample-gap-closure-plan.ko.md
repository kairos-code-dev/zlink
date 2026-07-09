# C++ Framework E2E/Sample 문서 갭 제거 계획

## 목적

이 문서는 C++ 담당 에이전트가 공통 framework e2e 문서와 공통 sample 문서에 적힌 내용을
`framework/languages/cpp`에 빠짐없이 구현하도록 안내한다.

E2E는 공통 e2e 문서의 모든 scenario를 C++ public framework 표면으로 검증하는 것이 목표다. Sample은
공통 sample 문서를 계약 기준으로 삼고, `.NET` sample 구현을 포팅 기준으로 삼아 C++ sample을 같은
사용자 체감 동작으로 맞춘다.

기존 계획은 아래 문서를 따른다.

- `framework/doc/plan/framework-cpp-e2e-dotnet-porting-plan.ko.md`
- `framework/doc/plan/framework-cpp-sample-dotnet-porting-plan.ko.md`

이 문서는 위 두 계획을 한 작업 흐름으로 묶는 완료 계획이다.

## 담당 범위

- E2E 대상: `framework/languages/cpp/e2e/`
- Sample 대상: `framework/languages/cpp/samples/`
- C++ framework 대상: `framework/languages/cpp/framework/`
- C++ 검증 대상: `framework/languages/cpp/tests/`, `framework/languages/cpp/CMakeLists.txt`
- 공통 E2E 기준: `framework/doc/framework/common/e2e/`
- 공통 sample 기준: `framework/doc/framework/common/sample/`
- `.NET` E2E 기준 구현: `framework/languages/dotnet/e2e/`
- `.NET` sample 기준 구현: `framework/languages/dotnet/samples/`

Core 성능 작업과 충돌하지 않도록 `core/`는 수정하지 않는다. C++ framework에서 발견한 문제가 core
버그로 의심되면 C++만 우회하지 말고, C++/Node/Java/Kotlin 또는 바인딩 수준에서 같은 현상이 재현되는지
확인한 뒤 버그 리포트로 분리한다.

## 버그 처리 원칙

작업 중 버그가 드러나면 scenario나 sample만 통과시키는 우회 코드를 넣지 않는다. 실패 로그, 재현
절차, 영향을 받는 언어와 계층을 먼저 확인하고, 원인이 C++ framework, binding, connector, e2e/sample
harness 중 어디에 있는지 좁힌다.

실제 버그로 확인되면 가능한 범위에서 먼저 회귀테스트를 작성하거나 같은 변경에 포함한다. 그 다음 원인
계층에서 버그를 수정하고, 회귀테스트와 해당 e2e/sample runner를 다시 실행한 뒤 원래 작업을 계속
진행한다. 버그 수정 없이 `sleep`, retry-only wrapper, private helper, raw frame 조작, test-only adapter,
sample 코드 변경으로 실패를 숨기지 않는다.

## 메시지 handler 정책 적용

C++ framework, E2E, sample을 고칠 때는 공통 framework API spec의 메시지 handler 등록 정책을 함께
적용한다.

기준 문서:

- `framework/doc/framework/common/spec/framework-api.ko.md`
  - `3.3 Handler 등록 정책`

적용 원칙:

- handler 등록 호출부에 packet 이름, actor 타입, request/send 종류처럼 handler 타입에서 알 수 있는
  정보를 반복해서 적지 않는다.
- Spot 메시지 handler, actor request/send handler, subscription handler는 같은 등록 원칙을 따른다.
  subscription topic처럼 handler interface만으로 알 수 없는 값은 handler metadata나 명시 override로
  분리하고, 기본 등록 표면을 얕게 만들지 않는다.
- C++는 assembly/package scan이 자연스럽지 않으므로 자동 scan을 억지로 흉내 내지 않는다. 대신
  compile-time 타입과 명시 builder 호출을 기준으로 등록하되, 같은 메시지와 같은 handler 책임,
  같은 중복/충돌 검증 규칙을 유지한다.
- E2E나 sample을 통과시키기 위해 메시지별 codec 등록 함수, raw payload helper, 테스트 전용 adapter를
  handler 등록 표면에 새로 추가하지 않는다.
- 공통 spec이 요구하는 handler 정책을 현재 C++ public API로 표현할 수 없으면 private helper로
  우회하지 말고 `feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 public contract gap으로
  남긴 뒤 설계 검토 항목으로 분리한다.

## ref 기반 전송 대상 통일 병행

C++ gap 제거 작업 중 actor 또는 Spot 전송 표면을 고치면 아래 worker prompt를 같은 작업 흐름에서 함께
완료한다.

- `framework/doc/plan/framework-ref-target-unification-cpp-worker-prompt.ko.md`

이 작업은 actor id 또는 spot id만 받아 메시지를 보내는 public API를 제거하고, 전송 대상은
`actor_ref_t`와 `spot_ref_t`로 통일하는 일이다. id는 조회 입력이고 ref는 전송 입력이라는 원칙을
E2E, sample, C++ framework public header, 문서에 같이 적용한다.

적용 원칙:

- `spot_address_t`, `spot_address_resolver_t`, `resolve_spot_address`,
  `resolve_actor_spot_address` 이름은 각각 `spot_ref_t`, `spot_ref_resolver_t`,
  `resolve_spot_ref`, `resolve_actor_spot_ref`로 바꾼다.
- `actor_client_t`는 actor id 문자열만 받는 send/request API를 public surface에 남기지 않는다.
  actor 전송은 `actor_ref_t`를 받는 API로 표현한다.
- Spot 전송은 node rid와 spot rid를 낱개로 받는 API가 아니라 `spot_ref_t`를 받는 API로 표현한다.
  route client와 spot context 양쪽에서 같은 원칙을 유지한다.
- C++ E2E와 sample은 새 ref 기반 API를 사용하도록 갱신한다. public API 공백이 드러나면 private
  helper, raw frame, 테스트 전용 adapter로 우회하지 않고 public contract 설계 항목으로 분리한다.
- ref 전송 대상 정리 중 handler 등록 표면을 고치면 위 `메시지 handler 정책 적용` 절을 함께 지킨다.
- framework는 이미 core/binding socket에 넘긴 connection의 transport 재연결을 직접 구현하지 않는다.
  stale location 재조회와 topology handover는 구분하되, disconnected event 기반 reconnect loop나
  reconnect timer/backoff를 framework 기능으로 추가하지 않는다.

완료 전에는 worker prompt의 grep gate와 build/CTest를 함께 실행한다. 특히 id-only messaging API,
`spot_address_t` 계열 이름, framework-level reconnect/retry hook이 public surface에 남아 있으면 이
계획은 완료로 보지 않는다.

## 현재 남은 작업

아래 항목은 2026-07-08 현재 C++ 작업을 완료 처리하기 전에 반드시 닫아야 한다.

- `SpotService` `SM-Q9` route readiness는 requester가 manual endpoint connect를 쓰지 않고
  location auto-connect desired set으로 route peer를 붙이도록 고친 뒤 focused shuffle run에서
  통과했다. 다만 이 증거는 focused proof이므로 full child sweep 완료 proof를 대체하지 않는다.
- `SpotService` full child sweep은 non-gdb, 낮은 CPU 부하 상태에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 420s nice -n 10 env ZLINK_CPP_E2E_SKIP_BUILD=1
    framework/languages/cpp/e2e/SpotService/run_e2e.sh` 통과.
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260708-123007-1259169`.
  - 이 run에서 이전 실패 구간인 `SM-A5`, `SM-B5`, `SM-D15`, `SM-Q9`가 모두 통과했다. 중간의
    `Killed` 출력은 scenario 종료 시 runner cleanup이 자신이 띄운 role process를 종료하면서 나온
    메시지이고, 전체 runner exit code는 0이다.
  - 2026-07-08 수정: `SM-D15` gateway actor push 실패 원인은 framework 기본 DI에
    `actor_directory_t` 구현이 등록되지 않은 것이었다. 기본 `actor_directory_t::find()`를 location
    store 기반으로 등록했고, gateway는 actor id 문자열을 직접 전송하지 않고 public `actor_ref_t`를
    찾아 `request_to_actor(actor_ref_t, ...)`를 호출한다.
- `PubSub` full runner는 낮은 CPU 부하 상태에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 420s nice -n 10 framework/languages/cpp/e2e/PubSub/run_e2e.sh`
    통과.
  - 로그: `framework/languages/cpp/e2e/PubSub/logs/20260708-123833-1298240`.
  - 이 run에서 `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1`이 모두 통과했다.
    Subscriber role은 publisher endpoint를 직접 인자로 받지 않고 location store의 publisher peer row를
    발견해 연결한다. 따라서 client는 역할 server가 준비될 때까지 기다리지만, Publisher/Subscriber
    server 사이의 시작 순서를 runner가 고정하지 않는다.
- `RegistrationCodec` full runner는 낮은 CPU 부하 상태에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 420s nice -n 10
    framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh` 통과.
  - 로그: `framework/languages/cpp/e2e/RegistrationCodec/logs/20260708-124643-1329498`.
  - 이 run에서 `RC-A1`, `RC-A2`, `RC-A3`, `RC-A4`, `RC-A5`, `RC-A6`, `RC-B1`,
    `RC-B2`, `RC-B3`, `RC-B4`, `RC-B5`가 모두 통과했다.
- `RegistryMessaging`/Config-1 LocationMessaging full runner는 최신 checkout에서 다시 실행해
  통과했다.
  - 2026-07-08 검증: `timeout 560s framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh all`
    통과.
  - 로그: `framework/languages/cpp/e2e/RegistryMessaging/logs/20260708-131829-51832`.
  - 이 run에서 `RM-A1`, `RM-A2`, `RM-A4`, `RM-A6`, `RM-B1`, `RM-B2`, `RM-C1`,
    `RM-C2`, `RM-C3`, `RM-C4`, `RM-C5`, `RM-C7`, `RM-C8`, `RM-C8-max`, `RM-C9`가
    모두 통과했다. 이전 실행에서 cleanup 시 provider segmentation fault가 한 번 표시됐기 때문에,
    runner가 정상 종료/SIGTERM 외의 provider 비정상 종료를 실패로 드러내도록 보강한 뒤 focused
    `RM-B2`와 full runner를 다시 통과시켰다.
- `ResilienceLifecycle` full runner는 최신 checkout에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 560s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh all`
    통과.
  - 로그: `framework/languages/cpp/e2e/ResilienceLifecycle/logs/20260708-133049-101113`.
  - 이 run에서 Consumer smoke, `RL-A1`, `RL-A2`, `RL-A3`, `RL-A4`, `RL-A5`, `RL-B1`,
    `RL-B2`, `RL-B3`, `RL-B4`, `RL-B5`, `RL-B6`, `RL-C1`, `RL-C2`, `RL-C3`, `RL-C4`,
    `RL-D1`, `RL-D2`, `RL-D3`, `RL-D4`, `RL-D5`가 모두 통과했다. `RL-B2`의 `kill -9`와
    `RL-C2`의 SIGABRT는 시나리오가 의도한 failure injection으로만 허용하고, cleanup 또는 일반
    provider 종료의 비정상 status는 runner 실패로 드러내도록 보강했다.
  - 2026-07-08 재검증: `ZLINK_CPP_E2E_LOCAL_READINESS_TIMEOUT_SECONDS=30
    ZLINK_CPP_E2E_SKIP_BUILD=1 ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build-redis-vcpkg
    ZLINK_REDIS_E2E_ENDPOINT=127.0.0.1:27313
    ZLINK_CPP_E2E_OWNED_REDIS_CONTAINER=zlink-cpp-rl-redis-manual-947211 timeout 900s
    framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh` 통과.
  - 로그: `framework/languages/cpp/e2e/ResilienceLifecycle/logs/20260708-210132-952354`.
  - 이 재검증에서 `RL-B5` in-flight drain, `RL-A5` provider flapping, `RL-C4` Redis outage/recovery,
    `RL-D1/D4/D5`까지 모두 통과했다. 원인 분석 결과 `RL-B5` 실패는 drain weight 0 전파를 endpoint
    removal처럼 처리해 consumer의 해당 endpoint native client를 닫은 것이었다. weight-only 변경은
    auto connection weight 갱신으로만 처리하고, endpoint 또는 owner identity가 바뀔 때만 native client를
    닫는다.
  - cleanup 시 consumer `double free or corruption`은 failure path에서 host service가 socket/context를
    run thread join 전에 닫는 순서와 맞물려 드러났다. channel host stop 순서를 stop flag, run thread
    join, loop resource close 순서로 바꾸고, 이미 받은 request의 reply는 `routing_id + request_seq`로
    flush한다. 이 변경은 framework 재연결 기능이 아니라 in-flight reply ownership과 shutdown 순서 정리다.
  - `RL-A5` down window는 provider B stop 직후 client가 바로 검증을 시작해 stale endpoint를 때릴 수
    있었다. runner가 `wait_location_topology api-b Ready 0`과 settle 대기를 수행한 뒤 down probe를
    시작하도록 바꿨다. 이는 server 간 시작/종료 순서를 고정하는 것이 아니라 client 검증 시작 전
    readiness를 기다리는 규칙이다.
- `RuntimeMonitoring` full runner는 최신 checkout에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 560s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
    통과.
  - 로그: `framework/languages/cpp/e2e/RuntimeMonitoring/logs/20260708-133413-118111`.
  - 이 run에서 `MON-A1`, `MON-A2`, `MON-A3`, `MON-A4`, `MON-A5`, `MON-B1`, `MON-B2`,
    `MON-C1`, `MON-D1`이 모두 통과했다.
- `DiscoveryRegistryHa`/Config-6 StoreFailure full runner는 최신 checkout에서 다시 실행해 통과했다.
  - 2026-07-08 검증: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
    통과.
  - 로그: `framework/languages/cpp/e2e/DiscoveryRegistryHa/logs/20260708-135153-159069`(SF-A1),
    `logs/20260708-135202-159895`(SF-A2), `logs/20260708-135206-160402`(SF-B1),
    `logs/20260708-135216-161152`(SF-B2), `logs/20260708-135231-162310`(SF-C1),
    `logs/20260708-135254-163218`(SF-C2), `logs/20260708-135302-163973`(SF-D1),
    `logs/20260708-135314-164810`(SF-D2), `logs/20260708-135336-165762`(SF-D3),
    `logs/20260708-135342-166331`(SF-E1).
  - runner는 parent run에서 Redis container 하나를 준비하고 child scenario에 endpoint와 container
    이름을 넘겨 Docker port 노출 flake를 줄인다. `SF-C1`과 `SF-D2`의 provider SIGABRT는
    `/admin/crash`로 만든 failure injection으로만 허용하고, cleanup 또는 일반 provider/consumer
    종료의 비정상 status는 실패로 드러낸다. Redis outage 이후 async Redis future가 종료를
    막지 않도록 C++ Redis location store operation은 제한 시간 안에 끝나지 않으면 실패를 반환한다.
- `framework-ref-target-unification-cpp-worker-prompt.ko.md`의 ref 기반 전송 대상 통일 작업을 C++
  E2E/sample 수정과 함께 끝낸다. id-only actor/Spot 전송 API, `spot_address_t` 계열 이름,
  framework-level reconnect/retry hook이 남아 있으면 완료가 아니다.
  - 2026-07-08 진행: C++ public location row/resolver 표면에서 `spot_address_t`를 `spot_ref_t`로,
    `resolve_spot_address`/`resolve_actor_spot_address`를
    `resolve_spot_ref`/`resolve_actor_spot_ref`로 바꿨다. framework/include, framework/src,
    tests, framework 공통/C++ 문서 범위의 `spot_address` gate는 no-hit이다.
  - 2026-07-08 진행: `spot_ref_t`를 전용 lightweight header로 분리했고,
    `spot_context_t::send_to`/`request_to` public 표면을 `spot_ref_t` 입력으로 바꿨다. SpotService
    Play/MultiNode 호출부와 spot runtime unit test도 같은 ref 기반 호출로 갱신했다.
  - 검증 상태: `git diff --check` 통과. `nice -n 10 cmake --build
    framework/languages/cpp/build --target test_cpp_framework_spot_runtime -j1` 통과.
    `nice -n 10 ctest --test-dir framework/languages/cpp/build --output-on-failure -R
    'test_cpp_framework_(contract_headers|spot_runtime)$' -j1` 통과. `nice -n 10 cmake --build
    framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play
    zlink_cpp_e2e_spot_service_multinode zlink_cpp_e2e_spot_service_multinode_requester -j1`
    통과.
  - 2026-07-08 진행: `actor_client_t::send_to_actor`/`request_to_actor` public 표면을
    `actor_ref_t` 입력으로 바꿨다. `ToActorMessaging`, `DeliveryDispatch`, `SpotService Gateway`,
    actor gateway unit test 호출부는 actor id 문자열을 전송 입력으로 넘기지 않고 public
    `actor_directory_t`로 ref를 찾은 뒤 전송한다.
  - 2026-07-08 진행: route `channel.hpp`의 spot-directed public overload를 node rid와 spot rid
    낱개 입력이 아니라 `spot_ref_t` 입력으로 바꿨다. `SpotService Play/MultiNode`,
    `YieldDispatch`, `GameQuest`, `ShoppingMall`, `TicTacToe` 호출부와 channel messaging unit test도
    같은 ref 기반 호출로 갱신했다.
  - 2026-07-08 검증: public header grep gate
    `send_to_actor(...actor_id)`, `request_to_actor(...actor_id)`, `target_spot_rid`,
    `spot_address_t`, `resolve_spot_address`, `resolve_actor_spot_address` no-hit. `git diff --check`
    통과. `nice -n 10 cmake --build framework/languages/cpp/build --target
    zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_gateway
    zlink_cpp_e2e_spot_service_multinode zlink_cpp_e2e_to_actor_messaging_caller
    zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session
    sample_cpp_framework_deliverydispatch_tracking sample_cpp_framework_gamequest_game_api
    sample_cpp_framework_shoppingmall_commerce_api sample_cpp_framework_tictactoe_play
    test_cpp_framework_contract_headers test_cpp_framework_channel_messaging
    test_cpp_framework_actor_gateway test_cpp_framework_spot_runtime -j1` 통과.
  - 2026-07-08 진행: `test_cpp_framework_channel_messaging`의 `hosted-nested` 재진입 request 실패를
    수정했다. 원인은 hosted handler 내부 channel request가 outer request와 같은 channel client
    transport를 공유하면서 client-side mutex 대기와 handler reply 대기가 맞물린 것이다. 이 변경은 일반
    client/server channel 요청 경로에만 적용한다. route request는 요청마다 dealer/native client를
    만들지 않고 기존 route channel transport를 계속 사용한다. orchestrated route test는 production
    retry 없이 client 쪽 route readiness를 먼저 확인하게 했다.
  - 2026-07-08 검증: `nice -n 10 cmake --build framework/languages/cpp/build --target
    test_cpp_framework_channel_messaging -j1` 통과. `nice -n 10 ctest --test-dir
    framework/languages/cpp/build --output-on-failure -R 'test_cpp_framework_channel_messaging$'
    -j1` 통과.
  - 2026-07-08 검증: 관련 CTest 묶음 `nice -n 10 ctest --test-dir
    framework/languages/cpp/build --output-on-failure -R
    'test_cpp_framework_(contract_headers|channel_messaging|spot_runtime)|test_cpp_framework_ActorGateway_actor_session_relay'
    -j1` 통과.
  - 2026-07-08 reconnect/retry 감사: `submit_queue_t`는 pending submit 보관 큐이며 reconnect loop가
    아니다. `on_retry`/`retry_pending`은 pending operation 관찰 hook이고 transport reconnect 정책이
    아니다. `actor_client.cpp`의 두 번째 submit은 stale actor location을 public location store에서
    다시 조회하는 처리이며 disconnected connection 복구 루프가 아니다. `route_channel_host_service`의
    reconnect interval 설정은 zlink socket option 전달이며 framework가 별도 reconnect loop를 돌리는
    구현은 아니다. 이번 `channel_outbound_exchange.cpp` 변경도 request별 native client 분리라서
    reconnect/backoff 기능 추가가 아니다.
- framework 공통 spec의 handler 등록 정책과 transport 재접속 금지 정책을 C++ public surface와
  E2E/sample 수정에 같이 적용한다.

## 완료와 gap 처리 원칙

이 계획의 목표는 문서와 구현 사이의 gap을 없애는 것이다. `partial`이나 `gap` 표기는 작업 중 상태를
보이게 하기 위한 임시 표시일 뿐 완료 판정이 아니다.

공통 e2e 문서나 공통 sample 문서가 요구하는 공개 동작인데 C++에서 바로 구현할 수 없으면, 먼저
`feature-map.ko.md`나 `sample-porting-inventory.ko.md`에 이유를 적고 설계 이슈로 분리한다. 그 뒤
필요한 spec/guide/draft 검토와 public API 설계를 거쳐 다시 구현해야 한다. 설계 이슈로 분리했다는
사실만으로 이 계획을 완료 처리하지 않는다.

## E2E 구현 절차

1. `framework/doc/framework/common/e2e/README.ko.md`와 `config-1`부터 `config-9`까지 모든 문서를
   읽고 scenario ID를 표로 만든다.
2. 각 config마다 `.NET` 기준 구현과 `.NET` `feature-map.ko.md`를 읽는다.
3. C++의 `porting-inventory.ko.md`와 `feature-map.ko.md`를 먼저 갱신한다.
   - 공통 문서의 scenario ID를 모두 행으로 둔다.
   - 공통 문서의 scenario 상태는 `implemented`, `partial`, `gap` 중 하나로 적는다.
   - `partial`과 `gap`은 이유, 필요한 public API, 막힌 계층을 함께 적는다.
   - `.NET` 파일이나 기존 C++ 파일을 inventory에서 매핑할 때만 `merged`, `stale`, `not needed` 같은
     보조 상태를 쓸 수 있다. 공통 scenario 자체를 `not applicable`로 닫지 않는다.
4. C++ public framework API로 구현 가능한 항목은 실제 역할 프로세스, runner, scenario evidence까지
   구현한다.
5. public API가 없어 구현할 수 없는 항목은 private helper, raw frame, 테스트 전용 adapter로 우회하지
   않는다. 문서에 gap으로 남기고 설계 검토 항목으로 분리한다.
6. 각 config의 `run_e2e.sh`는 standalone으로 실행 가능해야 하며, 성공 시 명확한 최종 pass marker를
   출력해야 한다.
7. config 하나가 끝날 때마다 빌드, runner, feature-map, inventory를 맞춘 뒤 다음 config로 넘어간다.

필수 config 목록:

- `LocationMessaging` 또는 현재 C++ 트리에서 같은 의미로 매핑되는 config
- `SpotService`
- `PubSub`
- `RegistrationCodec`
- `ResilienceLifecycle`
- `StoreFailure` 또는 C++에서 같은 의미로 명명된 store failure/recovery config
- `RuntimeMonitoring`
- `YieldDispatch`
- `ToActorMessaging`

현재 트리에 `.NET` 기준 이름과 다른 `RegistryMessaging`, `DiscoveryRegistryHa`, `DeliveryDispatch`,
`Monitoring` 같은 디렉터리가 있으면 바로 삭제하거나 완료로 인정하지 않는다. 먼저 공통 config 문서와
`.NET` config에 어느 scenario가 대응되는지 inventory에 매핑하고, 중복·stale·rename 대상 여부를
리뷰로 확인한 뒤 정리한다.

## Sample 포팅 절차

1. `framework/doc/framework/common/sample/README.ko.md`와 sample별 문서를 모두 읽는다.
   - event sample은 `framework/doc/framework/common/sample/event/*.ko.md`도 함께 읽는다.
2. `.NET` sample 6종의 실제 코드, runner, README를 읽고 C++ sample에 대응시킨다.
3. 각 C++ sample에 `sample-porting-inventory.ko.md`를 유지한다.
   - `.NET`의 역할, shared contract, client self-check, runner evidence를 빠짐없이 매핑한다.
   - C++ idiom 때문에 파일명이나 클래스 구조가 달라도 책임은 누락하지 않는다.
4. Sample 코드는 사용자가 따라 할 public API 예시다. 내부 helper, raw buffer 처리, codec 수동 우회,
   테스트 전용 hook을 sample 코드에 넣지 않는다.
5. 각 sample의 `run_sample.sh`는 standalone으로 실행 가능해야 하며, client success뿐 아니라 서버 역할
   로그와 scenario evidence도 확인해야 한다.
6. 모든 sample이 끝난 뒤 `framework/languages/cpp/samples/run_samples.sh`를 실행한다.

필수 sample 목록:

- `TicTacToe`
- `Bingo`
- `DeliveryDispatch`
- `SupportChat`
- `GameQuest`
- `ShoppingMall`

## 검증 명령

담당 에이전트는 실제 checkout 상태에 맞게 target을 확인한 뒤 실행한다.

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
for f in framework/languages/cpp/e2e/*/run_e2e.sh; do timeout 420s "$f"; done
timeout 900s framework/languages/cpp/samples/run_samples.sh
```

실행 환경이나 port 충돌 때문에 전체 루프가 실패하면 실패 config/sample을 먼저 단독 재현하고, 단독 pass
후 전체 루프를 다시 실행한다.

## 완료 전 누락 리뷰

구현 담당 에이전트가 완료를 주장하기 전에 별도 Codex 에이전트로 read-only 리뷰를 요청한다.

리뷰 요청은 아래 범위를 포함해야 한다.

- 공통 e2e 문서의 모든 scenario ID가 C++ `feature-map.ko.md`와 runner evidence에 존재하는지
- 공통 sample 문서의 모든 역할, 메시지 흐름, self-check가 C++ sample inventory와 runner evidence에
  존재하는지
- `.NET` 기준 구현에 있는 역할과 client 검증이 C++에서 누락되지 않았는지
- public contract gap을 private helper나 테스트 전용 adapter로 숨기지 않았는지
- `run_e2e.sh`, `run_sample.sh`, `run_samples.sh`, CTest 결과가 실제로 pass했는지

리뷰 결과가 `NO MISSING CPP ITEMS`가 아니면 모든 finding을 수정하고 같은 리뷰를 다시 요청한다.

## POSD/DDD 반복 리뷰

누락 리뷰가 깨끗해진 뒤에만 별도 Codex 에이전트로 POSD/DDD 리뷰를 요청한다. 이 리뷰는 동작 누락이
아니라 구조 개선 가능성만 본다.

리뷰 기준:

- public API가 shallow wrapper로 늘어나지 않았는지
- codec, transport, registry, location store, actor/session lifecycle 같은 지식이 호출자나 sample로
  새어나오지 않았는지
- domain role과 infrastructure 책임이 섞이지 않았는지
- handler, runtime, runner, sample 사이에 같은 정책이 반복 구현되지 않았는지
- C++ idiom을 따르면서도 `.NET` 기준 domain 흐름과 같은 의미를 유지하는지

의미 있는 refactoring finding이 나오면 구현, 테스트, 문서 갱신을 한 뒤 E2E/sample 검증과 POSD/DDD
리뷰를 다시 실행한다. 리뷰가 `NO POSD/DDD CPP REFACTOR ITEMS`를 반환할 때 종료한다.

## 최종 종료 조건

- 모든 공통 E2E scenario가 C++에서 `implemented`로 남아 있다.
- 공통 sample 문서와 `.NET` sample 기준에 대해 C++ sample gap이 없다.
- `partial` 또는 `gap`으로 남은 E2E/sample 항목이 없다. public contract 설계가 필요한 항목이 있으면
  이 계획은 완료가 아니라 blocked 상태로 남긴다.
- 모든 C++ E2E runner와 sample runner가 pass했다.
- 누락 리뷰가 `NO MISSING CPP ITEMS`를 반환했다.
- POSD/DDD 반복 리뷰가 `NO POSD/DDD CPP REFACTOR ITEMS`를 반환했다.

## 2026-07-08 진행 기록

- C++ route request 경로는 요청마다 dealer/native client를 새로 만들지 않는다. 기존 route channel/native
  transport 위에서 route client state가 소유한 offload executor만 사용해 blocking reply wait를 분리했다.
- framework는 재연결 loop, backoff timer, retry policy를 구현하지 않는다. endpoint readiness 또는 zlink
  dealer submit 준비가 아직 끝나지 않은 경우만 요청 timeout 안에서 readiness를 기다리고, protocol/handler
  오류는 retry로 숨기지 않는다.
- Bingo sample runner는 client 시작 전에 API/Session process가 Play route peer를 발견했다는 evidence를
  기다리도록 보강했다. 이는 client readiness gate이며 server 간 시작 순서를 고정하지 않는다.
- ShoppingMall sample의 provider cleanup/startup segfault 조사는 HTTP listener shutdown 경합으로 좁혀
  수정했다. listener transport를 fd 기반 synchronous stream으로 바꾸고, stop 시 open fd shutdown 뒤
  worker drain을 수행한다.
- 검증:
  `ctest --test-dir framework/languages/cpp/build -R test_cpp_framework_channel_messaging --output-on-failure`
  통과.
- 검증:
  `BINGO_KEEP_RUN_DIR=1 timeout 300s framework/languages/cpp/samples/Bingo/run_sample.sh` 통과.
- 검증:
  `SHOPPINGMALL_KEEP_RUN_DIR=1 timeout 300s framework/languages/cpp/samples/ShoppingMall/run_sample.sh`
  통과, 이후 같은 명령 5회 반복 통과.
- 검증:
  `timeout 900s env -u ZLINK_CPP_AUTO_CONNECT_TRACE -u ZLINK_CPP_CHANNEL_TRACE
  CMAKE_BUILD_PARALLEL_LEVEL=12 framework/languages/cpp/samples/run_samples.sh` 통과. 출력은
  `PASS TicTacToe.Cpp`, `bingo full client/server self-check completed`,
  `deliverydispatch sample result=passed`, `PASS SupportChat.Cpp`, `PASS GameQuest.Cpp`,
  `PASS ShoppingMall.Cpp`를 포함한다.
- aggregate 통과 뒤 `dmesg -T | tail -35`에서 새 ShoppingMall segfault가 보이지 않았다.
- 아직 최종 종료 조건 중 POSD/DDD 반복 리뷰는 남아 있다.
- 2026-07-09 SpotService `SM-B6` stream auth timeout은 framework stream dispatch가 coroutine handler에
  넘긴 dispatch context 임시 객체의 수명을 보장하지 못해서 발생했다. handler가 route request를
  `co_await`한 뒤 `dispatch.can_reply()`가 잘못된 참조를 읽었다. stream runtime은 context/payload를
  dispatch coroutine이 소유하도록 수정했고, session handler는 stream reply submit 실패를 무시하지
  않는다.
- 검증:
  `cmake --build framework/languages/cpp/build --target zlink_framework
  zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client -j 20` 통과.
- 검증:
  `timeout 900s framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B6` 통과. stream request timeout을
  원래 3초로 되돌린 뒤에도 통과했다.
- 검증:
  별도 Redis 조건에서 `SM-B1 SM-B2 SM-B3 SM-B5 SM-B6 SM-B8 SM-B9 SM-D1 SM-D4` 3회 반복 통과.
- 검증:
  `timeout 900s env -u ZLINK_CPP_AUTO_CONNECT_TRACE -u ZLINK_CPP_CHANNEL_TRACE
  CMAKE_BUILD_PARALLEL_LEVEL=20 framework/languages/cpp/samples/run_samples.sh` 통과. 출력은
  `PASS TicTacToe.Cpp`, `bingo full client/server self-check completed`,
  `deliverydispatch sample result=passed`, `PASS SupportChat.Cpp`, `PASS GameQuest.Cpp`,
  `PASS ShoppingMall.Cpp`를 포함한다.
- 2026-07-09 재검증 전에는 SpotService full child sweep의 shared Redis/연속 child 실행 조건에서
  play/session process가 시작 직후 segmentation fault를 내거나 HTTP health를 열지 못하는
  startup/cleanup flake가 남아 있었다. 이후 재현 과정에서 `SM-B6` 사전 readiness ping이
  `session-a -> play-a` route request를 계속 보내지만 play-a가 실제 request를 받지 못하는 상태를
  확인했다. 원인은 route mesh server가 router row와 endpoint 없는 dealer row를 동시에 publish해
  중복 연결과 stale peer-ready를 만들 수 있던 것이다.
- 수정: route mesh에서 bind endpoint가 있는 server는 router row의 pairwise initiator 연결만 사용하고,
  bind endpoint가 없는 순수 route client만 dealer loop를 사용한다. 이 변경은 framework reconnect나
  request retry가 아니며, server 간 구동 순서 의존을 만들지 않고 자동 연결 desired set을 공통
  단방향 규칙에 맞춘 것이다.
- 검증: `ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  '^test_cpp_framework_store_location_resolvers$' -j1` 통과. 이 테스트는 route mesh server가 dealer
  row를 publish하지 않는 회귀를 포함한다.
- 검증: `timeout 900s env ZLINK_CPP_E2E_SKIP_BUILD=1
  framework/languages/cpp/e2e/SpotService/run_e2e.sh` 통과. parent 로그:
  `framework/languages/cpp/e2e/SpotService/logs/20260709-092450-3052019`, child 44개.
- 검증: `timeout 900s env ZLINK_CPP_E2E_SKIP_BUILD=1 E2E_START_ORDER=reverse
  framework/languages/cpp/e2e/SpotService/run_e2e.sh` 통과. parent 로그:
  `framework/languages/cpp/e2e/SpotService/logs/20260709-093059-3078532`, child 44개.
- 2026-07-09 TicTacToe bound-session notify 재검증 중 route request reply가 peer 준비 전에 submit되면
  대상 node가 `replied`까지 만들고도 호출 node가 reply를 받지 못해 join/first-move가 timeout될 수
  있음을 확인했다. route reply 경로는 새 request retry를 만들지 않고, reply를 한 번 submit하기 전에
  같은 route channel의 connected/peer-ready 상태를 요청 timeout 안에서 확인하도록 보강했다.
- remote actor bound-session sink는 요청마다 dealer나 native client를 만들지 않는다. 기존 route
  channel/native Spot route transport 위에서 sink를 유지하며, framework reconnect loop나 backoff
  timer를 추가하지 않는다.
- 검증: `ctest --test-dir framework/languages/cpp/build --output-on-failure -R
  'test_cpp_framework_(channel_messaging|ActorGateway_actor_session_relay)$' -j1` 통과.
- 검증: `for i in 1 2 3; do TICTACTOE_CPP_STARTUP_SETTLE_SECONDS=0 timeout 300s
  framework/languages/cpp/samples/TicTacToe/run_sample.sh; done` 3회 연속 통과. 각 실행은
  `PASS TicTacToe.Cpp`와 `tictactoe full client/server self-check completed`를 출력했다.

## 2026-07-09 추가 진행 기록

- `SM-D14` TLS stream flake는 retry 부족이 아니라 fd-backed TLS stream의 read/write 소유권 문제였다.
  connector의 post-connect request는 이미 read pump가 돌고 있으면 같은 pump로 수신을 이어가고, stream
  host의 TLS connection도 TCP처럼 즉시 writer 경로를 사용하도록 수정했다. TLS/raw port probe는
  runner에서 제거했다.
- 검증: `cmake --build framework/languages/cpp/build --target zlink_framework
  zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client -j 20` 통과.
- 검증: `SM-D14` reverse order 12회 반복 통과. 로그는
  `framework/languages/cpp/e2e/SpotService/logs/20260709-052159-2443250`부터
  `framework/languages/cpp/e2e/SpotService/logs/20260709-052248-2445901`까지다.
- 검증: SpotService full forward run 통과
  (`framework/languages/cpp/e2e/SpotService/logs/20260709-052259-2446202`), full reverse run 통과
  (`framework/languages/cpp/e2e/SpotService/logs/20260709-052822-2460179`). reverse run의 cleanup
  시점에 core `msg.hpp:116` assertion이 한 번 출력됐지만 `SM-D15` scenario 자체는 통과했다. 이
  항목은 framework scenario retry로 덮지 않고 core cleanup/assertion 조사 항목으로 분리한다.
- fd-backed HTTPS listener는 accept 뒤 TLS stream을 실제 request pipeline에 넘기지 않고 닫는 경로가
  있었다. accepted fd를 `tcp::socket`에 assign한 뒤 TLS handshake, request 처리, shutdown을 수행하도록
  수정했다.
- handler registry와 stream session dispatch는 Boost coroutine executor 수명에 cleanup 시점이 묶여
  있었다. handler 실행과 stream dispatch를 framework offload executor로 옮기고, stream relay dispatch
  context는 inline 실행으로 보존했다.
- TicTacToe bound-session notify 실패는 sample이 send 결과를 무시해서 숨겨지던 route sink 실패였다.
  sample은 bound-session `submit()` 실패를 즉시 드러내도록 바꿨고, implicit Spot route channel이
  router bind/connect와 peer routing id를 함께 등록하도록 수정했다. 이 peer 연결은 내부 helper로
  등록하며 C++ public `route_channel_builder_t`에 새 peer-rid `connect` overload를 추가하지 않는다.
  route request는 기존 route channel/native transport를 사용하고, 요청마다 dealer나 native client를
  만들지 않는다.
- readiness 대기는 `RouteNotConnected`인 경우에만 요청 timeout 안에서 끝난다. payload decode 실패,
  handler 오류, protocol 오류, request 실패는 반복 호출로 숨기지 않는다. 이 대기는 zlink socket의
  reconnect 기능을 framework가 대체하는 것이 아니라, startup 직후 route submit 준비가 수렴할 시간을
  주는 client-side readiness 대기다.
- 검증: `cmake --build framework/languages/cpp/build --target zlink_framework
  sample_cpp_framework_tictactoe_play sample_cpp_framework_tictactoe_api
  sample_cpp_framework_tictactoe_client -j 20 && TICTACTOE_CPP_KEEP_RUN_DIR=1 timeout 300s
  framework/languages/cpp/samples/TicTacToe/run_sample.sh` 통과. 로그: `/tmp/tmp.XziUXFCWw5`.
- 검증: `scripts/local-package/README.ko.md`의 local package 방식에 따라
  `.artifacts/wsl/build/framework-cpp-local-package-tests` build와
  `ctest --test-dir .artifacts/wsl/build/framework-cpp-local-package-tests -L framework-unit
  --output-on-failure`를 실행했고 20/20 통과했다.
- 2026-07-09 SpotService full sweep 재현 중 `SM-B6` readiness가 `errno=113`으로 실패했다. trace에서
  session node는 play node를 peer-ready로 봤지만 실제 request는 play node에 도착하지 않았다. 원인은
  route mesh server가 router 역할과 endpoint 없는 dealer 역할을 함께 publish해 pairwise initiator
  규칙을 깨고 중복 연결을 만들 수 있던 것이다. C++ auto-connect host는 bind endpoint가 있는 route
  mesh server에 dealer loop를 만들지 않도록 수정했고, endpoint 없는 순수 route client만 dealer loop를
  유지한다. 검증은 store location resolver unit, SpotService `SM-B6` focused, full forward/reverse
  sweep을 모두 통과했다.
