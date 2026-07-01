# Java RuntimeMonitoring .NET 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`
- `framework/languages/dotnet/e2e/RuntimeMonitoring/feature-map.ko.md`
- `framework/languages/dotnet/e2e/RuntimeMonitoring/`

## 요약

기존 Java config 이름은 `Monitoring`이었고 단일 Gradle application에서 `ZLINK_JAVA_E2E_ROLE`로 role을
전환했다. 현재 디렉터리는 `.NET` 기준 이름인 `RuntimeMonitoring`으로 맞췄고, `Shared`, `Client`,
`Server/Registry`, `Server/Service`, `Server/FilteredService`, `Server/ThrowingService`,
`Server/Trigger` Gradle subproject로 분리했다.

Java 구현은 public monitoring API와 public runtime API만 사용한다. Client는 HTTP driver이고,
framework channel traffic은 `Server/Trigger` role이 수행한다. `.NET`에 있는 별도
`FilteredService`, `ThrowingService` role은 Java Gradle subproject와 process로 존재한다.

## Inventory

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | multi-project build, `.gradle`, logs 산출물 제외 |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | role별 installDist binary를 실행한다. MON-A1/A2/A3/A5/B1/B2/C1 marker를 확인한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | Java 완료/gap scenario 구분 |
| `Shared/RuntimeMonitoring.Shared.csproj` | `Shared/build.gradle.kts` | build | done | shared Java library project |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/runtimemonitoring/shared/Contracts.java` | shared | done | request, reply, evidence record |
| `Client/RuntimeMonitoring.Client.csproj` | `Client/build.gradle.kts` | build | done | client application project |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Program.java` | client | done | HTTP driver entrypoint. `Server/Trigger`의 `/scenario/<id>`를 호출한다. |
| `Client/Support/ClientOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/runtimemonitoring/shared/Env.java` | support | done | Java runner는 환경 변수 helper로 option을 읽는다 |
| `Client/Support/ScenarioAssert.cs` | `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java` | support | done | Trigger role의 `ensure(...)` helper로 scenario assertion 수행 |
| `Client/Scenarios/MonA1SocketEventsScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonA1SocketEventsScenario.java` | scenario | done | Client HTTP driver scenario file. socket `CONNECTED` 또는 `CONNECTION_READY` evidence |
| `Client/Scenarios/MonA2RegistryEventsScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonA2RegistryEventsScenario.java` | scenario | done | Client HTTP driver scenario file. registry status/topology/summary event evidence |
| `Client/Scenarios/MonA3SpotEventsScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonA3SpotEventsScenario.java` | scenario | done | Client HTTP driver scenario file. spot status/peers/subjects/timer failure evidence |
| `Client/Scenarios/MonA4AvailabilityTransitionScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonA4AvailabilityTransitionScenario.java` | scenario | gap | File exists and fails explicitly if selected. failover/drain 전이를 socket/registry monitoring event로 묶는 runner가 아직 없다 |
| `Client/Scenarios/MonA5FixedKindsScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonA5FixedKindsScenario.java` | scenario | done | Client HTTP driver scenario file. malformed connection, status, timer-stopped evidence |
| `Client/Scenarios/MonB1KindFilterScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonB1KindFilterScenario.java`, `Server/FilteredService` | scenario | done | Client HTTP driver scenario file. socket source를 `CONNECTION_READY`로 제한 |
| `Client/Scenarios/MonB2RegistrationValidationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonB2RegistrationValidationScenario.java`, `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java` | scenario | done | Client HTTP driver scenario file. bad interval, missing socket, missing spot validation |
| `Client/Scenarios/MonC1DispatchFailureScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonC1DispatchFailureScenario.java`, `Server/ThrowingService` | scenario | done | Client HTTP driver scenario file. monitoring handler failure 후 follow-up messaging 성공 |
| `Client/Scenarios/MonD1FailureRecoveryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Scenarios/MonD1FailureRecoveryScenario.java` | scenario | gap | File exists and fails explicitly if selected. 장애/복구 반복 중 monitoring event 연속성을 보는 장시간 harness 없음 |
| `Server/Registry/RuntimeMonitoring.Registry.csproj` | `Server/Registry/build.gradle.kts` | build | done | registry application project |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/runtimemonitoring/registry/Program.java` | server-role | done | embedded registry entrypoint |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/runtimemonitoring/registry/Program.java` | server-role | done | Spring entrypoint가 host factory 역할 |
| `Server/Registry/Support/RegistryOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/runtimemonitoring/shared/Env.java` | support | done | registry endpoint 환경 변수 |
| `Server/Registry/Support/RegistryEvidenceStore.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/runtimemonitoring/registry/support/EvidenceState.java` | support | done | registry evidence store |
| `Server/Registry/Handlers/RegistryEventRecorders.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/runtimemonitoring/registry/support/MonitoringEventHandlers.java` | server-role | done | registry event recorder |
| `Server/Registry/Handlers/RegistryHandlers.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/runtimemonitoring/registry/Program.java` | server-role | not-needed | Java registry는 embedded registry option bean으로 구성 |
| `Server/Service/RuntimeMonitoring.Service.csproj` | `Server/Service/build.gradle.kts` | build | done | service application project |
| `Server/Service/Program.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/Program.java` | server-role | done | channel, spot, monitoring source 설정 |
| `Server/Service/ServiceHostFactory.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/Program.java` | server-role | done | Spring entrypoint가 host factory 역할 |
| `Server/Service/Support/ServiceOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/runtimemonitoring/shared/Env.java` | support | done | service endpoint 환경 변수 |
| `Server/Service/Support/ServiceEvidenceStore.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/support/EvidenceState.java` | support | done | service evidence store |
| `Server/Service/Handlers/ServiceEventRecorders.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/handlers/MonitoringEventHandlers.java` | server-role | done | socket/spot/failing monitoring recorder |
| `Server/Service/Handlers/ServiceHandlers.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/handlers/WorkReqHandler.java`, `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/handlers/MonitoringSpot.java` | server-role | done | request handler와 monitoring spot |
| `Server/FilteredService/*` | `Server/FilteredService/build.gradle.kts`, `Server/FilteredService/src/main/java/systems/zlink/e2e/runtimemonitoring/filteredservice/Program.java` | server-role | done | 별도 filtered service process. shared service configuration을 socket filter role로 실행한다 |
| `Server/ThrowingService/*` | `Server/ThrowingService/build.gradle.kts`, `Server/ThrowingService/src/main/java/systems/zlink/e2e/runtimemonitoring/throwingservice/Program.java` | server-role | done | 별도 throwing service process. shared service configuration을 throwing monitor role로 실행한다 |
| `Server/Trigger/RuntimeMonitoring.Trigger.csproj` | `Server/Trigger/build.gradle.kts` | build | done | trigger/validation application project |
| `Server/Trigger/Program.cs` | `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java` | server-role | done | framework client와 HTTP scenario endpoint entrypoint |
| `Server/Trigger/Support/*` | `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java`, `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/validation/*.java`, `Client/src/main/java/systems/zlink/e2e/runtimemonitoring/client/Support/TriggerScenarioClient.java`, `run_e2e.sh` | support | partial | Java trigger support는 validation config와 HTTP client helper에 통합되어 있다 |
| `Server/Trigger/TriggerEndpoints.cs` | `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java` | server-role | done | Java Trigger가 `/health`, `/scenario/<id>` HTTP endpoint를 제공한다 |
| `Server/Trigger/TriggerHandlers.cs` | `Server/Service/src/main/java/systems/zlink/e2e/runtimemonitoring/service/handlers/WorkReqHandler.java` | server-role | done | request trigger handler |
| `Server/Trigger/TriggerHostFactory.cs` | `Server/Trigger/src/main/java/systems/zlink/e2e/runtimemonitoring/trigger/Program.java` | server-role | done | Spring validation entrypoint |

## 남은 gap

- `MON-A4`: failover/drain 전이를 socket/registry monitoring event로 함께 보는 runner가 없다.
- `MON-D1`: 장애/복구 반복 중 monitoring event 연속성을 보는 장시간 harness가 없다.
