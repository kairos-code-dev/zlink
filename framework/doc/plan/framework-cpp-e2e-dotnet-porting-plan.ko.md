# C++ Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/cpp/e2e`를 같은 폴더 형태, 역할 분리, 파일 분류, 실행 검증 수준으로 다시
정렬하는 절차를 정의한다.

C++ e2e는 기존 파일을 단순히 보존하면서 이름만 바꾸는 작업으로 보지 않는다. 목표는 `.NET` e2e의
config, role, shared contract, client scenario, support, run script 구조를 C++에 같은 의미로 포팅하는
것이다. 기존 C++ e2e에 일부 config가 없거나 통합 파일로 남아 있으면, `.NET` 기준 구조에 맞춰 다시
나눈다.

## 완료 기준

1. `framework/languages/cpp/e2e/<Config>/`가 대응하는 `.NET` config와 같은 의미의 구조를 가진다.
2. `.NET`의 `Client/`, `Server/`, `Shared/`, `Client/Scenarios/`, `Client/Support/`,
   `Server/<Role>/...` 분리가 C++에도 대응된다.
3. `.NET`에 있는 scenario, role, shared message, support 책임이 빠지지 않는다.
4. C++ public framework API로 구현할 수 없는 항목은 내부 helper, raw frame 조작, 테스트 전용 adapter로
   메우지 않고 `feature-map.ko.md`에 gap으로 남긴다.
5. 포팅 중 버그가 발생하면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 원인을 public runtime,
   framework, stream connector, zlink http client, e2e harness 중 책임 계층까지 추적하고, 같은 문제가
   다시 생기지 않도록 회귀 테스트를 먼저 추가하거나 함께 추가한 뒤 수정한다.
6. 한 config의 구현, 빌드, `run_e2e.sh` 실행, feature-map 갱신, Codex 에이전트 리뷰가 끝나기 전에는
   다음 config를 시작하지 않는다.
7. Codex 에이전트 리뷰에서 이슈 없음이 나오기 전에는 해당 config를 완료로 보지 않는다.

## 기준

작업할 때는 아래 순서로 확인한다.

1. 공통 e2e 문서:
   - `framework/doc/framework/common/e2e/README.ko.md`
   - `framework/doc/framework/common/e2e/config-*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/e2e/<Config>/`
   - `framework/languages/dotnet/e2e/<Config>/feature-map.ko.md`
3. C++ framework public surface:
   - `framework/languages/cpp/framework/include/`
   - `framework/doc/framework/cpp/`
   - `framework/languages/cpp/tests/`
4. C++ e2e 대상:
   - `framework/languages/cpp/e2e/<Config>/`

공통 e2e 문서는 검증 기준이고, 새 public API 추가 근거가 아니다. `.NET`에 기능이 있어도 C++ spec 또는
공통 framework 계약에 근거가 없으면 바로 public API를 추가하지 않는다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 C++에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, C++에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 현재 C++ 작업물 처리 원칙

C++ e2e에는 이미 상당 부분 진행된 config가 있다. 이 작업물은 무조건 삭제하거나 무조건 이어 쓰는
기준이 아니라, `.NET` e2e inventory와 공통 e2e 문서에 맞춰 config별로 검토할 대상이다. 작업자는 먼저
현재 C++ 파일이 어떤 `.NET` 파일과 scenario에 대응하는지 표로 확인한 뒤, 유지할 파일과 새로 작성할
파일을 나눈다.

현재 상태:

- `framework/languages/cpp/e2e`의 기존 파일은 삭제 기준이 아니라 검토 입력이다.
- 기존 C++ e2e가 `.NET`의 폴더 구조, 파일 분류, scenario 분류와 맞으면 유지하고 필요한 부분만 보강한다.
- 기존 C++ e2e가 `.NET` 기준과 다르면 해당 파일을 그대로 옮기지 말고 `porting-inventory.ko.md`에
  불일치 사유와 목표 위치를 먼저 기록한다.
- 새 작업은 항상 `framework/languages/dotnet/e2e/<Config>`와 공통 e2e 문서에서 inventory를 만든 뒤
  시작한다.

판단:

- 기존 C++ e2e를 그대로 완료로 인정하면 누락 scenario나 stale 분류를 놓칠 수 있다.
- 반대로 잘 진행된 C++ 작업물을 삭제하고 처음부터 다시 작성하면 이미 해결된 C++ 런타임 연결, build/run
  script, scenario 구현을 잃을 수 있다.
- 따라서 C++은 config별로 **기존 작업물 보존을 기본값**으로 두고, `.NET` 기준 inventory에서 불일치가
  확인된 파일만 이동, 재작성, 삭제한다.
- config별 첫 산출물은 `porting-inventory.ko.md`다. 이 파일에서 기존 C++ 파일의 유지, 이동, 재작성,
  삭제 판단을 먼저 끝낸 뒤 코드 변경을 시작한다.

1차 분류:

