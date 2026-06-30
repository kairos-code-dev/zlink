# Kotlin DiscoveryRegistryHa .NET 기준 포팅 인벤토리

기준 구현: `framework/languages/dotnet/e2e/DiscoveryRegistryHa`

공통 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

현재 Kotlin DiscoveryRegistryHa E2E는 `Shared`, `Client`, `Server/Registry`, `Server/Provider`,
`Server/Consumer`, `Server/Probe`, `Server/Embedded` Gradle project로 process 역할을 나눠 실행한다.
client scenario dispatcher, scenario ID별 실행 파일, shared message 타입, registry/provider/consumer/probe/embedded
role support는 Kotlin source에 있고, 일부 CLI option parser만 Java source로 유지한다.

상태 값:

- `done`: 현재 파일이 목표 위치와 의미를 만족한다.
- `not-needed`: Kotlin 구조에서 같은 파일 단위가 필요 없으며 비고에 근거를 적었다.
- `gap`: public contract 또는 runtime 지원이 없어 완료로 주장할 수 없다.

| .NET 기준 파일 | Kotlin 대응 파일 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `.gitignore` | `.gitignore` | config-root | done | Gradle 산출물과 logs 제외는 유지한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | role별 Gradle project runner 검증 결과와 Kotlin scenario/support 분리 완료 상태를 반영했다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | role별 installDist binary를 시작하고 readiness, cleanup, 실패 로그 출력을 수행한다. |
| `Shared/DiscoveryRegistryHa.Shared.csproj` | `Shared/build.gradle.kts` | build | done | Shared Gradle project를 만들고 client/server role project가 의존한다. |
| `Shared/Messages.cs` | `Shared/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/Messages.kt` | shared | done | request/reply/channel 타입을 Kotlin shared source로 옮겼고 Java role code가 쓰는 `Contracts` JVM surface는 유지했다. |
| `Client/DiscoveryRegistryHa.Client.csproj` | `Client/build.gradle.kts` | build | done | Client application project를 만들었다. |
| `Client/Program.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/Program.kt` | client-entry | done | Client binary entry point가 client application만 실행한다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/systems/zlink/e2e/kotlin/discoveryregistryha/ClientOptions.java` | support | done | client scenario, registry, consumer, probe, expected rid, log dir 입력을 role CLI option으로 파싱한다. |
| `Client/Support/DiscoveryApiRes.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Support/DiscoveryApiRes.kt` | support | done | `.NET` 대응 support record를 Kotlin data class로 추가했다. probe HTTP 응답을 typed result로 읽을 때 사용한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Support/ScenarioAssert.kt` | support | done | scenario assertion helper를 Kotlin support로 분리했다. HTTP/probe/messaging wait helper는 `ClientScenarioContext.kt`가 맡는다. |
| `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/BasicDiscoveryScenario.kt` | scenario | done | DR-A1. Kotlin scenario file이 shared context를 통해 member wait와 messaging 검증을 실행한다. |
| `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrA2ClusterBridgeScenario.kt` | scenario | done | DR-A2. Kotlin scenario file이 shared context를 통해 peer 합산 view와 messaging 검증을 실행한다. |
| `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrA3ClusterBridgeScenario.kt` | scenario | done | DR-A3. Kotlin scenario file이 shared context를 통해 3 registry peer 합산 검증을 실행한다. |
| `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrA4ThirdRegistryScenario.kt` | scenario | done | DR-A4. Kotlin scenario file이 same-rid/different-endpoint case를 bounded messaging 검증으로 실행한다. |
| `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrB1FailoverScenario.kt` | scenario | done | DR-B1. Kotlin scenario file이 late-start registry 합류 검증을 실행한다. |
| `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrB2FailoverScenario.kt` | scenario | done | DR-B2. Kotlin scenario file이 registry stop/recover 검증을 실행한다. |
| `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrB3RecoveryScenario.kt` | scenario | done | DR-B3. Kotlin scenario file이 peer flapping 후 수렴 검증을 실행한다. |
| `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrC1EmbeddedRegistryScenario.kt` | scenario | done | DR-C1. Kotlin scenario file이 살아 있는 registry endpoint와 죽은 probe bounded failure를 검증한다. |
| `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrC2EmbeddedRegistryScenario.kt` | scenario | done | DR-C2. Kotlin scenario file이 복구 registry 재합류 검증을 실행한다. |
| `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrC3EmbeddedRegistryScenario.kt` | scenario | done | DR-C3. Kotlin scenario file이 전체 registry 복구 후 재광고 수렴 검증을 실행한다. |
| `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrD1DirectEndpointScenario.kt` | scenario | done | DR-D1. Kotlin scenario file이 embedded deployment messaging 검증을 실행한다. |
| `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrD2DirectEndpointScenario.kt` | scenario | done | DR-D2. Kotlin scenario file이 standalone deployment 대조 검증을 실행한다. |
| `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrD3DirectEndpointScenario.kt` | scenario | done | DR-D3. Kotlin scenario file이 embedded+standalone mixed cluster 검증을 실행한다. |
| `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/client/Scenarios/DrD4DirectEndpointScenario.kt` | scenario | done | DR-D4. Kotlin scenario file이 in-process probe와 remote query topology snapshot 동등성 검증을 실행한다. |
| `Server/Registry/DiscoveryRegistryHa.Registry.csproj` | `Server/Registry/build.gradle.kts` | build | done | Registry role project를 만들었다. |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/Program.kt` | server-entry | done | Registry binary entry point가 registry application만 실행한다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/registry/RegistryApplication.kt` | server-role | done | registry server, peer endpoints, probe HTTP endpoint 구성을 Registry role Kotlin package로 옮겼다. |
| `Server/Registry/RegistryOptions.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/kotlin/discoveryregistryha/RegistryOptions.java` | configuration | done | registry id/pub/router/http/peer endpoints를 role CLI option으로 파싱한다. |
| `Server/Provider/DiscoveryRegistryHa.Provider.csproj` | `Server/Provider/build.gradle.kts` | build | done | Provider role project를 만들었다. |
| `Server/Provider/Program.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/Program.kt` | server-entry | done | Provider binary entry point가 provider application만 실행한다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/provider/ProviderApplication.kt` | server-role | done | provider API channel, discovery advertisement, flow logging 구성을 Provider role Kotlin package로 옮겼다. |
| `Server/Provider/ProviderHandlers.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/provider/Handlers/WorkRequestHandler.kt` | handlers | done | provider request handler를 Kotlin Provider handler package로 옮겼다. |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/provider/Support/ProviderEvidenceStore.kt` | support | done | provider rid state를 Kotlin Provider support로 옮겼다. |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/kotlin/discoveryregistryha/ProviderOptions.java` | configuration | done | provider rid/api endpoint/registry endpoints/log dir를 role CLI option으로 파싱한다. |
| `Server/Consumer/DiscoveryRegistryHa.Consumer.csproj` | `Server/Consumer/build.gradle.kts` | build | done | Consumer role Gradle project를 추가했다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/consumer/Program.kt` | server-entry | done | Consumer binary entry point가 consumer application만 실행한다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/consumer/ConsumerApplication.kt` | server-role | done | consumer channel client role을 별도 process로 분리했고 HTTP request endpoint가 public `ZLinkClient` discovery 경로를 사용한다. |
| `Server/Consumer/ConsumerOptions.cs` | `Server/Consumer/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/consumer/Configuration/ConsumerOptions.kt` | configuration | done | consumer registry endpoint/log option을 CLI로 파싱한다. |
| `Server/Probe/DiscoveryRegistryHa.Probe.csproj` | `Server/Probe/build.gradle.kts` | build | done | Probe role Gradle project를 추가했다. |
| `Server/Probe/Program.cs` | `Server/Probe/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/probe/Program.kt` | server-entry | done | Probe binary entry point가 remote query probe application만 실행한다. |
| `Server/Probe/ProbeHostFactory.cs` | `Server/Probe/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/probe/ProbeApplication.kt` | server-role | done | remote registry query probe를 별도 process로 분리했고 public `ZLinkRegistryQueryClient`로 topology를 조회한다. |
| `Server/Probe/ProbeOptions.cs` | `Server/Probe/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/probe/Configuration/ProbeOptions.kt` | configuration | done | probe target endpoint/http/log option을 CLI로 파싱한다. |
| `Server/Embedded/DiscoveryRegistryHa.Embedded.csproj` | `Server/Embedded/build.gradle.kts` | build | done | Embedded role project를 만들었다. |
| `Server/Embedded/Program.cs` | `Server/Embedded/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/Program.kt` | server-entry | done | Embedded binary entry point가 registry와 provider application을 같은 process에서 실행한다. |
| `Server/Embedded/EmbeddedHostFactory.cs` | `Server/Embedded/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/embedded/EmbeddedApplication.kt` | server-role | done | embedded process가 registry application과 provider application을 같은 JVM에서 실행하는 구성을 Embedded role Kotlin package로 옮겼다. |
| `Server/Embedded/EmbeddedHandlers.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/provider/Handlers/WorkRequestHandler.kt` | handlers | not-needed | Embedded process는 Provider role application을 그대로 함께 실행하므로 별도 embedded handler class를 만들지 않는다. |
| `Server/Embedded/Support/EmbeddedEvidenceStore.cs` | `Server/Provider/src/main/kotlin/systems/zlink/e2e/kotlin/discoveryregistryha/provider/Support/ProviderEvidenceStore.kt` | support | not-needed | Embedded provider state도 Provider role support를 재사용하므로 별도 embedded evidence store를 만들지 않는다. |
| `Server/Embedded/Support/EmbeddedOptions.cs` | `Server/Embedded/src/main/java/.../RegistryOptions.java`, `Server/Embedded/src/main/java/.../ProviderOptions.java` | configuration | not-needed | Embedded process는 registry와 provider application을 같은 JVM에서 실행하며 같은 CLI args를 `RegistryOptions`와 `ProviderOptions`가 각각 파싱한다. 별도 option object를 만들지 않는다. |

## 기존 Kotlin/Java 파일 처리

| 기존 파일 | 판단 | 목표 |
|-----------|------|------|
| `src/main/kotlin/.../Program.kt` | role env 분기를 제거하고 role별 project entry point로 나눴다. | 완료했다. |
| `src/main/java/.../ClientApplication.java` | client scenario 실행 application을 Kotlin client package로 옮겼다. | 완료했다. |
| `src/main/java/.../ClientScenario.java` | Java monolith를 제거하고 root Kotlin dispatcher, scenario ID별 Kotlin file, `Client/Support/ClientScenarioContext.kt`로 나눴다. | 완료했다. |
| `src/main/java/.../Contracts.java` | shared message/channel 타입을 `Shared/src/main/kotlin/.../Messages.kt`로 옮겼다. | 완료했다. |
| `src/main/java/.../Env.java` | role 입력용 환경 변수 helper였고, 현재 production source에서 더 이상 쓰지 않는다. | 삭제했다. build/cache 환경 변수는 runner와 Gradle build control로만 남긴다. |
| `src/main/java/.../RegistryApplication.java` | Registry role을 Kotlin Registry package로 옮겼다. | 완료했다. |
| `src/main/java/.../RegistryProbeServer.java` | registry in-process probe endpoint를 Kotlin Registry package로 옮겼다. | 완료했다. |
| `src/main/java/.../ProviderApplication.java` | Provider role을 Kotlin Provider package로 옮겼다. | 완료했다. |
| `src/main/java/.../ProviderState.java` | Provider state를 Kotlin Provider support로 옮겼다. | 완료했다. |
| `src/main/java/.../handlers/WorkRequestHandler.java` | provider request handler를 Kotlin Provider handler package로 옮겼다. | Embedded는 Provider role application을 재사용한다. |

## Scenario ID 매핑

| Scenario ID | 공통 우선순위 | .NET 기준 scenario 파일 | Kotlin 목표 파일 | 상태 |
|-------------|---------------|-------------------------|------------------|------|
| `DR-A1` | P0 | `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/.../Scenarios/BasicDiscoveryScenario.kt` | done |
| `DR-A2` | P0 | `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/.../Scenarios/DrA2ClusterBridgeScenario.kt` | done |
| `DR-A3` | P0 | `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/.../Scenarios/DrA3ClusterBridgeScenario.kt` | done |
| `DR-A4` | P2 | `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/.../Scenarios/DrA4ThirdRegistryScenario.kt` | done |
| `DR-B1` | P1 | `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/.../Scenarios/DrB1FailoverScenario.kt` | done |
| `DR-B2` | P1 | `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/.../Scenarios/DrB2FailoverScenario.kt` | done |
| `DR-B3` | P2 | `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/.../Scenarios/DrB3RecoveryScenario.kt` | done |
| `DR-C1` | P0 | `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/.../Scenarios/DrC1EmbeddedRegistryScenario.kt` | done |
| `DR-C2` | P1 | `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/.../Scenarios/DrC2EmbeddedRegistryScenario.kt` | done |
| `DR-C3` | P2 | `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/.../Scenarios/DrC3EmbeddedRegistryScenario.kt` | done |
| `DR-D1` | P1 | `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/.../Scenarios/DrD1DirectEndpointScenario.kt` | done |
| `DR-D2` | P1 | `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/.../Scenarios/DrD2DirectEndpointScenario.kt` | done |
| `DR-D3` | P2 | `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/.../Scenarios/DrD3DirectEndpointScenario.kt` | done |
| `DR-D4` | P1 | `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/.../Scenarios/DrD4DirectEndpointScenario.kt` | done |
