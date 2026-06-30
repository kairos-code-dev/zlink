# Java ResilienceLifecycle .NET 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`
- `framework/languages/dotnet/e2e/ResilienceLifecycle/feature-map.ko.md`
- `framework/languages/dotnet/e2e/ResilienceLifecycle/`

## 요약

기존 Java 구현은 단일 Gradle application과 `ZLINK_JAVA_E2E_ROLE` 전환 구조였다. 현재 구조는
`.NET` 기준에 맞춰 `Shared`, `Client`, `Server/Registry`, `Server/Provider` Gradle subproject로
분리했다. Java 구현은 기존 public framework API 경로를 유지하며, 완료되지 않은 scenario는
`feature-map.ko.md`에 gap으로 남긴다.

## Inventory

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | multi-project build, `.gradle`, logs 산출물 제외 |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | role별 installDist binary를 실행한다. RL-A1/A2/A3/A5/B1/B3/B4/B5/B6/C1/C3/D1/D3/D5 marker를 확인한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | Java 완료/gap scenario 구분 유지 |
| `README.ko.md` | `README.ko.md` | docs | done | Java role 구조와 실행 방법 |
| `Shared/ResilienceLifecycle.Shared.csproj` | `Shared/build.gradle.kts` | build | done | shared Java library project |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/resiliencelifecycle/shared/Contracts.java` | shared | done | request, command, reply, evidence record |
| `Client/ResilienceLifecycle.Client.csproj` | `Client/build.gradle.kts` | build | done | client application project |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/Program.java` | client | done | Spring client entrypoint와 framework 설정 |
| `Client/Support/ClientOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/resiliencelifecycle/shared/Env.java` | support | done | Java runner는 환경 변수 helper로 option을 읽는다 |
| `Client/Support/LifecycleApiResult.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | support | not-needed | Java는 provider evidence JSON을 `JsonNode`로 읽어 marker만 검증한다 |
| `Client/Support/ResilienceProcessManager.cs` | `run_e2e.sh` | support | done | Java process orchestration은 shell runner가 담당한다 |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | support | done | `ensure(...)` helper로 scenario assertion 수행 |
| `Client/Support/TopologyEntryResult.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | support | done | Java는 `ZLinkRegistryQueryClient.topology()` public result를 직접 검사한다 |
| `Client/Scenarios/RlA1ProviderRestartScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | `restart` mode, `scenario RL-A1 passed` marker |
| `Client/Scenarios/RlA2ProviderEndpointRemapScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | `reschedule` mode, replacement endpoint topology marker |
| `Client/Scenarios/RlA3ReconnectStormScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java`, `run_e2e.sh` | scenario | done | storm client process wave 실행 |
| `Client/Scenarios/RlA4DrainAndGreenEndpointScenario.cs` | `feature-map.ko.md` | scenario | gap | Java rolling/blue-green orchestration 미구현 |
| `Client/Scenarios/RlA5ProviderFlappingScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java`, `run_e2e.sh` | scenario | done | flapping control signal과 follow-up 검증 |
| `Client/Scenarios/RlB1CancellationCleanupScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | timeout 후 follow-up request 성공 |
| `Client/Scenarios/RlB2CrashDuringInflightScenario.cs` | `feature-map.ko.md` | scenario | gap | provider crash 중 native context close hang 경로가 남아 완료 처리하지 않음 |
| `Client/Scenarios/RlB3GracefulShutdownScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | provider admin shutdown 후 topology 수렴 |
| `Client/Scenarios/RlB4RuntimeDrainScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | provider admin weight drain/restore |
| `Client/Scenarios/RlB5DrainInflightScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | slow in-flight 완료 후 restore |
| `Client/Scenarios/RlB6GrayFaultScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | gray failure와 healthy provider 성공 검증 |
| `Client/Scenarios/RlC1ClientHostLifecycleScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java`, `run_e2e.sh` | scenario | done | cleanup mode와 process 종료 확인 |
| `Client/Scenarios/RlC2TopologyRecoveryScenario.cs` | `feature-map.ko.md` | scenario | gap | crash 후 TTL stale entry 제거를 결정적으로 고정하는 runner 연결 미완료 |
| `Client/Scenarios/RlC3NodePauseRecoveryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java`, `run_e2e.sh` | scenario | done | provider down/restart 복구를 RL-A1 flow에서 함께 검증 |
| `Client/Scenarios/RlC4RegistryOutageScenario.cs` | `feature-map.ko.md` | scenario | gap | registry 재기동 뒤 새 client follow-up이 `NOT_ADMITTED`로 남는 경로 |
| `Client/Scenarios/RlD1HighFanoutScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java`, `run_e2e.sh` | scenario | done | storm wave marker |
| `Client/Scenarios/RlD2ObserverFaultScenario.cs` | `feature-map.ko.md` | scenario | gap | observer 실패 격리와 runtime error sink assertion 미구현 |
| `Client/Scenarios/RlD3DispatchErrorEvidenceScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | dispatch error marker evidence 검증 |
| `Client/Scenarios/RlD4MissingRequestHandlerScenario.cs` | `feature-map.ko.md` | scenario | gap | error reply wire header roundtrip 확인 harness 미구현 |
| `Client/Scenarios/RlD5MixedBurstScenario.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | scenario | done | request/send mixed workload marker |
| `Server/Registry/ResilienceLifecycle.Registry.csproj` | `Server/Registry/build.gradle.kts` | build | done | registry application project |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/resiliencelifecycle/registry/Program.java` | server-role | done | embedded registry entrypoint |
| `Server/Registry/Configuration/ServerOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/resiliencelifecycle/shared/Env.java` | server-role | done | registry endpoint 환경 변수 |
| `Server/Registry/Endpoints/RegistryEndpoints.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/resiliencelifecycle/registry/Program.java` | server-role | not-needed | Java registry role은 별도 HTTP endpoint가 없다 |
| `Server/Registry/Endpoints/TopologyEntryResult.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/scenarios/ClientScenario.java` | support | not-needed | public topology API result를 직접 사용한다 |
| `Server/Registry/Handlers/RegistryHandlers.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/resiliencelifecycle/registry/Program.java` | server-role | not-needed | Java embedded registry option bean만 필요하다 |
| `Server/Registry/Infrastructure/EvidenceStore.cs` | `feature-map.ko.md` | server-role | not-needed | registry evidence endpoint 미사용 |
| `Server/Registry/Infrastructure/FaultState.cs` | `feature-map.ko.md` | server-role | gap | registry outage/fault scenario는 Java gap |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/resiliencelifecycle/registry/Program.java` | server-role | done | Spring entrypoint가 host factory 역할 |
| `Server/Consumer/ResilienceLifecycle.Consumer.csproj` | `Client/build.gradle.kts` | build | not-needed | Java consumer 역할은 scenario client process에 포함 |
| `Server/Consumer/Program.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/Program.java` | server-role | not-needed | Java는 별도 persistent consumer server를 두지 않는다 |
| `Server/Consumer/ConsumerHostFactory.cs` | `Client/src/main/java/systems/zlink/e2e/resiliencelifecycle/client/Program.java` | server-role | not-needed | client Spring entrypoint가 consumer host 역할 |
| `Server/Provider/ResilienceLifecycle.Provider.csproj` | `Server/Provider/build.gradle.kts` | build | done | provider application project |
| `Server/Provider/Program.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/Program.java` | server-role | done | provider entrypoint와 framework 설정 |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/Program.java` | server-role | done | Spring entrypoint가 host factory 역할 |
| `Server/Provider/ProviderEndpoints.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/endpoints/EvidenceHttpServer.java` | server-role | done | health, evidence, drain, restore, fault, shutdown endpoint |
| `Server/Provider/ProviderSupport.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/infrastructure/ScenarioState.java` | server-role | done | evidence state, slow release, gray fault state |
| `Server/Provider/Handlers/ProviderHandlers.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/handlers/WorkRequestHandler.java`, `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/handlers/WorkCommandHandler.java` | server-role | done | request/send handler |
| `Server/Provider/Handlers/EvidenceDispatchErrorObserver.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/resiliencelifecycle/provider/Program.java` | server-role | done | message flow observer가 dispatch error marker를 evidence에 기록 |

## 남은 gap

- `RL-A4`: rolling/blue-green 전환 orchestration 없음.
- `RL-B2`: provider crash 중 in-flight request와 native context close hang 경로.
- `RL-C2`: crash 후 TTL stale entry 제거를 빠르게 고정하는 runner 연결 없음.
- `RL-C4`: registry restart/outage 뒤 새 client follow-up이 `NOT_ADMITTED`로 남는 경로.
- `RL-D2`: observer failure와 runtime error sink assertion 없음.
- `RL-D4`: error reply wire header roundtrip 확인 harness 없음.
