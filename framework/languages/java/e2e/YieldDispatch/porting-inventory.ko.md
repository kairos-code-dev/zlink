# Java YieldDispatch .NET 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`
- `framework/languages/dotnet/e2e/YieldDispatch/feature-map.ko.md`
- `framework/languages/dotnet/e2e/YieldDispatch/`

## 요약

Java에는 `YD-A1`, `YD-A2`, `YD-A4`를 검증하고 `YD-A3`의 request id, spot rid, correlation id
보존을 부분 검증하는 첫 번째 `YieldDispatch` E2E 구현이 있다. `YD-B1`은 actor A와 actor B를 같은
target spot에 join한 뒤, actor A가 yield로 기다리는 동안 actor B의 fast request가 먼저 완료되는지
검증한다. `YD-B2`는 같은 target actor의 fast request가 yield continuation 뒤에
처리되는지 검증한다. `YD-B3`는 Play role에서 만든 actor ref를 session에 bind하고, actor join call을
`yield`로 기다리는 동안 다른 actor의 entry actor request가 먼저 완료되는지 검증한다. `YD-C1`은 같은
target spot에서 yield 중인 timer가 기다리는 동안 빠른 timer tick이 먼저 완료되는지 검증한다.
`YD-C2`는 같은 timer의 다음 tick이 이전 tick의 yield continuation과 completion 뒤에 처리되는지
검증한다. `YD-C3`는 actor와 timer가 서로 다른 mailbox로 진행되는지 양방향으로 검증한다. `YD-D2`는
`play-a` owner spot과 `play-b` target spot 사이의 remote spot yield 재개 위치를 검증한다. `YD-D3`는
session gateway가 route mesh로 보낸 packet이 `play-b` target spot handler에서 yield하는 동안 probe가
먼저 처리되는지 검증한다. `YD-D4`는 stream session relay로 bound actor handler에 들어간 request가
yield 중일 때 bound session push를 원래 stream connector로 보내고, 다른 actor의 push wait는 진행되지
않는지 검증한다. 이 inventory는
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
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | registry, delay, play-a, play-b, session, client process를 띄우고 YD-A1/YD-A2/YD-A3/YD-A4, YD-B1, YD-B2, YD-B3, YD-C1, YD-C2, YD-C3, YD-D2, YD-D3, YD-D4를 검증한다. `logs/20260630-032840-2493437`에서 이 범위는 통과했다 |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 구현된 YD-A1/YD-A2와 남은 gap을 구분 |
| `Shared/YieldDispatch.Shared.csproj` | `Shared/build.gradle.kts` | build | done | shared project 구성 |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/yielddispatch/shared/Contracts.java` | shared | partial | YD-A1/YD-A2/YD-A3/YD-A4, YD-B1, YD-B2, YD-B3, YD-C1, YD-C2, YD-C3, YD-D2, YD-D3, YD-D4 scenario packet/evidence 타입 구현 |
| `Client/YieldDispatch.Client.csproj` | `Client/build.gradle.kts` | build | done | client project 구성 |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | client | partial | stream connector consumer와 A1/A2/A3/A4, B1, B2, B3, C1, C2, C3, D2, D3, D4 marker 검증을 구현했다 |
| `Client/GlobalUsings.cs` | `not-needed` | client | not-needed | Java에는 전역 using이 없다 |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/support/ClientOptions.java` | support | gap | client option parser 미구현 |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/support/ScenarioAssert.java` | support | gap | assertion helper 미구현 |
| `Client/Scenarios/YieldActorScenarioContext.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/support/YieldActorScenarioContext.java` | support | gap | actor scenario context 미구현 |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 A1 marker 순서를 검증 |
| `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 A2 marker 순서를 검증 |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | partial | Java는 현재 Program 안에서 request id, spot rid, correlation id 보존을 검증한다. cancellation token 상태 검증은 공개 spot request handler 계약에서 접근할 방법이 없어 gap으로 남김 |
| `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 A4 worker-yield marker 순서를 검증 |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 Program 안에서 actor A와 actor B를 같은 target spot에 join한 뒤 actor A target-spot yield 중 actor B fast request가 먼저 reply되는 범위를 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-B1 passed`까지 통과했다 |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 target spot에 join한 actor A의 yield continuation 뒤에 같은 actor A fast request가 처리되는 marker 순서를 검증 |
| `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 actor join yield flow를 실행하고, actor binding은 Play role actor ref를 session에 bind하도록 맞췄다. `logs/20260630-032840-2493437`에서 `scenario YD-B3 passed`까지 통과했다 |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 yield 중인 timer와 빠른 timer의 marker 순서를 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-C1 passed`까지 통과했다 |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 같은 timer의 다음 tick이 이전 tick continuation 뒤에 처리되는 marker 순서를 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-C2 passed`까지 통과했다 |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 actor yield 중 timer 진행, timer yield 중 다른 actor 진행을 모두 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-C3 passed`까지 통과했다 |
| `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 play-a owner spot과 play-b target spot을 만들고, owner continuation이 play-a로 돌아오는 marker를 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-D2 passed`까지 통과했다 |
| `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 session gateway가 route mesh로 보낸 YieldCommand와 ProbeCommand를 play-b target spot에서 처리하고, target spot handler yield 중 probe가 먼저 완료되는 marker 순서를 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-D3 passed`까지 통과했다 |
| `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/Program.java` | scenario | done | Java는 현재 Program 안에서 stream session relay actor yield와 bound session push reply 흐름을 검증한다. `logs/20260630-032840-2493437`에서 `scenario YD-D4 passed`까지 통과했다 |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/scenarios/YdE1TimeoutScenario.java` | scenario | gap | 미구현 |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/scenarios/YdE2CancellationScenario.java` | scenario | gap | 미구현 |
| `Client/Scenarios/ShutdownYieldScenario.cs` | `Client/src/main/java/systems/zlink/e2e/yielddispatch/client/scenarios/ShutdownYieldScenario.java` | scenario | gap | YD-E3 미구현 |
| `Server/Registry/*` | `Server/Registry/src/main/java/systems/zlink/e2e/yielddispatch/registry/` | server-role | done | embedded registry role 구현 |
| `Server/Delay/*` | `Server/Delay/src/main/java/systems/zlink/e2e/yielddispatch/delay/` | server-role | done | delay service role 구현 |
| `Server/Play/*` | `Server/Play/src/main/java/systems/zlink/e2e/yielddispatch/play/` | server-role | partial | route mesh, spot mesh, YD-A1/A2/A3/A4 probe spot, B1/B2 target actor handler, YD-B3 handler bean, C1/C2/C3 timer start/stop/tick handler, D2 remote spot owner/target handler, D3 route bridge target spot handler, D4 actor push yield handler 구현 |
| `Server/Session/*` | `Server/Session/src/main/java/systems/zlink/e2e/yielddispatch/session/` | server-role | partial | stream session gateway, routed spot egress bridge, actor bind/relay entry path, timer command relay, D2 ensure-spot/remote spot request relay, D3 YieldCommand relay, D4 bound actor relay 구현 |

