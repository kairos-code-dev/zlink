# Kotlin YieldDispatch .NET 포팅 inventory

기준 구현은 `framework/languages/dotnet/e2e/YieldDispatch`이고, 공통 기준 문서는
`framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`이다.
현재 Kotlin 구현은 `Shared`, `Client`, `Server/Delay`, `Server/Play`,
`Server/Session` Gradle module과 module별 binary로 delay/play/session/client를 실행한다.
registry role은 Redis location store 전환 뒤 제거했다.
stream endpoint와 session auth/relay runtime은 `Server/Session`으로 분리했다.

현재 Kotlin runner는 `logs/20260704-041428-16476`에서 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`,
`YD-B2`, `YD-B3`, `YD-C1`, `YD-C2`, `YD-E1`, `YD-E2`, `YD-D1`, `YD-D2`, `YD-D3` marker와
module별 binary 실행을 확인한다. D2 전용 mode에서는 `play-b`도 추가 실행한다. `.NET` feature-map이
구현으로 표시한 `YD-C3`, `YD-D4`, `YD-E3`는 아직 Kotlin에서 같은 수준으로 검증하지 않는다. `YD-E4`는 runner 정적
검사 일부만 추가했다. `.NET`도
`YD-A3` metadata 보존과 `YD-E5` cross-language report aggregation은 부분 구현으로 남겼으므로,
Kotlin inventory에서도 완료로 과장하지 않는다.

## Scenario 상태

| scenario | .NET 기준 파일 | Kotlin 현재 대응 | 상태 | 비고 |
|----------|----------------|------------------|------|------|
| `YD-A1` | `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/src/main/java/.../scenarios/YdA1BasicTerminatorScenario.java`, `Server/Session/src/main/java/.../HoldMsgRouteHandler.java`, `Server/Play/src/main/java/.../HoldMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java`, `Server/Delay/src/main/java/.../DelayHandler.java` | done | stream connector가 session gateway를 통해 같은 Spot에 `HoldMsg`와 `ProbeMsg`를 보내고, `HoldMsgHandler`가 public `requestToChannel(...).await(...)`로 기다릴 때 `hold-started` → `hold-resumed` → `hold-completed` → `probe-started` 순서를 확인한다. `logs/20260704-041428-16476` full runner에서 marker를 확인했다. |
| `YD-A2` | `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/src/main/java/.../scenarios/YdA2YieldTerminatorScenario.java`, `Server/Session/src/main/java/.../YieldMsgRouteHandler.java`, `Server/Session/src/main/java/.../ProbeMsgRouteHandler.java`, `Server/Play/src/main/java/.../YieldMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java`, `Server/Delay/src/main/java/.../DelayHandler.java` | done | stream connector가 session gateway를 통해 같은 Spot에 `YieldMsg`와 `ProbeMsg`를 보내고, `YieldMsgHandler`가 public `requestToChannel(...).yield(...)`로 turn을 반납한 뒤 `yield-started` → `yield-released` → `probe-started` → `probe-completed` → `yield-resumed` → `yield-completed` 순서를 확인한다. `logs/20260704-041428-16476` full runner에서 marker를 확인했다. |
| `YD-A3` | `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/src/main/java/.../scenarios/YdA3ContinuationContextScenario.java` | partial | client scenario file은 분리했다. Kotlin은 cross-spot progress를 확인하며, `.NET` feature-map의 request id/spot rid 보존 및 metadata gap과는 의미가 다르므로 완료가 아니다. |
| `YD-A4` | `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdA4WorkerYieldScenario.java`, `Server/Session/src/main/java/.../WorkerYieldMsgRouteHandler.java`, `Server/Session/src/main/java/.../ProbeMsgRouteHandler.java`, `Server/Play/src/main/java/.../WorkerYieldMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java` | done | stream connector가 session gateway를 통해 같은 Spot에 `WorkerYieldMsg`와 `ProbeMsg`를 보내고, `WorkerYieldMsgHandler`가 public `context.runWorker(...).yield()`로 turn을 반납한 뒤 `worker-yield-started` → `worker-yield-released` → `probe-started` → `probe-completed` → `worker-yield-resumed` → `worker-yield-completed` 순서를 확인한다. `logs/20260704-041428-16476` full runner에서 marker를 확인했다. |
| `YD-B1` | `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/src/main/java/.../scenarios/YdB1OtherActorProgressScenario.java`, `Server/Session/src/main/java/.../ProbeSession.java`, `Server/Session/src/main/java/.../ActorAuthHandler.java`, `Server/Play/src/main/java/.../EntryActorYieldHandler.java`, `Server/Play/src/main/java/.../EntryActorFastHandler.java`, `Server/Play/src/main/java/.../ProbeActorYieldHandler.java`, `Server/Play/src/main/java/.../ProbeActorFastHandler.java` | done | stream connector가 session gateway actor relay로 들어간다. actor A가 public `requestToChannel(...).yield(...)`로 delay service를 기다리는 동안 actor B의 fast request가 actor A continuation보다 먼저 완료되는 evidence 순서를 확인한다. `logs/20260704-041428-16476` full runner에서 marker를 확인했다. |
| `YD-B2` | `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/src/main/java/.../scenarios/YdB2SameActorReentryScenario.java` | partial | 같은 actor의 slow/fast request 순서와 대기 시간을 stream/actor 경로로 확인한다. `logs/20260704-041428-16476` full runner에서 `scenario YD-B2 passed`를 확인했다. 다만 `.NET` evidence marker와 `ActorYieldReq`/`ActorFastReq` 흐름을 그대로 맞춘 것은 아니므로 parity 상태는 partial로 둔다. |
| `YD-B3` | `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdB3ActorJoinYieldScenario.java`, `Server/Play/src/main/java/.../ProbeActorJoinHandler.java` | partial | actor handler의 public `joinSpot(...).yield(...)` 대기 중 다른 actor의 fast request가 진행되는지 확인한다. `logs/20260704-041428-16476` full runner에서 `scenario YD-B3 passed`를 확인했다. 다만 `.NET` evidence marker와 `ActorJoinYieldReq`/`ActorFastReq` contract를 그대로 맞춘 것은 아니므로 parity 상태는 partial로 둔다. |
| `YD-C1` | `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/src/main/java/.../scenarios/YdC1TimerIsolationScenario.java`, `Server/Play/src/main/java/.../TimerTickHandler.java`, `Server/Play/src/main/java/.../PlayEvidenceStore.java`, `Server/Session/src/main/java/.../TimerStartRouteHandler.java` | partial | timer yield 중 다른 timer fast tick이 진행되는 순서를 node-level evidence store로 확인한다. 다만 `.NET` evidence HTTP surface와 `TimerStartMsg` 전체 mode set은 아직 그대로 맞추지 않았다. |
| `YD-C2` | `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/src/main/java/.../scenarios/YdC2TimerReentryScenario.java`, `Server/Play/src/main/java/.../TimerTickHandler.java`, `Server/Play/src/main/java/.../PlayEvidenceStore.java` | partial | 같은 timer의 다음 tick이 첫 yield 완료 뒤 처리되는 순서를 node-level evidence store로 확인한다. 다만 `.NET` evidence HTTP surface와 timer command 전체 mode set은 아직 그대로 맞추지 않았다. |
| `YD-C3` | `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | 없음 | gap | actor yield와 timer fast tick, timer yield와 actor fast request 교차 검증이 없다. 현재 public stream actor request interleaving 시도는 `logs/20260630-071558-3077374`, `logs/20260630-071810-3084675`에서 timeout으로 실패했다. |
| `YD-D1` | feature-map 기준 local topology 검증 | `Client/src/main/java/.../scenarios/YdD1LocalTopologyScenario.java` | partial | 현재 single Play/Delay topology에서 A/B/C/E1 시나리오가 통과한 뒤 aggregate marker를 남긴다. 다만 `.NET`처럼 local topology mode에서 evidence marker 전체를 다시 수집해 검증하지는 않는다. |
| `YD-D2` | `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdD2RemoteSpotYieldScenario.java`, `Server/Play/src/main/java/.../RemoteSpotYieldReqHandler.java`, `Server/Play/src/main/java/.../YieldReqHandler.java`, `Server/Play/src/main/java/.../EnsureSpotRouteRequestHandler.java`, `Server/Session/src/main/java/.../RemoteSpotYieldRouteHandler.java`, `Server/Session/src/main/java/.../EnsureSpotHandler.java` | partial | D2 전용 mode에서 `play-b`를 추가 실행하고 `play-a` owner Spot의 public `requestToSpot(...).yield(...)`와 `play-b` target evidence를 확인한다. 다만 `.NET`처럼 full scenario sweep 처음부터 play-b/session-b topology를 함께 띄우는 구조는 아니다. |
| `YD-D3` | `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdD3RouteBridgeYieldScenario.java`, `Server/Play/src/main/java/.../YieldMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java`, `Server/Session/src/main/java/.../YieldMsgRouteHandler.java`, `Server/Session/src/main/java/.../ProbeMsgRouteHandler.java` | partial | D2 전용 topology에서 Session route mesh를 통한 `play-b` target Spot yield와 probe interleaving marker를 확인한다. 다만 `.NET`처럼 full scenario sweep 처음부터 play-b/session-b topology를 함께 띄우는 구조는 아니다. |
| `YD-D4` | `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | 없음 | gap | public `boundSession().send(...)`와 stream connector `waitFor(...)` 기반 구현을 시도했지만 full runner에 넣으면 기존 actor request sweep 또는 D4 actor join이 timeout되었다(`logs/20260630-074648-3163279`, `logs/20260630-074614-3161019`, `logs/20260630-074527-3158860`). D4 전용 client marker만 통과한 로그는 full-sweep proof로 쓰지 않는다. |
| `YD-E1` | `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/src/main/java/.../scenarios/YdE1TimeoutScenario.java`, `Server/Play/src/main/java/.../YieldTimeoutMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java`, `Server/Session/src/main/java/.../YieldTimeoutRouteHandler.java`, `Server/Session/src/main/java/.../ProbeMsgRouteHandler.java` | partial | `yield(...)` timeout 뒤 같은 Spot이 post-timeout probe packet을 처리하는지 evidence store로 확인한다. `logs/20260704-041428-16476` full runner에서 `scenario YD-E1 passed`를 확인했다. 다만 `.NET`의 reply contract와 HTTP evidence surface를 그대로 맞춘 것은 아니다. |
| `YD-E2` | `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/src/main/java/.../scenarios/YdE2CancellationScenario.java`, `Server/Play/src/main/java/.../YieldCancelMsgHandler.java`, `Server/Session/src/main/java/.../YieldCancelMsgRouteHandler.java` | done | Java/Kotlin public `ZLinkRequestCall.yield(..., CancellationToken)`으로 yield 대기를 cooperative cancellation token으로 끊고, 같은 Spot이 post-cancel probe packet을 처리하는지 확인한다. `logs/20260704-041428-16476` full runner에서 marker를 확인했다. |
| `YD-E3` | `Client/Scenarios/ShutdownYieldScenario.cs` | 없음 | harness gap | `.NET`은 pending yield marker를 확인한 뒤 `play-a`를 SIGTERM으로 종료하고, client가 public closed/cancelled 계열 오류를 본 뒤 같은 `play-a` rid를 재시작해 recovery probe를 확인한다. Kotlin runner에는 shutdown-wait/recovery client mode, shell-waitable Play evidence log, same-rid Play restart supervision이 없어 아직 검증하지 않는다. |
| `YD-E4` | `run_e2e.sh` static checks | `run_e2e.sh` | partial | HTTP trigger/client 금지, Play handler와 Session Entry Spot join 예외 밖 `yield(...)` 금지, `Yd*.java` scenario connector 인자 검사, scenario file의 connector 생성/lifecycle 소유 금지를 추가했다. 다만 `.NET`처럼 모든 client scenario가 thin helper 없이 connector를 직접 쓰는 구조까지는 아직 맞추지 않았다. |
| `YD-E5` | `feature-map.ko.md` | `feature-map.ko.md` | partial | `.NET`도 cross-language report aggregation은 부분 구현으로 남긴다. Kotlin report는 현재 공통 marker coverage가 부족하다. |

## .NET 파일 대응

| .NET 기준 파일 | Kotlin 대응 파일 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | Kotlin config에 존재한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | partial | 현재 Kotlin 구현/gap을 기록하지만 `.NET` feature-map과 scenario 의미가 일부 맞지 않는다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | `Client`, `Server/Delay`, `Server/Play`, `Server/Session` binary를 빌드/실행한다. Redis location store endpoint/key prefix를 각 server role에 전달한다. HTTP trigger/client, `yield(...)` 위치, scenario connector 인자 정적 검사를 일부 수행한다. `.NET`의 shutdown/restart와 helper-free connector usage 검사는 아직 없다. |
| `Shared/YieldDispatch.Shared.csproj` | `Shared/build.gradle.kts` | shared-project | done | Shared Gradle module을 추가했다. |
| `Shared/Messages.cs` | `Shared/src/main/java/.../Contracts.java` | shared | partial | YD-B1용 actor yield/fast message를 추가했다. 아직 `.NET`의 전체 YieldDispatch message contract와 맞지 않는다. |
| `Client/YieldDispatch.Client.csproj` | `Client/build.gradle.kts` | client-project | done | Client 전용 Gradle module과 binary를 추가했다. |
| `Client/GlobalUsings.cs` | 없음 | client-support | not-needed | Kotlin/Java에는 직접 대응이 필요 없다. |
| `Client/Program.cs` | `Client/src/main/java/.../ClientApplication.java`, `Client/src/main/java/.../ClientScenario.java`, `Client/src/main/kotlin/.../Program.kt` | client-role | partial | Client binary는 분리했고 `ClientScenario`가 현재 구현된 scenario file들을 조립한다. `.NET`의 scenario option/shutdown modes는 아직 없다. |
| `Client/Support/ClientOptions.cs` | `Shared/src/main/java/.../Env.java` | support | partial | option class가 없고 env helper에서 직접 읽는다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/.../support/ScenarioAssert.java` | support | partial | assertion helper를 support file로 분리했다. `.NET`의 evidence ordering helpers는 아직 없다. |
| `Client/Scenarios/YieldActorScenarioContext.cs` | 없음 | scenario-support | gap | actor scenario 공통 context가 없다. |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/src/main/java/.../scenarios/YdA1BasicTerminatorScenario.java` | scenario | done | `.NET`처럼 stream connector command와 evidence marker 순서로 기본 terminator를 검증한다. |
| `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/src/main/java/.../scenarios/YdA2YieldTerminatorScenario.java` | scenario | done | `.NET`처럼 `yield-released`를 확인한 뒤 같은 Spot에 probe command를 보내 yield terminator 순서를 검증한다. |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/src/main/java/.../scenarios/YdA3ContinuationContextScenario.java` | scenario | partial | client scenario file은 분리했다. Kotlin marker 의미가 `.NET` feature-map의 A3와 다르다. |
| `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdA4WorkerYieldScenario.java` | scenario | done | `.NET`처럼 `worker-yield-released`를 확인한 뒤 같은 Spot에 probe command를 보내 worker yield 순서를 검증한다. |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/src/main/java/.../scenarios/YdB1OtherActorProgressScenario.java` | scenario | done | `.NET`처럼 actor A yield 중 actor B fast request가 actor A continuation보다 먼저 완료되는 evidence 순서를 검증한다. |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/src/main/java/.../scenarios/YdB2SameActorReentryScenario.java` | scenario | partial | 같은 actor의 slow/fast request 순서와 대기 시간을 확인하려는 scenario file은 남아 있지만 현재 full runner 완료 marker에서는 제외한다. `.NET` evidence marker와 actor message contract 정렬은 남아 있다. |
| `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdB3ActorJoinYieldScenario.java` | scenario | partial | actor join yield 중 다른 actor progress를 확인하려는 scenario file은 남아 있지만 현재 full runner 완료 marker에서는 제외한다. `.NET` evidence marker와 actor message contract 정렬은 남아 있다. |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/src/main/java/.../scenarios/YdC1TimerIsolationScenario.java` | scenario | partial | timer isolation 순서를 확인한다. `.NET` evidence HTTP surface와 timer command 전체 정렬은 남아 있다. |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/src/main/java/.../scenarios/YdC2TimerReentryScenario.java` | scenario | partial | 같은 timer reentry 순서를 확인한다. `.NET` evidence HTTP surface와 timer command 전체 정렬은 남아 있다. |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | 없음 | scenario | gap | actor/timer isolation scenario가 없다. 현재 Kotlin public stream actor request 경로로는 C3 interleaving proof가 timeout으로 실패한다. |
| `feature-map 기준 YD-D1 local topology` | `Client/src/main/java/.../scenarios/YdD1LocalTopologyScenario.java` | scenario | partial | 현재 runner의 local topology A/B/C/E1 통과를 aggregate marker로 묶는다. `.NET` 수준의 topology-specific evidence replay는 남아 있다. |
| `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdD2RemoteSpotYieldScenario.java` | scenario | partial | D2 전용 client mode에서 remote Spot yield를 확인한다. |
| `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/src/main/java/.../scenarios/YdD3RouteBridgeYieldScenario.java` | scenario | partial | D2 전용 client mode에서 route bridge target Spot yield를 확인한다. |
| `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | 없음 | scenario | gap | session relay actor yield scenario는 시도했지만 full runner 안정성을 깨서 반영하지 않았다. 실패/부분 증거는 scenario 상태 표의 `YD-D4` 행에 남긴다. |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/src/main/java/.../scenarios/YdE1TimeoutScenario.java` | scenario | partial | timeout 뒤 post-timeout probe evidence를 확인한다. `.NET` reply contract와 evidence HTTP surface 정렬은 남아 있다. |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/src/main/java/.../scenarios/YdE2CancellationScenario.java` | scenario | done | public `YieldCancelMsg` command와 Play evidence marker로 cancellation 이후 post-cancel probe를 확인한다. |
| `Client/Scenarios/ShutdownYieldScenario.cs` | 없음 | scenario | harness gap | shutdown-wait와 shutdown-recovery client mode가 없다. Kotlin runner도 pending yield 중 `play-a` 종료와 same-rid 재시작을 아직 수행하지 않는다. |
| `Server/Registry/YieldDispatch.Registry.csproj` | 없음 | server-project | not-needed | Redis location store 전환 뒤 registry role은 제거했다. |
| `Server/Registry/Program.cs` | 없음 | server-role | not-needed | embedded registry process를 실행하지 않는다. |
| `Server/Registry/RegistryHostFactory.cs` | 없음 | server-role | not-needed | registry option 설정은 Redis location store registration으로 대체했다. |
| `Server/Delay/YieldDispatch.Delay.csproj` | `Server/Delay/build.gradle.kts` | server-project | done | Delay 전용 Gradle module과 binary를 추가했다. |
| `Server/Delay/Program.cs` | `Server/Delay/src/main/kotlin/.../Program.kt`, `Server/Delay/src/main/java/.../DelayApplication.java` | server-role | done | Delay server를 PlayApplication에서 분리했다. |
| `Server/Delay/DelayHostFactory.cs` | `Server/Delay/src/main/java/.../DelayApplication.java` | server-role | partial | delay channel server 설정은 Delay role에 있다. 별도 support/evidence는 없다. |
| `Server/Delay/DelayHandler.cs` | `Server/Delay/src/main/java/.../DelayHandler.java` | handler | done | Delay role handler로 분리했다. |
| `Server/Delay/DelaySupport.cs` | 없음 | support | gap | delay support/HTTP evidence가 없다. |
| `Server/Play/YieldDispatch.Play.csproj` | `Server/Play/build.gradle.kts` | server-project | done | Play 전용 Gradle module과 binary를 추가했다. |
| `Server/Play/Program.cs` | `Server/Play/src/main/java/.../PlayApplication.java`, `Server/Play/src/main/kotlin/.../Program.kt` | server-role | partial | Play binary를 분리했고 target spot runtime만 실행한다. `.NET` Play support/evidence는 아직 없다. |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/src/main/java/.../PlayApplication.java` | server-role | partial | Play spot mesh, route mesh, delay client 설정이 있다. `.NET`의 support/evidence setup은 아직 없다. |
| `Server/Play/PlaySupport.cs` | 없음 | support | gap | evidence, marker, node option support가 없다. |
| `Server/Play/Spots/PlaySpotRuntime.cs` | `Server/Play/src/main/java/.../ProbeSpot.java`, `ProbeEntrySpot.java`, `ProbeActor.java` | spot | partial | 일부 spot/actor runtime만 있다. `.NET` marker/evidence runtime과 맞지 않는다. |
| `Server/Play/Spots/PlaySpotTypes.cs` | `Server/Play/src/main/java/.../ProbeSpot.java`, `ProbeEntrySpot.java`, `ProbeActor.java`, `ProbeActorFactory.java` | spot | partial | 일부 spot/actor type만 있다. timer/remote/failure spot type이 없다. |
| `Server/Play/Handlers/PlayBasicSpotHandlers.cs` | `Server/Play/src/main/java/.../ProbeReqHandler.java` | handler | partial | basic probe/yield 일부만 있다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/src/main/java/.../ProbeActorRequestHandler.java`, `Server/Play/src/main/java/.../EntryActorProbeHandler.java`, `Server/Play/src/main/java/.../ProbeActorJoinHandler.java`, `Server/Play/src/main/java/.../EntryActorJoinHandler.java`, `Server/Play/src/main/java/.../EntryActorYieldHandler.java`, `Server/Play/src/main/java/.../EntryActorFastHandler.java`, `Server/Play/src/main/java/.../ProbeActorYieldHandler.java`, `Server/Play/src/main/java/.../ProbeActorFastHandler.java` | handler | partial | YD-B1 actor yield/fast handler는 entry spot과 user spot 양쪽에 추가했다. Actor relay가 entry spot으로 남는 경우에도 `ProbeReq`를 처리하도록 entry spot actor probe handler를 추가했다. B2/B3는 현재 ProbeReq/ActorJoinReq 기반 부분 검증이며, D4 대응 handler는 없다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | 없음 | handler | gap | control channel handler가 없다. |
| `Server/Play/Handlers/PlayFailureSpotHandlers.cs` | `Server/Play/src/main/java/.../YieldTimeoutMsgHandler.java`, `YieldCancelMsgHandler.java`, `ProbeMsgHandler.java` | handler | partial | `YD-E1` timeout 뒤 기존 probe handler 경로와 `YD-E2` cancellation/probe handler가 있다. `YD-E3`는 runner가 Play restart와 recovery probe를 검증할 shutdown harness를 아직 갖추지 않았다. |
| `Server/Play/Handlers/PlayRemoteSpotHandlers.cs` | `Server/Play/src/main/java/.../RemoteSpotYieldReqHandler.java`, `Server/Play/src/main/java/.../YieldMsgHandler.java`, `Server/Play/src/main/java/.../ProbeMsgHandler.java` | handler | partial | `YD-D2` remote Spot yield와 `YD-D3` route bridge yield/probe handler가 있다. |
| `Server/Play/Handlers/PlayTimerSpotHandlers.cs` | `Server/Play/src/main/java/.../TimerStartMsgHandler.java`, `TimerStopMsgHandler.java`, `TimerTickHandler.java`, `EvidenceRouteRequestHandler.java`, `PlayEvidenceStore.java` | handler | partial | `YD-C1`/`YD-C2`용 timer start/stop/tick/evidence path가 있다. `YD-C3`와 `.NET` 전체 timer mode는 아직 없다. |
| `Server/Session/YieldDispatch.Session.csproj` | `Server/Session/build.gradle.kts` | server-project | done | Session 전용 Gradle module과 binary를 추가했다. |
| `Server/Session/Program.cs` | `Server/Session/src/main/kotlin/.../Program.kt`, `Server/Session/src/main/java/.../SessionApplication.java` | server-role | partial | stream endpoint와 actor auth/relay runtime을 Session role로 분리했다. `.NET`의 HTTP/evidence support는 아직 없다. |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/src/main/java/.../SessionApplication.java` | server-role | partial | session host setup과 Redis location store 기반 spot route mesh를 Session role에 둔다. 별도 host factory/support class는 없다. |
| `Server/Session/Support/SessionSpotTypes.cs` | 없음 | support | gap | session relay용 spot type이 없다. |
| `Server/Session/Support/SessionSupport.cs` | `Server/Session/src/main/java/.../ProbeSession.java`, `Server/Session/src/main/java/.../ActorAuthHandler.java` | support | partial | auth/session 일부만 있다. relay/push/evidence support는 없다. |
| `Server/Session/Support/YieldSession.cs` | `Server/Session/src/main/java/.../ProbeSession.java` | support | partial | minimal stream session과 bound actor relay만 있다. |
| `Server/Session/Support/YieldSessionRelay.cs` | 없음 | support | gap | session relay support가 없다. |
| `Server/Session/Support/YieldShutdownRelay.cs` | 없음 | support | harness gap | pending yield 중 peer shutdown을 유도하고 same-rid recovery request를 보내는 전용 relay support가 없다. 현재 Session role은 일반 stream packet relay와 D2/D3 route handlers만 제공한다. |

