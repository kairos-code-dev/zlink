# Java RegistryMessaging .NET 포팅 inventory

기준:
- `.NET`: `framework/languages/dotnet/e2e/RegistryMessaging`
- 공통 문서: `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`

상태 의미:
- `done`: Java 파일이 실제로 존재하고 같은 책임을 수행한다.
- `gap`: 파일 또는 책임이 아직 `.NET` 기준 의미까지 대응되지 않는다.
- `not-needed`: Java 구조에서는 별도 파일이 필요 없고, 근거를 비고에 적었다.

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 로그와 build 출력 제외. |
| `README.ko.md` | `README.ko.md` | config-doc | done | Java 실행 구조와 실행 방법을 기록한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | feature-map | done | 구현 scenario와 검증 로그를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | registry, provider, workflow, consumer, client role binary를 실행한다. |
| `Shared/RegistryMessaging.Shared.csproj` | `Shared/build.gradle.kts` | build | done | Java shared DTO project. |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/registrymessaging/shared/Contracts.java` | shared | done | request, reply, route, evidence DTO 대응. |
| `Client/RegistryMessaging.Client.csproj` | `Client/build.gradle.kts` | build | done | Java client application. |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Program.java` | client-entry | done | HTTP client를 만들고 scenario catalog를 실행한다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Support/ClientOptions.java` | support | done | 환경 변수 기반 실행 옵션. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Support/ScenarioAssert.java` | support | done | scenario assertion helper. |
| `Client/Support/DynamicClusterLauncher.cs` | `run_e2e.sh` | support | done | Java는 runner가 scale-out, scale-in, failover 프로세스 조작을 담당한다. |
| 없음 | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Support/RegistryMessagingHttp.java` | support | done | 역할 server별 HTTP client를 구성한다. |
| 없음 | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Support/ScenarioCatalog.java` | support | done | scenario 이름에 따라 RM-* 실행 순서를 고른다. |
| 없음 | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Support/ScenarioSignals.java` | support | done | runner phase 동기화와 짧은 대기 helper. |
| `Client/Scenarios/RmA1DiscoveryRequestScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmA1DiscoveryRequestScenario.java` | scenario | done | registry topology와 discovery request 검증. |
| `Client/Scenarios/RmA2ManualEndpointScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmA2ManualEndpointScenario.java` | scenario | done | manual endpoint request 검증. |
| `Client/Scenarios/RmA4SameRidFailoverScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmA4SameRidFailoverScenario.java` | scenario | done | same rid failover 검증. |
| `Client/Scenarios/RmA6MultipleChannelsScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmA6MultipleChannelsScenario.java` | scenario | done | API channel과 workflow channel 분리 검증. |
| `Client/Scenarios/RmB1ScaleOutScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmB1ScaleOutScenario.java` | scenario | done | provider 추가 뒤 분산 검증. |
| `Client/Scenarios/RmB2ScaleInScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmB2ScaleInScenario.java` | scenario | done | provider 종료 뒤 남은 provider 복구 검증. |
| `Client/Scenarios/RmC1RequestSendScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC1RequestSendScenario.java` | scenario | done | request와 send happy path 검증. |
| `Client/Scenarios/RmC2TargetedRouteScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC2TargetedRouteScenario.java` | scenario | done | target rid route request와 missing rid 실패 검증. |
| `Client/Scenarios/RmC3MultiProviderDistributionScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC3MultiProviderDistributionScenario.java` | scenario | done | direct consumer multi-endpoint 분산 검증. |
| `Client/Scenarios/RmC4TimeoutIsolationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC4TimeoutIsolationScenario.java` | scenario | done | timeout 뒤 정상 request 복구 검증. |
| `Client/Scenarios/RmC5MissingPacketScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC5MissingPacketScenario.java` | scenario | done | missing request/send 뒤 정상 request 복구 검증. |
| `Client/Scenarios/RmC7WeightedProviderScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC7WeightedProviderScenario.java` | scenario | done | weighted provider 분산 검증. |
| `Client/Scenarios/RmC8PayloadRoundTripScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC8PayloadRoundTripScenario.java` | scenario | done | 1 byte, 4 KiB, 256 KiB, 1 MiB payload 왕복 검증. max size 초과 거부 marker는 runtime 배선 뒤 확장 대상. |
| `Client/Scenarios/RmC9BackpressureScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrymessaging/client/Scenarios/RmC9BackpressureScenario.java` | scenario | done | one-way send pressure 제출과 recovery를 검증한다. public send는 bounded-failure oracle을 노출하지 않는다. |
| `Server/Registry/RegistryMessaging.Registry.csproj` | `Server/Registry/build.gradle.kts` | build | done | Java registry role application. |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/registrymessaging/registry/Program.java` | server-role | done | registry process entry와 Spring app 설정. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/registrymessaging/registry/Program.java` | server-role | done | Java는 registry host 구성을 Program의 bean으로 둔다. |
| `Server/Registry/Configuration/ServerOptions.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/registrymessaging/registry/Configuration/RegistryOptions.java` | configuration | done | registry endpoint와 HTTP port 환경 변수. |
| `Server/Registry/Endpoints/RegistryMessagingEndpoints.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/registrymessaging/registry/Endpoints/RegistryMessagingEndpoints.java` | endpoints | done | health와 topology endpoint 제공. |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | 없음 | infrastructure | not-needed | Java registry scenario는 registry evidence를 사용하지 않고 public topology endpoint만 검증한다. |
| `Server/Provider/RegistryMessaging.Provider.csproj` | `Server/Provider/build.gradle.kts` | build | done | Java provider role application. |
| `Server/Provider/Program.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Program.java` | server-role | done | provider process entry와 Spring framework 설정. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Program.java` | server-role | done | Java는 host factory 책임을 Program의 bean 구성으로 둔다. |
| `Server/Provider/Configuration/ServerOptions.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Configuration/ServerOptions.java` | configuration | done | provider endpoint, rid, weight 환경 변수. |
| `Server/Provider/Endpoints/ProviderEndpoints.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Endpoints/ProviderEndpoints.java` | endpoints | done | health, evidence, request, send, route endpoint 제공. |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Handlers/ProfileReqHandler.java`, `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Handlers/ProfileMsgHandler.java`, `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Handlers/PayloadReqHandler.java`, `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Handlers/RouteReqHandler.java` | handlers | done | request, send, payload, route handler 분리. |
| `Server/Provider/Infrastructure/EvidenceStore.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/registrymessaging/provider/Infrastructure/ScenarioState.java` | infrastructure | done | evidence와 provider identity를 보관한다. |
| `Server/Workflow/RegistryMessaging.Workflow.csproj` | `Server/Workflow/build.gradle.kts` | build | done | Java workflow role application. |
| `Server/Workflow/Program.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Program.java` | server-role | done | workflow process entry와 Spring framework 설정. |
| `Server/Workflow/WorkflowHostFactory.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Program.java` | server-role | done | Java는 host factory 책임을 Program의 bean 구성으로 둔다. |
| `Server/Workflow/Configuration/ServerOptions.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Configuration/ServerOptions.java` | configuration | done | workflow endpoint, rid, weight 환경 변수. |
| `Server/Workflow/Endpoints/WorkflowEndpoints.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Endpoints/WorkflowEndpoints.java` | endpoints | done | health, evidence, workflow request endpoint 제공. |
| `Server/Workflow/Handlers/WorkflowHandlers.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Handlers/ProfileReqHandler.java`, `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Handlers/ProfileMsgHandler.java`, `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Handlers/RouteReqHandler.java`, `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Handlers/WorkflowReqHandler.java` | handlers | done | workflow role handler 분리. |
| `Server/Workflow/Infrastructure/EvidenceStore.cs` | `Server/Workflow/src/main/java/systems/zlink/e2e/registrymessaging/workflow/Infrastructure/ScenarioState.java` | infrastructure | done | evidence와 workflow identity를 보관한다. |
| `Server/Consumer/RegistryMessaging.Consumer.csproj` | `Server/Consumer/build.gradle.kts` | build | done | Java consumer role application. |
| `Server/Consumer/Program.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/registrymessaging/consumer/Program.java` | server-role | done | consumer process entry와 Spring framework 설정. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/registrymessaging/consumer/Program.java` | server-role | done | Java는 host factory 책임을 Program의 bean 구성으로 둔다. |
| `Server/Consumer/Configuration/ConsumerOptions.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/registrymessaging/consumer/Configuration/ConsumerOptions.java` | configuration | done | discovery/direct mode, registry, provider endpoint 환경 변수. |
| `Server/Consumer/Endpoints/ConsumerEndpoints.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/registrymessaging/consumer/Endpoints/ConsumerEndpoints.java` | endpoints | done | profile request/send/payload/backpressure endpoint 제공. |
