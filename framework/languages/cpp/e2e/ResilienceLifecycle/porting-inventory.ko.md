# C++ ResilienceLifecycle .NET 기준 포팅 inventory

기준 구현: `framework/languages/dotnet/e2e/ResilienceLifecycle`

현재 C++ `ResilienceLifecycle`은 RegistryMessaging에서 이미 검증한 public framework 흐름을
ResilienceLifecycle 전용 target과 runner 아래로 옮긴 1차 slice다. 전체 config 완료는 아니며, 남은
scenario는 `.NET` 기준 파일명에 맞춰 계속 분리해야 한다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 실행 로그 제외 규칙만 있다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 현재 gap 상태를 과장 없이 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | 전용 role target을 빌드하고 Consumer HTTP smoke, RL-A1/A2/A3/A4/A5, RL-B1/B2/B3/B4/B5/B6, RL-C1/C2/C3/C4, RL-D1/D2/D3/D4/D5 slice를 실행한다. RL-B1, RL-C1, RL-C2, RL-C3, RL-D1, RL-D3, RL-D4, RL-D5는 Consumer HTTP endpoint와 provider evidence로 실행하고, RL-C4는 registry outage 뒤 registry와 provider A를 재기동하고 새 discovery client 복구까지 검증한다. runner 자체의 남은 partial은 process orchestration과 일부 helper 이름 정리다. |
| `Shared/ResilienceLifecycle.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | ResilienceLifecycle 전용 C++ target 묶음이 추가됐다. |
| `Shared/Messages.cs` | `Shared/resilience_lifecycle_contracts.hpp`, `Shared/registry_messaging_contracts.hpp` | shared | partial | ResilienceLifecycle 전용 contract facade를 추가했고 profile request/reply/send/failure/status DTO가 `.NET`식 marker 필드를 지원한다. 내부 파일 이름은 아직 RegistryMessaging 기반이므로 완전한 이름 정리는 남아 있다. |
| `Client/ResilienceLifecycle.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | 전용 client target이 추가됐다. |
| `Client/Program.cs` | `Client/main.cpp`, `Client/Support/client_options.hpp` | client | partial | 전용 target 아래 client dispatcher가 있고 endpoint/scenario env 값을 전용 option 객체로 모은다. scenario 이름 일부는 아직 RegistryMessaging 기반이다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_options.hpp` | support | done | C++ runner가 env로 주입한 endpoint와 scenario 값을 전용 option 객체로 읽는다. |
| `Client/Support/LifecycleApiResult.cs` | `Client/Support/lifecycle_api_result.hpp`, `Client/Support/resilience_request_support.hpp` | support | done | provider evidence HTTP fetch/wait와 lifecycle request helper를 전용 support 파일로 분리했다. |
| `Client/Support/ResilienceProcessManager.cs` | `run_e2e.sh`; `Client/Support/client_support.hpp` | support | partial | shell runner가 process 제어를 담당하고 client helper가 marker 파일을 처리한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/scenario_assert.hpp` | support | done | assertion과 marker-file wait helper를 전용 support 파일로 분리했다. |
| `Client/Support/TopologyEntryResult.cs` | `Client/Support/topology_entry_result.hpp`, `Client/Scenarios/rm_a1_discovery_request_scenario.hpp` | support | done | registry topology response를 typed DTO로 fetch해 provider readiness를 검증한다. |
| `Client/Scenarios/RlA1ProviderRestartScenario.cs` | `Client/Scenarios/rl_a1_provider_restart_scenario.hpp`, `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`, `run_e2e.sh` | scenario | partial | 같은 endpoint restart를 RL 전용 wrapper와 runner orchestration으로 검증한다. 내부 request flow helper는 아직 RM-A4 failover 구현을 재사용한다. |
| `Client/Scenarios/RlA2ProviderEndpointRemapScenario.cs` | `Client/Scenarios/rl_a2_provider_endpoint_remap_scenario.hpp`, `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`, `run_e2e.sh` | scenario | partial | 다른 endpoint remap을 RL 전용 wrapper와 runner orchestration으로 검증한다. 내부 request flow helper는 아직 RM-A4 failover 구현을 재사용한다. |
| `Client/Scenarios/RlA3ReconnectStormScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | 반복 recovery request를 RL 전용 runner scenario 이름으로 검증한다. |
| `Client/Scenarios/RlA4DrainAndGreenEndpointScenario.cs` | `Client/Scenarios/rl_a4_drain_and_green_endpoint_scenario.hpp`, `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`, `run_e2e.sh` | scenario | partial | runtime drain/restore와 blue-green marker를 RL 전용 wrapper로 검증한다. 내부 drain helper는 B4/B5/B6와 공유한다. |
| `Client/Scenarios/RlA5ProviderFlappingScenario.cs` | `Client/Scenarios/rl_a5_provider_flapping_scenario.hpp`, `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`, `run_e2e.sh` | scenario | partial | provider B stop/restart 반복과 follow-up request를 RL 전용 wrapper로 검증한다. 내부 quick probe helper는 A3/C1/C2/C3와 공유한다. |
| `Client/Scenarios/RlB1CancellationCleanupScenario.cs` | `run_e2e.sh`, `Server/Consumer/main.cpp` | scenario | partial | runner가 Consumer HTTP `/profile/request/timeout/100`으로 timeout 실패 payload를 확인하고, 같은 consumer의 `/profile/request` 후속 request 정상화를 검증한다. C++ client wrapper 파일은 남아 있지만 현재 runner는 HTTP 경로를 사용한다. |
| `Client/Scenarios/RlB2CrashDuringInflightScenario.cs` | `Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp`; `run_e2e.sh` | scenario | partial | B provider in-flight crash 실패와 A provider follow-up 성공을 검증한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/Scenarios/RlB3GracefulShutdownScenario.cs` | `Client/Scenarios/rl_b3_graceful_shutdown_scenario.hpp`, `Client/Scenarios/rm_b2_scale_in_scenario.hpp`, `run_e2e.sh` | scenario | partial | provider 정상 종료 뒤 남은 provider로 request가 성공하는지 RL 전용 wrapper로 검증한다. 내부 scale-in helper는 아직 RM-B2 구현을 재사용한다. |
| `Client/Scenarios/RlB4RuntimeDrainScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | runtime drain 후 신규 request 차단과 restore를 검증한다. drain orchestration은 A4/B5/B6와 공유한다. |
| `Client/Scenarios/RlB5DrainInflightScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | drain 중 B provider in-flight reply 유지를 검증한다. B4/B5 shared drain helper 분리는 의도적이다. |
| `Client/Scenarios/RlB6GrayFaultScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | drain/restore 중 healthy provider 수렴을 검증한다. fault injection은 남아 있다. |
| `Client/Scenarios/RlC1ClientHostLifecycleScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | Consumer HTTP `/profile/request/new-client`가 요청마다 transient client host를 만들어 request를 보내고, 반복 request와 cleanup follow-up marker가 provider evidence에 남는지 검증한다. |
| `Client/Scenarios/RlC2TopologyRecoveryScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | provider crash 뒤 Consumer HTTP `/profile/request/new-client`가 `api-a`로 수렴하는지 확인하고, provider B 재기동 뒤 restored marker가 `api-b` evidence에 남는지 검증한다. |
| `Client/Scenarios/RlC3NodePauseRecoveryScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | partial | provider 정지 중 Consumer HTTP `/profile/request`가 `api-a`로 수렴하고, provider B 재기동 뒤 recovered marker가 `api-b` evidence에 남는지 검증한다. split-brain topology DTO 단언은 남아 있다. |
| `Client/Scenarios/RlC4RegistryOutageScenario.cs` | `Client/Scenarios/rl_c4_registry_outage_scenario.hpp`; `run_e2e.sh` | scenario | done | registry outage 중 established manual channel request가 계속 성공하는지 검증한다. registry와 provider A 재기동 뒤 새 discovery client request와 provider evidence도 검증한다. |
| `Client/Scenarios/RlD1HighFanoutScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | runner가 Consumer HTTP `/profile/request`로 120개 request burst를 만들고 provider evidence에서 `rl-d1-` marker가 남는지 검증한다. |
| `Client/Scenarios/RlD2ObserverFaultScenario.cs` | `Client/Scenarios/rl_d2_observer_fault_scenario.hpp` | scenario | done | provider observer fault mode를 켠 뒤 missing request dispatch error evidence, observer exception isolation, follow-up request evidence를 검증한다. |
| `Client/Scenarios/RlD3DispatchErrorEvidenceScenario.cs` | `run_e2e.sh`, `Server/Consumer/main.cpp`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | runner가 Consumer HTTP `/profile/request/missing`을 호출하고 provider flow log에서 missing request의 `handler_missing`/`reply_error` marker를 검증한다. |
| `Client/Scenarios/RlD4MissingRequestHandlerScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | runner가 Consumer HTTP `/profile/request/missing`을 호출하고 typed success reply 대신 public failure payload와 dispatch error flow marker를 검증한다. |
| `Client/Scenarios/RlD5MixedBurstScenario.cs` | `run_e2e.sh`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | scenario | done | runner가 Consumer HTTP `/profile/request`와 `/profile/command`로 request/send mixed burst workload를 만들고 provider evidence에서 `rl-d5-req-`, `rl-d5-cmd-` marker가 남는지 검증한다. |
| `Server/Registry/ResilienceLifecycle.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | registry role target이 추가됐다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | done | registry role 진입점이 있다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry_host_factory.hpp`, `Server/Registry/main.cpp` | server-role | done | registry framework, registry service, HTTP health/topology endpoint wiring은 factory header로 분리했고 main은 진입점과 logging만 담당한다. |
| `Server/Registry/Configuration/ServerOptions.cs` | `Server/Registry/Configuration/registry_options.hpp` | configuration | partial | registry endpoint option을 해석한다. provider/consumer 통합 option은 남아 있다. |
| `Server/Registry/Endpoints/RegistryEndpoints.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp` | endpoint | partial | topology endpoint와 registry evidence 조회/clear endpoint가 있다. topology wait, shutdown, full lifecycle admin endpoint는 남아 있다. |
| `Server/Registry/Endpoints/TopologyEntryResult.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp` | endpoint | partial | topology response를 JSON으로 직접 만든다. 전용 DTO 파일은 남아 있다. |
| `Server/Registry/Handlers/RegistryHandlers.cs` | `Server/Registry/Handlers/registry_handlers.hpp`, `Server/Registry/Endpoints/registry_endpoints.hpp` | handler | partial | registry topology query handler가 endpoint 파일에 있고, 선택적 registry channel endpoint가 설정되면 profile request/send handler와 dispatch error observer를 설치한다. profile handler는 marker 필드를 우선 evidence marker로 기록하고, marker가 없으면 기존 value/command id를 사용한다. |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | `Server/Registry/Infrastructure/evidence_store.hpp` | infrastructure | done | registry evidence store가 있고 `/evidence`, `/evidence/clear` endpoint에서 사용한다. profile/dispatch evidence 기록은 handler gap으로 남아 있다. |
| `Server/Registry/Infrastructure/FaultState.cs` | `Server/Registry/Infrastructure/fault_state.hpp` | infrastructure | done | registry fault mode state를 DI에 등록하고 registry profile handler/observer가 참조한다. fault mode admin endpoint는 아직 없다. |
| `Server/Provider/ResilienceLifecycle.Provider.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | provider role target이 추가됐다. |
| `Server/Provider/Program.cs` | `Server/Provider/main.cpp` | server-role | done | provider role 진입점이 있다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/provider_host_factory.hpp`, `Server/Provider/main.cpp` | server-role | done | provider framework, discovery, channel/route/http endpoint, handler group wiring은 factory header로 분리했고 main은 진입점과 logging만 담당한다. |
| `Server/Provider/ProviderEndpoints.cs` | `Server/Provider/Endpoints/provider_endpoints.hpp` | endpoint | partial | evidence, server-weight admin, observer fault mode endpoint가 있다. |
| `Server/Provider/ProviderSupport.cs` | `Server/Provider/Configuration/provider_options.hpp`; `Server/Provider/Infrastructure/evidence_store.hpp`; `Server/Provider/Infrastructure/fault_state.hpp` | support | partial | provider option, evidence store, observer fault mode state를 분리했다. drain/gray fault 전용 파일 분리는 남아 있다. |
| `Server/Provider/Handlers/EvidenceDispatchErrorObserver.cs` | `Server/Provider/Handlers/evidence_dispatch_error_observer.hpp`; `Server/Provider/Infrastructure/evidence_store.hpp`; `Server/Provider/Infrastructure/fault_state.hpp` | handler | done | message flow observer가 dispatch error를 evidence store에 기록하고 fault state가 `observer-throws`일 때 예외를 던진다. Provider host factory는 전용 observer helper를 설치한다. |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/Handlers/provider_handlers.hpp` | handler | partial | request/send/slow handler가 있다. gray fault handler는 남아 있다. |
| `Server/Consumer/ResilienceLifecycle.Consumer.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | consumer role target이 추가됐다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/main.cpp` | server-role | done | consumer role 진입점이 있고, ResilienceLifecycle 전용 consumer configuration/endpoint wrapper를 사용한다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp`, `Server/Consumer/Configuration/consumer_options.hpp`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | server-role | partial | long-running consumer HTTP host가 `/health`, `/profile/request`, `/profile/request/timeout/100`, `/profile/request/missing`, `/profile/command`, `/profile/request/new-client` endpoint를 제공한다. `/profile/request/new-client`는 `.NET`처럼 요청마다 새 client host를 만들고 별도 flow log를 남긴다. option 읽기와 endpoint handler 이름은 ResilienceLifecycle 전용 wrapper 경로로 분리했고, endpoint handler는 ResilienceLifecycle marker contract를 보존한다. runner는 smoke, RL-B1, RL-C1, RL-C2, RL-C3, RL-D1, RL-D3, RL-D4, RL-D5에서 이 HTTP 경로를 사용한다. 남은 partial은 host factory 파일명/분리 수준 정리다. |