| config | 현재 판단 | 작업 원칙 |
|--------|-----------|-----------|
| `RegistryMessaging` | 완료 | `.NET`의 `Client/Scenarios`, `Server/<Role>`, `Shared` 분류에 맞춘 C++ role/source와 inventory가 있다. `timeout 420s framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh all` 최신 통과 증거는 `logs/20260701-141526-48855`이고, RM-C9 focused 통과 증거는 `logs/20260701-141721-60851`이다. RM-C9는 public one-way send 계약에 맞춰 send pressure/recovery를 검증한다. bounded send failure oracle은 공통 E2E 기대에서 제거했다. |
| `SpotService` | 완료 | `.NET` 기준 inventory와 C++ scenario header/role source가 있다. `Client/Support/client_support.hpp`, `Client/Support/client_options.hpp`, `Client/Support/spot_lifecycle_order_context.hpp`를 추가해 client 공통 env/assert/stream helper, option 값, channel handler state, `.NET`식 lifecycle context를 `Client/main.cpp`에서 분리했고, `Shared/spot_service_contracts.hpp`에 generic JSON stream payload hook을 추가해 stream connector DTO가 기본 JSON codec 경로를 쓰게 했다. `SM-A5`는 `.NET`의 app-level `ScenarioStage` 의미를 C++ user spot handler와 public timer API 위에 구현했다. `SM-B9`는 user spot admission 허용/거부를 public actor join 결과로 검증하고, `SM-C5`는 cross-node SpotMesh publish를 target spot subscriber evidence로 확인한다. `SM-D15`는 gateway HTTP endpoint가 public actor client로 actor handler를 호출하고 bound-session push를 stream client가 받는 경로를 검증한다. `SM-F3`/`SM-F5`는 `.NET`에 별도 scenario 파일은 없지만 공통 E2E와 feature-map에 있는 scenario ID라 `Client/Scenarios/sm_f3_scenario.hpp`, `Client/Scenarios/sm_f5_scenario.hpp`로 분리하고 focused runner로 검증했다. `SM-F6`는 RouteMesh를 끈 MultiNode SpotMesh-only role에서 서버 간 구동 순서와 무관하게 client readiness 뒤 remote spot request/send와 actor join을 검증하며, `E2E_START_ORDER=reverse` focused runtime proof를 남겼다. `SM-Q9`는 MultiNode A/B role과 외부 route client target spot request로 focused runtime proof를 추가했고 scenario/evidence marker를 남긴다. `.NET` SM-D2처럼 `session-a`에서 `play-b`로 가는 stream relay readiness를 확인하고, crash setup/recover route endpoint를 독립시켰다. `SM-B8`은 stream auth 뒤 public actor destroy를 호출하고 destroy evidence와 post-destroy request failure를 확인한다. `SM-G1`은 `.NET`처럼 `session-a`/`session-b`를 각각 `play-a`/`play-b`에 bind한 뒤 `play-a` crash, `play-b` survivor ping, `play-b` recovery rebind를 검증한다. `SM-G3`는 `.NET`처럼 두 stream client를 먼저 순차 auth/bind한 뒤 ping/leave만 동시에 실행하도록 맞췄다. Play/Session role은 runner가 넘긴 local route endpoint를 명시적으로 연결한다. Full `all` runner는 낡은 monolith client 대신 `.NET`처럼 focused scenario child sweep로 실행한다. 최신 full 통과 증거는 `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build ZLINK_CPP_E2E_SKIP_BUILD=1 timeout 1200s framework/languages/cpp/e2e/SpotService/run_e2e.sh all`, 로그 `logs/20260701-183404-20982`이다. 이 실행은 child retry 없이 통과했고, route/control readiness는 기본 3초 settle 뒤 단일 3초 probe로 검증한다. SpotService target은 `framework/src` 내부 include 없이 build된다. |
| `PubSub` | 완료 | `.NET`의 publisher, registry, subscriber 역할에 대응하는 C++ 전용 executable과 Client/Scenarios, Client/Support, Shared contract가 있다. subscriber role server의 `/evidence/wait` endpoint로 PS-A1, PS-A2, PS-A3, PS-A4, PS-B1, PS-B2, PS-C1을 검증한다. registry/publisher role은 `/health`, `/evidence`, `/evidence/clear`, `/shutdown` operational endpoint를 제공하고 runner가 `verify.log`와 operational log/final snapshot을 남긴다. `timeout 420s framework/languages/cpp/e2e/PubSub/run_e2e.sh all` 최신 통과 증거는 `logs/20260701-170557-98147`이다. |
| `RegistrationCodec` | 완료 | `.NET` 기준 inventory와 C++ client scenario/support, server configuration/handler/endpoint/support, invalid role, JSON-only peer role 분리가 있다. `timeout 420s framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh` 최신 통과 증거는 `logs/20260707-134236-1993396`이며 RC-A1, RC-A2, RC-A3, RC-A4, RC-A5, RC-A6, RC-B1, RC-B2, RC-B3, RC-B4, RC-B5를 검증한다. 최종 `registration-codec e2e result=passed` marker는 RC-A6 invalid startup checks까지 끝난 뒤 runner가 한 번만 출력한다. RC-A2는 C++에서 handler 타입의 `request_type`/`message_type`, `topic_name`, DTO의 `packet_name` metadata로 공통 spec의 annotation 의미인 packet kind/name override를 검증한다. |
| `DeliveryDispatch` | 완료 | 기존 `DiscoveryRegistryHa` harness 대신 `.NET DeliveryDispatch` 샘플 기준 C++ E2E가 있다. registry, dispatch API, dispatch center, customer gateway, courier session, courier gateway, courier actor node 1/2, tracking, probe, client target을 실행하고, customer stream 1개와 courier stream 2개로 `SubscribeDelivery`, actor-bound `DeliveryStatusNotify`, `BindCourierSessionReq`, `BindCourierReq`, `EnsureCourierActorReq`, actor-bound `OfferDeliveryNotify`, `CourierDecisionMsg` 흐름을 검증한다. 최신 통과 증거는 `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`, 로그 `logs/last-run`, `logs/flow-*.log`이며 output은 `delivery-dispatch e2e result=passed`다. 이 실행은 successful delivery, reassignment, server evidence self-check, customer/courier gateway/session/actor-node message-flow evidence를 검증한다. CustomerGateway와 CourierSession은 public actor/session API로 stream을 actor에 bind하고 bound session으로 status/offer를 push한다. |
| `StoreFailure` (`DiscoveryRegistryHa`) | 완료 | 디렉터리 이름은 아직 `DiscoveryRegistryHa`이지만 `.NET StoreFailure`와 공통 config-6 기준으로 Redis location store 장애/복구 검증을 수행한다. C++ provider 2개, consumer 1개, Redis container, client target을 실행하며 SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3, SF-E1을 모두 검증한다. SF-E1은 consumer process의 Redis location store 호출에 E2E 전용 delay wrapper로 store 응답 지연을 주입하고, 같은 process의 무관 application request p99가 baseline budget 안에 남는지 확인한다. 최신 통과 증거는 `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`이며 로그는 `logs/20260707-190210-3183591`(SF-A1), `logs/20260707-190227-3185256`(SF-A2), `logs/20260707-190236-3186388`(SF-B1), `logs/20260707-190302-3188778`(SF-B2), `logs/20260707-190332-3191759`(SF-C1), `logs/20260707-190403-3194237`(SF-C2), `logs/20260707-190417-3195710`(SF-D1), `logs/20260707-190453-3199077`(SF-D2), `logs/20260707-190530-3201684`(SF-D3), `logs/20260707-190636-3206735`(SF-E1)이다. |
| `ResilienceLifecycle` | 완료 | 전용 Registry, Provider, Consumer, Client target과 `.NET` 기준 inventory가 있다. Client support를 `client_options.hpp`, `scenario_assert.hpp`, `lifecycle_api_result.hpp`, `topology_entry_result.hpp`로 분리했고 profile request/reply/send/failure/status DTO는 `.NET`식 marker 필드를 지원한다. Shared message file, namespace, handler group, channel 이름은 ResilienceLifecycle 전용 이름으로 정리했다. Registry host wiring은 `registry_host_factory.hpp`로 분리했고 Registry evidence store와 fault state를 `Infrastructure/evidence_store.hpp`, `Infrastructure/fault_state.hpp`로 뒀으며, 선택적 registry channel endpoint가 설정되면 `Handlers/registry_handlers.hpp`의 profile handler와 dispatch error observer를 설치한다. Registry endpoint는 topology wait와 shutdown도 제공한다. Provider host wiring은 `provider_host_factory.hpp`로 분리했고 Provider admin endpoint는 `.NET`과 같은 shutdown/crash/drain/restore/weight/wait 이름을 제공한다. Consumer host wiring도 `consumer_host_factory.hpp`로 분리했다. Consumer role main은 ResilienceLifecycle 전용 `Configuration/consumer_options.hpp`, `Endpoints/consumer_endpoints.hpp` wrapper를 사용하며, Consumer endpoint handler는 marker를 보존하는 ResilienceLifecycle contract로 분리했다. Consumer option reader도 RegistryMessaging alias 없이 전용 파일에서 읽는다. Provider evidence store는 `Infrastructure/evidence_store.hpp`로, Provider dispatch error observer는 `Handlers/evidence_dispatch_error_observer.hpp`로, observer/gray fault mode state는 `Infrastructure/fault_state.hpp`로 분리했다. RL-A4는 전용 `rl_a4_drain_and_green_endpoint_scenario.hpp` 안에서 green provider endpoint 시작, original provider shutdown, topology Ready 1, green evidence, original provider 복구를 `.NET`처럼 검증한다. RL-A3는 전용 `rl_a3_reconnect_storm_scenario.hpp` 안에서 Consumer HTTP `/profile/request/new-client` 24회 storm과 provider evidence를 `.NET`처럼 검증한다. RL-A5는 전용 `rl_a5_provider_flapping_scenario.hpp` 안에서 down window `api-a` 수렴, up window request 성공, provider B evidence prefix를 `.NET`처럼 검증한다. RL-B4는 전용 `rl_b4_runtime_drain_scenario.hpp` 안에서 provider B drain/restore, drained 신규 request의 `api-a` 수렴, provider B evidence 불변, restored evidence를 `.NET`처럼 검증한다. RL-B5는 전용 `rl_b5_drain_inflight_scenario.hpp` 안에서 Consumer HTTP slow request, 실제 slow provider 식별, drained provider 신규 request 차단, in-flight reply 완료, restore 뒤 recovered evidence를 `.NET`처럼 검증하고, RL-B6는 전용 `rl_b6_gray_fault_scenario.hpp`와 provider gray fault endpoint/handler로 `.NET`처럼 gray request 실패와 healthy provider 성공을 함께 검증한다. RL-D4/RL-D5도 shell-only 검증에서 전용 client scenario header 경로로 옮겼다. RL-B1은 Consumer HTTP `/profile/request/timeout/100`과 후속 `/profile/request`로 검증한다. RL-B2는 Consumer HTTP slow in-flight request, provider B start evidence, provider crash, topology Ready 0, in-flight failure, `api-a` follow-up, provider B restored evidence를 검증한다. RL-C1은 전용 `rl_c1_client_host_lifecycle_scenario.hpp` 안에서 Consumer HTTP `/profile/request/new-client` 반복 request와 cleanup marker evidence를 검증한다. RL-C2는 provider crash 뒤 Consumer HTTP new-client request가 `api-a`로 수렴하고 provider B 재기동 뒤 restored marker가 `api-b` evidence에 남는지 검증한다. RL-C3는 전용 `rl_c3_node_pause_recovery_scenario.hpp` 안에서 provider B `/shutdown`, surviving provider request, provider B 재기동 뒤 Registry `/topology/wait` Ready 1, recovered evidence를 검증한다. `.NET`의 `ResilienceProcessManager`가 담당하는 provider/registry process 시작, health 대기, 종료, stdout/stderr 로그 저장 책임은 C++ runner가 같은 의미로 담당한다. `cmake --build framework/languages/cpp/build --target zlink_framework zlink_cpp_e2e_resilience_lifecycle_consumer zlink_cpp_e2e_resilience_lifecycle_client -j 4`와 `test_cpp_framework_app_host`가 통과했고, `timeout 1200s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh` 최신 통과 증거는 `logs/20260701-173140-37072`이다. 이 실행은 Consumer smoke, RL-C1 consumer new-client, RL-A1, RL-A2, RL-A3 new-client storm, RL-A4, RL-A5 flapping cycle, RL-B1, RL-B2, RL-B3, RL-B4, RL-B5, RL-B6, RL-C1, RL-C2, RL-C3 topology wait, RL-C4 registry outage 및 registry/provider 재기동 뒤 새 discovery client 복구, RL-D1, RL-D2, RL-D3, RL-D4, RL-D5를 검증한다. |
| `RuntimeMonitoring` | 완료 | `.NET` 기준 이름인 `RuntimeMonitoring`으로 전환했고 기존 C++ `Monitoring` PubSub 보조 wrapper는 삭제했다. C++ 전용 registry, service, filtered service, throwing service, trigger, client target과 `.NET` 기준 inventory가 있다. `.NET ClientOptions`에 대응하는 C++ `client_options.hpp`를 추가해 client endpoint option을 한 곳에서 읽고 scenario에 전달한다. Registry role host wiring은 `Server/Registry/registry_host_factory.hpp`로 분리했고 health/evidence/wait/shutdown endpoint mapping도 factory에서 담당한다. Registry event evidence 기록은 `Server/Registry/Handlers/registry_event_recorders.hpp`로 분리했다. Service/FilteredService/ThrowingService는 role-local factory wrapper가 all/socket-filter/throwing profile을 선택하고 공통 service host 구성을 재사용한다. Service event evidence 기록은 `Server/Service/Handlers/service_event_recorders.hpp`로 분리했다. Trigger role host wiring은 `Server/Trigger/trigger_host_factory.hpp`로 분리했고 trigger endpoint route mapping은 factory에서 담당한다. Trigger HTTP handler class는 `Server/Trigger/trigger_handlers.hpp`로 분리했다. `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh` 최신 통과 증거는 `logs/20260701-165138-53788`이며 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1과 service/trigger message-flow trace 파일을 검증한다. local port readiness timeout은 기본 3초다. |
| `YieldDispatch` | 완료 | C++ e2e config와 runner가 생겼고 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D1~YD-D4, YD-E1~YD-E5는 runner 통과 증거가 있다. 최신 `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build CMAKE_BUILD_PARALLEL_LEVEL=2 nice -n 10 timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh` 통과 증거는 `logs/20260707-151703-2374204`이며 `scenario YD-E2 passed`, `scenario YD-E5 passed`, `yield-dispatch-report.json`을 확인한다. runner readiness와 client connect timeout은 로컬 기준 3초로 낮췄다. session actor bind는 public `actor_gateway_t`/`session_actor_manager_t`로 user Spot에 join한 actor ref를 session에 bind하고, Play user Spot actor handler가 actor yield/join/push를 처리한다. Entry Spot에는 actor admission을 위한 fast handler만 둔다. route actor request dispatch가 yielded request 하나로 receive loop를 막지 않게 수정해, B1/B3/C3의 교차 실행선 검증은 session-b observer 없이 같은 session-a connector에서 `.NET`과 같은 in-flight actor/spot request 경로로 통과한다. D3는 `yield-released` evidence를 확인한 뒤 probe를 보내 route bridge scheduling 차이 없이 yield turn release를 검증한다. YD-E2는 public `cancellation_token_source_t`와 `cancellation_token_t`, `yield(token)`으로 delay request 대기를 취소하고 같은 Spot의 후속 probe가 처리되는지 검증한다. 구현된 client scenario header는 YD-A/B/C/D2~D4/E1/E2/E3까지 완료했고, Registry host factory/support, Delay host factory/handler/support, Play host factory/support/control/basic/timer/actor/remote/failure handler, spot type, `YieldProbeSpot` runtime, Session host factory/support, `yield_session_t` support header도 분리했다. |
| `ToActorMessaging` | 완료 | Config 9 C++ E2E는 public `actor_client_t::send_to_actor(...).async()`와 `request_to_actor(...).async<TReply>()`로 TA-A1~TA-A4, TA-B1~TA-B3을 검증한다. actor owner 서버, caller 서버, Redis location store, client runner target을 실행하며, caller 서버가 public actor client failure kind인 `actor_route_not_found`, `actor_location_stale`, `route_not_connected`를 JSON response로 반환하는지 확인한다. 최신 통과 증거는 `logs/20260707-113726-1467120`이고 client output은 `to-actor-messaging e2e result=passed`다. |