## 구현 전 확인한 public surface

- `ZLinkRequestCall.yield(Class<TReply>)`
- `ZLinkActorJoinSpotCall.yield(...)`
- `ZLinkActorJoinEntrySpotCall.yield(...)`
- `ZLinkWorkerCall.yield()`

위 표면 중 `ZLinkRequestCall.yield(Class<TReply>)`는 YD-A2, YD-A3, YD-B1, YD-B2, YD-C1, YD-C2에서 검증했고,
`ZLinkWorkerCall.yield()`는 YD-A4에서 검증했다. actor join yield는 YD-B3에서 검증했다. actor/timer
교차 격리는 YD-C3에서 검증했다. remote spot topology는 YD-D2에서 검증했다. route bridge target spot
yield는 YD-D3에서 검증했다. stream session relay actor yield는 YD-D4에서 검증했다. cleanup scenario와
YD-A3의 cancellation token 상태 검증은 남아 있으므로 Java Config 8 전체는 완료 상태가 아니다.

## 검증

- `../../gradlew --project-cache-dir "$HOME/.cache/zlink/java-e2e/YieldDispatch-gradle-cache" --no-daemon --no-parallel --max-workers=1 installDist`
- `../../gradlew --project-cache-dir "$HOME/.cache/zlink/java-e2e/YieldDispatch-gradle-cache" --no-daemon --no-parallel --max-workers=1 :zlink-framework-java-build:zlink-framework-core:test --tests systems.zlink.framework.execution.ZLinkAsyncSerialQueueTest`
- `./run_e2e.sh`

최근 통과 로그: `logs/20260630-032840-2493437`

actor B도 target spot에 join하도록 `YD-B1`을 확장한 뒤 `ZLinkAsyncSerialQueue`에 actor queue용
non-reentrant yield 모드를 추가했다. 이 모드는 actor A가 yield 중이어도 actor B dispatch는 진행시키되,
같은 actor A의 다음 dispatch는 continuation 뒤에 실행되도록 한다. `ZLinkAsyncSerialQueueTest`가 이
queue 동작을 검증하고, `logs/20260630-032840-2493437`는 `scenario YD-B1 passed`,
`scenario YD-B2 passed`, `scenario YD-B3 passed`, `scenario YD-C1 passed`,
`scenario YD-C2 passed`, `scenario YD-C3 passed`, `scenario YD-D2 passed`,
`scenario YD-D3 passed`, `scenario YD-D4 passed`,
`yield-dispatch e2e result=passed`까지 출력했다.