## 기존 Kotlin 파일 처리

| Kotlin 파일 | 판단 | 다음 작업 |
|-------------|------|-----------|
| `src/main/kotlin/.../Program.kt` | 분리 완료 | `Client`, `Server/Delay`, `Server/Play`, `Server/Session` program으로 나눴다. |
| `src/main/java/.../ClientApplication.java` | 이동 완료 | Client module로 옮겼다. |
| `src/main/java/.../ClientScenario.java` | 부분 분해 | Client module의 thin orchestrator로 남기고 현재 구현된 marker logic은 `scenarios/`와 `support/` file로 분리했다. Kotlin 전환과 `.NET`/공통 scenario 의미 정렬은 남아 있다. |
| `src/main/java/.../Contracts.java` | 이동 완료 / Kotlin 재작성 대기 | Shared module로 옮겼다. `.NET` `Messages.cs`와 필드/packet 의미 대조는 남아 있다. |
| `src/main/java/.../Env.java` | 이동 완료 / 재분류 대기 | Shared module로 옮겼다. role별 option/support class 분리는 남아 있다. |
| `src/main/java/.../RegistryApplication.java` | 제거 완료 | Redis location store 전환 뒤 registry role source를 삭제했다. |
| `src/main/java/.../PlayApplication.java` | 부분 분해 | Play role module로 옮기고 Delay server와 Session runtime은 분리했다. Play support/evidence 정렬은 남아 있다. |
| `src/main/java/.../DelayHandler.java` | 이동 완료 | Delay role handler로 옮겼다. |
| `src/main/java/.../ProbeSpot.java` | 이동 후 재설계 필요 | Play role module로 옮겼다. `.NET` PlaySpotRuntime/PlaySpotTypes 기준 marker/evidence responsibility 정렬은 남아 있다. |
| `src/main/java/.../ProbeEntrySpot.java` | 이동 후 재설계 필요 | Play role module로 옮겼다. entry spot actor relay 경로 정렬은 남아 있다. |
| `src/main/java/.../ProbeActor.java` | 이동 후 재설계 필요 | Play role module로 옮겼다. actor yield/reentry/join scenarios 확장은 남아 있다. |
| `src/main/java/.../ProbeSession.java` | 이동 후 확장 필요 | Session role module로 옮겼다. `.NET` 기준 relay/push/shutdown support 추가가 남아 있다. |
