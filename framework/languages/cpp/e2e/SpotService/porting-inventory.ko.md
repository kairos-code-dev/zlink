# C++ SpotService E2E porting inventory

이 문서는 `.NET` SpotService E2E 파일이 현재 C++ SpotService E2E에서 어디에 대응되는지 기록한다.
`status`가 `gap`인 행은 구현 검증 또는 파일 분류가 아직 완료 판정에 부족한 항목이다.

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | support | done | 로그 산출물 제외 |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | 실제 registry/play/session/gateway/client 프로세스를 시작하고 route-ready probe 뒤 `all` 실행을 검증한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | feature-map | done | C++ public API로 구현한 항목과 남은 gap을 구분한다. |
| `Shared/Messages.cs` | `Shared/spot_service_contracts.hpp` | shared | done | payload, evidence, stream message DTO 대응 |
| `Shared/SpotService.Shared.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Client/SpotService.Client.csproj` | `CMakeLists.txt` | build | not-needed | C++는 `zlink_cpp_e2e_spot_service_client` target으로 빌드된다. |
| `Client/Program.cs` | `Client/main.cpp` | client | done | scenario 실행 순서와 client framework 설정 |
| `Client/Support/ClientOptions.cs` | `run_e2e.sh`, `Client/main.cpp` | support | done | C++ runner env와 client main이 옵션을 나누어 처리한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/main.cpp` | support | done | `ensure(...)` helper와 scenario별 예외로 대응 |
| `Client/Support/SpotLifecycleOrderContext.cs` | `Client/main.cpp` | support | done | lifecycle order 검증은 client evidence assertion으로 대응 |
| `Client/Scenarios/SmA1Scenario.cs` | `Client/Scenarios/sm_a1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A1 |
| `Client/Scenarios/SmA2Scenario.cs` | `Client/Scenarios/sm_a2_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A2 |
| `Client/Scenarios/SmA3Scenario.cs` | `Client/Scenarios/sm_a3_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A3 |
| `Client/Scenarios/SmA4Scenario.cs` | `Client/Scenarios/sm_a4_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A4 |
| `Client/Scenarios/SmA5Scenario.cs` | `feature-map.ko.md` | scenario | gap | C++ public framework는 Stage wrapper를 별도 계약으로 제공하지 않아 feature-map gap으로 남긴다. |
| `Client/Scenarios/SmA6Scenario.cs` | `Client/Scenarios/sm_a6_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A6 |
| `Client/Scenarios/SmA7Scenario.cs` | `Client/Scenarios/sm_a7_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A7 |
| `Client/Scenarios/SmA8Scenario.cs` | `Client/Scenarios/sm_a8_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-A8. `/spot/worker/start`와 `/spot/worker/complete`로 spot-level worker offload와 interleaved state request evidence를 검증한다. |
| `Client/Scenarios/SmB1Scenario.cs` | `Client/Scenarios/sm_b1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B1 |
| `Client/Scenarios/SmB2Scenario.cs` | `Client/Scenarios/sm_b2_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B2 |
| `Client/Scenarios/SmB3Scenario.cs` | `Client/Scenarios/sm_b3_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B3 |
| `Client/Scenarios/SmB4Scenario.cs` | `Client/Scenarios/sm_b4_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B4 |
| `Client/Scenarios/SmB5Scenario.cs` | `Client/Scenarios/sm_b5_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B5 |
| `Client/Scenarios/SmB6Scenario.cs` | `Client/Scenarios/sm_b6_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B6 |
| `Client/Scenarios/SmB7Scenario.cs` | `Client/Scenarios/sm_b7_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B7 |
| `Client/Scenarios/SmB8Scenario.cs` | `Client/Scenarios/sm_b8_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-B8 |
| `Client/Scenarios/SmC1Scenario.cs` | `Client/Scenarios/sm_c1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-C1 |
| `Client/Scenarios/SmC2Scenario.cs` | `Client/Scenarios/sm_c2_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-C2 |
| `Client/Scenarios/SmC3Scenario.cs` | `Client/Scenarios/sm_c3_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-C3 |
| `Client/Scenarios/SmC4Scenario.cs` | `Client/Scenarios/sm_c4_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-C4 |
| `Client/Scenarios/SmD1Scenario.cs` | `Client/Scenarios/sm_d1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D1 |
| `Client/Scenarios/SmD2Scenario.cs` | `Client/Scenarios/sm_d2_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D2 |
| `Client/Scenarios/SmD3Scenario.cs` | `Client/Scenarios/sm_d3_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D3 |
| `Client/Scenarios/SmD4Scenario.cs` | `Client/Scenarios/sm_d4_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D4 |
| `Client/Scenarios/SmD5Scenario.cs` | `Client/Scenarios/sm_d5_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D5 |
| `Client/Scenarios/SmD6Scenario.cs` | `Client/Scenarios/sm_d6_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D6 |
| `Client/Scenarios/SmD7Scenario.cs` | `Client/Scenarios/sm_d7_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D7 |
| `Client/Scenarios/SmD8Scenario.cs` | `Client/Scenarios/sm_d8_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D8 |
| `Client/Scenarios/SmD9Scenario.cs` | `Client/Scenarios/sm_d9_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D9 |
| `Client/Scenarios/SmD10Scenario.cs` | `Client/Scenarios/sm_d10_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D10 |
| `Client/Scenarios/SmD11Scenario.cs` | `Client/Scenarios/sm_d11_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D11 |
| `Client/Scenarios/SmD12Scenario.cs` | `Client/Scenarios/sm_d12_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-D12 |
| `Client/Scenarios/SmD13Scenario.cs` | `Client/Scenarios/sm_d13_scenario.hpp`, `feature-map.ko.md` | scenario | gap | 유지 경로는 구현됐지만 heartbeat 중단을 유도하는 public harness knob이 없다. |
| `Client/Scenarios/SmD14Scenario.cs` | `Client/Scenarios/sm_d14_scenario.hpp`, `Server/Session/session_host_factory.hpp`, `run_e2e.sh`, `feature-map.ko.md` | scenario | done | public stream node TLS server 설정과 stream connector strict rejection/skip-validation 성공 경로로 bind, relay, push를 검증한다. |
| `Client/Scenarios/SmE1Scenario.cs` | `Client/Scenarios/sm_e1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-E1 |
| `Client/Scenarios/SmE2Scenario.cs` | `Client/Scenarios/sm_e2_scenario.hpp`, `Server/Play/Spots/play_actor_model.hpp`, `run_e2e.sh` | scenario | done | SM-E2 public spot timer tick evidence |
| `Client/Scenarios/SmE3Scenario.cs` | `Client/Scenarios/sm_e3_scenario.hpp`, `Server/Play/Spots/play_actor_model.hpp`, `Server/Play/Handlers/play_spot_route_handlers.hpp`, `run_e2e.sh` | scenario | done | SM-E3 public spot create lifecycle에서 idle timer를 등록하고 timer handler의 public close와 닫힌 spot request 실패를 검증한다. |
| `Client/Scenarios/SmE4Scenario.cs` | `Client/Scenarios/sm_e4_scenario.hpp`, `Server/Play/Spots/play_actor_model.hpp`, `Server/Play/Handlers/play_spot_route_handlers.hpp`, `run_e2e.sh` | scenario | done | SM-E4 public timer overrun policy와 tick delivery/scheduled/skipped evidence를 검증한다. |
| `Client/Scenarios/SmF1Scenario.cs` | `Client/Scenarios/sm_f1_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-F1 |
| `Client/Scenarios/SmF2Scenario.cs` | `Client/Scenarios/sm_f2_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-F2 |
| `Client/Scenarios/SmF4Scenario.cs` | `Client/Scenarios/sm_f4_scenario.hpp`, `Client/main.cpp` | scenario | done | SM-F4 |
| `Client/Scenarios/SmG1Scenario.cs` | `Client/Scenarios/sm_g1_scenario.hpp`, `Client/main.cpp`, `run_e2e.sh` | scenario | done | SM-G1 crash/restart evidence |
| `Client/Scenarios/SmG2Scenario.cs` | `Client/Scenarios/sm_g2_scenario.hpp` | scenario | done | SM-G2 |
| `Client/Scenarios/SmG3Scenario.cs` | `Client/Scenarios/sm_g3_scenario.hpp` | scenario | done | SM-G3 |
| `Client/Scenarios/SmG4Scenario.cs` | `Client/Scenarios/sm_g4_scenario.hpp` | scenario | done | SM-G4 |
| `Client/Scenarios/SmQ9Scenario.cs` | `feature-map.ko.md`, `Server/MultiNode/` | scenario | not-needed | 공통 Config 2 scenario ID가 아니므로 C++ feature-map의 완료 범위에 넣지 않는다. 별도 MultiNode scaffold는 build까지만 확인했고, .NET식 spot-rid-only route request는 C++ public route client 계약과 맞지 않아 runner scenario로 승격하지 않았다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | done | registry role 진입점 |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry_host_factory.hpp` | server-role | done | registry host factory 책임을 role-local header로 분리했다. |
| `Server/Registry/RegistrySupport.cs` | `Server/Shared/Support/env.hpp`, `Server/Registry/registry_host_factory.hpp` | support | done | registry role의 env option helper와 host 설정을 shared runtime 없이 분리했다. |
| `Server/Registry/SpotService.Registry.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Server/Play/Program.cs` | `Server/Play/main.cpp` | server-role | done | play role 진입점 |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/play_host_factory.hpp` | server-role | done | play host factory 책임을 role-local header로 분리했다. |
| `Server/Play/PlaySupport.cs` | `Server/Shared/Support/env.hpp`, `Server/Shared/Support/codecs.hpp`, `Server/Shared/Endpoints/evidence_endpoint.hpp`, `Server/Shared/scenario_state.hpp` | support | done | env, codec, evidence snapshot, scenario state 책임을 shared support/endpoint 파일로 분리했다. |
| `Server/Play/SpotService.Play.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Server/Play/Endpoints/OperationalEndpoints.cs` | `Server/Play/Endpoints/operational_endpoints.hpp`, `Server/Shared/Endpoints/evidence_endpoint.hpp`, `Server/Shared/Handlers/channel_control_ping_handler.hpp` | endpoint | done | health/evidence/evidence-wait/control-ping/shutdown/crash mapping을 endpoint 파일로 분리했고, `/evidence/wait`는 SM-A6 focused run에서 직접 검증했다. |
| `Server/Play/Endpoints/SpotFailureEndpoints.cs` | `Server/Play/Endpoints/spot_failure_endpoints.hpp`, `Server/Play/Handlers/play_spot_route_handlers.hpp` | endpoint | done | slow, missing-handler request/command, missing-target, missing-route, spot-to-spot timeout/negative endpoint mapping을 endpoint 파일로 분리했고 SM-E1 focused run에서 missing-handler/missing-target endpoint를 직접 검증했다. |
| `Server/Play/Endpoints/SpotInteractionEndpoints.cs` | `Server/Play/Endpoints/spot_interaction_endpoints.hpp`, `Server/Play/Handlers/play_actor_handlers.hpp`, `Server/Play/Handlers/play_spot_route_handlers.hpp` | endpoint | gap | 구현된 spot interaction endpoint mapping과 publish-wait endpoint는 endpoint 파일로 분리했고 SM-C4 focused run에서 `/spot/publish/wait`를 직접 검증했다. idle-close endpoint는 SM-E3 focused run에서 검증했고 overrun timer endpoint는 SM-E4 focused run에서 검증했다. `/spot/worker/start`와 `/spot/worker/complete`는 SM-A8 focused run에서 검증했다. stage 계열은 feature-map gap으로 남긴다. |
| `Server/Play/Endpoints/SpotLifecycleEndpoints.cs` | `Server/Play/Endpoints/spot_lifecycle_endpoints.hpp`, `Server/Play/Handlers/play_control_handlers.hpp`, `Server/Play/Handlers/play_spot_route_handlers.hpp` | endpoint | done | lifecycle create/alternate/close/type-mismatch endpoint mapping은 endpoint 파일로 분리했다. C++의 state request/command route mapping은 interaction endpoint 파일에 둔다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/Handlers/play_actor_handlers.hpp`, `Server/Play/Spots/play_actor_model.hpp` | handler | done | actor/channel HTTP bridge와 channel handlers는 handler 파일로 분리했고, spot actor packet handler 구현은 role-local spot model에 유지했다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | `Server/Play/Handlers/play_control_handlers.hpp`, `Server/Shared/Handlers/channel_control_ping_handler.hpp` | handler | done | ensure/lifecycle/create/close/type-mismatch control handler는 play handler 파일로 분리했고, play/session 공통 control-ping handler는 shared handler로 분리했다. |
| `Server/Play/Handlers/PlaySessionHandlers.cs` | `Server/Play/Handlers/play_session_handlers.hpp`, `Server/Session/Handlers/session_session_handlers.hpp`, `Server/Play/Spots/play_actor_model.hpp` | handler | done | Play-local bound session push HTTP bridge는 play session handler 파일로 분리했고, stream lifecycle/auth/relay 책임은 C++ Session role handler에 대응시켰다. SM-D6 focused run으로 `/spot/push-bound-session` 경로를 검증했다. |
| `Server/Play/Handlers/PlaySpotRouteHandlers.cs` | `Server/Play/Handlers/play_spot_route_handlers.hpp` | handler | done | route client HTTP bridge handler를 목표 handler 파일로 분리했고 SM-C1/SM-C3 focused run으로 검증했다. |
| `Server/Play/Handlers/PlayStageHandlers.cs` | `feature-map.ko.md` | handler | gap | C++는 Stage wrapper를 public E2E로 제공하지 않는다. |
| `Server/Play/Spots/PlayActorModel.cs` | `Server/Play/Spots/play_actor_model.hpp`, `Server/Shared/spot_actor_support.hpp` | spot | done | actor model은 role-local spot 파일로 분리했고 actor ref 변환 helper는 shared support로 분리했다. |
| `Server/Play/Spots/PlayMultiNodeScenario.cs` | `feature-map.ko.md` | spot | not-needed | `.NET` 전용 SM-Q9 관련 파일이며 공통 Config 2 완료 범위에 넣지 않는다. |
| `Server/Gateway/Program.cs` | `Server/Gateway/main.cpp` | server-role | done | gateway role 진입점 |
| `Server/Gateway/GatewayHostFactory.cs` | `Server/Gateway/gateway_host_factory.hpp` | server-role | done | gateway host factory 책임을 role-local header로 분리했다. |
| `Server/Gateway/SpotService.Gateway.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Server/Session/Program.cs` | `Server/Session/main.cpp` | server-role | done | session role 진입점 |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/session_host_factory.hpp`, `Server/Shared/Endpoints/evidence_endpoint.hpp` | server-role | done | session host factory 책임을 role-local header로 분리했고, health/evidence/evidence-wait/shutdown/crash operational endpoint는 shared endpoint handler로 연결했다. |
| `Server/Session/SessionSupport.cs` | `Server/Shared/Support/env.hpp`, `Server/Shared/Support/codecs.hpp`, `Server/Shared/Endpoints/evidence_endpoint.hpp`, `Server/Shared/scenario_state.hpp` | support | done | env, codec, evidence snapshot, scenario state 책임을 shared support/endpoint 파일로 분리했다. |
| `Server/Session/SpotService.Session.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Server/Session/Handlers/SessionControlHandlers.cs` | `Server/Shared/Handlers/channel_control_ping_handler.hpp` | handler | done | `/channel/control-ping` route-client probe는 play/session 공통 handler로 분리했고 SM-D11 focused run으로 검증했다. |
| `Server/Session/Handlers/SessionSessionHandlers.cs` | `Server/Session/Handlers/session_session_handlers.hpp` | handler | done | session stream lifecycle, auth binding, actor relay 책임을 role-local handler header로 분리했다. |
| `Server/Session/Handlers/SessionStageHandlers.cs` | `feature-map.ko.md` | handler | gap | Stage/timer 관련 항목은 C++ feature-map gap으로 남긴다. |
| `Server/Session/Spots/SessionActorModel.cs` | `Server/Play/Spots/play_actor_model.hpp`, `Server/Session/Handlers/session_session_handlers.hpp` | spot | done | `.NET` Session role의 actor/entry/user spot model 책임은 C++에서 Play role spot model로 재분류하고, stream session bind/relay 책임은 Session handler로 분리했다. |
| `Server/Session/Spots/SessionMultiNodeScenario.cs` | `feature-map.ko.md` | spot | not-needed | `.NET` 전용 SM-Q9 관련 파일이며 공통 Config 2 완료 범위에 넣지 않는다. |
| `Server/MultiNode/Program.cs` | `Server/MultiNode/main.cpp` | server-role | not-needed | `.NET` 전용 SM-Q9 scaffold로 남긴다. C++ 완료 판정은 build proof까지만 기록하고 runtime scenario로 승격하지 않는다. |
| `Server/MultiNode/MultiNodeHostFactory.cs` | `Server/MultiNode/multi_node_host_factory.hpp` | server-role | not-needed | public framework API로 작성한 scaffold build는 통과했지만, SM-Q9가 공통 완료 범위가 아니므로 추가 runner proof를 요구하지 않는다. |
| `Server/MultiNode/MultiNodeSupport.cs` | `Server/MultiNode/multi_node_host_factory.hpp`, `Server/Shared/Support/env.hpp`, `Server/Shared/Support/codecs.hpp`, `Server/Shared/Endpoints/evidence_endpoint.hpp`, `Server/Shared/scenario_state.hpp` | support | not-needed | `.NET` 전용 SM-Q9 scaffold가 shared support를 재사용한다. 공통 완료 범위에는 포함하지 않는다. |
| `Server/MultiNode/SpotService.MultiNode.csproj` | `CMakeLists.txt` | build | not-needed | C++는 상위 CMake target에 통합된다. |
| `Server/MultiNode/Handlers/MultiNodeControlHandlers.cs` | `Server/MultiNode/Handlers/multi_node_handlers.hpp` | handler | not-needed | `.NET` 전용 SM-Q9 scaffold다. `.NET`식 spot-rid-only route request는 현재 C++ public route client 계약으로 받지 않는다. |
| `Server/MultiNode/Handlers/MultiNodeSessionHandlers.cs` | `Server/Session/Handlers/session_session_handlers.hpp` | handler | done | multi-node stream session binding/relay 책임은 session handler 파일로 분리했다. |
| `Server/MultiNode/Handlers/MultiNodeStageHandlers.cs` | `feature-map.ko.md` | handler | not-needed | `.NET` 전용 SM-Q9/Stage 조합은 C++ 공통 완료 범위에 넣지 않는다. Stage wrapper 자체는 SM-A5 gap에 기록한다. |
| `Server/MultiNode/Spots/MultiNodeActorModel.cs` | `feature-map.ko.md` | spot | not-needed | `.NET` 전용 SM-Q9 관련 파일이며 C++ 공통 완료 범위에 넣지 않는다. |
| `Server/MultiNode/Spots/MultiNodeMultiNodeScenario.cs` | `Server/MultiNode/Spots/multi_node_spots.hpp`, `Server/MultiNode/Handlers/multi_node_handlers.hpp` | spot | not-needed | MultiNode spot scaffold는 build proof까지만 유지하고, `.NET` 전용 SM-Q9 runtime proof는 완료 판정에 요구하지 않는다. |

## 현재 검증

- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_registry zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client`
  - 결과: passed
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_client`
  - 결과: passed
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client`
  - 결과: passed
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D6`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-045614-2707220`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D6`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-054426-2816196`
  - 비고: `Server/Play/Handlers/play_session_handlers.hpp` 분리 후 `/spot/push-bound-session` 경로를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-C1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-045614-2707204`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-C3`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-043829-2669425`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A6`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-050307-2724272`
  - 비고: `/evidence/wait` POST endpoint를 직접 호출해 lifecycle evidence wait 응답을 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A7`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-044250-2678660`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-C4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-051426-2749225`
  - 비고: `/spot/publish/wait` POST endpoint를 직접 호출해 publish evidence wait 응답을 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D11`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-050158-2721357`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-050848-2738250`
  - 비고: `/spot/missing-handler/request`, `/spot/missing-handler/command`, `/spot/missing-target/request` endpoint를 직접 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-F1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-052350-2774338`
  - 비고: `Client/Scenarios/sm_f1_scenario.hpp` 분리 후 direct spot request/send evidence를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-F2`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-052350-2774348`
  - 비고: `Client/Scenarios/sm_f2_scenario.hpp` 분리 후 direct spot request/send evidence를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-F4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-052322-2773321`
  - 비고: `Client/Scenarios/sm_f4_scenario.hpp` 분리 후 missing target negative와 recovery evidence를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-G1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-053352-2791240`
  - 비고: `Client/Scenarios/sm_g1_scenario.hpp` 분리 후 crash observation/recovery evidence를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-G1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-055119-2834665`
  - 비고: crash recovery client가 죽은 play-a route endpoint와 readiness probe endpoint를 재사용하지 않도록 runner를 조정한 뒤 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D10`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-054015-2804555`
  - 비고: `max_received_messages` bounded queue와 `received_message_dropped` callback 이후 session 유지 및 다른 session push를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A2`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-044905-2692120`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B3`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-044932-2693624`
  - 비고: runner build 단계에서 짧은 clock skew 경고가 출력됐지만 scenario와 evidence marker는 통과했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B5`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-044958-2694818`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B2`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-045023-2695549`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-B4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-045049-2696262`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A8`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-045117-2697176`
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh all`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-055133-2835811`
  - 비고: base suite, stream suite, SM-G1 crash/recovery evidence를 통합 runner에서 검증했다.
