# Java SpotService .NET 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-2-spot-service.ko.md`
- `framework/languages/dotnet/e2e/SpotService/feature-map.ko.md`
- `framework/languages/dotnet/e2e/SpotService/`

## 요약

기존 Java SpotService E2E는 단일 Gradle application에서 `ZLINK_JAVA_E2E_ROLE`로 `registry`, `play`,
`publisher`, `client` 역할을 전환했다. 현재 구조는 기존 구현을 보존하면서 `Shared`, `Client`,
`Server/Registry`, `Server/Play`, `Server/Publisher` Gradle subproject로 분리했다.

`.NET` 기준에는 `Gateway`, `MultiNode`, `Session` 등 더 많은 role과 `SM-*` scenario 파일이 있다.
Java feature-map은 현재 구현된 public API 경로와 public contract/harness gap을 구분한다. gap은
테스트 전용 adapter, raw frame 우회, 새 public API 추가로 메우지 않는다.

## Inventory

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | multi-project build, `.gradle`, logs 산출물 제외 |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | role별 installDist binary를 실행한다. Java feature-map의 완료 marker를 검증한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | Java 완료/gap scenario 구분 |
| `Shared/SpotService.Shared.csproj` | `Shared/build.gradle.kts` | build | done | shared Java library project |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/Contracts.java` | shared | done | request, reply, evidence, stream payload record |
| `Client/SpotService.Client.csproj` | `Client/build.gradle.kts` | build | done | client application project |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/spotservice/client/Program.java` | client | done | scenario driver spot를 생성하는 client entrypoint |
| `Client/Scenarios/SmA1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | `SM-A1` marker |
| `Client/Scenarios/SmA2Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | `SM-A2` marker |
| `Client/Scenarios/SmA3Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | `SM-A3` marker |
| `Client/Scenarios/SmA4Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | `SM-A4` marker |
| `Client/Scenarios/SmA5Scenario.cs` | `feature-map.ko.md` | scenario | gap | Java에는 app-level Stage wrapper public 계층이 없다 |
| `Client/Scenarios/SmA6Scenario.cs` | `run_e2e.sh`, `Server/Play/src/main/java/systems/zlink/e2e/spotservice/play/Program.java` | scenario | done | explicit close lifecycle marker |
| `Client/Scenarios/SmA7Scenario.cs` | `run_e2e.sh`, `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/MismatchedSpot.java` | scenario | done | spot type mismatch marker |
| `Client/Scenarios/SmA8Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/UserSpot.java`, `ClientScenario.java` | scenario | done | worker offload marker |
| `Client/Scenarios/SmB1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ScenarioSession.java`, `ScenarioActor.java`, `ClientScenario.java` | scenario | done | local actor join marker |
| `Client/Scenarios/SmB2Scenario.cs` | `feature-map.ko.md` | scenario | gap | remote actor join E2E gap |
| `Client/Scenarios/SmB3Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ScenarioActor.java`, `ClientScenario.java` | scenario | done | payload fidelity marker |
| `Client/Scenarios/SmB4Scenario.cs` | `feature-map.ko.md` | scenario | gap | remote actor request/reply E2E gap |
| `Client/Scenarios/SmB5Scenario.cs` | `feature-map.ko.md` | scenario | gap | missing actor packet negative path 미구현 |
| `Client/Scenarios/SmB6Scenario.cs` | `feature-map.ko.md` | scenario | gap | actor leave/disconnect callback 차이 미구현 |
| `Client/Scenarios/SmB7Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ScenarioActor.java`, `ClientScenario.java` | scenario | done | actor lifecycle/order marker |
| `Client/Scenarios/SmB8Scenario.cs` | `feature-map.ko.md` | scenario | gap | Java destroyActor 의미 고정 scenario 미구현 |
| `Client/Scenarios/SmC1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | external consumer to spot messaging |
| `Client/Scenarios/SmC2Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/UserSpot.java`, `ClientScenario.java` | scenario | done | spot to channel and publish |
| `Client/Scenarios/SmC3Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/UserSpot.java`, `ClientScenario.java` | scenario | done | spot to spot and publish |
| `Client/Scenarios/SmC4Scenario.cs` | `Server/Publisher/src/main/java/systems/zlink/e2e/spotservice/publisher/Program.java` | scenario | done | publisher client marker |
| `Client/Scenarios/SmD1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ScenarioSession.java`, `ClientScenario.java` | scenario | done | local actor session bind/relay |
| `Client/Scenarios/SmD2Scenario.cs` | `feature-map.ko.md` | scenario | gap | remote stream session bind/relay E2E gap |
| `Client/Scenarios/SmD3Scenario.cs` | `feature-map.ko.md` | scenario | gap | entry/user spot actor bind 비교 미구현 |
| `Client/Scenarios/SmD4Scenario.cs` | `feature-map.ko.md` | scenario | gap | multiple actor bind 미구현 |
| `Client/Scenarios/SmD5Scenario.cs` | `feature-map.ko.md` | scenario | gap | disconnect callback target 미구현 |
| `Client/Scenarios/SmD6Scenario.cs` | `feature-map.ko.md` | scenario | gap | bound session push target isolation 미구현 |
| `Client/Scenarios/SmD7Scenario.cs` | `feature-map.ko.md` | scenario | gap | stream auth and pre-auth dispatch failure 미구현 |
| `Client/Scenarios/SmD8Scenario.cs` | `feature-map.ko.md` | scenario | gap | reconnect pending failure/reauth/rebind 미구현 |
| `Client/Scenarios/SmD9Scenario.cs` | `feature-map.ko.md` | scenario | gap | stream inbound observer evidence 미구현 |
| `Client/Scenarios/SmD10Scenario.cs` | `feature-map.ko.md` | scenario | gap | stream backpressure public contract 미고정 |
| `Client/Scenarios/SmD11Scenario.cs` | `feature-map.ko.md` | scenario | gap | mixed stream/channel request 미구현 |
| `Client/Scenarios/SmD12Scenario.cs` | `feature-map.ko.md` | scenario | gap | session reconnect migration 미구현 |
| `Client/Scenarios/SmD13Scenario.cs` | `feature-map.ko.md` | scenario | gap | heartbeat disconnect detection 미구현 |
| `Client/Scenarios/SmD14Scenario.cs` | `feature-map.ko.md` | scenario | gap | TLS stream endpoint/certificate 미구현 |
| `Client/Scenarios/SmE1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java`, `Server/Play/src/main/java/systems/zlink/e2e/spotservice/play/Program.java` | scenario | done | missing route dispatch observer evidence |
| `Client/Scenarios/SmE2Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/TimerScenarioSpot.java` | scenario | done | timer tick marker |
| `Client/Scenarios/SmE3Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/IdleCloseTimerHandler.java` | scenario | done | idle timer close marker |
| `Client/Scenarios/SmE4Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/TimerOverrunHandler.java` | scenario | done | timer overrun policy marker |
| `Client/Scenarios/SmF1Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | route mesh target spot |
| `Client/Scenarios/SmF2Scenario.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | scenario | done | route mesh channel name |
| `Client/Scenarios/SmF4Scenario.cs` | `feature-map.ko.md` | scenario | partial | missing route marker 일부, malformed raw-frame harness 없음 |
| `Client/Scenarios/SmG1Scenario.cs` | `feature-map.ko.md` | scenario | gap | play node crash/rejoin harness 없음 |
| `Client/Scenarios/SmG2Scenario.cs` | `feature-map.ko.md` | scenario | gap | owner remap harness 없음 |
| `Client/Scenarios/SmG3Scenario.cs` | `feature-map.ko.md` | scenario | gap | join/leave/request 경합 harness 없음 |
| `Client/Scenarios/SmG4Scenario.cs` | `feature-map.ko.md` | scenario | gap | bound session push 부하 harness 없음 |
| `Client/Scenarios/SmQ9Scenario.cs` | `feature-map.ko.md` | scenario | gap | Java feature-map에 완료 근거 없음 |
| `Client/Support/ClientOptions.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/Env.java` | support | done | 환경 변수 option helper |
| `Client/Support/ScenarioAssert.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ClientScenario.java` | support | done | scenario assertion helper |
| `Client/Support/SpotLifecycleOrderContext.cs` | `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/ScenarioState.java` | support | done | evidence order 검증 입력 |
| `Server/Registry/*` | `Server/Registry/src/main/java/systems/zlink/e2e/spotservice/registry/Program.java`, `Server/Registry/build.gradle.kts` | server-role | done | embedded registry role |
| `Server/Play/*` | `Server/Play/src/main/java/systems/zlink/e2e/spotservice/play/Program.java`, `Shared/src/main/java/systems/zlink/e2e/spotservice/shared/*.java`, `Server/Play/build.gradle.kts` | server-role | done | play role 구현과 support/handler/spot 타입 |
| `Server/Gateway/*` | `feature-map.ko.md` | server-role | gap | 별도 gateway role 미구현, Java stream 일부는 play role에 포함 |
| `Server/MultiNode/*` | `feature-map.ko.md` | server-role | gap | multi-node 고급 actor/session scenario gap |
| `Server/Session/*` | `feature-map.ko.md` | server-role | gap | 별도 session role 미구현 |
| `Server/Publisher/*` | `Server/Publisher/src/main/java/systems/zlink/e2e/spotservice/publisher/Program.java`, `Server/Publisher/build.gradle.kts` | server-role | done | Java publisher role |

## 남은 gap

남은 gap은 `feature-map.ko.md`의 세 구역을 기준으로 관리한다.

- public contract parity 또는 spec 검토 대기: `SM-A5`, `SM-B5`, `SM-B6`, `SM-B8`, `SM-D3`~`SM-D14`.
- Java public contract 기반 E2E 미구현: `SM-B2`, `SM-B4`, `SM-D2`.
- E2E/harness 대기: `SM-F4` 일부, `SM-F5`, `SM-G1`~`SM-G4`.
