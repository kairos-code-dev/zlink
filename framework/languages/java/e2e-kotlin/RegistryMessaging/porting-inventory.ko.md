# Kotlin RegistryMessaging .NET 기준 포팅 인벤토리

기준 구현: `framework/languages/dotnet/e2e/RegistryMessaging`

공통 문서: `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`

이 문서는 `.NET` 기준 파일이 Kotlin 목표 구조의 어느 파일로 대응되는지 기록한다. 기존 Kotlin 구현은
`src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/` 아래 단일 application과
`ZLINK_KOTLIN_E2E_ROLE` 분기로 registry, provider, client를 바꾸어 실행했다. 계획 문서는 서로 다른
server role을 하나의 application에서 role 옵션으로 바꾸는 방식을 완료로 보지 않으므로, 현재 Kotlin
구현은 `Shared`, `Client`, `Server/Registry`, `Server/Provider`, `Server/Consumer`,
`Server/Workflow` project로 분리했다. Kotlin 사용자에게 보이는 설정, handler 등록, client scenario
흐름은 Kotlin code path에 둔다.

상태 값:

- `done`: 현재 파일이 목표 위치와 의미를 만족한다.
- `not-needed`: Kotlin/Gradle 구조에서 같은 파일 단위가 필요 없으며 비고에 근거를 적었다.
- `gap`: public contract 또는 runtime 지원이 없어 완료로 주장할 수 없다.