- `bash -n framework/languages/cpp/e2e/SpotService/run_e2e.sh`
  - 결과: passed
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_multinode`
  - 결과: passed
  - 비고: MultiNode scaffold build만 확인했다. SM-Q9 runtime proof는 없으므로 완료 판정에 포함하지 않는다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-061609-2901434`
  - 비고: 실패한 SM-Q9 runner branch 제거 후 기존 common route-to-spot proof가 유지되는지 확인했다.
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_client`
  - 결과: passed
  - 비고: SM-E2 timer evidence 추가 후 play/client target build를 확인했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E2`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-062148-2925039`
  - 비고: `user_spot_t`가 public `spot_context_t::add_timer<THandler>`로 등록한 timer tick을 `/evidence/wait`와 runner evidence assertion으로 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E3`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-063649-2977469`
  - 비고: public spot create lifecycle에서 등록한 idle timer가 `spot_context_t::close()`로 spot을 닫고, 닫힌 spot request가 실패하는지 검증했다.
- `bash -n framework/languages/cpp/e2e/SpotService/run_e2e.sh`
  - 결과: passed
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_client`
  - 결과: passed
  - 비고: SM-E4 overrun timer evidence 추가 후 play/client target build를 확인했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-064906-3006249`
  - 비고: public `timer_options_t` overrun policy와 `timer_tick_t` delivery/scheduled/skipped evidence를 runner assertion으로 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E2`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-065027-3010019`
  - 비고: SM-E4 payload decode 순서 변경 뒤 기존 spot timer tick 경로가 유지되는지 확인했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-E3`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-065054-3010912`
  - 비고: SM-E4 payload decode 순서 변경 뒤 idle-close timer create payload가 유지되는지 확인했다.
- `bash -n framework/languages/cpp/e2e/SpotService/run_e2e.sh`
  - 결과: passed
- `cmake --build framework/languages/cpp/build --target zlink_framework test_cpp_framework_contract_headers test_cpp_framework_module_hosted zlink_cpp_e2e_spot_service_session zlink_cpp_e2e_spot_service_client -j 4`
  - 결과: passed
  - 비고: build tree timestamp clock skew 경고가 한 번 출력됐지만 target build는 완료됐다.
- `./framework/languages/cpp/build/test_cpp_framework_contract_headers`
  - 결과: passed
- `./framework/languages/cpp/build/test_cpp_framework_module_hosted`
  - 결과: passed
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D14`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-071842-3086927`
  - 비고: TLS strict certificate rejection, skip-validation connect, stream bind, relay, push evidence를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-D1`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-071910-3088627`
  - 비고: stream host 변경 뒤 기존 TCP stream bind/push 경로가 유지되는지 확인했다.
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_client -j 4`
  - 결과: passed
  - 비고: SM-A8 spot-level worker endpoint 추가 뒤 play/client target build를 확인했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A8`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-073736-3137405`
  - 비고: `/spot/worker/start`와 `/spot/worker/complete` endpoint, worker 중 state request interleave, owner evidence order를 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh SM-A4`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-073801-3138786`
  - 비고: `spot_state_route_req_t` 확장 뒤 기존 key-based owner route가 유지되는지 확인했다.
