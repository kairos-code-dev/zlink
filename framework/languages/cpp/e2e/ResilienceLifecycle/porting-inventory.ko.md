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
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | 전용 role target을 빌드하고 RL-A1/A2/A3/A4/A5, RL-B1/B2/B3/B4/B5/B6, RL-C1/C2/C3/C4, RL-D1/D2/D3/D4/D5 slice를 실행한다. RL-C4는 registry outage 뒤 registry와 provider A를 재기동하고 새 discovery client 복구까지 검증한다. |
| `Shared/ResilienceLifecycle.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | ResilienceLifecycle 전용 C++ target 묶음이 추가됐다. |
| `Shared/Messages.cs` | `Shared/registry_messaging_contracts.hpp` | shared | partial | 현재 slice는 RegistryMessaging contract를 재사용한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/ResilienceLifecycle.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | 전용 client target이 추가됐다. |
| `Client/Program.cs` | `Client/main.cpp` | client | partial | 전용 target 아래 client dispatcher가 있지만 scenario 이름은 RegistryMessaging 기반이다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | support | partial | env/endpoint option helper를 포함하지만 `.NET` 파일명 단위 분리는 남아 있다. |
| `Client/Support/LifecycleApiResult.cs` | `Client/Support/client_support.hpp`; `Client/Support/resilience_request_support.hpp` | support | partial | evidence HTTP result DTO와 request helper를 RegistryMessaging snapshot으로 재사용한다. |
| `Client/Support/ResilienceProcessManager.cs` | `run_e2e.sh`; `Client/Support/client_support.hpp` | support | partial | shell runner가 process 제어를 담당하고 client helper가 marker 파일을 처리한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp` | support | partial | assertion helper는 존재하지만 전용 파일명 분리는 남아 있다. |
| `Client/Support/TopologyEntryResult.cs` | `Client/Support/client_support.hpp` | support | gap | topology DTO 전용 분리는 아직 없다. |
| `Client/Scenarios/RlA1ProviderRestartScenario.cs` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`; `run_e2e.sh` | scenario | partial | 같은 endpoint restart를 `RM-A4` failover flow와 runner orchestration으로 검증한다. |
| `Client/Scenarios/RlA2ProviderEndpointRemapScenario.cs` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`; `run_e2e.sh` | scenario | partial | 다른 endpoint remap을 `RM-A4` failover flow와 runner orchestration으로 검증한다. |
| `Client/Scenarios/RlA3ReconnectStormScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | 반복 recovery request로 reconnect storm을 검증한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/Scenarios/RlA4DrainAndGreenEndpointScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | runtime drain/restore와 blue-green marker를 검증한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/Scenarios/RlA5ProviderFlappingScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | provider B stop/restart 반복과 follow-up request를 검증한다. provider flapping 전용 header 분리는 남아 있다. |
| `Client/Scenarios/RlB1CancellationCleanupScenario.cs` | `Client/Scenarios/rm_c4_timeout_isolation_scenario.hpp` | scenario | partial | timeout 뒤 후속 request 정상화는 검증한다. 전용 파일명 분리는 남아 있다. |
| `Client/Scenarios/RlB2CrashDuringInflightScenario.cs` | `Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp`; `run_e2e.sh` | scenario | partial | B provider in-flight crash 실패와 A provider follow-up 성공을 검증한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/Scenarios/RlB3GracefulShutdownScenario.cs` | `Client/Scenarios/rm_b2_scale_in_scenario.hpp`; `run_e2e.sh` | scenario | partial | provider 정상 종료 뒤 남은 provider로 request가 성공하는지 검증한다. 전용 topology DTO는 남아 있다. |
| `Client/Scenarios/RlB4RuntimeDrainScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | runtime drain 후 신규 request 차단과 restore를 검증한다. 전용 contract 이름 정리는 남아 있다. |
| `Client/Scenarios/RlB5DrainInflightScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | drain 중 B provider in-flight reply 유지를 검증한다. B4/B5 shared drain helper 분리는 의도적이다. |
| `Client/Scenarios/RlB6GrayFaultScenario.cs` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | scenario | partial | drain/restore 중 healthy provider 수렴을 검증한다. fault injection은 남아 있다. |
| `Client/Scenarios/RlC1ClientHostLifecycleScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | 반복 client 실행 뒤 follow-up request 성공과 정상 종료를 검증한다. C1 전용 header 분리는 남아 있다. |
| `Client/Scenarios/RlC2TopologyRecoveryScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | provider crash 뒤 public retry window 안의 follow-up request 성공을 검증한다. topology DTO 단언은 남아 있다. |
| `Client/Scenarios/RlC3NodePauseRecoveryScenario.cs` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | scenario | partial | provider 정지/복구 뒤 각각 follow-up request 성공을 검증한다. split-brain topology 단언은 남아 있다. |
| `Client/Scenarios/RlC4RegistryOutageScenario.cs` | `Client/Scenarios/rl_c4_registry_outage_scenario.hpp`; `run_e2e.sh` | scenario | done | registry outage 중 established manual channel request가 계속 성공하는지 검증한다. registry와 provider A 재기동 뒤 새 discovery client request와 provider evidence도 검증한다. |
| `Client/Scenarios/RlD1HighFanoutScenario.cs` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | scenario | partial | burst request workload를 검증한다. 장시간 high fanout soak는 남아 있다. |
| `Client/Scenarios/RlD2ObserverFaultScenario.cs` | `Client/Scenarios/rl_d2_observer_fault_scenario.hpp` | scenario | done | provider observer fault mode를 켠 뒤 missing request dispatch error evidence, observer exception isolation, follow-up request evidence를 검증한다. |
| `Client/Scenarios/RlD3DispatchErrorEvidenceScenario.cs` | `Client/Scenarios/rm_c5_missing_packet_scenario.hpp`; `run_e2e.sh` | scenario | partial | missing send 뒤 provider flow log의 dispatch error marker를 검증한다. |
| `Client/Scenarios/RlD4MissingRequestHandlerScenario.cs` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | scenario | partial | missing request가 typed reply 없이 public error path로 끝나는지 검증한다. raw wire code 검증은 남아 있다. |
| `Client/Scenarios/RlD5MixedBurstScenario.cs` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | scenario | partial | request/send mixed burst workload를 검증한다. 장시간 soak는 남아 있다. |
| `Server/Registry/ResilienceLifecycle.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | registry role target이 추가됐다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | done | registry role 진입점이 있다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp` | server-role | partial | C++ registry host 구성은 main에 있다. 별도 factory 파일 분리는 남아 있다. |
| `Server/Registry/Configuration/ServerOptions.cs` | `Server/Registry/Configuration/registry_options.hpp` | configuration | partial | registry endpoint option을 해석한다. provider/consumer 통합 option은 남아 있다. |
| `Server/Registry/Endpoints/RegistryEndpoints.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp` | endpoint | partial | topology endpoint가 있다. full lifecycle evidence/admin endpoint는 남아 있다. |
| `Server/Registry/Endpoints/TopologyEntryResult.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp` | endpoint | partial | topology response를 JSON으로 직접 만든다. 전용 DTO 파일은 남아 있다. |
| `Server/Registry/Handlers/RegistryHandlers.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp` | handler | partial | registry topology query handler가 endpoint 파일에 있다. |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | `Server/Registry/Infrastructure/evidence_store.hpp` | infrastructure | gap | scenario evidence store가 필요하다. |
| `Server/Registry/Infrastructure/FaultState.cs` | `Server/Registry/Infrastructure/fault_state.hpp` | infrastructure | gap | fault injection state가 필요하다. |
| `Server/Provider/ResilienceLifecycle.Provider.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | provider role target이 추가됐다. |
| `Server/Provider/Program.cs` | `Server/Provider/main.cpp` | server-role | done | provider role 진입점이 있다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/main.cpp` | server-role | partial | C++ provider host 구성은 main에 있다. 별도 factory 파일 분리는 남아 있다. |
| `Server/Provider/ProviderEndpoints.cs` | `Server/Provider/Endpoints/provider_endpoints.hpp` | endpoint | partial | evidence, server-weight admin, observer fault mode endpoint가 있다. |
| `Server/Provider/ProviderSupport.cs` | `Server/Provider/Configuration/provider_options.hpp`; `Server/Provider/Infrastructure/scenario_state.hpp` | support | partial | provider option, scenario state, observer fault mode state가 있다. fault/drain 전용 파일 분리는 남아 있다. |
| `Server/Provider/Handlers/EvidenceDispatchErrorObserver.cs` | `Server/Provider/main.cpp`; `Server/Provider/Infrastructure/scenario_state.hpp` | handler | partial | message flow observer가 dispatch error를 state에 기록하고 fault mode에서는 예외를 던진다. 전용 observer 파일은 남아 있다. |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/Handlers/provider_handlers.hpp` | handler | partial | request/send/slow handler가 있다. gray fault handler는 남아 있다. |
| `Server/Consumer/ResilienceLifecycle.Consumer.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | gap | consumer role target이 필요하다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/main.cpp` | server-role | gap | consumer role 진입점이 필요하다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp` | server-role | gap | long-running consumer host 구성이 필요하다. |

