# Node.js AutomaticTurnDispatch E2E 포팅 인벤토리

기준 문서: `framework/doc/framework/common/e2e/config-8-automatic-turn-dispatch.ko.md`

기준 구현: `framework/languages/dotnet/e2e/AutomaticTurnDispatch`

현재 상태: Node.js `AutomaticTurnDispatch` config는 ATD-A1~ATD-E5 범위를 구현했다. ATD-E3 shutdown wait는
`core/v8.6.3`과 Node native binding 갱신 뒤 `logs/20260708-141507-206725`에서 다시 통과했다.
ATD-E5의 cross-language aggregation은 Node config 완료 조건이 아니라 별도 parity gate 입력이다.

## Scenario

| Scenario | .NET 기준 파일 | Node.js 대상 파일 | 상태 | 비고 |
|----------|----------------|-------------------|------|------|
| ATD-A1 | `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/Scenarios/atd-a1-basic-terminator-scenario.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts` | done | stream connector와 Session gateway 경로를 구현했다. `logs/20260702-051530-20410`에서 marker order 검증 통과 |
| ATD-A2 | `Client/Scenarios/YdA2AwaitTerminatorScenario.cs` | `Client/Scenarios/atd-a2-await-terminator-scenario.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts` | done | `ZLinkRequestCall.submit<TReply>()`를 쓰는 Spot handler를 추가했다. `logs/20260702-051530-20410`에서 await/probe marker order 검증 통과 |
| ATD-A3 | `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/Scenarios/atd-a3-continuation-context-scenario.ts` | done | `.NET`과 같은 request id, spot rid, correlation id, await continuation marker order 검증은 통과했다. stream metadata 직접 노출은 Spot request handler public surface가 아니므로 이 시나리오의 완료 조건에 넣지 않는다. |
| ATD-A4 | `Client/Scenarios/YdA4WorkerAwaitScenario.cs` | `Client/Scenarios/atd-a4-worker-await-scenario.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts` | done | `runIoWorker(...).yield()`와 probe interleaving marker order가 `logs/20260715-092009-2649385`에서 통과했다. |
| ATD-B1 | `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/Scenarios/atd-b1-other-actor-progress-scenario.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Play/Handlers/control-handlers.ts`, `Server/Session/Handlers/await-session.ts` | done | actor A await 중 actor B fast request가 먼저 완료되는 marker order와 actor mailbox id를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-B2 | `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/Scenarios/atd-b2-same-actor-reentry-scenario.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Session/Handlers/await-session.ts` | done | 같은 actor A의 fast packet이 await continuation과 completion 뒤에 실행되는 marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-B3 | `Client/Scenarios/YdB3ActorJoinAwaitScenario.cs` | `Client/Scenarios/atd-b3-actor-join-await-scenario.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Play/Spots/await-probe-spot.ts`, `Server/Session/Handlers/await-session.ts` | done | `actor.context.joinSpot(...).submit()` 중 actor B fast request가 먼저 완료되는 marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-C1 | `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/Scenarios/atd-c1-timer-isolation-scenario.ts`, `Server/Play/Handlers/timer-spot-handlers.ts`, `Server/Play/Spots/await-probe-spot.ts`, `Server/Play/Spots/await-timer-state.ts`, `Server/Session/Handlers/await-session.ts` | done | timer A await 중 timer B fast tick이 먼저 완료되는 marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-C2 | `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/Scenarios/atd-c2-timer-reentry-scenario.ts`, `Server/Play/Handlers/timer-spot-handlers.ts`, `Server/Play/Spots/await-probe-spot.ts`, `Server/Play/Spots/await-timer-state.ts`, `Server/Session/Handlers/await-session.ts` | done | 같은 timer의 다음 tick이 await continuation 뒤에 실행되는 marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-C3 | `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/Scenarios/atd-c3-actor-timer-isolation-scenario.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Play/Handlers/timer-spot-handlers.ts`, `Server/Play/Spots/await-probe-spot.ts`, `Server/Session/Handlers/await-session.ts` | done | joined Spot actor await와 timer fast tick, timer await와 actor fast request의 marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-D1 | `run_e2e.sh`, `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `run_e2e.sh`, ATD-A1/ATD-C3/ATD-E1 범위 | done | `play-a`와 `delay-a` local topology에서 A/B/C/E1 marker를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-D2 | `Client/Scenarios/YdD2RemoteSpotAwaitScenario.cs` | `Client/Scenarios/atd-d2-remote-spot-await-scenario.ts`, `Server/Play/Handlers/remote-spot-handlers.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts`, `run_e2e.sh` | done | `play-a` owner Spot handler가 `play-b` target Spot request를 `await()`로 기다리고, continuation이 owner node로 돌아오는지 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-D3 | `Client/Scenarios/YdD3RouteBridgeAwaitScenario.cs` | `Client/Scenarios/atd-d3-route-bridge-await-scenario.ts`, `Server/Session/Handlers/await-session.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `run_e2e.sh` | done | session gateway가 `play-b` target Spot으로 `AwaitMsg`와 `ProbeMsg`를 relay하고, target Spot의 await/probe marker order를 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-D4 | `Client/Scenarios/YdD4SessionRelayActorAwaitScenario.cs` | `Client/Scenarios/atd-d4-session-relay-actor-await-scenario.ts`, `Server/Session/Handlers/await-session.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Play/Spots/await-probe-spot.ts`, `run_e2e.sh` | done | bound actor relay, actor handler await, bound session push, unbound session 미수신을 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-E1 | `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/Scenarios/atd-e1-timeout-scenario.ts`, `Server/Play/Handlers/failure-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts`, `run_e2e.sh` | done | timeout보다 늦은 delay request를 `await()`로 기다린 뒤 timeout marker를 남기고, 같은 Spot의 probe가 정상 처리되는지 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-E2 | `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/Scenarios/atd-e2-cancellation-scenario.ts`, `Server/Play/Handlers/failure-spot-handlers.ts`, `Server/Session/Handlers/await-session.ts`, `run_e2e.sh` | done | server-side cancellation signal을 `await()`에 전달한 뒤 cancellation marker를 남기고, 같은 Spot의 probe가 정상 처리되는지 검증했다. `logs/20260702-051530-20410`에서 통과 |
| ATD-E3 | `Client/Scenarios/ShutdownAwaitScenario.cs` | `Client/Scenarios/shutdown-await-scenario.ts`, `Server/Session/Handlers/await-session.ts`, `run_e2e.sh` | done | pending await 중 `play-a`를 종료한 뒤 stream request가 public closed/cancelled error로 끝나는지 검증했다. `logs/20260708-141507-206725`에서 `timeout 420s ./run_e2e.sh ATD-E3`가 `await-dispatch shutdown wait result=passed`, `await-dispatch shutdown recovery result=passed`, `scenario ATD-E3 passed`, `await-dispatch e2e result=passed`를 출력했다. 실행 중 `session-a`가 한동안 높은 CPU를 사용했지만 timeout이나 retry가 아니라 graceful shutdown 완료 뒤 pass했다. |
| ATD-E4 | `run_e2e.sh` | `run_e2e.sh` | done | await 시작용 HTTP endpoint/client 사용, `Server/Play` 밖의 `.submit(...)` 사용, YD scenario 파일의 connector helper 우회, shutdown scenario의 stream connector 직접 생성 누락을 정적으로 검사한다. `logs/20260702-051530-20410`에서 `scenario ATD-E4 passed` |
| ATD-E5 | `feature-map.ko.md` | `feature-map.ko.md` | done | Node 상태와 검증 로그는 공통 scenario id와 marker 이름으로 기록했다. 여러 framework 언어의 Config 8 report를 한 번에 모아 비교하는 aggregation은 별도 cross-language parity gate에서 수행한다. |