- `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_spot_service_play zlink_cpp_e2e_spot_service_client -j 4`
  - 결과: passed
  - 비고: integrated base remote actor join 경로와 SM-C3 retry parity 조정 뒤 play/client target build를 확인했다.
- `bash -n framework/languages/cpp/e2e/SpotService/run_e2e.sh`
  - 결과: passed
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh all`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-075600-3183386`
  - 비고: base suite, stream suite, SM-G1 crash/recovery evidence를 통합 runner에서 다시 검증했다.
- `./framework/languages/cpp/e2e/SpotService/run_e2e.sh all`
  - 결과: passed
  - 로그: `framework/languages/cpp/e2e/SpotService/logs/20260630-080501-3203717`
  - 비고: `all` runner가 base/stream/crash 외에 SM-B3, SM-B4, SM-B7, SM-D3, SM-D8, SM-D10, SM-D14, SM-E2, SM-E3, SM-E4, SM-G2, SM-G3, SM-G4 focused evidence gate도 함께 실행하도록 확장한 뒤 검증했다.

## 완료 판정

- server runtime 통합 header는 제거했고, 남은 공통 support는 shared support/endpoint 파일로 분리했다.
- 최신 build 검증, focused runtime 검증, full `all` runner 검증은 통과했다.
- `feature-map.ko.md`는 public API 또는 harness gap을 별도로 기록한다.
- registry, play, session, gateway host factory 책임은 role-local header로 분리했다.
- play actor model 책임은 `Server/Play/Spots/play_actor_model.hpp`로 분리했다.
- play spot route handler 책임은 `Server/Play/Handlers/play_spot_route_handlers.hpp`로 분리했고 SM-C1/SM-C3
  focused runtime 검증을 통과했다.