## Scenario ID 대응

| Scenario ID | C++ 대응 | 상태 |
|-------------|----------|------|
| `RL-A1` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A2` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A3` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A4` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-A5` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B1` | `Client/Scenarios/rm_c4_timeout_isolation_scenario.hpp` | partial |
| `RL-B2` | `Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B3` | `Client/Scenarios/rm_b2_scale_in_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B4` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B5` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-B6` | `Client/Scenarios/rl_b4_runtime_drain_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-C1` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-C2` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-C3` | `Client/Scenarios/rl_a3_reconnect_storm_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-C4` | `Client/Scenarios/rl_c4_registry_outage_scenario.hpp`; `run_e2e.sh` | done |
| `RL-D1` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | partial |
| `RL-D2` | `Client/Scenarios/rl_d2_observer_fault_scenario.hpp` | done |
| `RL-D3` | `Client/Scenarios/rm_c5_missing_packet_scenario.hpp`; `run_e2e.sh` | partial |
| `RL-D4` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | partial |
| `RL-D5` | `Client/Scenarios/rl_d1_high_fanout_scenario.hpp` | partial |

## 검증

- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/ResilienceLifecycle/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-184559-770532`
  - 의미: 현재 runner에 포함된 RL-A1, RL-A2, RL-A3, RL-A4, RL-A5, RL-B1, RL-B2, RL-B3,
    RL-B4, RL-B5, RL-B6, RL-C1, RL-C2, RL-C3, RL-C4, RL-D1, RL-D2, RL-D3, RL-D4, RL-D5 slice는
    통과한다. RL-C4는 registry outage 중 established channel 유지와 registry/provider A 재기동 뒤
    새 discovery client 복구까지 검증한다.