## 후속 계약 판정

| Scenario | 판정 | 다음 작업 |
|----------|------|-----------|
| ATD-E5 | 구현 | Node report는 공통 scenario id와 marker 이름을 사용한다. cross-language aggregation은 Node config 완료 조건이 아니라 모든 언어 report가 준비된 뒤 실행할 별도 parity gate다. |

## File Mapping

| .NET 기준 파일 | Node.js 대상 파일 | 분류 | 상태 | 비고 |
|----------------|-------------------|------|------|------|
| `.gitignore` | `.gitignore`, `logs/.gitignore` | ignore | done | dist, node_modules, 실행 로그 제외 |
| `feature-map.ko.md` | `feature-map.ko.md` | feature-map | done | ATD-A1~ATD-E5 pass 상태 기록 |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | build/start/readiness/client 실행이 있고 full 실행 `logs/20260708-003706-140692`가 통과했다. D2부터 `play-b`와 `delay-b`도 함께 구동하고, D4부터 `session-b`도 함께 구동한다. E3는 pending await 중 `play-a` 종료와 재시작 뒤 recovery probe를 검증하며, 선택 실행 `logs/20260708-141507-206725`가 통과했다. E4는 금지된 await 사용 표면을 정적으로 검사한다. |
| `Shared/Messages.cs` | `Shared/messages.ts` | shared | done | ATD-A1/ATD-E4, control, evidence, delay, actor bind/await/fast/join, timer start/stop, remote Spot await, actor push, timeout/cancellation, shutdown payload를 포팅했다. |
| `Shared/AutomaticTurnDispatch.Shared.csproj` | `Shared/messages.ts` | shared-project | done | Node는 별도 shared package 없이 각 role `tsconfig.json`에서 Shared를 포함한다. |
| `Client/GlobalUsings.cs` | not-needed | client-project | done | TypeScript에는 대응 파일이 필요 없다. |
| `Client/Program.cs` | `Client/main.ts` | client-entry | done | `full`, `ATD-A1`, `ATD-A2`, `ATD-A3`, `ATD-A4`, `ATD-B1`, `ATD-B2`, `ATD-B3`, `ATD-C1`, `ATD-C2`, `ATD-C3`, `ATD-D2`, `ATD-D3`, `ATD-D4`, `ATD-E1`, `ATD-E2` 선택 실행이 가능하다. ATD-E3는 runner가 `shutdown-wait`와 `shutdown-recovery` subcommand를 조합해 실행한다. |
| `Client/AutomaticTurnDispatch.Client.csproj` | `Client/package.json`, `Client/tsconfig.json` | client-project | done | Client build 설정 추가 |
| `Client/Support/ClientOptions.cs` | `Client/Support/client-options.ts` | client-support | done | Session A/B stream endpoint와 scenario option을 포팅했다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/scenario-assert.ts` | client-support | done | marker order assertion을 포팅했다. |
| `Client/Scenarios/AwaitActorScenarioContext.cs` | `Client/Scenarios/atd-b1-other-actor-progress-scenario.ts` | client-support | merged | B1/B2/B3/D4에 필요한 actor context를 B1 scenario 파일에서 export한다. 별도 context 파일은 만들지 않았다. |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/Scenarios/atd-a1-basic-terminator-scenario.ts` | scenario | done | stream connector 요청과 marker order 검증을 포팅했고 runner에서 통과했다. |
| `Client/Scenarios/YdA2AwaitTerminatorScenario.cs` | `Client/Scenarios/atd-a2-await-terminator-scenario.ts` | scenario | done | await marker order 검증을 포팅했고 runner에서 통과했다. |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/Scenarios/atd-a3-continuation-context-scenario.ts` | scenario | done | request id, spot rid, correlation id, await continuation marker order를 검증한다. |
| `Client/Scenarios/YdA4WorkerAwaitScenario.cs` | `Client/Scenarios/atd-a4-worker-await-scenario.ts` | scenario | done | worker await와 probe interleaving 검증을 포팅했고 runner에서 통과했다. |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/Scenarios/atd-b1-other-actor-progress-scenario.ts` | scenario | done | 두 stream connector를 같은 actor set에 바인딩해 actor A await와 actor B fast request를 동시에 검증한다. |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/Scenarios/atd-b2-same-actor-reentry-scenario.ts` | scenario | done | Node stream connector 제약 때문에 두 번째 same-actor packet은 같은 connector에서 send로 전달한다. actor mailbox 재진입 금지는 Play evidence order로 검증한다. |
| `Client/Scenarios/YdB3ActorJoinAwaitScenario.cs` | `Client/Scenarios/atd-b3-actor-join-await-scenario.ts` | scenario | done | actor join await와 actor B fast request interleaving을 검증한다. |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/Scenarios/atd-c1-timer-isolation-scenario.ts` | scenario | done | timer await와 other timer fast tick interleaving을 검증한다. |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/Scenarios/atd-c2-timer-reentry-scenario.ts` | scenario | done | 같은 timer의 다음 tick이 이전 tick continuation 뒤에 처리되는지 검증한다. |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/Scenarios/atd-c3-actor-timer-isolation-scenario.ts` | scenario | done | actor await 중 timer fast tick 진행과 timer await 중 actor fast request 진행을 검증한다. actor await와 timer control은 서로 다른 stream connector로 보내 pending request가 timer command를 막지 않게 한다. |
| `Client/Scenarios/YdD2RemoteSpotAwaitScenario.cs` | `Client/Scenarios/atd-d2-remote-spot-await-scenario.ts` | scenario | done | owner Spot은 `play-a`, target Spot은 `play-b`에 생성하고 owner/target evidence를 나눠 검증한다. |
| `Client/Scenarios/YdD3RouteBridgeAwaitScenario.cs` | `Client/Scenarios/atd-d3-route-bridge-await-scenario.ts` | scenario | done | target Spot을 `play-b`에 만들고 session gateway relay를 통해 `AwaitMsg`/`ProbeMsg` marker order를 검증한다. |
| `Client/Scenarios/YdD4SessionRelayActorAwaitScenario.cs` | `Client/Scenarios/atd-d4-session-relay-actor-await-scenario.ts` | scenario | done | bound session으로 relay한 actor request가 await 뒤 reply를 반환하고, actor가 보낸 push가 bound session에만 도착하는지 검증한다. |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/Scenarios/atd-e1-timeout-scenario.ts` | scenario | done | timeout 뒤 같은 Spot의 probe가 처리되고, 늦은 reply가 continuation marker를 남기지 않는지 검증한다. |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/Scenarios/atd-e2-cancellation-scenario.ts` | scenario | done | cancellation 뒤 같은 Spot의 probe가 처리되고, 늦은 reply가 continuation marker를 남기지 않는지 검증한다. |
| `Client/Scenarios/ShutdownAwaitScenario.cs` | `Client/Scenarios/shutdown-await-scenario.ts` | scenario | done | stream connector를 직접 만들어 shutdown wait와 recovery probe를 실행한다. shutdown wait는 `play-a` 종료 뒤 public closed/cancelled error를 통과 조건으로 보고, recovery는 재시작한 `play-a`의 Spot probe marker를 검증한다. |
| `Server/Registry/*` | 없음 | server-role | not-needed | AutomaticTurnDispatch runner는 registry role 없이 Redis location store와 explicit endpoint wiring을 사용한다. |
| `Server/Delay/Program.cs` | `Server/Delay/main.ts` | delay-role | done | delay role entrypoint 추가 |
| `Server/Delay/DelayHostFactory.cs` | `Server/Delay/delay-host-factory.ts`, `Server/Delay/Configuration/delay-options.ts` | delay-role | done | host 구성과 option parsing을 분리했다. |
| `Server/Delay/DelayHandler.cs` | `Server/Delay/Handlers/delay-handler.ts` | delay-role | done | ATD-A1/ATD-E4 범위의 delay reply handler 추가 |
| `Server/Delay/DelaySupport.cs` | `Server/Delay/Support/evidence-store.ts`, `Server/Delay/Support/http-server.ts` | delay-role | merged | Delay role 내부 evidence와 HTTP readiness helper로 재분류했다. |
| `Server/Delay/AutomaticTurnDispatch.Delay.csproj` | `Server/Delay/package.json`, `Server/Delay/tsconfig.json` | delay-project | done | Delay build 설정 추가 |
| `Server/Play/Program.cs` | `Server/Play/main.ts` | play-role | done | play role entrypoint 추가 |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/play-host-factory.ts`, `Server/Play/Configuration/play-options.ts` | play-role | done | ATD-A1/ATD-E4 범위에 필요한 route, spot, actor, timer, delay channel 구성 추가. runner에서 같은 binary를 `play-a`와 `play-b`로 구동한다. |
| `Server/Play/PlaySupport.cs` | `Server/Play/Support/evidence-store.ts`, `Server/Play/Support/http-server.ts` | play-role | merged | Play role 내부 evidence와 HTTP readiness helper로 재분류했다. |
| `Server/Play/Handlers/PlayBasicSpotHandlers.cs` | `Server/Play/Handlers/basic-spot-handlers.ts` | play-role | done | Hold/Await/Probe spot handler 추가. Await handler는 public `.submit<TReply>()`를 사용하고 D2 target Spot request에는 reply를 반환한다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | `Server/Play/Handlers/control-handlers.ts` | play-role | done | spot 생성, actor bind, evidence wait/query control handler 추가 |
| `Server/Play/Spots/PlaySpotTypes.cs` | `Server/Play/Spots/await-probe-spot.ts`, `Server/Play/Spots/await-actors.ts`, `Server/Play/Spots/await-timer-state.ts` | play-role | merged | basic Spot, B1/B2/B3 actor entry spot/factory, B3 target Spot actor join hook, C1/C2 timer state, C3/D4 joined Spot actor handler 등록을 역할별 파일로 나눴다. |
| `Server/Play/Spots/PlaySpotRuntime.cs` | `Server/Play/Spots/await-probe-spot.ts`, `Server/Play/Handlers/basic-spot-handlers.ts`, `Server/Play/Handlers/timer-spot-handlers.ts` | play-role | merged | Spot runtime 책임을 Spot class와 handler로 나눴고, actor join delay/evidence hook과 timer lifecycle/state 관리를 추가했다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/Spots/await-actors.ts` | play-role | done | B1/B2/B3 actor await/fast/join-await handler, C3 joined Spot actor await/fast handler, D4 actor push await handler를 추가했다. |
| `Server/Play/Handlers/PlayFailureSpotHandlers.cs` | `Server/Play/Handlers/failure-spot-handlers.ts` | play-role | done | ATD-E1 timeout handler와 ATD-E2 cancellation handler를 추가했다. ATD-E3 shutdown은 기존 Spot await/probe handler와 session relay 조합으로 검증한다. |
| `Server/Play/Handlers/PlayRemoteSpotHandlers.cs` | `Server/Play/Handlers/remote-spot-handlers.ts` | play-role | done | D2 remote Spot await request/command handler를 추가했다. D3는 기존 Spot await/probe handler와 session relay 조합으로 검증한다. |
| `Server/Play/Handlers/PlayTimerSpotHandlers.cs` | `Server/Play/Handlers/timer-spot-handlers.ts` | play-role | done | C1/C2 timer start/stop/await/fast/next tick handler를 추가했고 C3 actor/timer 조합 검증에도 사용한다. |
| `Server/Play/AutomaticTurnDispatch.Play.csproj` | `Server/Play/package.json`, `Server/Play/tsconfig.json` | play-project | done | Play build 설정 추가 |
| `Server/Session/Program.cs` | `Server/Session/main.ts` | session-role | done | session role entrypoint 추가 |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/session-host-factory.ts`, `Server/Session/Configuration/session-options.ts` | session-role | done | stream node, control route, spot route transport, Spot address discovery용 Spot mesh를 구성했다. |
| `Server/Session/Support/AwaitSession.cs` | `Server/Session/Handlers/await-session.ts` | session-role | done | stream dispatch, public `ZLinkSpotOutbound.sendToSpot(...)` command relay, actor bind/relay, timer command relay, D4 actor push await relay, E1/E2 failure command relay, E3 shutdown wait/recovery relay를 추가했다. |
| `Server/Session/Support/SessionSupport.cs` | `Server/Session/Support/evidence-store.ts`, `Server/Session/Support/http-server.ts` | session-role | merged | Session role 내부 evidence와 HTTP readiness helper로 재분류했다. |
| `Server/Session/Support/SessionSpotTypes.cs` | not-needed | session-role | not-needed | Node session actor relay는 runtime이 제공하는 bound actor reference와 `context.actors.bind(...)`/`actor.relay(...)`를 사용하므로 별도 session actor type 파일이 필요 없다. |
| `Server/Session/Support/AwaitSessionRelay.cs` | `Server/Session/Handlers/await-session.ts` | session-role | merged | play control 요청, Spot relay, actor relay, evidence 요청 helper 책임을 stream session handler 안으로 합쳤다. Spot relay retry loop는 현재 checkout에서 제거했다. |
| `Server/Session/Support/AwaitShutdownRelay.cs` | `Server/Session/Handlers/await-session.ts` | session-role | merged | Node에서는 shutdown cleanup relay를 stream session handler 안의 public Spot outbound request/send 경로로 합쳤다. 별도 raw frame 또는 private helper는 추가하지 않았다. |
| `Server/Session/AutomaticTurnDispatch.Session.csproj` | `Server/Session/package.json`, `Server/Session/tsconfig.json` | session-project | done | Session build 설정 추가 |

## Public Contract 확인 결과

- Node framework는 Spot handler 안에서 public `ZLinkRequestCall.submit<TReply>()`를 제공한다. ATD-A2 handler는 이
  public API를 사용한다.
- Session에서 created Spot으로 command를 전달할 때는 public `ZLinkSpotOutbound.sendToSpot(...)`을 사용한다.
  이 API는 location store 기반 Spot address resolution을 요구하므로 Session role도 `await.spot` Spot mesh router를
  가진다.
- 내부 bridge 호출, raw frame, 테스트 전용 adapter는 추가하지 않았다.
- B1 client driver는 actor A request와 actor B request를 서로 다른 stream connector로 보낸다. Node stream connector는 한
  connector에서 동시에 두 request/reply를 열 수 없기 때문에, 이 방식으로 같은 public session actor relay 계약을 검증한다.
- B2 client driver는 같은 connector에서 `ActorAwaitReq` request가 pending인 동안 `ActorFastMsg`를 send로 보낸다.
  이 방식은 request/reply 동시 pending 제약을 피하면서 같은 actor mailbox가 재진입하지 않는지를 public actor relay 경로로 검증한다.
- B3 handler는 public `actor.context.joinSpot(...).submit()`를 사용한다. target Spot의 join hook은 `.NET` 기준처럼
  join request의 delay payload를 기다린 뒤 evidence를 남긴다.
- C1/C2 timer handler는 public `spot.context.addTimer(...)`와 timer handler 안의 public `.submit<TReply>()`를 사용한다.
  timer overrun policy는 `.NET` 기준과 같은 delay-next-tick 의미의 `DelayNextTick`으로 설정했다.
- C3 joined Spot actor handler도 public Spot actor handler 등록과 public `.submit<TReply>()`만 사용한다. 동적으로
  생성되는 Spot은 Nest transient provider로 등록해 Spot instance, context, serial line이 Spot rid별로 분리되도록 했다.
- C3 actor-await 중 timer command는 별도 stream connector로 보낸다. Node stream connector가 pending request와
  같은 connector의 request/reply 동시 진행을 제한하기 때문에, timer command가 actor request 뒤에 막히지 않도록
  같은 public session relay 계약을 두 connector로 검증한다.
- D2 remote Spot handler는 public `spot.context.outbound.requestToSpot(...).submit<TReply>()`를 사용한다.
  target Spot은 `play-b`에서 일반 `AwaitMsg` request handler로 실행되고, continuation marker는 owner인
  `play-a`에만 남는지 검증한다.
- D3 route bridge 경로는 stream session에서 들어온 command가 session gateway의 public Spot outbound relay를
  거쳐 `play-b` target Spot handler에 도달하는 흐름으로 검증한다. target Spot handler 안의 `await()`와
  같은 Spot `ProbeMsg` interleaving만 사용하며 route mesh request 자체에 await를 붙이지 않는다.
- D4 actor handler는 public session actor relay와 public bound session send를 사용한다. actor가 joined Spot에
  있어도 같은 public actor handler 등록으로 처리하며, push 수신 범위는 bound session과 별도 unbound session을
  함께 열어 검증한다.
- E1 timeout handler는 Spot handler 안에서 public `requestToChannel(...).timeout(...).submit<TReply>()`를
  사용한다. timeout 뒤 같은 Spot에 `ProbeMsg`를 보내 mailbox가 다시 진행되는지 확인하고, 늦게 온
  reply가 continuation marker를 남기지 않는지도 검증한다.
- E2 cancellation handler는 Spot handler 안에서 server-side `AbortController` signal을 public
  `await<TReply>(signal)`에 전달한다. cancellation 뒤 같은 Spot에 `ProbeMsg`를 보내 mailbox가
  다시 진행되는지 확인하고, 늦게 온 reply가 continuation marker를 남기지 않는지도 검증한다.
- E3 shutdown scenario는 stream connector request가 pending인 동안 `play-a` process를 종료한다.
  client는 public stream connector가 내보내는 closed/cancelled error를 통과 조건으로 본다. 재시작 뒤에는
  같은 public session relay와 Spot outbound send 경로로 `ProbeMsg`를 보내 새 Spot이 정상 처리되는지
  확인한다.
- E4 static check는 await 시작용 HTTP endpoint/client 사용을 금지하고, `.submit(...)` 호출이
  `Server/Play` 아래 Spot, actor, timer, worker handler에만 남아 있는지 검사한다. YD scenario 파일은
  stream connector를 인자로 받아야 하며 connector 생성 helper로 감추지 않는다. shutdown scenario는
  stream connector를 직접 만든다.
- E5 report는 공통 scenario id와 marker 이름을 유지한다. 여러 framework 언어의 report를 한 번에
  모아 의미를 비교하는 aggregation은 별도 cross-language parity gate에서 수행한다.

## 검증 기록

- PASS: `timeout 420s ./run_e2e.sh full`
  - 로그: `logs/20260702-065606-73391`
  - 이전 로그: `logs/20260702-051530-20410`
  - `scenario ATD-E4 passed`
  - `scenario ATD-A1 passed`
  - `scenario ATD-A2 passed`
  - `scenario ATD-A3 passed`
  - `scenario ATD-A4 passed`
  - `scenario ATD-B1 passed`
  - `scenario ATD-B2 passed`
  - `scenario ATD-B3 passed`
  - `scenario ATD-C1 passed`
  - `scenario ATD-C2 passed`
  - `scenario ATD-C3 passed`
  - `scenario ATD-D2 passed`
  - `scenario ATD-D3 passed`
  - `scenario ATD-D4 passed`
  - `scenario ATD-E1 passed`
  - `scenario ATD-E2 passed`
  - `scenario ATD-E3 passed`
  - `await-dispatch shutdown wait result=passed`
  - `await-dispatch shutdown recovery result=passed`
  - `await-dispatch e2e result=passed`