| .NET 기준 파일 | Kotlin 대응 파일 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `.gitignore` | `.gitignore` | config-root | done | 기존 파일은 있으나 목표 구조의 `logs/`, Gradle 산출물 제외를 재확인한다. |
| `README.ko.md` | `README.ko.md` | docs | done | Kotlin RegistryMessaging 보충 설명을 새로 작성한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | RM-C9 public contract gap과 구현된 scenario를 분리해 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | 기존 runner는 단일 application role 분기 실행이다. 목표는 별도 role project 시작, readiness, cleanup, 실패 로그 출력이다. |
| `Shared/RegistryMessaging.Shared.csproj` | `Shared/build.gradle.kts` | build | done | Kotlin Shared project로 분리한다. |
| `Shared/Messages.cs` | `Shared/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/shared/Messages.kt` | shared | done | 기존 `Contracts.kt`의 message 타입을 Shared로 이동하고 `Payload*`, `Workflow*`, failure result를 보강한다. |
| `Client/RegistryMessaging.Client.csproj` | `Client/build.gradle.kts` | build | done | Kotlin Client application project로 분리한다. |
| `Client/Program.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Program.kt` | client-entry | done | 기존 `ClientScenario.kt`는 framework client를 직접 사용한다. 목표는 HTTP client로 실제 role server endpoint를 호출하는 scenario 목록이다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Support/ClientOptions.kt` | support | done | CLI option parsing으로 포팅한다. |
| `Client/Support/DynamicClusterLauncher.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Support/DynamicClusterLauncher.kt` | support | done | RM-A4/RM-C7 동적 registry/provider 실행 support로 포팅한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Support/ScenarioAssert.kt` | support | done | 공통 assertion/evidence count helper로 포팅한다. |
| `Client/Scenarios/RmA1DiscoveryRequestScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmA1DiscoveryRequestScenario.kt` | scenario | done | RM-A1. 역할 server HTTP endpoint 호출과 registry topology 확인으로 포팅한다. |
| `Client/Scenarios/RmA2ManualEndpointScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmA2ManualEndpointScenario.kt` | scenario | done | RM-A2. provider role endpoint `/profile/manual` 호출로 포팅한다. |
| `Client/Scenarios/RmA4SameRidFailoverScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmA4SameRidFailoverScenario.kt` | scenario | done | RM-A4. 동적 provider 교체와 같은 rid 검증으로 포팅한다. |
| `Client/Scenarios/RmA6MultipleChannelsScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmA6MultipleChannelsScenario.kt` | scenario | done | RM-A6. API/workflow channel 독립 discovery 검증으로 포팅한다. |
| `Client/Scenarios/RmB1ScaleOutScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmB1ScaleOutScenario.kt` | scenario | done | RM-B1. 동적 provider 추가를 client support에서 처리한다. |
| `Client/Scenarios/RmB2ScaleInScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmB2ScaleInScenario.kt` | scenario | done | RM-B2. 동적 provider 제거 뒤 stale 회피를 검증한다. |
| `Client/Scenarios/RmC1RequestSendScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC1RequestSendScenario.kt` | scenario | done | RM-C1. request/send marker를 provider evidence로 확인한다. |
| `Client/Scenarios/RmC2TargetedRouteScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC2TargetedRouteScenario.kt` | scenario | done | RM-C2. provider role endpoint가 route mesh request를 수행한다. |
| `Client/Scenarios/RmC3MultiProviderDistributionScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC3MultiProviderDistributionScenario.kt` | scenario | done | RM-C3. direct consumer role endpoint를 통해 다중 endpoint 분산을 검증한다. |
| `Client/Scenarios/RmC4TimeoutIsolationScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC4TimeoutIsolationScenario.kt` | scenario | done | RM-C4. discovery consumer role endpoint로 timeout과 후속 request를 검증한다. |
| `Client/Scenarios/RmC5MissingPacketScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC5MissingPacketScenario.kt` | scenario | done | RM-C5. missing packet request/send와 dispatch-error evidence를 검증한다. |
| `Client/Scenarios/RmC7WeightedProviderScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC7WeightedProviderScenario.kt` | scenario | done | RM-C7. weighted provider 동적 cluster 검증으로 포팅한다. |
| `Client/Scenarios/RmC8PayloadRoundTripScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC8PayloadRoundTripScenario.kt` | scenario | done | RM-C8. length/hash 기반 payload 왕복 검증으로 보강한다. |
| `Client/Scenarios/RmC9BackpressureScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Scenarios/RmC9BackpressureScenario.kt` | scenario | gap | Kotlin/Java public channel builder에 low-HWM socket setup 표면이 없어 internal socket access 없이 완료 구현하지 않는다. |
| `Server/Registry/RegistryMessaging.Registry.csproj` | `Server/Registry/build.gradle.kts` | build | done | Registry role application project로 분리한다. |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/registry/Program.kt` | server-entry | done | 별도 registry 실행 진입점으로 포팅한다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/registry/Program.kt` (`RegistryApplication`) | server-role | done | Spring Boot registry host 구성은 같은 파일의 `RegistryApplication` class에 둔다. |
| `Server/Registry/Configuration/ServerOptions.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/registry/Configuration/ServerOptions.kt` | configuration | done | CLI 옵션, rid, endpoint, log dir 파싱. |
| `Server/Registry/Endpoints/RegistryMessagingEndpoints.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/registry/Endpoints/RegistryEndpoints.kt` | endpoints | done | `/health`, `/registry/topology`, `/evidence`, `/shutdown`. |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/registry/Infrastructure/EvidenceStore.kt` | infrastructure | done | registry evidence store. |
| `Server/Provider/RegistryMessaging.Provider.csproj` | `Server/Provider/build.gradle.kts` | build | done | Provider role application project로 분리한다. |
| `Server/Provider/Program.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Program.kt` | server-entry | done | 별도 provider 실행 진입점으로 포팅한다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Program.kt` (`ProviderApplication`) | server-role | done | discovery, profile/manual, route mesh, weight 설정은 같은 파일의 `ProviderApplication` class에 둔다. |
| `Server/Provider/Configuration/ServerOptions.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Configuration/ServerOptions.kt` | configuration | done | provider CLI 옵션과 route peer 목록 파싱. |
| `Server/Provider/Endpoints/ProviderEndpoints.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Endpoints/ProviderEndpoints.kt` | endpoints | done | 실제 role server HTTP endpoint와 evidence wait/shutdown endpoint. |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Handlers/ProviderHandlers.kt` | handlers | done | `ProfileRequest`, `PayloadRequest`, `ProfileCommand`, route handler, dispatch observer. |
| `Server/Provider/Infrastructure/EvidenceStore.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/provider/Infrastructure/EvidenceStore.kt` | infrastructure | done | wait 가능한 provider evidence store. |
| `Server/Consumer/RegistryMessaging.Consumer.csproj` | `Server/Consumer/build.gradle.kts` | build | done | Consumer role application project로 분리한다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/consumer/Program.kt` | server-entry | done | 별도 consumer 실행 진입점으로 포팅한다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/consumer/Program.kt` (`ConsumerApplication`) | server-role | done | direct/single/discovery consumer 구성은 같은 파일의 `ConsumerApplication` class에 둔다. RM-C9는 public contract gap이라 low-HWM consumer를 만들지 않는다. |
| `Server/Consumer/Configuration/ConsumerOptions.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/consumer/Configuration/ConsumerOptions.kt` | configuration | done | provider endpoint 목록과 registry endpoint를 파싱한다. |
| `Server/Consumer/Endpoints/ConsumerEndpoints.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/consumer/Endpoints/ConsumerEndpoints.kt` | endpoints | done | `/profile/*` consumer endpoint로 framework 호출을 role server 안에 둔다. |
| `Server/Workflow/RegistryMessaging.Workflow.csproj` | `Server/Workflow/build.gradle.kts` | build | done | Workflow role application project로 분리한다. |
| `Server/Workflow/Program.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Program.kt` | server-entry | done | 별도 workflow 실행 진입점으로 포팅한다. |
| `Server/Workflow/WorkflowHostFactory.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Program.kt` (`WorkflowApplication`) | server-role | done | workflow channel provider와 client 설정은 같은 파일의 `WorkflowApplication` class에 둔다. |
| `Server/Workflow/Configuration/ServerOptions.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Configuration/ServerOptions.kt` | configuration | done | workflow CLI 옵션과 weight 파싱. |
| `Server/Workflow/Endpoints/WorkflowEndpoints.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Endpoints/WorkflowEndpoints.kt` | endpoints | done | workflow request endpoint, evidence wait, shutdown endpoint. |
| `Server/Workflow/Handlers/WorkflowHandlers.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Handlers/WorkflowHandlers.kt` | handlers | done | `WorkflowRequest` handler와 dispatch observer. |
| `Server/Workflow/Infrastructure/EvidenceStore.cs` | `Server/Workflow/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/workflow/Infrastructure/EvidenceStore.kt` | infrastructure | done | wait 가능한 workflow evidence store. |

## Kotlin 전용 파일

Gradle multi-project와 HTTP helper는 `.NET` 파일과 일대일로 대응되지 않는다. 그래도 review에서 누락으로
보지 않도록 목표 구조에 필요한 Kotlin 파일을 따로 기록한다.

| Kotlin 파일 | 분류 | 상태 | 비고 |
|-------------|------|------|------|
| `build.gradle.kts` | build-root | done | RegistryMessaging 하위 project 공통 Kotlin/JVM 설정을 둔다. |
| `settings.gradle.kts` | build-root | done | `Shared`, `Client`, `Server/*` project를 Gradle에 등록한다. |
| `logs/.gitignore` | config-root | done | 실행 로그 디렉토리는 보존하고 로그 파일은 추적하지 않는다. |
| `Client/src/main/kotlin/systems/zlink/e2e/kotlin/registrymessaging/client/Support/HttpJson.kt` | support | done | Client scenario가 role server HTTP endpoint를 호출할 때 쓰는 작은 JSON HTTP helper다. |

## 기존 Kotlin 파일 처리

| 기존 Kotlin 파일 | 판단 | 목표 |
|------------------|------|------|
| `src/main/kotlin/.../Program.kt` | role 옵션 분기라 완료 구조가 아니다. | 삭제하고 각 role project의 `Program.kt`로 분리한다. |
| `src/main/kotlin/.../Contracts.kt` | message/shared 타입과 channel 상수가 섞여 있다. | `Shared/.../Messages.kt`로 옮기고 role별 상수는 각 role package에서 필요한 만큼 참조한다. |
| `src/main/kotlin/.../ClientApplication.kt` | client가 framework client를 직접 들고 scenario를 실행한다. | 삭제하고 `Client` project는 HTTP client로 실제 role server endpoint를 호출한다. |
| `src/main/kotlin/.../ClientScenario.kt` | 여러 scenario와 helper가 한 파일에 섞여 있고 role server endpoint를 우회한다. | scenario ID별 파일과 `Client/Support` helper로 분리한다. |
| `src/main/kotlin/.../ProviderApplication.kt` | provider/workflow 역할과 endpoint 없는 framework 직접 검증이 섞여 있다. | `Server/Provider/.../Program.kt`의 `ProviderApplication`과 `Server/Workflow/.../Program.kt`의 `WorkflowApplication`으로 분리한다. |
| `src/main/kotlin/.../RegistryApplication.kt` | registry 역할 코드는 재사용 가능하다. | `Server/Registry/.../Program.kt`의 `RegistryApplication`으로 이동해 CLI 옵션과 HTTP endpoint를 보강한다. |
| `src/main/kotlin/.../ProfileHandlers.kt` | provider handler 기반은 재사용 가능하지만 payload/workflow handler가 빠져 있다. | `Server/Provider/Handlers`와 `Server/Workflow/Handlers`로 분리한다. |
| `src/main/kotlin/.../ScenarioState.kt` | 단순 evidence store라 wait endpoint와 file evidence가 없다. | role별 `Infrastructure/EvidenceStore.kt`로 대체한다. |
| `src/main/kotlin/.../Env.kt` | 환경 변수 helper는 목표 구조의 CLI 옵션과 맞지 않는다. | 삭제하거나 작은 process helper 내부로 흡수한다. |

## Scenario ID 매핑

| Scenario ID | 공통 우선순위 | .NET 기준 scenario 파일 | Kotlin 목표 파일 | 상태 |
|-------------|---------------|-------------------------|------------------|------|
| `RM-A1` | P0 | `Client/Scenarios/RmA1DiscoveryRequestScenario.cs` | `Client/.../Scenarios/RmA1DiscoveryRequestScenario.kt` | done |
| `RM-A2` | P0 | `Client/Scenarios/RmA2ManualEndpointScenario.cs` | `Client/.../Scenarios/RmA2ManualEndpointScenario.kt` | done |
| `RM-A4` | P0 | `Client/Scenarios/RmA4SameRidFailoverScenario.cs` | `Client/.../Scenarios/RmA4SameRidFailoverScenario.kt` | done |
| `RM-A6` | P1 | `Client/Scenarios/RmA6MultipleChannelsScenario.cs` | `Client/.../Scenarios/RmA6MultipleChannelsScenario.kt` | done |
| `RM-B1` | P0 | `Client/Scenarios/RmB1ScaleOutScenario.cs` | `Client/.../Scenarios/RmB1ScaleOutScenario.kt` | done |
| `RM-B2` | P0 | `Client/Scenarios/RmB2ScaleInScenario.cs` | `Client/.../Scenarios/RmB2ScaleInScenario.kt` | done |
| `RM-C1` | P0 | `Client/Scenarios/RmC1RequestSendScenario.cs` | `Client/.../Scenarios/RmC1RequestSendScenario.kt` | done |
| `RM-C2` | P0 | `Client/Scenarios/RmC2TargetedRouteScenario.cs` | `Client/.../Scenarios/RmC2TargetedRouteScenario.kt` | done |
| `RM-C3` | P0 | `Client/Scenarios/RmC3MultiProviderDistributionScenario.cs` | `Client/.../Scenarios/RmC3MultiProviderDistributionScenario.kt` | done |
| `RM-C4` | P0 | `Client/Scenarios/RmC4TimeoutIsolationScenario.cs` | `Client/.../Scenarios/RmC4TimeoutIsolationScenario.kt` | done |
| `RM-C5` | P0 | `Client/Scenarios/RmC5MissingPacketScenario.cs` | `Client/.../Scenarios/RmC5MissingPacketScenario.kt` | done |
| `RM-C7` | P1 | `Client/Scenarios/RmC7WeightedProviderScenario.cs` | `Client/.../Scenarios/RmC7WeightedProviderScenario.kt` | done |
| `RM-C8` | P1 | `Client/Scenarios/RmC8PayloadRoundTripScenario.cs` | `Client/.../Scenarios/RmC8PayloadRoundTripScenario.kt` | done |
| `RM-C9` | P2 | `Client/Scenarios/RmC9BackpressureScenario.cs` | `Client/.../Scenarios/RmC9BackpressureScenario.kt` | gap |
