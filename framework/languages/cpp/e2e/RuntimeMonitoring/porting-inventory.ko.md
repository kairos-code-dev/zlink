# C++ RuntimeMonitoring .NET porting inventory

기준 구현: `framework/languages/dotnet/e2e/RuntimeMonitoring`

| .NET 파일 | C++ 대응 | 분류 | 상태 | 비고 |
|-----------|----------|------|------|------|
| `.gitignore` | `.gitignore` | metadata | done | logs 제외를 유지한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 현재 gap/부분 구현 상태를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | registry, service, filtered service, throwing service, trigger, client role executable을 실행하고 MON-A1~MON-D1 전체 scenario gate를 검증한다. MON-D1은 service restart를 위해 별도 client invocation으로 실행한다. |
| `Shared/RuntimeMonitoring.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | not-needed | C++ shared contract는 `Shared/runtime_monitoring_contracts.hpp` header로 각 role/client target에 포함된다. 별도 shared binary target은 필요 없다. |
| `Shared/Messages.cs` | `Shared/runtime_monitoring_contracts.hpp` | shared | done | profile message, monitoring source 이름, evidence wait request를 C++ role들이 사용한다. |
| `Client/RuntimeMonitoring.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_runtime_monitoring_client` target이 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client | done | common MON-A/B/C slice와 MON-D1 재시작 검증 slice를 dispatch하고 `client_options.hpp`에서 읽은 endpoint option을 scenario에 전달한다. runner가 두 client invocation을 모두 실행해 `.NET` Program의 전체 scenario 실행 의미를 맞춘다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_options.hpp` | support | done | C++ runner의 env 기반 실행 규약에 맞춰 scenario, registry router, HTTP endpoint, direct channel endpoint option을 한 곳에서 읽는다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp` | support | done | assertion helper뿐 아니라 evidence wait, evidence count, log wait, trigger HTTP request helper를 scenario들이 공유한다. |
| `Client/Scenarios/MonA1SocketEventsScenario.cs` | `Client/Scenarios/mon_a1_socket_events_scenario.hpp` | scenario | done | trigger role이 service A에 transient request를 보내고 socket `Connected`, `ConnectionReady`, `Disconnected` event와 remote address evidence를 검증한다. |
| `Client/Scenarios/MonA2RegistryEventsScenario.cs` | `Client/Scenarios/mon_a2_registry_events_scenario.hpp` | scenario | done | standalone registry role의 topology/service summary evidence를 검증한다. |
| `Client/Scenarios/MonA3SpotEventsScenario.cs` | `Client/Scenarios/mon_a3_spot_events_scenario.hpp` | scenario | done | registry-discovered TCP SPOT mesh peer로 `PeersChanged`, spot create 뒤 `SubjectsChanged`, failing timer 뒤 `TimerHandlerFailed` evidence를 검증한다. |
| `Client/Scenarios/MonA4AvailabilityTransitionScenario.cs` | `Client/Scenarios/mon_a4_availability_transition_scenario.hpp` | scenario | done | drain/restore admin evidence, socket admission event, registry topology evidence를 검증한다. |
| `Client/Scenarios/MonA5FixedKindsScenario.cs` | `Client/Scenarios/mon_a5_fixed_kinds_scenario.hpp` | scenario | done | trigger의 invalid handshake action으로 socket `HandshakeFailed` evidence를 검증하고, registry `StatusChanged`, spot `StatusChanged`, `TimerStoppedAfterUnhandledException` evidence도 검증한다. |
| `Client/Scenarios/MonB1KindFilterScenario.cs` | `Client/Scenarios/mon_b1_kind_filter_scenario.hpp` | scenario | done | filtered service의 socket event kind filter를 `ConnectionReady` event로 검증한다. |
| `Client/Scenarios/MonB2RegistrationValidationScenario.cs` | `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | scenario | done | 중복 source, 비양수 interval, missing spot/socket source framework 적용 검증을 확인한다. |
| `Client/Scenarios/MonC1DispatchFailureScenario.cs` | `Client/Scenarios/mon_c1_dispatch_failure_scenario.hpp` | scenario | done | throwing service evidence, stderr marker, follow-up request recovery를 검증한다. |
| `Client/Scenarios/MonD1FailureRecoveryScenario.cs` | `Client/Scenarios/mon_d1_failure_recovery_scenario.hpp` | scenario | done | service stop/restart 뒤 direct request, restarted service evidence, restart 이후 registry topology continuity evidence를 검증한다. |
| `Server/Registry/RuntimeMonitoring.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | registry target이 있고 shared evidence wait endpoint와 shutdown endpoint를 사용한다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | done | registry role 진입점은 `registry_host_factory.hpp`의 host factory 실행만 담당한다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry_host_factory.hpp` | server-role | done | registry host 구성, registry monitoring source, HTTP health/evidence/wait/shutdown endpoint mapping을 role-local factory header로 분리했다. |
| `Server/Registry/Handlers/RegistryEventRecorders.cs` | `Server/Registry/Handlers/registry_event_recorders.hpp`, `Server/Registry/registry_host_factory.hpp` | handler | done | registry event kind 변환과 evidence 기록 helper를 handler header로 분리했고, host factory는 monitoring callback에서 helper를 호출한다. |
| `Server/Registry/Handlers/RegistryHandlers.cs` | not-needed | handler | not-needed | `.NET` RegistryHostFactory는 이 파일의 profile/spot/timer handlers를 등록하지 않는다. 해당 handler 책임은 Service role의 `service_handlers.hpp`에서 검증하므로 C++ Registry role에는 별도 대응 파일을 두지 않는다. Registry evidence GET/wait endpoint는 shared `Server/Shared/evidence_store.hpp`와 `registry_host_factory.hpp` route mapping으로 제공한다. |
| `Server/Registry/Support/RegistryEvidenceStore.cs` | `Server/Shared/evidence_store.hpp` | support | done | shared evidence store와 waiter API가 있다. |
| `Server/Registry/Support/RegistryOptions.cs` | `Server/Registry/Support/registry_options.hpp` | support | done | C++ runner env 기반 registry endpoint/evidence option parser가 있다. |
| `Server/Service/RuntimeMonitoring.Service.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_runtime_monitoring_service` target이 있다. |
| `Server/Service/Program.cs` | `Server/Service/main.cpp` | server-role | done | service role 진입점은 `service_host_factory.hpp`의 all-profile host factory 실행만 담당한다. |
| `Server/Service/ServiceHostFactory.cs` | `Server/Service/service_host_factory.hpp`, `Server/Service/Support/service_host.hpp` | server-role | done | role-local factory header가 all-profile host 실행을 감싸고, 공통 service host 구성은 `Support/service_host.hpp`가 담당한다. |
| `Server/Service/Handlers/ServiceEventRecorders.cs` | `Server/Service/Handlers/service_event_recorders.hpp`, `Server/Service/Support/service_host.hpp` | handler | done | socket, spot/timer, throwing socket event evidence 기록 helper를 handler header로 분리했고, service host는 monitoring callback에서 helper를 호출한다. |
| `Server/Service/Handlers/ServiceHandlers.cs` | `Server/Service/Handlers/service_handlers.hpp` | handler | done | profile request handler, monitoring entry spot, failing timer handler를 분리했다. C++ service role의 admin weight, spot create, shutdown HTTP handlers도 같은 role-local handler header에 둔다. |
| `Server/Service/Support/ServiceEvidenceStore.cs` | `Server/Shared/evidence_store.hpp` | support | done | shared evidence store와 waiter API가 있다. |
| `Server/Service/Support/ServiceOptions.cs` | `Server/Service/Support/service_options.hpp` | support | done | C++ runner env 기반 service endpoint/evidence/monitor profile option parser가 있다. |
| `Server/FilteredService/RuntimeMonitoring.FilteredService.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | filtered service target이 있다. |
| `Server/FilteredService/Program.cs` | `Server/FilteredService/main.cpp` | server-role | done | filtered service executable이 socket event kind filter profile로 실행된다. |
| `Server/FilteredService/FilteredServiceHostFactory.cs` | `Server/FilteredService/filtered_service_host_factory.hpp`, `Server/Service/Support/service_host.hpp` | server-role | done | filtered service factory가 socket-filter profile을 선택하고 공통 service host 구성을 재사용한다. |
| `Server/ThrowingService/RuntimeMonitoring.ThrowingService.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | throwing service target이 있다. |
| `Server/ThrowingService/Program.cs` | `Server/ThrowingService/main.cpp` | server-role | done | throwing service executable이 monitoring handler 예외 profile로 실행된다. |
| `Server/ThrowingService/ThrowingServiceHostFactory.cs` | `Server/ThrowingService/throwing_service_host_factory.hpp`, `Server/Service/Support/service_host.hpp` | server-role | done | throwing service factory가 throwing profile을 선택하고 공통 service host 구성을 재사용한다. |
| `Server/Trigger/RuntimeMonitoring.Trigger.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | trigger executable target이 있다. |
| `Server/Trigger/Program.cs` | `Server/Trigger/main.cpp` | server-role | done | trigger role 진입점은 `trigger_host_factory.hpp`의 host factory 실행만 담당한다. |
| `Server/Trigger/TriggerHostFactory.cs` | `Server/Trigger/trigger_host_factory.hpp` | server-role | done | trigger host 구성, discovery client, evidence store, HTTP endpoint mapping을 role-local factory header로 분리했다. |
| `Server/Trigger/TriggerEndpoints.cs` | `Server/Trigger/trigger_host_factory.hpp`, `Server/Shared/evidence_store.hpp` | endpoint | done | health, evidence, evidence/wait, profile request, service-a/service-b direct request, throw stderr log wait, validation endpoint, handshake failure endpoint route mapping은 trigger host factory에서 담당한다. |
| `Server/Trigger/TriggerHandlers.cs` | `Server/Trigger/trigger_handlers.hpp` | handler | done | profile request, service-a/service-b/throw direct request, throw stderr log, validation, handshake failure handler class를 role-local handler header로 분리했다. `.NET`의 socket event recorder는 현재 C++ trigger role이 public monitoring source를 등록하지 않아 대응하지 않는다. |
| `Server/Trigger/Support/TriggerClientRequests.cs` | `Server/Trigger/Support/trigger_client_requests.hpp` | support | done | transient direct channel request helper와 invalid handshake sender가 있다. |
| `Server/Trigger/Support/TriggerLogReader.cs` | `Server/Trigger/Support/trigger_log_reader.hpp` | support | done | throw stderr wait helper가 있다. |
| `Server/Trigger/Support/TriggerSupport.cs` | `Server/Trigger/Support/trigger_options.hpp`, `Server/Shared/evidence_store.hpp` | support | done | C++ runner env 기반 trigger endpoint/log option parser와 shared evidence store가 있다. log reader, direct request helper, validation helper는 별도 support header로 분리했다. |
| `Server/Trigger/Support/TriggerValidation.cs` | `Server/Trigger/Support/trigger_validation.hpp`, `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | support | done | trigger validation endpoint가 public builder/framework 적용 오류를 실행하고 client가 결과를 단언한다. |

