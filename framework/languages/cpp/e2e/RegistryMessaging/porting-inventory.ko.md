# C++ RegistryMessaging .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/RegistryMessaging`의 파일을 기준으로
C++ `RegistryMessaging` E2E의 대응 파일과 남은 gap을 기록한다. 상태가 `gap`인 행은 현재 C++ 파일이
동작을 담고 있어도, 목표 구조나 검증 수준이 아직 `.NET` 기준과 같은 의미로 정렬되지 않았다는 뜻이다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/RegistryMessaging`
- C++ 대상: `framework/languages/cpp/e2e/RegistryMessaging`

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 실행 로그와 산출물을 제외한다. |
| `README.ko.md` | `feature-map.ko.md` | docs | not-needed | C++에는 config별 보충 README가 없고, 공통 기준과 구현 상태는 feature-map에 둔다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | RM-A/B/C 시나리오 상태를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | registry, provider, workflow, consumer, client 프로세스를 실제로 띄우고 단일 시나리오 실행을 지원한다. RM-C3/C4/C5/C8/C9는 `.NET` 기준처럼 consumer HTTP role을 거쳐 검증한다. |
| `Shared/Messages.cs` | `Shared/registry_messaging_contracts.hpp` | shared | done | request/reply/evidence DTO와 channel 이름을 C++ 타입으로 대응한다. |
| `Shared/RegistryMessaging.Shared.csproj` | `Shared/registry_messaging_contracts.hpp` | build | not-needed | C++ shared contract는 별도 프로젝트 파일 없이 header로 포함된다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | scenario 선택과 framework client 구성을 수행한다. |
| `Client/RegistryMessaging.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registry_messaging_client` target이 대응한다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | client-support | done | env parsing helper가 C++ support header에 있다. |
| `Client/Support/DynamicClusterLauncher.cs` | `run_e2e.sh` | runner-support | done | 프로세스 시작, stop, scenario별 cluster 조작은 shell runner가 담당한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp` | client-support | done | `ensure` helper가 C++ support header에 있다. |
| `Client/Scenarios/RmA1DiscoveryRequestScenario.cs` | `Client/Scenarios/rm_a1_discovery_request_scenario.hpp` | scenario | done | RM-A1 registry discovery request를 직접 실행한다. |
| `Client/Scenarios/RmA2ManualEndpointScenario.cs` | `Client/Scenarios/rm_a2_manual_endpoint_scenario.hpp` | scenario | done | RM-A2 manual endpoint request를 직접 실행한다. |
| `Client/Scenarios/RmA4SameRidFailoverScenario.cs` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp` | scenario | done | RM-A4 same-rid failover request flow를 직접 실행한다. |
| `Client/Scenarios/RmA6MultipleChannelsScenario.cs` | `Client/Scenarios/rm_a6_multiple_channels_scenario.hpp` | scenario | done | RM-A6 api/workflow channel 분리 검증을 직접 실행한다. |
| `Client/Scenarios/RmB1ScaleOutScenario.cs` | `Client/Scenarios/rm_b1_scale_out_scenario.hpp` | scenario | done | RM-B1 scale-out barrier와 post-scale 검증을 직접 실행한다. |
| `Client/Scenarios/RmB2ScaleInScenario.cs` | `Client/Scenarios/rm_b2_scale_in_scenario.hpp` | scenario | done | RM-B2 scale-in barrier와 stale 회피 검증을 직접 실행한다. |
| `Client/Scenarios/RmC1RequestSendScenario.cs` | `Client/Scenarios/rm_c1_request_send_scenario.hpp` | scenario | done | RM-C1 request/send happy path를 직접 실행한다. |
| `Client/Scenarios/RmC2TargetedRouteScenario.cs` | `Client/Scenarios/rm_c2_targeted_route_scenario.hpp` | scenario | done | RM-C2 route mesh targeted request를 직접 실행한다. |
| `Client/Scenarios/RmC3MultiProviderDistributionScenario.cs` | `Client/Scenarios/rm_c3_multi_provider_distribution_scenario.hpp` | scenario | done | direct consumer HTTP role을 거쳐 RM-C3 multi-provider distribution을 검증한다. C++ HTTP array body binding 차이 때문에 `.NET`의 batch endpoint 대신 같은 consumer의 단건 request endpoint를 반복 호출한다. |
| `Client/Scenarios/RmC4TimeoutIsolationScenario.cs` | `Client/Scenarios/rm_c4_timeout_isolation_scenario.hpp` | scenario | done | discovery consumer HTTP role을 거쳐 RM-C4 timeout/late-reply isolation을 검증한다. |
| `Client/Scenarios/RmC5MissingPacketScenario.cs` | `Client/Scenarios/rm_c5_missing_packet_scenario.hpp` | scenario | done | discovery consumer HTTP role을 거쳐 RM-C5 missing packet negative path를 검증한다. |
| `Client/Scenarios/RmC7WeightedProviderScenario.cs` | `Client/Scenarios/rm_c7_weighted_provider_scenario.hpp` | scenario | done | RM-C7 weighted provider distribution을 직접 실행한다. |
| `Client/Scenarios/RmC8PayloadRoundTripScenario.cs` | `Client/Scenarios/rm_c8_payload_round_trip_scenario.hpp` | scenario | done | single consumer HTTP role을 거쳐 RM-C8 payload round-trip과 max-size subflow를 검증한다. |
| `Client/Scenarios/RmC9BackpressureScenario.cs` | `Client/Scenarios/rm_c9_backpressure_scenario.hpp` | scenario | done | one-way send pressure 제출과 recovery evidence를 검증한다. public send는 bounded-failure oracle을 노출하지 않는다. |
| `Server/Consumer/Configuration/ConsumerOptions.cs` | `Server/Consumer/Configuration/consumer_options.hpp` | consumer-role | done | consumer HTTP endpoint, registry router, direct provider endpoints를 env에서 읽는다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp` | consumer-role | done | C++ consumer role이 direct, single, discovery, backpressure 구성의 framework client와 HTTP endpoint를 구성한다. payload scenario의 JSON wrapper 크기를 받기 위해 public HTTP server option으로 request body limit을 높인다. |
| `Server/Consumer/Endpoints/ConsumerEndpoints.cs` | `Server/Consumer/Endpoints/consumer_endpoints.hpp` | consumer-role | done | profile request, slow/missing request, missing command, payload, backpressure endpoint가 scenario 검증 경로에 쓰인다. RM-C3은 같은 consumer public request 경로를 반복 호출해 multi-provider distribution을 검증한다. C++ HTTP array body binding 차이는 scenario/public messaging 동작 차이로 보지 않는다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/main.cpp` | consumer-role | done | consumer role executable 진입점이 있다. |
| `Server/Consumer/RegistryMessaging.Consumer.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registry_messaging_consumer` target이 대응한다. |
| `Server/Provider/Configuration/ServerOptions.cs` | `Server/Provider/Configuration/provider_options.hpp` | server-role | done | provider rid, endpoints, weight, max message size, log dir를 env에서 읽는다. |
| `Server/Provider/Endpoints/ProviderEndpoints.cs` | `Server/Provider/Endpoints/provider_endpoints.hpp` | endpoint | done | health/evidence endpoint를 제공한다. |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/Handlers/provider_handlers.hpp`; `Server/Provider/main.cpp` | handler | done | profile, payload, send, route ping handler는 handler header에 있고 dispatch error observer는 framework 구성 lambda에 있다. |
| `Server/Provider/Infrastructure/EvidenceStore.cs` | `Server/Provider/Infrastructure/scenario_state.hpp` | infrastructure | done | evidence snapshot 저장소가 대응한다. |
| `Server/Provider/Program.cs` | `Server/Provider/main.cpp` | server-entry | done | provider role 진입점과 framework 구성을 수행한다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/main.cpp` | server-role | done | C++ app 구성은 provider main에 직접 노출한다. |
| `Server/Provider/RegistryMessaging.Provider.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registry_messaging_provider` target이 대응한다. |
| `Server/Registry/Configuration/ServerOptions.cs` | `Server/Registry/Configuration/registry_options.hpp` | server-role | done | registry pub/router endpoint와 log dir를 env에서 읽는다. |
| `Server/Registry/Endpoints/RegistryMessagingEndpoints.cs` | `Server/Registry/Endpoints/registry_endpoints.hpp`; `Server/Registry/main.cpp` | endpoint | done | C++ registry role은 health와 `/registry/topology` endpoint를 제공하고 runner가 HTTP readiness도 확인한다. |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | `run_e2e.sh` | infrastructure | not-needed | C++ RegistryMessaging 시나리오는 registry evidence를 판정에 쓰지 않는다. provider/workflow messaging evidence로 검증한다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-entry | done | registry role 진입점이다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp` | server-role | done | C++ app 구성은 registry main에 직접 노출한다. |
| `Server/Registry/RegistryMessaging.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registry_messaging_registry` target이 대응한다. |
| `Server/Workflow/Configuration/ServerOptions.cs` | `Server/Workflow/Configuration/workflow_options.hpp` | server-role | done | workflow rid, endpoint, registry endpoint, log dir를 env에서 읽는다. |
| `Server/Workflow/Endpoints/WorkflowEndpoints.cs` | `Server/Workflow/Endpoints/workflow_endpoints.hpp`; `Server/Workflow/main.cpp` | endpoint | done | C++ workflow role은 health/evidence endpoint를 제공하고 runner가 HTTP readiness도 확인한다. |
| `Server/Workflow/Handlers/WorkflowHandlers.cs` | `Server/Workflow/Handlers/workflow_handlers.hpp` | handler | done | workflow request handler가 대응한다. |
| `Server/Workflow/Infrastructure/EvidenceStore.cs` | `Server/Workflow/Infrastructure/scenario_state.hpp` | infrastructure | done | workflow evidence snapshot 저장소가 대응한다. |
| `Server/Workflow/Program.cs` | `Server/Workflow/main.cpp` | server-entry | done | workflow role 진입점이다. |
| `Server/Workflow/WorkflowHostFactory.cs` | `Server/Workflow/main.cpp` | server-role | done | C++ app 구성은 workflow main에 직접 노출한다. |
| `Server/Workflow/RegistryMessaging.Workflow.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registry_messaging_workflow` target이 대응한다. |

