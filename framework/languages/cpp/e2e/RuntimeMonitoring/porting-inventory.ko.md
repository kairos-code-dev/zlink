# C++ RuntimeMonitoring .NET porting inventory

기준 구현: `framework/languages/dotnet/e2e/RuntimeMonitoring`

| .NET 파일 | C++ 대응 | 분류 | 상태 | 비고 |
|-----------|----------|------|------|------|
| `.gitignore` | `.gitignore` | metadata | done | logs 제외를 유지한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 현재 gap/부분 구현 상태를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | registry, service, filtered service, throwing service, trigger, client role executable을 실행하고 구현된 부분 slice를 검증한다. |
| `Shared/RuntimeMonitoring.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | C++ shared header는 별도 target 없이 client target에 포함된다. |
| `Shared/Messages.cs` | `Shared/runtime_monitoring_contracts.hpp` | shared | done | profile message, monitoring source 이름, evidence wait request를 C++ role들이 사용한다. |
| `Client/RuntimeMonitoring.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | client target만 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client | partial | 구현된 RuntimeMonitoring slice와 재시작 검증 slice를 dispatch하고 trigger URL이 있으면 trigger role을 통해 request를 보낸다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | support | gap | HTTP endpoint option parsing은 아직 없다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp` | support | partial | assertion helper만 있다. |
| `Client/Scenarios/MonA1SocketEventsScenario.cs` | `Client/Scenarios/mon_a1_socket_events_scenario.hpp` | scenario | done | trigger role이 service A에 transient request를 보내고 socket `Connected`, `ConnectionReady`, `Disconnected` event와 remote address evidence를 검증한다. |
| `Client/Scenarios/MonA2RegistryEventsScenario.cs` | `Client/Scenarios/mon_a2_registry_events_scenario.hpp` | scenario | done | standalone registry role의 topology/service summary evidence를 검증한다. |
| `Client/Scenarios/MonA3SpotEventsScenario.cs` | `Client/Scenarios/mon_a3_spot_events_scenario.hpp` | scenario | done | registry-discovered TCP SPOT mesh peer로 `PeersChanged`, spot create 뒤 `SubjectsChanged`, failing timer 뒤 `TimerHandlerFailed` evidence를 검증한다. |
| `Client/Scenarios/MonA4AvailabilityTransitionScenario.cs` | `Client/Scenarios/mon_a4_availability_transition_scenario.hpp` | scenario | partial | drain/restore admin evidence, socket admission event, registry topology evidence를 검증한다. provider 교체/failover socket transition은 남아 있다. |
| `Client/Scenarios/MonA5FixedKindsScenario.cs` | `Client/Scenarios/mon_a5_fixed_kinds_scenario.hpp` | scenario | done | trigger의 invalid handshake action으로 socket `HandshakeFailed` evidence를 검증하고, registry `StatusChanged`, spot `StatusChanged`, `TimerStoppedAfterUnhandledException` evidence도 검증한다. |
| `Client/Scenarios/MonB1KindFilterScenario.cs` | `Client/Scenarios/mon_b1_kind_filter_scenario.hpp` | scenario | done | filtered service의 socket event kind filter를 `ConnectionReady` event로 검증한다. |
| `Client/Scenarios/MonB2RegistrationValidationScenario.cs` | `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | scenario | done | 중복 source, 비양수 interval, missing spot/socket source framework 적용 검증을 확인한다. |
| `Client/Scenarios/MonC1DispatchFailureScenario.cs` | `Client/Scenarios/mon_c1_dispatch_failure_scenario.hpp` | scenario | done | throwing service evidence, stderr marker, follow-up request recovery를 검증한다. |
| `Client/Scenarios/MonD1FailureRecoveryScenario.cs` | `Client/Scenarios/mon_d1_failure_recovery_scenario.hpp` | scenario | done | service stop/restart 뒤 direct request, restarted service evidence, restart 이후 registry topology continuity evidence를 검증한다. |
| `Server/Registry/RuntimeMonitoring.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | registry target이 있고 shared evidence wait endpoint를 사용한다. shutdown endpoint 분리는 남아 있다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | partial | registry role이 registry monitoring source와 evidence endpoint를 구성하고 원격 service topology/summary event를 검증한다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp` | server-role | partial | C++ registry host 구성은 main에 있다. factory 분리는 남아 있다. |
| `Server/Registry/Handlers/RegistryEventRecorders.cs` | `Server/Registry/main.cpp` | handler | partial | registry event recorder lambda가 있다. 별도 handler 파일 분리는 남아 있다. |
| `Server/Registry/Handlers/RegistryHandlers.cs` | `Server/Shared/evidence_store.hpp` | handler | partial | evidence GET handler와 wait endpoint handler가 있다. registry 전용 profile handler 분리는 남아 있다. |
| `Server/Registry/Support/RegistryEvidenceStore.cs` | `Server/Shared/evidence_store.hpp` | support | done | shared evidence store와 waiter API가 있다. |
| `Server/Registry/Support/RegistryOptions.cs` | `Server/Registry/Support/registry_options.hpp` | support | partial | env 기반 endpoint option parser가 있다. |
| `Server/Service/RuntimeMonitoring.Service.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | service target이 있다. |
| `Server/Service/Program.cs` | `Server/Service/main.cpp` | server-role | partial | service role이 request/admin/spot/evidence path와 socket/spot monitoring recorder를 제공한다. |
| `Server/Service/ServiceHostFactory.cs` | `Server/Service/main.cpp` | server-role | partial | C++ service host 구성은 main에 있다. factory 분리는 남아 있다. |
| `Server/Service/Handlers/ServiceEventRecorders.cs` | `Server/Service/main.cpp` | handler | partial | socket event recorder와 spot/timer event recorder lambda가 있다. registry recorder는 남아 있다. |
| `Server/Service/Handlers/ServiceHandlers.cs` | `Server/Service/Handlers/service_handlers.hpp` | handler | partial | profile request, server weight admin, spot create handler, failing timer handler가 있다. |
| `Server/Service/Support/ServiceEvidenceStore.cs` | `Server/Shared/evidence_store.hpp` | support | done | shared evidence store와 waiter API가 있다. |
| `Server/Service/Support/ServiceOptions.cs` | `Server/Service/Support/service_options.hpp` | support | partial | env 기반 endpoint option parser가 있다. |
| `Server/FilteredService/RuntimeMonitoring.FilteredService.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | filtered service target이 있다. |
| `Server/FilteredService/Program.cs` | `Server/FilteredService/main.cpp` | server-role | done | filtered service executable이 socket event kind filter profile로 실행된다. |
| `Server/FilteredService/FilteredServiceHostFactory.cs` | `Server/Service/Support/service_host.hpp` | server-role | partial | service host 구성을 공유한다. 별도 factory 파일 분리는 남아 있다. |
| `Server/ThrowingService/RuntimeMonitoring.ThrowingService.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | throwing service target이 있다. |
| `Server/ThrowingService/Program.cs` | `Server/ThrowingService/main.cpp` | server-role | done | throwing service executable이 monitoring handler 예외 profile로 실행된다. |
| `Server/ThrowingService/ThrowingServiceHostFactory.cs` | `Server/Service/Support/service_host.hpp` | server-role | partial | service host 구성을 공유한다. 별도 factory 파일 분리는 남아 있다. |
| `Server/Trigger/RuntimeMonitoring.Trigger.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | trigger executable target이 있다. |
| `Server/Trigger/Program.cs` | `Server/Trigger/main.cpp` | server-role | partial | trigger role이 HTTP endpoint, discovery client, evidence store, throw stderr log wait, validation endpoint, handshake failure endpoint를 구성한다. socket event recorder는 남아 있다. |
| `Server/Trigger/TriggerHostFactory.cs` | `Server/Trigger/main.cpp` | server-role | partial | C++ trigger host 구성은 main에 있다. factory 분리는 남아 있다. |
| `Server/Trigger/TriggerEndpoints.cs` | `Server/Trigger/trigger_endpoints.hpp`, `Server/Shared/evidence_store.hpp` | endpoint | partial | health, evidence, evidence/wait, profile request, service-a/service-b direct request, generic recovery request, throw stderr log wait, validation endpoint, handshake failure endpoint가 있다. |
| `Server/Trigger/TriggerHandlers.cs` | `Server/Trigger/trigger_endpoints.hpp` | handler | partial | profile request, service-a/service-b/throw direct request, validation handler가 있다. socket event recorder는 남아 있다. |
| `Server/Trigger/Support/TriggerClientRequests.cs` | `Server/Trigger/Support/trigger_client_requests.hpp` | support | done | transient direct channel request helper와 invalid handshake sender가 있다. |
| `Server/Trigger/Support/TriggerLogReader.cs` | `Server/Trigger/Support/trigger_log_reader.hpp` | support | done | throw stderr wait helper가 있다. |
| `Server/Trigger/Support/TriggerSupport.cs` | `Server/Trigger/Support/trigger_options.hpp`, `Server/Shared/evidence_store.hpp` | support | partial | env 기반 trigger option parser와 shared evidence store가 있다. CLI argument parser와 trigger 전용 waiter store 분리는 남아 있다. |
| `Server/Trigger/Support/TriggerValidation.cs` | `Server/Trigger/Support/trigger_validation.hpp`, `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | support | done | trigger validation endpoint가 public builder/framework 적용 오류를 실행하고 client가 결과를 단언한다. |

## Scenario 대응

| Scenario | C++ 대응 | 상태 |
|----------|----------|------|
| `MON-A1` | `Client/Scenarios/mon_a1_socket_events_scenario.hpp` | done |
| `MON-A2` | `Client/Scenarios/mon_a2_registry_events_scenario.hpp` | done |
| `MON-A3` | `Client/Scenarios/mon_a3_spot_events_scenario.hpp` | done |
| `MON-A4` | `Client/Scenarios/mon_a4_availability_transition_scenario.hpp` | partial |
| `MON-A5` | `Client/Scenarios/mon_a5_fixed_kinds_scenario.hpp` | done |
| `MON-B1` | `Client/Scenarios/mon_b1_kind_filter_scenario.hpp` | done |
| `MON-B2` | `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | done |
| `MON-C1` | `Client/Scenarios/mon_c1_dispatch_failure_scenario.hpp` | done |
| `MON-D1` | `Client/Scenarios/mon_d1_failure_recovery_scenario.hpp` | done |

## 검증

- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_service zlink_cpp_e2e_runtime_monitoring_filtered_service zlink_cpp_e2e_runtime_monitoring_throwing_service zlink_cpp_e2e_runtime_monitoring_registry zlink_cpp_e2e_runtime_monitoring_trigger zlink_cpp_e2e_runtime_monitoring_client`
  - 결과: 통과
- 2026-06-30: `./framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-083319-3292697`
  - 의미: registry, service, filtered service, throwing service, trigger, client role target이 같은 gate에서
    MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