## Scenario 대응

| Scenario | C++ 대응 | 상태 |
|----------|----------|------|
| `MON-A1` | `Client/Scenarios/mon_a1_socket_events_scenario.hpp` | done |
| `MON-A2` | `Client/Scenarios/mon_a2_registry_events_scenario.hpp` | done |
| `MON-A3` | `Client/Scenarios/mon_a3_spot_events_scenario.hpp` | done |
| `MON-A4` | `Client/Scenarios/mon_a4_availability_transition_scenario.hpp` | done |
| `MON-A5` | `Client/Scenarios/mon_a5_fixed_kinds_scenario.hpp` | done |
| `MON-B1` | `Client/Scenarios/mon_b1_kind_filter_scenario.hpp` | done |
| `MON-B2` | `Client/Scenarios/mon_b2_registration_validation_scenario.hpp` | done |
| `MON-C1` | `Client/Scenarios/mon_c1_dispatch_failure_scenario.hpp` | done |
| `MON-D1` | `Client/Scenarios/mon_d1_failure_recovery_scenario.hpp` | done |

## 검증

- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-182556-730831`
  - 의미: registry, service, filtered service, throwing service, trigger, client role target이 같은 gate에서
    MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_client`
  - 결과: 통과
  - 의미: `.NET ClientOptions`에 대응하는 C++ `client_options.hpp` 추가 뒤 client target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-002651-1564972`
  - 의미: client option 집중화 뒤에도 registry, service, filtered service, throwing service, trigger, client role target이
    같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_registry -j 4`
  - 결과: 통과
  - 의미: registry host 구성을 `Server/Registry/registry_host_factory.hpp`로 분리한 뒤 registry target이 빌드된다.