SpotService 최신 재검증:

- `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build ZLINK_CPP_E2E_SKIP_BUILD=1 timeout 1200s framework/languages/cpp/e2e/SpotService/run_e2e.sh all`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260701-183404-20982`
  - 비고: child retry 없이 focused child sweep 전체가 통과했다. route/control readiness는 기본 3초 settle 뒤 단일 3초 probe로 검증하고, port/file readiness helper는 local 기준 3초를 기본값으로 사용한다. SpotService client의 actor relay는 public `session_actor_t::relay_request(packet_name, payload)` overload를 사용하며 SpotService target의 `framework/src` 내부 include 의존을 제거했다. route request backend는 같은 native ROUTER socket을 여러 dispatch worker가 동시에 쓰지 않도록 submit 구간만 짧게 보호한다. stream host shutdown은 accept thread와 stop thread가 worker 목록을 동시에 갱신하지 않도록 수정했다. SM-Q9 child output은 scenario/evidence marker를 모두 남긴다. READ-ONLY 리뷰 결과는 `이슈 없음`이다.

YieldDispatch 최신 재검증:

- `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build CMAKE_BUILD_PARALLEL_LEVEL=2 nice -n 10 timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/YieldDispatch/logs/20260707-151703-2374204`
  - 비고: local readiness와 stream connect timeout은 3초 기준이다. YD-E3 shutdown wait도 3초 기본 wait helper를 사용하며, shutdown path의 session route spot request는 2초 안에 public `remote_error`를 올려 client 자체 request timeout과 경쟁하지 않게 한다. route actor request dispatch는 yielded request 하나로 receive loop를 막지 않고, spot route bridge lifetime은 local `shared_ptr`로 보호한다. `play-a.evidence.log`에서 YD-E2 순서가 `cancel-yield-started`, `cancel-yield-released`, `cancel-yield-completed`, `probe-started`, `probe-completed`로 확인됐다. YD-E2는 public cancellation token과 `yield(token)`으로 검증한다.

남은 public contract gap:

- 현재 C++ E2E 계획 문서 기준으로 남은 public contract gap은 없다. 새 gap이 나오면 해당 config의
  `feature-map.ko.md`와 공통 draft/spec 후보에 사유와 필요한 공개 계약을 분리한다.

완료 판정 감사:

- 위 1차 분류의 `완료`는 현재 runner와 inventory 기준 구현 상태다. 이 계획의 최종 완료 기준은
  config별 Codex 에이전트 리뷰 `이슈 없음`까지 포함한다.
- 2026-07-01 현재 로컬 재확인한 runner 증거는 `RegistryMessaging`, `PubSub`, `RegistrationCodec`,
  `DeliveryDispatch`, `ResilienceLifecycle`, `RuntimeMonitoring`, `SpotService`, `YieldDispatch` 로그가
  모두 존재하고, 각 config의 READ-ONLY 리뷰 또는 재리뷰 결과도 `이슈 없음`이다.
- `RegistryMessaging`은 `logs/20260701-141526-48855` parent/child manifest와
  `logs/20260701-141721-60851` RM-C9 focused 증거를 기준으로 READ-ONLY 리뷰 결과 `이슈 없음`을 받았다.
- `RuntimeMonitoring`은 `logs/20260701-165138-53788`에서 MON-A1~MON-D1 marker,
  `monitoring-event-dispatch` 격리 marker, service/trigger message-flow trace 파일을 확인했다.
  local port readiness timeout은 기본 3초다. READ-ONLY 리뷰에서 지적된 message-flow 증거 누락은
  이 실행으로 수정 검증했고, 재리뷰 결과 `이슈 없음`으로 완료 판정을 닫았다.
- `PubSub`는 `logs/20260701-170557-98147`에서 PS-A1~PS-C1 marker와
  `verify basic/topic/late/reconnect/slow/publisher-restart/negative passed` marker를 확인했다.
  registry/publisher operational endpoint는 `registry-operational.log`, `publisher-operational.log`,
  `publisher-restart-operational.log`, `registry-evidence-final.json`, `publisher-evidence-final.json`으로
  증거를 남긴다. READ-ONLY 재리뷰 결과 `이슈 없음`으로 완료 판정을 닫았다.
- `RegistrationCodec`은 `logs/20260707-134236-1993396`에서 RC-A1, RC-A2, RC-A3, RC-A4,
  RC-A5, RC-A6, RC-B1, RC-B2, RC-B3, RC-B4, RC-B5 marker와 최종
  `registration-codec e2e result=passed` marker를 확인했다. RC-A2는 C++ 타입 metadata 기반
  등록으로 공통 spec의 annotation 의미를 검증한다. JSON 기본 codec 경로를 메시지별 등록으로 우회한
  흔적은 없다는 READ-ONLY 리뷰 결과 `이슈 없음`을 받았다. runner readiness는 local 기준 3초다.
- `DeliveryDispatch`는 `logs/last-run`의 `deliverydispatch-probe=completed`,
  `deliverydispatch-reassignment=completed`, `deliverydispatch-server-evidence=completed`,
  `deliverydispatch=completed` marker와 `logs/flow-*.log`의 customer/courier gateway/session/actor-node
  message-flow evidence를 확인했다. 낡은 직접 stream 경로였던 `Server/Session`, `Server/Courier`
  source와 legacy target을 제거한 뒤 `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build timeout 420s
  framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`가 다시 통과했고, READ-ONLY 재리뷰 결과
  `이슈 없음`으로 완료 판정을 닫았다. runner readiness와 client stream connect timeout은 local 기준 3초다.
- `ResilienceLifecycle`은 `logs/20260701-173140-37072`에서 RL-consumer, RL-A1~RL-D5 marker와
  최종 `resilience-lifecycle e2e result=passed` marker를 확인했다. active client/provider code에 남아 있던
  낡은 cross-config selector/marker 잔재를 제거한 뒤 full runner가 다시 통과했고, READ-ONLY 재리뷰 결과
  `이슈 없음`으로 완료 판정을 닫았다. local port readiness와 provider crash health-down 확인은 3초 기준이며,
  topology/evidence/marker wait는 장애/복구 scenario event 검증용 대기로 분리한다.
- `SpotService`는 `logs/20260701-183404-20982` full sweep과 `SM-D5`/`SM-G3` focused 재확인으로
  route/stream 영향 지점을 확인했다. `.NET` 기준 파일 대응, 공통 Config 2 완료 기준, 3초 readiness,
  child retry 없는 focused child sweep, 메시지별 codec 등록 미사용을 READ-ONLY 리뷰로 재확인했고,
  결과는 `이슈 없음`이다.
- `YieldDispatch`는 `logs/20260707-151703-2374204`에서 YD-A1~YD-E5 marker와 최종
  `yield-dispatch e2e result=passed` marker를 확인했다. route backend bridge lifetime 보호, YD-E3
  3초 shutdown wait, 실패 로그 tail 출력, YD-C3A evidence-driven same-connector ordering,
  YD-E2 public cancellation token cleanup, 메시지별 codec 등록 미사용을 확인했다.

## 표준 C++ E2E 구조

```text
framework/languages/cpp/e2e/<Config>/
|-- Shared/
|   `-- <config>_contracts.hpp
|-- Server/
|   |-- <Role>/
|   |   |-- main.cpp
|   |   |-- CMakeLists.txt
|   |   |-- Configuration/
|   |   |-- Endpoints/
|   |   |-- Handlers/
|   |   |-- Infrastructure/
|   |   `-- Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- main.cpp
|   |-- Scenarios/
|   `-- Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- feature-map.ko.md
`-- run_e2e.sh
```

언어 특성상 project 파일 이름은 CMake 구조에 맞춰 조정할 수 있다. 하지만 role을 하나의 executable에서
`--role` 옵션으로 바꿔 실행하는 방식은 `.NET`의 role 분리와 다르므로 완료로 보지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared/` | client와 server가 함께 쓰는 payload, evidence, marker 타입 |
| `Client/main.cpp` | scenario 목록과 실행 순서 선언 |
| `Client/Scenarios/` | `.NET Client/Scenarios` 파일 하나에 대응하는 C++ scenario 파일 |
| `Client/Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/main.cpp` | role 실행 진입점 |
| `Server/<Role>/Configuration/` | role 실행 옵션과 포트, endpoint 설정 |
| `Server/<Role>/Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/Handlers/` | framework handler, observer, spot, actor handler |
| `Server/<Role>/Infrastructure/` | evidence store와 role 내부 상태 |
| `Server/<Role>/Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | build, 포트 할당, role process 시작과 종료, client 실행, 실패 로그 출력 |
| `feature-map.ko.md` | scenario ID별 구현 상태, gap, 검증 결과 |

`main.cpp`에 endpoint, handler, framework 설정을 모두 넣지 않는다. 역할별 설정과 handler는 성격별
폴더로 나눈다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. `.NET` role root에 option, endpoint, handler,
evidence 파일이 남아 있으면 C++에서는 책임별 폴더로 재분류한다.

- option, argument, endpoint 주소 설정: `Server/<Role>/Configuration/`
- HTTP endpoint mapping, evidence wait, shutdown endpoint: `Server/<Role>/Endpoints/`
- framework handler, dispatch filter, observer, spot, actor handler: `Server/<Role>/Handlers/`
- evidence store, runtime state, in-memory repository: `Server/<Role>/Infrastructure/`
- 해당 role 내부에서만 쓰는 relay, wait, runtime helper: `Server/<Role>/Support/`
- 여러 scenario가 함께 쓰는 client-side context나 helper: `Client/Support/`
- scenario ID 하나를 실행하는 파일: `Client/Scenarios/`

재분류한 파일은 `porting-inventory.ko.md` 비고에 원본 위치와 목표 위치를 함께 적는다.

## Scenario ID 판정 규칙

`Client/Scenarios/` 아래에 있다는 이유만으로 모두 scenario 파일로 보지 않는다. scenario 파일은 공통
e2e 문서의 scenario ID 하나를 직접 실행하고 marker를 검증하는 파일이다. context, shared record, fixture,
helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 C++에서는 `Client/Support/`나 `Shared/`로 옮긴다.

`.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
`porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 C++ 대응 scenario 파일을 명시한다.

## Inventory 매핑 산출물

각 config는 `.NET` 기준 파일 하나하나가 C++에서 어디로 옮겨졌는지 기록하는 매핑 문서를 반드시 둔다.

```text
framework/languages/cpp/e2e/<Config>/porting-inventory.ko.md
```

형식:

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/Scenarios/..._scenario.hpp` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/<config>_contracts.hpp` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/Support/...` | support | done/gap | 공통 helper 책임 |