## Scenario ID 대응

| Scenario ID | C++ 대응 | 상태 |
|-------------|----------|------|
| `RL-A1` | `Client/Scenarios/rl_a1_provider_restart_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A2` | `Client/Scenarios/rl_a2_provider_endpoint_remap_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A3` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A4` | `Client/Scenarios/rl_a4_drain_and_green_endpoint_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A5` | `Client/Scenarios/rl_a5_provider_flapping_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B1` | `run_e2e.sh`; `Server/Consumer/main.cpp` | partial |
| `RL-B2` | `Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B3` | `Client/Scenarios/rl_b3_graceful_shutdown_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B4` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B5` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B6` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-C1` | `Server/Consumer/Endpoints/consumer_endpoints.hpp`; `run_e2e.sh` | done |
| `RL-C2` | `Server/Consumer/Endpoints/consumer_endpoints.hpp`; `run_e2e.sh` | done |
| `RL-C3` | `Server/Consumer/Endpoints/consumer_endpoints.hpp`; `run_e2e.sh` | partial |
| `RL-C4` | `Client/Scenarios/rl_c4_registry_outage_scenario.hpp`; `run_e2e.sh` | done |
| `RL-D1` | `run_e2e.sh`; `Server/Consumer/Endpoints/consumer_endpoints.hpp` | done |
| `RL-D2` | `Client/Scenarios/rl_d2_observer_fault_scenario.hpp` | done |
| `RL-D3` | `run_e2e.sh`; `Server/Consumer/main.cpp`; `Server/Consumer/Endpoints/consumer_endpoints.hpp` | done |
| `RL-D4` | `run_e2e.sh`; `Server/Consumer/Endpoints/consumer_endpoints.hpp` | done |
| `RL-D5` | `run_e2e.sh`; `Server/Consumer/Endpoints/consumer_endpoints.hpp` | done |