- play/session control handler 책임은 `Server/Play/Handlers/play_control_handlers.hpp`와
  `Server/Shared/Handlers/channel_control_ping_handler.hpp`로 분리했고 SM-A6/SM-A7/SM-C4/SM-D11
  focused runtime 검증을 통과했다.
- play actor/channel handler 책임은 `Server/Play/Handlers/play_actor_handlers.hpp`로 분리했고
  SM-A2/SM-B2/SM-B3/SM-B4/SM-B5/SM-A8 focused runtime 검증을 통과했다.
- play endpoint mapping 책임은 `Server/Play/Endpoints/` 아래 파일로 분리했고 SM-A6/SM-C1/SM-D6
  focused runtime 검증을 다시 통과했다.
- play session HTTP bridge 책임은 `Server/Play/Handlers/play_session_handlers.hpp`로 분리했고,
  session lifecycle/auth/relay는 `Server/Session/Handlers/session_session_handlers.hpp` 대응으로
  정리했다.
- play/session operational endpoint의 evidence wait, shutdown, crash route를 shared endpoint handler로
  연결했고, `/evidence/wait`는 SM-A6 focused runtime 검증에서 직접 호출했다.
- play failure endpoint의 missing-handler/missing-target route를 public route client 경로로 연결했고,
  SM-E1 focused runtime 검증에서 직접 호출했다.