- 2026-07-01: `bash -n framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-014844-1701530`
  - 의미: registry host factory 분리와 `/shutdown` endpoint mapping 추가 뒤에도 registry, service,
    filtered service, throwing service, trigger, client role target이 같은 gate에서 MON-A1, MON-A2,
    MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_service zlink_cpp_e2e_runtime_monitoring_filtered_service zlink_cpp_e2e_runtime_monitoring_throwing_service -j 4`
  - 결과: 통과
  - 의미: service/filtered service/throwing service factory wrapper 추가 뒤 세 role target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-015129-1705140`
  - 의미: service/filtered service/throwing service factory wrapper 추가 뒤에도 registry, service,
    filtered service, throwing service, trigger, client role target이 같은 gate에서 MON-A1, MON-A2,
    MON-A3, MON-A4, MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_trigger -j 4`
  - 결과: 통과
  - 의미: trigger host 구성을 `Server/Trigger/trigger_host_factory.hpp`로 분리한 뒤 trigger target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-015350-1708292`
  - 의미: trigger host factory 분리 뒤에도 registry, service, filtered service, throwing service, trigger,
    client role target이 같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1, MON-B2,
    MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_registry -j 4`
  - 결과: 통과
  - 의미: registry event recorder helper 분리 뒤 registry target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-015734-1713025`
  - 의미: registry event recorder helper 분리 뒤에도 registry, service, filtered service, throwing service,
    trigger, client role target이 같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1,
    MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_service zlink_cpp_e2e_runtime_monitoring_filtered_service zlink_cpp_e2e_runtime_monitoring_throwing_service -j 4`
  - 결과: 통과
  - 의미: service event recorder helper 분리 뒤 service/filtered service/throwing service target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-020007-1716279`
  - 의미: service event recorder helper 분리 뒤에도 registry, service, filtered service, throwing service,
    trigger, client role target이 같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5, MON-B1,
    MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_runtime_monitoring_trigger -j 4`
  - 결과: 통과
  - 의미: trigger handler header rename과 host factory include 정리 뒤 trigger target이 빌드된다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-133323-16246`
  - 의미: trigger endpoint/handler 분리 상태를 정리한 뒤에도 registry, service, filtered service,
    throwing service, trigger, client role target이 같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4,
    MON-A5, MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
- 2026-07-01: `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build timeout 420s framework/languages/cpp/e2e/RuntimeMonitoring/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-165138-53788`
  - 의미: local port readiness timeout을 기본 3초로 명시하고 service/trigger message-flow trace
    파일을 runner 필수 gate로 추가한 뒤에도 registry, service, filtered service, throwing service,
    trigger, client role target이 같은 gate에서 MON-A1, MON-A2, MON-A3, MON-A4, MON-A5,
    MON-B1, MON-B2, MON-C1, MON-D1을 검증한다.