규칙:

- `.NET` 기준 파일 목록은 아래 명령으로 생성한다.

```bash
find framework/languages/dotnet/e2e/<Config> -type f \
  ! -path '*/bin/*' \
  ! -path '*/obj/*' \
  ! -path '*/logs/*' \
  | sed 's#^framework/languages/dotnet/e2e/<Config>/##' \
  | sort
```

- `.csproj`, `.gitignore`, `run_e2e.sh`, `feature-map.ko.md`, `README.ko.md`도 매핑에서 빠뜨리지 않는다.
- `.NET` 파일 하나가 여러 C++ 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- C++에서 해당 파일이 필요 없다고 판단해도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`로 두고
  근거를 비고에 적는다.
- `pending` 상태가 하나라도 있으면 config 완료로 보지 않는다.

## 진행 순서

아래 순서를 고정한다. 한 행이 Codex 에이전트 리뷰까지 이슈 없음으로 끝나기 전에는 다음 행으로
넘어가지 않는다.

| 순서 | Config | 기준 문서 | 완료 조건 |
|------|--------|-----------|-----------|
| 1 | `RegistryMessaging` | `config-1-location-messaging.ko.md` | `.NET`의 RM-* scenario와 provider/workflow/consumer/client role 전부 대응. C++ 디렉터리 이름은 scenario ID 연속성을 위해 유지한다. |
| 2 | `PubSub` | `config-3-pubsub.ko.md` | publisher/subscriber/registry role과 pubsub scenario 전부 대응 |
| 3 | `RegistrationCodec` | `config-4-registration-codec.ko.md` | registration, codec variant, invalid registration scenario 전부 대응 |
| 4 | `DeliveryDispatch` | `.NET DeliveryDispatch` sample | registry, dispatch API, dispatch center, courier A/B, tracking, session, probe, client role을 C++ sample/e2e로 포팅하고 registry discovery readiness와 delivery reassignment flow까지 검증 |
| 5 | `ResilienceLifecycle` | `config-5-resilience-lifecycle.ko.md` | restart, remap, drain, crash, outage, observer failure scenario 전부 대응 |
| 6 | `RuntimeMonitoring` | `config-7-monitoring.ko.md` | monitoring event, filter, dispatch failure, recovery scenario 전부 대응 |
| 7 | `SpotService` | `config-2-spot-service.ko.md` | spot, actor, session, route, timer, multi-node scenario 전부 대응 |
| 8 | `YieldDispatch` | `config-8-yield-dispatch.ko.md` | YD-A/B/C/D/E 전체 scenario 대응. 특히 YD-D1 local topology, YD-E3 runtime shutdown, YD-E4 금지 표면 정적 검증, YD-E5 언어별 의미 동등성까지 확인 |
| 9 | `ToActorMessaging` | `config-9-to-actor-messaging.ko.md` | TA-A/B 전체 scenario와 actor client failure kind 대응 |

## Config 단위 작업 절차

1. `.NET` inventory를 생성한다.
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
2. `.NET`의 `Client/Scenarios`, `Server/<Role>`, `Shared`, `Client/Support` 목록을
   `porting-inventory.ko.md`에 표로 정리한다.
3. 공통 e2e config 문서에서 scenario ID, 성공 marker, 실패 조건을 대조한다.
4. `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
5. C++ public API와 문서에서 같은 동작을 제공할 수 있는지 확인한다.
6. public contract gap이 있으면 구현하지 말고 `feature-map.ko.md`에 남긴다.
7. `.NET`과 같은 의미의 C++ 파일 트리를 만들되, stale `.NET` root 파일은 목표 폴더로 재분류한다.
8. Shared contract를 먼저 옮기고, 그 다음 server role, client scenario 순서로 구현한다.
9. scenario ID가 없는 helper/context 파일은 `Client/Scenarios/`가 아니라 `Client/Support/` 또는
   `Shared/`로 옮긴다.