## 공통 scenario ID 대응

| Scenario ID | C++ 대응 파일 | 상태 | 비고 |
|-------------|---------------|------|------|
| `RM-A1` | `Client/Scenarios/rm_a1_discovery_request_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-A2` | `Client/Scenarios/rm_a2_manual_endpoint_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-A4` | `Client/Scenarios/rm_a4_same_rid_failover_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-A6` | `Client/Scenarios/rm_a6_multiple_channels_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-B1` | `Client/Scenarios/rm_b1_scale_out_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-B2` | `Client/Scenarios/rm_b2_scale_in_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C1` | `Client/Scenarios/rm_c1_request_send_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C2` | `Client/Scenarios/rm_c2_targeted_route_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C3` | `Client/Scenarios/rm_c3_multi_provider_distribution_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C4` | `Client/Scenarios/rm_c4_timeout_isolation_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C5` | `Client/Scenarios/rm_c5_missing_packet_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C7` | `Client/Scenarios/rm_c7_weighted_provider_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C8` | `Client/Scenarios/rm_c8_payload_round_trip_scenario.hpp` | done | scenario 파일이 직접 검증한다. |
| `RM-C9` | `Client/Scenarios/rm_c9_backpressure_scenario.hpp` | done | P2 send pressure/recovery를 public one-way send 계약에 맞춰 검증한다. |

