# Java YieldDispatch .NET 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`
- `framework/languages/dotnet/e2e/YieldDispatch/feature-map.ko.md`
- `framework/languages/dotnet/e2e/YieldDispatch/`

## 요약

Java에는 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`를 검증하는 `YieldDispatch` E2E 구현이 있다.
`YD-B1`은 actor A와 actor B를 같은 target spot에 join한 뒤, actor A가 yield로 기다리는 동안 actor
B의 fast request가 먼저 완료되는지 검증한다. `YD-B2`는 같은 target actor의 fast request가 yield continuation 뒤에
처리되는지 검증한다. `YD-B3`는 Play role에서 만든 actor ref를 session에 bind하고, actor join call을
`yield`로 기다리는 동안 다른 actor의 entry actor request가 먼저 완료되는지 검증한다. `YD-C1`은 같은
target spot에서 yield 중인 timer가 기다리는 동안 빠른 timer tick이 먼저 완료되는지 검증한다.
`YD-C2`는 같은 timer의 다음 tick이 이전 tick의 yield continuation과 completion 뒤에 처리되는지
검증한다. `YD-C3`는 actor와 timer가 서로 다른 mailbox로 진행되는지 양방향으로 검증한다. `YD-D2`는
`play-a` owner spot과 `play-b` target spot 사이의 remote spot yield 재개 위치를 검증한다. `YD-D3`는
session gateway가 route mesh로 보낸 packet이 `play-b` target spot handler에서 yield하는 동안 probe가
먼저 처리되는지 검증한다. `YD-D4`는 stream session relay로 bound actor handler에 들어간 request가
yield 중일 때 bound session push를 원래 stream connector로 보내고, 다른 actor의 push wait는 진행되지
않는지 검증한다. `YD-E1`은 timeout 뒤 같은 Spot mailbox가 probe를 처리하는 cleanup을 검증한다.
현재 기본 runner proof log는 `logs/20260707-224104-3730324`다. `YD-E2`는 request, actor join,
worker yield public surface에 `CancellationToken` overload를 추가한 뒤 같은 runner에서 통과했다.
`YD-E3` diagnostic runner는
`logs/20260702-070504-3148`에서 pending yield 중 play-a SIGTERM shutdown, `.NET`과 같은 stream
error 또는 request timeout 종료 신호, 같은 endpoint 재시작 뒤 recovery request를 통과했다.
이 inventory는
`.NET` 기준 파일과 Java 대응 위치를 고정하고, 남은 scenario를
internal helper나 raw-frame 우회로 완료 처리하지 않기 위해 유지한다.

공통 문서의 핵심 제약은 다음과 같다.

- scenario 시작은 실제 stream connector가 session gateway에 보내는 request packet이어야 한다.
- HTTP endpoint는 readiness와 evidence 조회에만 사용한다.
- route mesh request 자체에 yield terminator를 붙이지 않는다.
- public framework API가 없는 부분은 내부 helper나 raw-frame 우회로 완료 처리하지 않는다.

## Inventory

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 목표 build/log 산출물 제외 |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | 기본 실행은 별도 endpoint가 없으면 실행별 Redis container를 준비하고, delay, play-a, play-b, session, client process를 띄워 YD-A1/YD-A2/YD-A3/YD-A4, YD-B1, YD-B2, YD-B3, YD-C1, YD-C2, YD-C3, YD-D2, YD-D3, YD-D4, YD-E1, YD-E2 marker, E4 정적 검증, E5 marker report 검증을 수행한다. cleanup은 runner가 기록한 process와 실행별 Redis container만 정리한다. `logs/20260707-224104-3730324`에서 통과했다. `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1` diagnostic gate는 `logs/20260702-070504-3148`에서 YD-E3 shutdown/restart recovery를 통과했다 |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 구현된 YD-A1/YD-A2와 남은 gap을 구분 |
| `Shared/YieldDispatch.Shared.csproj` | `Shared/build.gradle.kts` | build | done | shared project 구성 |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/Contracts.java` | shared | done | YD-A1/YD-A2/YD-A3/YD-A4, YD-B1, YD-B2, YD-B3, YD-C1, YD-C2, YD-D2, YD-D3, YD-D4, YD-E1, YD-E2 scenario packet/evidence 타입과 YD-E3 diagnostic packet을 구현했다 |
| `Client/YieldDispatch.Client.csproj` | `Client/build.gradle.kts` | build | done | client project 구성 |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | client | done | stream connector를 만들고 `.NET` scenario file 단위 wrapper를 순서대로 실행한다. `--readiness`, `--shutdown-wait`, `--shutdown-recovery` diagnostic mode만 dispatch한다. |
| `Client/GlobalUsings.cs` | `not-needed` | client | not-needed | Java에는 전역 using이 없다 |
| `Client/Support/ClientOptions.cs` | `not-needed` | support | not-needed | Java runner는 환경 변수를 직접 읽는 단일 Program 구조라 별도 option parser 파일이 필요 없다 |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | support | done | evidence 조회, marker order assertion, stream connector helper를 공유한다. YD-D3는 실제 yield 검증 전에 같은 public stream connector, session gateway, route mesh, target spot send 경로로 readiness probe를 보내 route bridge 경로 준비를 확인한다. |
| `Client/Scenarios/YieldActorScenarioContext.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | support | done | Java는 actor id와 binding metadata를 support method 안에서 구성한다. 별도 context DTO는 필요 없다. |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdA1BasicTerminatorScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 A1 marker 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdA2YieldTerminatorScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 A2 marker 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdA3ContinuationContextScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 request id, spot rid, correlation id 보존 검증을 support method로 실행한다. |
| `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdA4WorkerYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 A4 worker-yield marker 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdB1OtherActorProgressScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 actor A yield 중 actor B fast request 진행 검증을 support method로 실행한다. |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdB2SameActorReentryScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 같은 actor의 fast request가 yield continuation 뒤 처리되는 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdB3ActorJoinYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 actor join yield flow와 Play role actor ref binding 검증을 support method로 실행한다. |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdC1TimerIsolationScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 yield 중인 timer와 빠른 timer의 marker 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdC2TimerReentryScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 같은 timer의 다음 tick reentry 순서 검증을 support method로 실행한다. |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdC3ActorTimerIsolationScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 actor/timer mailbox 교차 격리 검증을 support method로 실행한다. |
| `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdD2RemoteSpotYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 remote spot yield owner/target marker 검증을 support method로 실행한다. |
| `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdD3RouteBridgeYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 route bridge target spot yield 중 probe 진행 검증을 support method로 실행한다. `logs/20260707-224021-3727350`에서 focused YD-D3, `logs/20260707-224104-3730324`에서 full runner를 통과했다. |
| `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdD4SessionRelayActorYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 stream session relay actor yield와 bound session push 검증을 support method로 실행한다. |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdE1TimeoutScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 timeout cleanup과 post-timeout probe 검증을 support method로 실행한다. |
| `Client/Scenarios/YdE5MarkerReportScenario.cs` | `run_e2e.sh` | runner | done | Java runner가 `yield-dispatch-marker-report.json`을 생성하고 scenario id별 공통 marker 이름을 검증한다. `logs/20260704-001935-37995/yield-dispatch-marker-report.json`에서 확인했다 |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/YdE2CancellationScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | Java scenario wrapper가 cancellation-aware yield cleanup과 post-cancel probe 검증을 support method로 실행한다. |
| `Client/Scenarios/ShutdownYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Scenarios/ShutdownYieldScenario.java`, `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Support/YieldDispatchScenarioSupport.java` | scenario | done | YD-E3 diagnostic client mode가 pending yield 중 shutdown wait와 restart recovery를 검증한다. |
| `Server/Registry/*` | `not-used` | server-role | removed | location store 전환으로 embedded registry role을 삭제했다. Delay/Play/Session role이 `zlink-framework-locations-redis`를 등록하고 같은 Redis endpoint와 실행별 key prefix를 공유한다. |
| `Server/Delay/*` | `Server/Delay/src/main/java/systems/zlink/e2e/yielddispatch/delay/` | server-role | done | delay service role 구현 |
| `Server/Play/*` | `Server/Play/src/main/java/systems/zlink/e2e/yielddispatch/play/` | server-role | done | route mesh, spot mesh, YD-A1/A2/A3/A4 probe spot, B1/B2 target actor handler, YD-B3 handler bean, C1/C2/C3 timer start/stop/tick handler, D2 remote spot owner/target handler, D3 route bridge target spot handler, D4 actor push yield handler, E1 timeout handler bean, E2 cancellation handler bean, E3 shutdown target role을 구현했다 |
| `Server/Session/*` | `Server/Session/src/main/java/systems/zlink/e2e/yielddispatch/session/` | server-role | done | stream session gateway, routed spot egress bridge, actor bind/relay entry path, timer command relay, D2 ensure-spot/remote spot request relay, D3 YieldMsg relay, D4 bound actor relay, E1 YieldTimeoutMsg relay, E2 YieldCancelMsg relay, E3 diagnostic relay를 구현했다 |

## 구현 전 확인한 public surface

- `ZLinkRequestCall.yield(Class<TReply>)`
- `ZLinkActorJoinSpotCall.yield(...)`
- `ZLinkActorJoinEntrySpotCall.yield(...)`
- `ZLinkWorkerCall.yield()`
- request, actor join, worker yield의 `CancellationToken` overload

위 표면 중 `ZLinkRequestCall.yield(Class<TReply>)`는 YD-A2, YD-A3, YD-B1, YD-B2, YD-C1, YD-C2에서 검증했고,
`ZLinkWorkerCall.yield()`는 YD-A4에서 검증했다. actor join yield는 YD-B3에서 검증했다. actor/timer
교차 격리는 YD-C3에서 검증했다. remote spot topology는 YD-D2에서 검증했다. route bridge target spot
yield는 YD-D3에서 검증했다. stream session relay actor yield는 YD-D4에서 검증했다. timeout cleanup은
YD-E1에서 검증했다. cancellation cleanup은 YD-E2에서 검증했다. marker report는 YD-E5에서 생성한다.
Java Config 8 안에서 남은 scenario 누락은 없다.

## 검증

- `../../gradlew --project-cache-dir "$HOME/.cache/zlink/java-e2e/YieldDispatch-gradle-cache" --no-daemon --no-parallel --max-workers=1 installDist`
- `../../gradlew --project-cache-dir "$HOME/.cache/zlink/java-e2e/YieldDispatch-gradle-cache" --no-daemon --no-parallel --max-workers=1 :zlink-framework-java-build:zlink-framework-core:test --tests systems.zlink.framework.execution.ZLinkAsyncSerialQueueTest`
- `./run_e2e.sh`

최근 재실행 로그: `logs/20260707-224104-3730324`

현재 runner는 YD-A1/A2/A3/A4, YD-B1/B2/B3, YD-C1/C2/C3, YD-D2/D3/D4, YD-E1/E2와 E4 정적 검증을
통과했고 `yield-dispatch-marker-report.json`을 생성한다. `ZLINK_JAVA_E2E_RUN_E3_SHUTDOWN=1 ./run_e2e.sh`는
`logs/20260702-070504-3148`에서 YD-E3 shutdown/restart recovery를 통과했다.