10. `run_e2e.sh`가 실제 role process를 띄우고 readiness, cleanup, 실패 로그 출력을 처리하게 한다.
11. `porting-inventory.ko.md`의 모든 행에 C++ 대응 파일, 분류, 상태를 채운다.
12. 해당 config의 `run_e2e.sh`를 실제 실행한다.
13. 실패하면 같은 config 안에서 고치고 다시 실행한다. 이때 실패 원인을 모른 채 sleep, retry 횟수 증가,
    runner-only adapter, raw frame 조작으로 덮지 않는다. 원인을 좁혀 framework, stream connector,
    zlink http client, e2e 중 책임 위치를 수정하고 회귀 테스트를 추가한다.
14. 버그를 수정했다면 feature-map 또는 README에 원인, 수정 계층, 추가한 회귀 테스트를 함께 기록한다.
15. Codex 에이전트 리뷰를 요청한다.
16. 리뷰 이슈가 있으면 수정, 재실행, 재리뷰를 반복한다.
17. 리뷰가 이슈 없음이면 config 완료로 기록하고 다음 config로 이동한다.

## Codex 에이전트 리뷰 요청

각 config 자체 검증 뒤 아래 요청을 사용한다.

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/cpp/e2e/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 C++에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 C++에서 빠짐없이 대응되는가.
3. Shared contract, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 C++ 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 C++ feature-map에서 과장 없이 반영했는가.
6. .NET role root에 있던 option/endpoint/handler/evidence/support 파일을 목표 분류로 재배치했는가.
7. Client/Scenarios 아래 helper/context 파일을 scenario로 오분류하지 않았는가.
8. public contract에 없는 기능을 private API, raw frame, test-only adapter로 우회하지 않았는가.
9. run_e2e.sh가 실제 프로세스 경계, readiness, cleanup, 실패 로그 출력을 제대로 처리하는가.
10. feature-map.ko.md가 구현 완료와 gap을 과장 없이 기록하는가.
11. 버그 수정이 임시 우회가 아니라 원인 계층을 고친 변경이며, 재발을 막는 회귀 테스트가 함께 있는가.
12. 실제 실행 결과가 문서와 코드의 완료 주장과 일치하는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```

## 누락 방지 체크리스트

- `.NET` inventory와 C++ 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `.NET Client/Scenarios` 아래 파일을 scenario ID 파일과 helper/context 파일로 구분했고, scenario ID 파일만
  C++ `Client/Scenarios/`에 대응된다.
- `.NET`의 모든 server role이 C++ `Server/<Role>/`에 대응된다.
- 공통 e2e 문서의 scenario ID가 `feature-map.ko.md`와 client scenario 파일에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/Scenarios/`에 두지 않았다.
- C++ e2e가 internal runtime detail을 호출자 코드로 밀어내지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- 빌드 산출물, 임시 로그, generated file은 커밋하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.