- play interaction endpoint의 publish-wait route를 public evidence wait 경로로 연결했고, SM-C4
  focused runtime 검증에서 직접 호출했다.
- play interaction endpoint의 worker start/complete route를 public spot handler와 evidence wait
  경로로 연결했고, SM-A8 focused runtime 검증에서 직접 호출했다.
- SM-F1/SM-F2/SM-F4 scenario 책임은 `Client/Scenarios/sm_f*_scenario.hpp` 파일로 분리했고
  focused runtime 검증에서 route evidence를 확인했다.
- SM-G1 crash/recovery scenario 책임은 `Client/Scenarios/sm_g1_scenario.hpp` 파일로 분리했고
  focused runtime 검증과 full `all` runner에서 crash observation/recovery evidence를 확인했다.
- SM-D10 stream backpressure scenario는 C++ stream connector의 public bounded receive queue 정책에
  맞춰 `Client/Scenarios/sm_d10_scenario.hpp`로 구현했고 focused runtime 검증을 통과했다.
- SM-E2 spot timer tick scenario는 public `spot_context_t::add_timer<THandler>` 경로로
  `Client/Scenarios/sm_e2_scenario.hpp`와 focused runtime 검증을 통과했다.
- SM-E3 idle timer close scenario는 public spot create lifecycle과 `spot_context_t::close()` 경로로
  `Client/Scenarios/sm_e3_scenario.hpp`와 focused runtime 검증을 통과했다.
- SM-E4 overrun timer scenario는 public `timer_options_t`와 `timer_tick_t` evidence 경로로
  `Client/Scenarios/sm_e4_scenario.hpp`와 focused runtime 검증을 통과했다.
- SM-D14 stream TLS scenario는 public stream node TLS server 설정과 C++ stream connector TLS 옵션으로
  `Client/Scenarios/sm_d14_scenario.hpp`와 focused runtime 검증을 통과했다.
- MultiNode scaffold는 build proof까지만 유지하며, `.NET` 전용 SM-Q9는 C++ public route client
  계약 차이 때문에 공통 완료 판정에 넣지 않는다.
- 남은 `gap` 행은 Stage wrapper public contract gap과 SM-D13 heartbeat-stop harness gap이다. 내부 helper,
  raw frame, 테스트 전용 adapter로 메우지 않고 feature-map에 남긴다.
