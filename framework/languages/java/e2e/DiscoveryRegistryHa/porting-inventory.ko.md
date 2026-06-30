# DiscoveryRegistryHa Java 포팅 인벤토리

이 문서는 `.NET` `DiscoveryRegistryHa` E2E 구성을 Java framework E2E로 옮길 때의 파일별 대응표다.
공통 시나리오 기준은 `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`이며,
새 public API, private API 우회, raw frame 우회 없이 Java framework의 공개 사용 패턴만 사용한다.

## 포팅 상태

| .NET 파일 | Java 대응 파일 | 상태 | 메모 |
| --- | --- | --- | --- |
| `.gitignore` | `.gitignore` | done | logs, Gradle 캐시, root/role build 산출물을 무시한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | done | 마지막 검증 결과와 `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`, `DR-C3` gap을 분리해 기록했다. |
| `run_e2e.sh` | `run_e2e.sh` | partial | 역할별 installDist binary를 실행한다. `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`는 public runtime gap marker로 남기고, 나머지 시나리오는 `logs/20260629-164645-539461` 실행에서 통과했다. |
| 없음 | `README.ko.md` | done | Java 실행 구조, 시나리오 범위, 남은 runtime gap을 설명한다. |
| `Shared/DiscoveryRegistryHa.Shared.csproj` | `Shared/build.gradle.kts` | done | 공용 DTO/helper project로 분리했다. |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/discoveryregistryha/shared/Contracts.java` | done | request, reply, wait request DTO를 공용 project에 둔다. |
| `Client/DiscoveryRegistryHa.Client.csproj` | `Client/build.gradle.kts` | done | 별도 client application으로 분리했다. |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/discoveryregistryha/client/Program.java` | done | scenario 이름을 대응 scenario class로 넘기는 진입점이다. 검증 로직은 support/scenario 파일로 분리했다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/.../client/support/ClientOptions.java` | done | 환경 변수 기반 client 입력을 한 곳에서 해석한다. |
| `Client/Support/DiscoveryApiResult.cs` | `Client/src/main/java/.../client/support/DiscoveryApiResult.java` | done | scenario 실행 결과 provider rid 집합을 담는다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/.../client/support/ScenarioAssert.java` | done | scenario 검증 실패를 일관된 예외로 올린다. |
| `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/src/main/java/.../client/scenarios/BasicDiscoveryScenario.java` | done | DR-A1 scenario class다. |
| `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/src/main/java/.../client/scenarios/DrA2ClusterBridgeScenario.java` | done | DR-A2 scenario class다. |
| `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/src/main/java/.../client/scenarios/DrA3ClusterBridgeScenario.java` | done | DR-A3 scenario class다. |
| `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/src/main/java/.../client/scenarios/DrA4ThirdRegistryScenario.java` | done | DR-A4 scenario class다. |
| `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/src/main/java/.../client/scenarios/DrB1FailoverScenario.java` | done | DR-B1 scenario class다. |
| `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/src/main/java/.../client/scenarios/DrB2FailoverScenario.java` | gap | Java runtime gap 때문에 runner는 `java-discovery-dead-registry-timeout` marker를 남긴다. |
| `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/src/main/java/.../client/scenarios/DrB3RecoveryScenario.java` | gap | Java runtime gap 때문에 runner는 `java-discovery-peer-flap-member-timeout` marker를 남긴다. |
| `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/src/main/java/.../client/scenarios/DrC1EmbeddedRegistryScenario.java` | gap | Java runtime gap 때문에 runner는 `java-discovery-survivor-member-timeout` marker를 남긴다. |
| `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/src/main/java/.../client/scenarios/DrC2EmbeddedRegistryScenario.java` | gap | Java runtime gap 때문에 runner는 `java-discovery-recovered-registry-member-timeout` marker를 남긴다. |
| `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/src/main/java/.../client/scenarios/DrC3EmbeddedRegistryScenario.java` | done | runner가 같은 consumer process로 outage 전/중 request를 보내고, 복구 뒤 재광고와 messaging 수렴을 확인한다. |
| `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/src/main/java/.../client/scenarios/DrD1DirectEndpointScenario.java` | done | DR-D1 scenario class다. |
| `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/src/main/java/.../client/scenarios/DrD2DirectEndpointScenario.java` | done | DR-D2 scenario class다. |
| `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/src/main/java/.../client/scenarios/DrD3DirectEndpointScenario.java` | done | DR-D3 scenario class다. |
| `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/src/main/java/.../client/scenarios/DrD4DirectEndpointScenario.java` | done | DR-D4 scenario class다. |
| `Server/Consumer/DiscoveryRegistryHa.Consumer.csproj` | `Server/Consumer/build.gradle.kts` | done | 별도 HTTP consumer application으로 분리했다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/discoveryregistryha/consumer/Program.java` | done | consumer process entrypoint다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/discoveryregistryha/consumer/ConsumerApplication.java`, `Server/Consumer/src/main/java/systems/zlink/e2e/discoveryregistryha/consumer/ConsumerEndpoints.java` | done | `/health`, `/profile/request`, `/profile/request/wait`, `/shutdown`을 제공한다. |
| `Server/Consumer/ConsumerOptions.cs` | `Server/Consumer/src/main/java/systems/zlink/e2e/discoveryregistryha/consumer/ConsumerOptions.java` | done | consumer HTTP endpoint, registry endpoint, log dir env를 해석한다. |
| `Server/Embedded/DiscoveryRegistryHa.Embedded.csproj` | `Server/Embedded/build.gradle.kts` | done | embedded registry+provider application으로 분리했다. |
| `Server/Embedded/Program.cs` | `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/Program.java` | done | embedded process entrypoint다. |
| `Server/Embedded/EmbeddedHandlers.cs` | `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/ProfileReqHandler.java` | done | embedded provider request handler다. |
| `Server/Embedded/EmbeddedHostFactory.cs` | `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/EmbeddedApplication.java`, `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/EmbeddedEndpoints.java` | done | embedded registry와 channel provider를 한 process에서 구성한다. |
| `Server/Embedded/Support/EmbeddedEvidenceStore.cs` | `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/EmbeddedEvidenceStore.java` | done | embedded provider evidence 저장과 wait를 제공한다. |
| `Server/Embedded/Support/EmbeddedOptions.cs` | `Server/Embedded/src/main/java/systems/zlink/e2e/discoveryregistryha/embedded/EmbeddedOptions.java` | done | embedded 전용 입력을 해석한다. |
| `Server/Probe/DiscoveryRegistryHa.Probe.csproj` | `Server/Probe/build.gradle.kts` | done | 별도 probe application project가 있다. |
| `Server/Probe/Program.cs` | `Server/Probe/src/main/java/.../probe/Program.java` | done | probe process entrypoint다. |
| `Server/Probe/ProbeHostFactory.cs` | `Server/Probe/src/main/java/.../probe/ProbeApplication.java`, `Server/Probe/src/main/java/.../probe/ProbeEndpoints.java` | done | `/health`, `/registry/topology`, `/registry/topology/wait`, `/shutdown`을 제공한다. DR-D4 runner가 별도 probe process를 remote query client로 사용한다. |
| `Server/Probe/ProbeOptions.cs` | `Server/Probe/src/main/java/.../probe/ProbeOptions.java` | done | probe HTTP endpoint와 query 대상 registry router endpoint를 env로 해석한다. |
| `Server/Provider/DiscoveryRegistryHa.Provider.csproj` | `Server/Provider/build.gradle.kts` | done | provider role application이다. |
| `Server/Provider/Program.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/Program.java` | done | provider process entrypoint다. |
| `Server/Provider/ProviderHandlers.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/ProfileReqHandler.java` | done | provider request handler다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/ProviderApplication.java`, `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/ProviderEndpoints.java` | done | provider framework 구성과 HTTP evidence API를 분리했다. |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/ProviderEvidenceStore.java` | done | provider 요청 evidence 저장과 `/evidence/wait`를 제공한다. |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/src/main/java/systems/zlink/e2e/discoveryregistryha/provider/ProviderOptions.java` | done | provider rid, channel endpoint, discovery endpoint, evidence file, log dir 입력을 해석한다. |
| `Server/Registry/DiscoveryRegistryHa.Registry.csproj` | `Server/Registry/build.gradle.kts` | done | registry role application이다. |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/discoveryregistryha/registry/Program.java` | done | registry process entrypoint다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/discoveryregistryha/registry/RegistryApplication.java`, `Server/Registry/src/main/java/systems/zlink/e2e/discoveryregistryha/registry/RegistryEndpoints.java` | done | registry HTTP 관리 API와 embedded registry 구성을 제공한다. |
| `Server/Registry/RegistryOptions.cs` | `Server/Registry/src/main/java/systems/zlink/e2e/discoveryregistryha/registry/RegistryOptions.java` | done | registry id, pub/router/http endpoint와 peer endpoint를 env에서 해석한다. |

## 시나리오 대응

| 공통 시나리오 | .NET 검증 방식 | Java 현재 상태 | 포팅 판단 |
| --- | --- | --- | --- |
| DR-A1 | registry, provider 2개, consumer HTTP request, provider evidence | 단일 client process가 직접 request하고 stdout만 확인 | consumer/provider evidence API를 도입해 .NET 수준으로 맞춘다. |
| DR-A2 | 2 registry peer bridge, consumer는 다른 registry를 통해 request | Java runner가 같은 topology를 구성하고 member peer와 messaging을 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-A3 | 3 registry peer bridge, A/B provider 수렴 | Java runner가 같은 topology를 구성하고 provider rid 수렴을 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-A4 | 같은 rid 다른 endpoint conflict에서 request가 stale hang 없이 성공 | Java runner가 duplicate provider를 추가하고 request 성공을 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-B1 | late-start registry join 후 member view와 messaging 확인 | Java runner가 late-start registry join 후 request 성공을 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-B2 | registry 정지 뒤 살아 있는 route 확인 | Java consumer가 살아 있는 reg-1만 configured해도 reg-2 정지 뒤 reply를 받지 못하고 timeout난다. `logs/repro-b2-20260629-164320-530103`에서 consumer send만 반복되고 provider flow는 남지 않았다. | `java-discovery-dead-registry-timeout` gap으로 남긴다. |
| DR-B3 | registry peer link flapping 뒤 recovery 확인 | Java runner가 reg2를 두 번 재시작한 뒤 recovered registry member view에서 provider를 보지 못한다. | `java-discovery-peer-flap-member-timeout` gap으로 남긴다. |
| DR-C1 | 한 registry down 상태에서 alive endpoint 성공과 dead endpoint bounded failure | Java reg-2 강제 종료 뒤 살아 있는 reg-1의 member query가 직접 광고된 `api-a`도 bounded timeout 안에 보지 못한다. | `java-discovery-survivor-member-timeout` gap으로 남긴다. |
| DR-C2 | down registry recover 뒤 messaging 수렴 | Java recovered reg-2 member view가 provider를 다시 보지 못한다. | `java-discovery-recovered-registry-member-timeout` gap으로 남긴다. |
| DR-C3 | 전체 registry outage 동안 기존 consumer channel 동작, recovery 뒤 재광고 확인 | Java runner가 같은 consumer process로 outage 전/중 request를 보내고, recovery 뒤 재광고와 messaging 수렴을 확인 | 구조와 검증 의미를 유지한다. |
| DR-D1 | embedded registry+provider direct endpoint | Java runner가 embedded role로 확인 | embedded app으로 분리한다. |
| DR-D2 | standalone registry direct endpoint | Java runner가 standalone registry로 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-D3 | embedded + standalone mixed cluster | Java runner가 embedded와 standalone registry를 함께 확인 | 구조는 분리하고 검증 의미는 유지한다. |
| DR-D4 | in-process query와 remote topology snapshot 일치 | Java runner가 registry HTTP endpoint와 별도 probe process의 remote topology 결과를 비교 | 구조와 검증 의미를 유지한다. |

## 구현 순서

1. Gradle을 root project와 역할별 subproject 구조로 나눈다.
2. 공용 메시지와 옵션 파서를 `Shared`로 옮긴다.
3. registry, provider, embedded, consumer, probe process를 public framework API만 사용해 구현한다.
4. client scenario runner를 HTTP 기반 검증으로 바꾸고 DR-C3의 outage 중 기존 consumer request를 보강한다.
5. `run_e2e.sh`를 역할별 installDist binary 실행으로 바꾼다.
6. 실제 `run_e2e.sh` 실행 결과와 feature-map, README를 같은 근거로 맞춘다.