## 검증

- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-184559-770532`
  - 의미: 현재 runner에 포함된 RL-A1, RL-A2, RL-A3, RL-A4, RL-A5, RL-B1, RL-B2, RL-B3,
    RL-B4, RL-B5, RL-B6, RL-C1, RL-C2, RL-C3, RL-C4, RL-D1, RL-D2, RL-D3, RL-D4, RL-D5 slice는
    통과한다. RL-C4는 registry outage 중 established channel 유지와 registry/provider A 재기동 뒤
    새 discovery client 복구까지 검증한다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-214445-1305240`
  - 의미: Consumer role target을 추가하고 runner가 `/profile/request` smoke와 `consumer-flow.log`
    message-flow를 확인한 뒤, 기존 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-235157-1506017`
  - 의미: Client support를 `client_options.hpp`, `scenario_assert.hpp`, `lifecycle_api_result.hpp`,
    `topology_entry_result.hpp`로 분리하고, topology readiness 검증을 typed DTO fetch 경로로 바꾼 뒤
    RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-235704-1515536`
  - 의미: RL-A1, RL-A2, RL-B1, RL-B3, RL-D3를 ResilienceLifecycle 전용 scenario wrapper와 runner
    scenario 이름으로 실행한 뒤 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-000224-1525059`
  - 의미: RL-A3, RL-A4, RL-A5, RL-C1, RL-C2, RL-C3도 ResilienceLifecycle 전용 scenario wrapper와
    runner scenario 이름으로 실행한 뒤 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-001148-1542721`
  - 의미: RL-B1은 Consumer HTTP `/profile/request/timeout/100`과 후속 `/profile/request`로 검증하고,
    RL-D3는 Consumer HTTP `/profile/request/missing`, `/profile/command` 및 provider flow log의
    `handler_missing`/`reply_error`, `handler_missing`/`drop` marker로 검증한 뒤 RL-A/B/C/D slice
    전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_consumer`
  - 결과: 통과
  - 의미: Consumer role main이 ResilienceLifecycle 전용 `Configuration/consumer_options.hpp`와
    `Endpoints/consumer_endpoints.hpp` wrapper를 사용하도록 바꾼 뒤 consumer target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-003306-1573336`
  - 의미: Consumer configuration/endpoint wrapper 분리 뒤에도 Consumer smoke, RL-B1, RL-D3와
    RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_provider`
  - 결과: 통과
  - 의미: Provider dispatch error observer를 `Handlers/evidence_dispatch_error_observer.hpp`로 분리한 뒤
    provider target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-003804-1582520`
  - 의미: Provider dispatch error observer 분리 뒤에도 RL-D3 provider flow marker, RL-D2 observer fault
    isolation, Consumer smoke, RL-B1, RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_registry`
  - 결과: 통과
  - 의미: Registry host wiring을 `Server/Registry/registry_host_factory.hpp`로 분리한 뒤 registry target이
    빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-004138-1590362`
  - 의미: Registry host factory 분리 뒤에도 Consumer smoke, RL-A/B/C/D slice 전체와 registry outage/recovery
    검증이 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_provider`
  - 결과: 통과
  - 의미: Provider host wiring을 `Server/Provider/provider_host_factory.hpp`로 분리한 뒤 provider target이
    빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-004544-1598577`
  - 의미: Provider host factory 분리 뒤에도 Consumer smoke, RL-D2/RL-D3 observer/dispatch evidence,
    provider restart/drain, registry outage/recovery를 포함한 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_provider`
  - 결과: 통과
  - 의미: Provider observer fault mode를 `Server/Provider/Infrastructure/fault_state.hpp`로 분리한 뒤
    provider target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-005337-1614770`
  - 의미: Provider observer fault mode state 분리 뒤에도 RL-D2 observer fault isolation, RL-D3 dispatch
    evidence, Consumer smoke, provider restart/drain, registry outage/recovery를 포함한 RL-A/B/C/D slice
    전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_provider`
  - 결과: 통과
  - 의미: Provider evidence state를 `.NET`의 `EvidenceStore` 역할에 맞춰
    `Server/Provider/Infrastructure/evidence_store.hpp`로 분리한 뒤 provider target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-005924-1624879`
  - 의미: Provider evidence store 분리 뒤에도 RL-D2 observer fault isolation, RL-D3 dispatch evidence,
    Consumer smoke, provider restart/drain, registry outage/recovery를 포함한 RL-A/B/C/D slice 전체가
    다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_registry`
  - 결과: 통과
  - 의미: Registry evidence store와 fault state를 `Server/Registry/Infrastructure/`로 분리하고
    `/evidence`, `/evidence/clear`, `/topology` alias를 추가한 뒤 registry target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-010541-1635780`
  - 의미: Registry evidence/fault infrastructure와 evidence endpoint 추가 뒤에도 Consumer smoke,
    RL-D2/RL-D3 dispatch evidence, provider restart/drain, registry outage/recovery를 포함한 RL-A/B/C/D
    slice 전체가 다시 통과했다.