## 검증

- 2026-06-30: `./framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh RM-C9`
  - 결과: 통과
  - 로그: `logs/20260630-081704-3233416`
  - 의미: 현재 C++ 경로의 send pressure, provider evidence, recovery 검증은 유지된다.
- 2026-06-30: `./framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh all`
  - 결과: 통과
  - parent 로그: `logs/20260630-081727-3234507`
  - RM-C9 child 로그: `logs/20260630-081915-3246228`
  - 의미: 구현된 RegistryMessaging 시나리오는 전체 sweep에서 통과한다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh all`
  - 결과: 통과
  - parent 로그: `logs/20260630-161051-366893`
  - RM-C9 child 로그: `logs/20260630-161317-377857`
  - 의미: 현재 checkout에서도 RM-A1, RM-A2, RM-A4, RM-A6, RM-B1, RM-B2, RM-C1, RM-C2,
    RM-C3, RM-C4, RM-C5, RM-C7, RM-C8, RM-C9 sweep가 모두 통과한다. RM-C9 child log의
    `backpressure-consumer-flow.log`와 `api-a-flow.log`에는 send pressure와 후속 recovery
    request/reply flow가 남는다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh RM-C9`
  - 결과: 통과
  - 로그: `logs/20260701-141721-60851`
  - 의미: 현재 public one-way send 계약에 맞춘 RM-C9 send pressure, provider evidence, recovery 검증이
    focused runner에서 통과한다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/RegistryMessaging/run_e2e.sh all`
  - 결과: 통과
  - parent 로그: `logs/20260701-141526-48855`
  - RM-C9 child 로그: `logs/20260701-141721-60851`
  - 의미: RM-A1, RM-A2, RM-A4, RM-A6, RM-B1, RM-B2, RM-C1, RM-C2,
    RM-C3, RM-C4, RM-C5, RM-C7, RM-C8, RM-C8-max, RM-C9 sweep가 모두 통과한다.