- 2026-07-01: focused registry handler check
  - 결과: 통과
  - 로그: `logs/focused-registry-1645959`
  - 의미: Registry role을 선택적 channel endpoint와 함께 띄우고 `rm-a2` manual client가 해당 endpoint로
    request를 보낸 뒤 `/evidence`에서 `profile-request|rid=api-a|marker=manual|value=manual` evidence를
    확인했다.
- 2026-07-01: focused marker contract check
  - 결과: 통과
  - 로그: `logs/focused-marker-1656661`
  - 의미: profile request의 `value=manual`, `marker=manual-marker`를 분리해 보내고 reply marker와
    registry evidence `profile-request|rid=api-a|marker=manual-marker|value=manual`을 확인했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-011221-1647207`
  - 의미: 선택적 Registry profile handler와 dispatch error observer 추가 뒤에도 기본 runner 경로의
    Consumer smoke, RL-D2/RL-D3 dispatch evidence, provider restart/drain, registry outage/recovery를
    포함한 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-011837-1657861`
  - 의미: profile request/reply/send DTO에 marker 필드를 추가하고 Provider/Registry handler가 marker를
    우선 evidence marker로 쓰도록 바꾼 뒤에도 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_consumer`
  - 결과: 통과
  - 의미: Consumer `/profile/request/new-client` endpoint가 transient client host를 생성하도록 추가한 뒤
    consumer target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-012511-1669180`
  - 의미: Consumer HTTP `/profile/request/new-client`가 별도 `storm-rl-c1-new-client-flow.log` message-flow를
    남기고 성공한 뒤에도 Consumer smoke, RL-B1, RL-D3, provider restart/drain, registry outage/recovery를
    포함한 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_resilience_lifecycle_consumer -j 4`
  - 결과: 통과
  - 의미: Consumer endpoint handler를 ResilienceLifecycle 전용 contract로 분리하고 `/profile/command`를
    정상 `ProfileMsg` send 경로로 맞춘 뒤 consumer target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-021730-1750031`
  - 의미: Consumer HTTP 경로가 marker를 보존하고, RL-D1 request burst, RL-D4 missing request, RL-D5
    request/send mixed burst evidence를 검증한 뒤에도 RL-A/B/C/D slice 전체가 다시 통과했다.
- 2026-07-01: `timeout 1200s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-031111-1848503`
  - 의미: RL-C1 반복 new-client/cleanup, RL-C2 provider crash 뒤 `api-a` 수렴과 `api-b` restored evidence,
    RL-C3 provider down 중 `api-a` 수렴과 recovery 뒤 `api-b` evidence를 `.NET`처럼 Consumer HTTP 경로로
    검증한 뒤에도 RL-A/B/C/D slice 전체가 다시 통과했다.
