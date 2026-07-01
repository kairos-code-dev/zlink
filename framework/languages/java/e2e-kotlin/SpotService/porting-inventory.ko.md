# Kotlin SpotService .NET 포팅 inventory

기준 구현은 `framework/languages/dotnet/e2e/SpotService`이다. 현재 Kotlin 구현은 `Shared`, `Client`,
`Server/Registry`, `Server/Play`, `Server/Gateway`, `Server/MultiNode`, `Server/Session` Gradle module을 만들고 각 module의 binary를 runner가
호출한다. shared message contracts, 환경 변수 helper, route resolver, plain HTTP/stream client driver,
client scenario runner, 구현된 client scenario file, client support helper,
Registry role entrypoint/application, Gateway/Play role application, spot 구현 class, actor model/factory, stream session/auth handler,
ingress/route handlers, user spot state/outbound handlers, timer handlers, actor request handlers, spot
subscription handler, evidence state store와 HTTP evidence endpoint는 `Shared`, `Client`, `Server/Registry`,
`Server/Gateway`, `Server/Play`, `Server/Session` module의 `src/main/kotlin` 아래에 있다. Registry role source는 `registry`
package, Gateway role source는 `gateway` package로 재분류했다. 초기 runner evidence에서는
module별 role binary 실행, 모든 구현된 client mode marker,
evidence marker, actor/session 묶음의 `SM-B1`, `SM-B3`, `SM-B7`, `SM-D1` 통과를 확인했다. `.NET` 기준
Play 전용 endpoint, handler, spot file은 `play/endpoints`, `play/handlers`, `play/spots` package로 재분류했다.
Session role entrypoint/application, stream session/auth handler, actor request handler, actor model, entry/user
spot, evidence endpoint는 `session`, `session/handlers`, `session/spots`, `session/endpoints` package로 분리했다.
`:Server:Session:installDist`는 통과했고 `logs/20260629-233928-1913086`에서 `Session.jar` 실행, `SM-B1`,
`SM-B3`, `SM-B7`, `SM-D1` marker, `SessionProgramKt` startup, session evidence, 최종
`spot-service kotlin e2e result=passed` marker를 확인했다. runner에서는 Play role이 stream endpoint를
소유하지 않고, actor-session mode를 시작할 때 Session role이 spot/stream/http endpoint를 새로 예약해
stream session을 소유한다. 이후 B/D actor-session client scenario는 `SmB1Scenario.kt`,
`SmB3Scenario.kt`, `SmB7Scenario.kt`, `SmD1Scenario.kt`로 분리했고 `ActorSessionScenarioSupport.kt`가
공통 stream setup을 맡는다. `SM-A8`은 worker follow-up evidence와 worker completion evidence를 모두
기다려 다음 mode와 worker 실행이 겹치지 않게 했다. `SM-A6`은 retry 시 이미 닫힌 bootstrap spot을 다시
닫지 않도록 Play-B HTTP admin endpoint가 public `ZLinkSpotManager.getOrCreate`로 만든 새 user spot을
닫는 흐름으로 바꿨다. `logs/20260630-004719-2079098`에서 actor-session mode의 `SM-B1`, `SM-B3`,
`SM-B7`, `SM-D1` marker, worker evidence, actor/session evidence, `SM-A6` lifecycle-close marker, 최종
runner stdout `spot-service kotlin e2e result=passed` marker를 확인했다.
Gateway module과 `spot-service-kotlin-gateway` binary는 public `ZLinkSpotPublisherClient` 기반
`/spot/publish` endpoint를 제공하고, client `gateway-publish` mode가 Play `/evidence/wait`로 target
spot publish evidence와 alternate spot isolation을 확인한다. `logs/20260630-013008-2203737` full runner에서
`scenario SM-C4-gateway passed`, `spot-service kotlin e2e mode=gateway-publish result=passed`,
`gateway-evidence.json`의 `spot-publish|rid=gateway` marker를 확인했다. MultiNode module과 `spot-service-kotlin-multi-node` binary는
`.NET` 추가 검증 파일인 `SM-Q9`의 local create/state/evidence 흐름을 public route mesh와 spot manager
API로 구현했고, `logs/20260630-004529-multinode-smoke-2076125` focused smoke에서 두 노드의
`multi-state-request` evidence를 확인했다. 이후 `logs/20260630-013008-2203737` full runner에서
client `multi-node` mode의 `SM-Q9` marker와 두 노드의 `multi-state-request` evidence를 확인했다.
Session evidence endpoint에는 `/evidence/wait`를 추가했고 `logs/20260630-010938-2136301` full runner의
actor-session mode에서 `SM-B1`, `SM-B3`, `SM-B7`, `SM-D1` marker와 함께 확인했다. Session의
control/stage/multi-node 세부 parity는 아직 완료되지 않았다.

최신 live checkout에서는 public `actor.context().boundSession().send(...)` 경로가 stream session rid로
직접 frame을 보내도록 runtime을 수정했다. `logs/20260630-121648-3951366` focused `actor-session`
runner에서 `SM-B1`, `SM-B3`, `SM-B5`, `SM-B6`, `SM-B7`, `SM-B8`, `SM-D1`, `SM-D3`,
`SM-D4`, `SM-D5`, `SM-D6`, `SM-D7`, `SM-D8`, `SM-D9`, `SM-D10`, `SM-D11`, `SM-D13`의
focused 통과 marker를 확인했다. 이어 `logs/20260630-121736-3954627` full runner에서 구현된 mode와
최종 `spot-service kotlin e2e result=passed` marker를 확인했다. 이 수정은 private/raw 우회 없이
기존 public bound-session contract의 runtime 전달 경로를 바로잡는다.

`logs/20260702-071019-20091` full runner에서는 Client가 framework runtime 없이 HTTP/stream driver로
실행되고, Play HTTP endpoint가 public `ZLinkRouteClient`로 spot/route 작업을 수행한다. `.NET` source
role에 없는 `Server/Publisher`와 root `ZLINK_KOTLIN_E2E_ROLE` switch는 제거했다. `SM-C4`는 Gateway
`/spot/publish` endpoint로 검증하고, `ActorSessionScenarioSupport.kt`는 client support package로 옮겼다.
최종 `spot-service kotlin e2e result=passed` marker를 확인했다.

## Scenario 상태

| scenario | .NET 기준 파일 | Kotlin 현재 대응 | 상태 | 비고 |
|----------|----------------|------------------|------|------|
| `SM-A1` | `Client/Scenarios/SmA1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA1Scenario.kt` | done | Client module binary에서 marker는 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `SM-A2` | `Client/Scenarios/SmA2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA2Scenario.kt` | done | state mutation marker는 통과한다. |
| `SM-A3` | `Client/Scenarios/SmA3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA3Scenario.kt` | done | `room-a` owner routing marker는 통과한다. |
| `SM-A4` | `Client/Scenarios/SmA4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA4Scenario.kt` | done | `room-a` 반복 request가 같은 owner에 유지되는 stable owner marker는 통과한다. |
| `SM-A5` | `Client/Scenarios/SmA5Scenario.cs` | 없음 | gap | Kotlin public wrapper 계층 계약이 아직 없다. |
| `SM-A6` | `Client/Scenarios/SmA6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA6Scenario.kt`, `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | done | Client module binary가 Play-B HTTP admin endpoint로 새 user spot을 만든 뒤 같은 rid를 close해 lifecycle marker를 확인한다. `logs/20260630-004719-2079098`에서 통과했다. |
| `SM-A7` | `Client/Scenarios/SmA7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA7Scenario.kt`, `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | done | type mismatch marker는 Client module binary가 Play `play/endpoints` HTTP admin/evidence endpoint로 확인한다. |
| `SM-A8` | `Client/Scenarios/SmA8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA8Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | done | worker offload marker는 통과한다. |
| `SM-B1` | `Client/Scenarios/SmB1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB1Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | local stream actor flow는 Session module binary에서 통과하고 client scenario file은 분리했다. |
| `SM-B2` | `Client/Scenarios/SmB2Scenario.cs` | 없음 | gap | remote actor join과 cross-node mailbox harness가 없다. |
| `SM-B3` | `Client/Scenarios/SmB3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB3Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | actor payload preservation marker는 Session module binary에서 통과하고 client scenario file은 분리했다. |
| `SM-B4` | `Client/Scenarios/SmB4Scenario.cs` | 없음 | gap | remote actor request/reply harness가 없다. |
| `SM-B5` | `Client/Scenarios/SmB5Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB5Scenario.kt`, `Server/Session/src/main/kotlin/.../session/SessionApplication.kt` | done | public stream connector로 `MissingActorReq`를 보내 request failure와 `SPOT_ACTOR/HANDLER_MISSING/REPLY_ERROR` message-flow evidence를 확인한다. `logs/20260630-035320-2565912` full runner에서 `SM-B5` marker와 flow evidence가 통과했다. |
| `SM-B6` | `Client/Scenarios/SmB6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB6Scenario.kt`, `Server/Session/src/main/kotlin/.../session/handlers/UserActorLeaveHandler.kt`, `Server/Session/src/main/kotlin/.../session/spots/UserSpot.kt` | done | public `ZLinkSpotContext.leaveActor(actor)` 기반 user spot leave와 stream disconnect callback 차이는 focused actor-session run `logs/focused-actor-session-20260630-023713-2377275`에서 통과했다. |
| `SM-B7` | `Client/Scenarios/SmB7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB7Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | lifecycle/order evidence marker는 Session module binary에서 통과하고 client scenario file은 분리했다. |
| `SM-B8` | `Client/Scenarios/SmB8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB8Scenario.kt`, `Server/Session/src/main/kotlin/.../session/handlers/EntryActorDestroyHandler.kt`, `Server/Session/src/main/kotlin/.../session/spots/ScenarioEntrySpot.kt` | done | public `ZLinkEntrySpotContext.destroyActor(actor)` 기반 actor destroy와 destroy 후 request failure marker는 focused actor-session run `logs/focused-actor-session-20260630-023307-2370308`에서 통과했다. |
| `SM-C1` | `Client/Scenarios/SmC1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC1Scenario.kt` | done | request/send/timeout marker는 통과한다. |
| `SM-C2` | `Client/Scenarios/SmC2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC2Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | done | spot-to-channel과 publish evidence marker는 통과한다. |
| `SM-C3` | `Client/Scenarios/SmC3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC3Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | done | spot-to-spot와 publish evidence marker는 통과한다. |
| `SM-C4` | `Client/Scenarios/SmC4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC4Scenario.kt`, `Server/Gateway/src/main/kotlin/.../gateway/GatewayApplication.kt` | done | `.NET`에 없는 Publisher role은 제거했고, Gateway mode는 `/spot/publish`, Play `/evidence/wait`, alternate spot isolation을 확인한다. `logs/20260702-071019-20091`에서 통과했다. |
| `SM-D1` | `Client/Scenarios/SmD1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD1Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | local stream actor bind/relay/push marker는 Session module binary에서 통과하고 client scenario file은 분리했다. |
| `SM-D2` | `Client/Scenarios/SmD2Scenario.cs` | 없음 | gap | remote stream session bind scenario가 없다. |
| `SM-D3` | `Client/Scenarios/SmD3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD3Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | entry/user spot actor bind 비교는 public `joinSpot` 기반 actor-session 경로에서 확인한다. focused actor-session run `logs/focused-actor-session-20260630-020720-2316864`에서 `SM-D3` marker와 Session evidence가 통과했다. |
| `SM-D4` | `Client/Scenarios/SmD4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD4Scenario.kt`, `Server/Session/src/main/kotlin/.../session/handlers/MultiBindHandler.kt` | done | public session actor bind와 `actor-id` metadata 분기 marker는 focused actor-session run `logs/focused-actor-session-20260630-021110-2323861`에서 통과했다. |
| `SM-D5` | `Client/Scenarios/SmD5Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD5Scenario.kt`, `Server/Session/src/main/kotlin/.../session/handlers/ScenarioSession.kt`, `Server/Session/src/main/kotlin/.../session/spots/ScenarioEntrySpot.kt` | done | session disconnect 때 선택 actor에 `notifyDisconnected`를 호출하는 marker는 focused actor-session run `logs/focused-actor-session-20260630-021110-2323861`에서 통과했다. |
| `SM-D6` | `Client/Scenarios/SmD6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD6Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | `session-a` bound session과 `session-b` shadow session 사이의 push target isolation marker는 focused actor-session run `logs/focused-actor-session-20260630-031506-2451994`에서 통과했다. |
| `SM-D7` | `Client/Scenarios/SmD7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD7Scenario.kt`, `Server/Session/src/main/kotlin/.../session/handlers/ScenarioSession.kt`, `Server/Session/src/main/kotlin/.../session/handlers/ActorAuthHandler.kt` | done | auth 전 actor request 실패와 auth 뒤 actor request/push dispatch marker는 focused actor-session run `logs/focused-actor-session-20260630-022502-2354570`에서 통과했다. |
| `SM-D8` | `Client/Scenarios/SmD8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD8Scenario.kt`, `Shared/src/main/kotlin/.../Contracts.kt`, `Server/Session/src/main/kotlin/.../session/handlers/SlowSessionHandler.kt` | done | reconnect 중 끊긴 stream의 pending failure, disconnect evidence, 같은 actor의 재auth/rebind marker는 focused actor-session run `logs/focused-actor-session-20260630-025247-2408499`에서 통과했다. |
| `SM-D9` | `Client/Scenarios/SmD9Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD9Scenario.kt`, `ActorSessionScenarioSupport.kt` | done | public stream connector `observeInbound` 기반 observer marker는 focused actor-session run `logs/focused-actor-session-20260630-020720-2316864`에서 통과했다. |
| `SM-D10` | `Client/Scenarios/SmD10Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD10Scenario.kt`, `Client/src/main/kotlin/.../client/support/StreamConnectorSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | public stream connector의 bounded received-message queue와 session별 push 격리 marker는 focused actor-session run `logs/focused-actor-session-20260630-031506-2451994`에서 통과했다. |
| `SM-D11` | `Client/Scenarios/SmD11Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD11Scenario.kt`, `Server/Play/src/main/kotlin/.../play/handlers/RoutePingHandler.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | 같은 client process에서 stream actor request와 route-channel request를 함께 처리하는 marker는 focused actor-session run `logs/focused-actor-session-20260630-030200-2426602`에서 통과했다. |
| `SM-D12` | `Client/Scenarios/SmD12Scenario.cs` | 없음 | gap | 두 Session role topology는 추가했지만 현재 Kotlin Session role은 연결 서버와 actor logic을 분리하지 않아 `session-b` 재접속 때 같은 actor id가 `session-b` 쪽 actor로 bind된다. 공통 기준의 gateway-independent actor state 유지 scenario가 없다. |
| `SM-D13` | `Client/Scenarios/SmD13Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD13Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/*` | done | heartbeat가 켜진 stream session의 연결 유지와 후속 actor request marker는 focused actor-session run `logs/focused-actor-session-20260630-025654-2416593`에서 통과했다. |
| `SM-D14` | `Client/Scenarios/SmD14Scenario.cs` | 없음 | gap | TLS stream endpoint와 certificate 구성이 없다. |
| `SM-E1` | `Client/Scenarios/SmE1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE1Scenario.kt` | done | missing packet dispatch evidence는 통과한다. |
| `SM-E2` | `Client/Scenarios/SmE2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE2Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | done | timer evidence는 Client module binary가 Play spot/handler와 HTTP evidence endpoint로 확인한다. |
| `SM-E3` | `Client/Scenarios/SmE3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE3Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/TimerScenarioSpot.kt` | done | idle close evidence는 통과한다. |
| `SM-E4` | `Client/Scenarios/SmE4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE4Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/TimerScenarioSpot.kt` | done | overrun policy evidence는 통과한다. |
| `SM-F1` | `Client/Scenarios/SmF1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF1Scenario.kt` | done | route mesh marker는 Client module binary에서 통과하고 Play route handler는 `play/handlers` package에 있다. |
| `SM-F2` | `Client/Scenarios/SmF2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF2Scenario.kt` | done | target spot egress channel marker는 Client module binary에서 통과하고 Play handler/spot은 package 재분류됐다. |
| `SM-F3` | 공통 e2e와 `.NET feature-map`; 별도 `.NET` scenario file 없음 | `Client/src/main/kotlin/.../client/scenarios/SmF3Scenario.kt` | done | route-channel packet과 target spot route packet 혼재 중 일반 route-channel request 부분은 Client module binary에서 통과하고 Play route handler는 `play/handlers` package에 있다. |
| `SM-F4` | `Client/Scenarios/SmF4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF4Scenario.kt` | gap | missing-route 부분은 통과하지만 malformed raw-frame harness는 없다. |
| `SM-F5` | 공통 e2e와 `.NET feature-map`; 별도 `.NET` scenario file 없음 | 없음 | gap | channel socket lifecycle과 spot routing 소유권 독립을 검증하는 harness가 아직 없다. |
| `SM-G1` | `Client/Scenarios/SmG1Scenario.cs` | 없음 | gap | play node crash/rejoin/rebind harness가 없다. |
| `SM-G2` | `Client/Scenarios/SmG2Scenario.cs` | 없음 | gap | scale-out owner remap harness가 없다. |
| `SM-G3` | `Client/Scenarios/SmG3Scenario.cs` | 없음 | gap | join/leave/request race 부하 harness가 없다. |
| `SM-G4` | `Client/Scenarios/SmG4Scenario.cs` | 없음 | gap | 다수 bound session push 부하와 오배달 검증 harness가 없다. |
| `.NET extra SM-Q9` | `Client/Scenarios/SmQ9Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmQ9Scenario.kt`, `Server/MultiNode/src/main/kotlin/.../multinode/*`, full runner `logs/20260630-013008-2203737` | done | 공통 e2e와 `.NET feature-map`에는 없는 추가 multi-node route-to-spot 검증 파일이다. Kotlin client `multi-node` mode가 MultiNode role의 local create/state/evidence 흐름을 실행하고 두 노드의 evidence를 확인한다. |

## .NET 파일 대응 요약

| .NET 기준 파일 그룹 | Kotlin 현재 대응 | 분류 | 상태 | 비고 |
|---------------------|------------------|------|------|------|
| `.gitignore`, `feature-map.ko.md`, `run_e2e.sh` | 같은 root 파일, module별 `build.gradle.kts` | config | done/gap | runner는 `Client`, `Server/Registry`, `Server/Play`, `Server/Gateway`, `Server/MultiNode`, `Server/Session` install build를 수행하고, Gateway `gateway-publish`와 MultiNode `multi-node` mode를 포함한 role binary를 호출한다. 물리적 파일 재분류와 남은 role gap은 아직 있다. |
| `Shared/*` | `Shared/build.gradle.kts`, `Shared/src/main/kotlin/.../Contracts.kt` 등 | shared | merged | `Shared` Gradle module source tree로 옮겼지만 message 외 support helper도 임시로 포함한다. |
| `Client/Program.cs` | `Client/build.gradle.kts`, `Client/src/main/kotlin/.../client/ClientProgram.kt`, `client/ClientScenario.kt`, `client/support/*` | client | done | Client role은 framework runtime을 띄우지 않는 plain HTTP/stream driver다. spot/route 작업은 Play HTTP endpoint로 위임하고, stream scenario는 public stream connector로 직접 실행한다. |
| `Client/Support/*` | `Shared/build.gradle.kts`, `Shared/src/main/kotlin/.../Env.kt`, `Client/src/main/kotlin/.../client/support/*` | support | merged | assert/wait/stream connector/HTTP evidence helper는 Client module로 옮겼다. `.NET` 기준 option object와 support 책임 재분류는 남아 있다. |
| `Client/Scenarios/*` | `Client/src/main/kotlin/.../client/scenarios/*Scenario.kt`, `Client/src/main/kotlin/.../client/support/ActorSessionScenarioSupport.kt`, `Client/src/main/kotlin/.../client/ClientScenario.kt`, `run_e2e.sh` | scenario | done/gap | 구현된 marker의 scenario file은 분리했고, scenario ID가 아닌 actor-session setup helper는 support package로 옮겼다. 없는 scenario는 gap으로 둔다. |
| `Server/Registry/*` | `Server/Registry/build.gradle.kts`, `Server/Registry/src/main/kotlin/.../registry/RegistryProgram.kt`, `Server/Registry/src/main/kotlin/.../registry/RegistryApplication.kt` | server-role | merged | Registry module source tree와 role binary는 있고 entrypoint/application은 `registry` package로 재분류했다. support file 분리는 아직 남아 있다. |
| `Server/Play/*` | `Server/Play/src/main/kotlin/.../play/PlayApplication.kt`, `play/handlers/*`, `play/spots/*`, `play/endpoints/EvidenceHttpServer.kt` | server-role | done/gap | Play module source tree와 `play/endpoints`, `play/handlers`, `play/spots` package로 재분류했다. |
| `Server/Gateway/*` | `Server/Gateway/build.gradle.kts`, `gateway/GatewayProgram.kt`, `gateway/GatewayApplication.kt` | server-role | done | Gateway module과 role binary를 추가했고 public `ZLinkSpotPublisherClient` 기반 `/spot/publish` HTTP endpoint를 구현했다. full runner `gateway-publish` mode에서 `SM-C4-gateway` marker와 gateway publish evidence를 확인했다. |
| `Server/MultiNode/*` | `Server/MultiNode/build.gradle.kts`, `multinode/MultiNodeProgram.kt`, `multinode/MultiNodeApplication.kt`, `multinode/MultiNodeSpots.kt` | server-role | done/gap | MultiNode 전용 module과 role binary를 추가했고 local create/state/evidence path는 full runner `multi-node` mode로 통과했다. session/stage/actor model 세부 parity는 아직 gap이다. |
| `Server/Session/*` | `Server/Session/build.gradle.kts`, `session/SessionProgram.kt`, `session/SessionApplication.kt`, `session/endpoints/*`, `session/handlers/*`, `session/spots/*` | server-role | done/gap | Session 전용 module과 binary를 추가했고 actor/session marker는 통과했다. `.NET` Session의 control/stage/multi-node 파일 대응은 아직 gap이다. |

## .NET 파일별 상세 대응

이 표는 `.NET` 기준 tree의 각 파일을 Kotlin tree에서 어떻게 다루는지 기록한다. `done`은 대응 책임을
Kotlin 파일이나 role에서 검증했다는 뜻이고, `merged`는 `.NET`의 별도 파일 책임을 Kotlin에서는 다른
파일이나 application/support 객체에 합쳐 두었다는 뜻이다. `gap`은 Kotlin에 대응 role, scenario,
public contract, 또는 검증 harness가 아직 없다는 뜻이다.

| .NET 기준 파일 | Kotlin 현재 대응 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | Kotlin SpotService root ignore 파일이 있다. |
| `feature-map.ko.md` | `feature-map.ko.md` | plan | done | 구현된 항목과 gap 표기를 이 inventory의 scenario 상태와 맞췄다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | module별 install build를 수행하고 role별 binary를 호출한다. `logs/20260630-013008-2203737`에서 구현된 mode/evidence marker, Session wiring, Gateway `gateway-publish`, publisher/type/timer/lifecycle/multi-node 후속 mode, 최종 runner stdout `spot-service kotlin e2e result=passed` marker를 확인했다. |
| `Shared/SpotService.Shared.csproj` | `Shared/build.gradle.kts` | project | done | Shared Gradle module은 module-local `src/main/kotlin`을 사용한다. |
| `Shared/Messages.cs` | `Shared/src/main/kotlin/.../Contracts.kt` | shared | merged | message contract는 Shared module source tree로 옮겼지만 support helper와 같은 package에 남아 있다. |
| `Client/SpotService.Client.csproj` | `Client/build.gradle.kts` | project | done | Client Gradle module과 binary는 module-local `src/main/kotlin`을 사용한다. |
| `Client/Program.cs` | `Client/build.gradle.kts`, `Client/src/main/kotlin/.../client/ClientProgram.kt`, `client/ClientScenario.kt`, `client/support/*` | client | done | Client role은 framework runtime을 띄우지 않는 plain HTTP/stream driver다. spot/route 작업은 Play HTTP endpoint로 위임하고, stream scenario는 public stream connector로 직접 실행한다. |
| `Client/Support/ClientOptions.cs` | `Shared/src/main/kotlin/.../Env.kt`, `run_e2e.sh` | support | merged | 환경 변수는 읽지만 `.NET` 기준 option object는 아직 없다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/kotlin/.../client/support/ScenarioAssert.kt` | support | done | assert helper는 Client module의 `client/support` package에 있다. |
| `Client/Support/SpotLifecycleOrderContext.cs` | 없음 | support | gap | lifecycle order 전용 context와 대응 scenario 분리가 없다. |
| `Client/Scenarios/SmA1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA1Scenario.kt` | scenario | done | marker는 Client module binary에서 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `Client/Scenarios/SmA2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA2Scenario.kt` | scenario | done | marker는 Client module binary에서 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `Client/Scenarios/SmA3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA3Scenario.kt` | scenario | done | `room-a` owner marker는 Client module binary에서 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `Client/Scenarios/SmA4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA4Scenario.kt` | scenario | done | `room-a` repeated-routing stable owner marker는 Client module binary에서 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `Client/Scenarios/SmA5Scenario.cs` | 없음 | scenario | gap | Kotlin public wrapper 계층 계약이 아직 없다. |
| `Client/Scenarios/SmA6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA6Scenario.kt`, `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | scenario | done | close evidence는 client scenario가 HTTP endpoint로 확인한다. |
| `Client/Scenarios/SmA7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA7Scenario.kt`, `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | scenario | done | type mismatch evidence는 client scenario가 HTTP endpoint로 확인한다. |
| `Client/Scenarios/SmA8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmA8Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | scenario | done | worker offload marker는 통과한다. |
| `Client/Scenarios/SmB1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB1Scenario.kt`, `ActorSessionScenarioSupport.kt`, Session actor/session handlers | scenario | done | client scenario file은 분리했고 Session module의 actor/session flow를 사용한다. |
| `Client/Scenarios/SmB2Scenario.cs` | 없음 | scenario | gap | remote actor join과 cross-node mailbox harness가 없다. |
| `Client/Scenarios/SmB3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB3Scenario.kt`, `ActorSessionScenarioSupport.kt`, Session actor/session handlers | scenario | done | client scenario file은 분리했고 shared actor-session context의 payload evidence를 검증한다. |
| `Client/Scenarios/SmB4Scenario.cs` | 없음 | scenario | gap | remote actor request/reply harness가 없다. |
| `Client/Scenarios/SmB5Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB5Scenario.kt` | scenario | done | handler 없는 actor packet request failure와 `SPOT_ACTOR/HANDLER_MISSING/REPLY_ERROR` evidence는 `logs/20260630-035320-2565912` full runner에서 통과했다. |
| `Client/Scenarios/SmB6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB6Scenario.kt` | scenario | done | explicit leave와 disconnect callback 차이 marker는 focused actor-session run `logs/focused-actor-session-20260630-023713-2377275`에서 통과했다. |
| `Client/Scenarios/SmB7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB7Scenario.kt`, `ActorSessionScenarioSupport.kt`, Session actor/session handlers | scenario | done | client scenario file은 분리했고 ordered actor request/push evidence를 검증한다. |
| `Client/Scenarios/SmB8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmB8Scenario.kt` | scenario | done | actor destroy와 destroy 후 request failure marker는 focused actor-session run `logs/focused-actor-session-20260630-023307-2370308`에서 통과했다. |
| `Client/Scenarios/SmC1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC1Scenario.kt` | scenario | done | marker는 Client module binary에서 통과하고 scenario file은 `client/scenarios` package에 있다. |
| `Client/Scenarios/SmC2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC2Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | scenario | done | spot-to-channel과 publish evidence marker는 통과한다. |
| `Client/Scenarios/SmC3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC3Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | scenario | done | spot-to-spot와 publish evidence marker는 통과한다. |
| `Client/Scenarios/SmC4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmC4Scenario.kt`, `Server/Gateway/src/main/kotlin/.../gateway/GatewayApplication.kt` | scenario | done | `.NET`에 없는 Publisher role은 제거했다. Gateway mode가 `/spot/publish`, Play `/evidence/wait`, alternate spot isolation을 확인한다. |
| `Client/Scenarios/SmD1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD1Scenario.kt`, `ActorSessionScenarioSupport.kt`, `Server/Session/src/main/kotlin/.../session/handlers/ScenarioSession.kt`, `ActorAuthHandler.kt` | scenario | done | client scenario file은 분리했고, Session role의 stream auth/relay handler를 사용한다. |
| `Client/Scenarios/SmD2Scenario.cs` | 없음 | scenario | gap | remote stream session bind scenario가 없다. |
| `Client/Scenarios/SmD3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD3Scenario.kt` | scenario | done | entry/user spot actor bind 비교 marker는 focused actor-session run `logs/focused-actor-session-20260630-020720-2316864`에서 통과했다. |
| `Client/Scenarios/SmD4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD4Scenario.kt` | scenario | done | 여러 actor bind와 metadata 분기 marker는 focused actor-session run `logs/focused-actor-session-20260630-021110-2323861`에서 통과했다. |
| `Client/Scenarios/SmD5Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD5Scenario.kt` | scenario | done | disconnect callback target isolation marker는 focused actor-session run `logs/focused-actor-session-20260630-021110-2323861`에서 통과했다. |
| `Client/Scenarios/SmD6Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD6Scenario.kt` | scenario | done | bound session push target isolation marker는 focused actor-session run `logs/focused-actor-session-20260630-021502-2331456`에서 통과했다. |
| `Client/Scenarios/SmD7Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD7Scenario.kt` | scenario | done | stream auth 전 dispatch 실패와 auth 후 정상 dispatch marker는 focused actor-session run `logs/focused-actor-session-20260630-022502-2354570`에서 통과했다. |
| `Client/Scenarios/SmD8Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD8Scenario.kt` | scenario | done | reconnect 중 pending failure와 재auth/rebind marker는 focused actor-session run `logs/focused-actor-session-20260630-025247-2408499`에서 통과했다. |
| `Client/Scenarios/SmD9Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD9Scenario.kt` | scenario | done | stream inbound observer marker는 focused actor-session run `logs/focused-actor-session-20260630-021110-2323861`에서 통과했다. |
| `Client/Scenarios/SmD10Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD10Scenario.kt` | scenario | done | public stream connector의 bounded received-message queue와 session별 push 격리 marker는 focused actor-session run `logs/focused-actor-session-20260630-031506-2451994`에서 통과했다. |
| `Client/Scenarios/SmD11Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD11Scenario.kt` | scenario | done | 같은 client process에서 stream actor request와 route-channel request를 함께 처리하는 marker는 focused actor-session run `logs/focused-actor-session-20260630-030200-2426602`에서 통과했다. |
| `Client/Scenarios/SmD12Scenario.cs` | 없음 | scenario | gap | `session-a`에서 `session-b`로 재접속하는 topology는 준비됐지만 gateway-independent actor state 유지 동작은 아직 없다. |
| `Client/Scenarios/SmD13Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmD13Scenario.kt` | scenario | done | heartbeat가 켜진 stream session의 연결 유지와 후속 actor request marker는 focused actor-session run `logs/focused-actor-session-20260630-025654-2416593`에서 통과했다. |
| `Client/Scenarios/SmD14Scenario.cs` | 없음 | scenario | gap | TLS stream endpoint와 certificate 구성이 없다. |
| `Client/Scenarios/SmE1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE1Scenario.kt` | scenario | done | missing packet dispatch evidence는 통과한다. |
| `Client/Scenarios/SmE2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE2Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt` | scenario | done | timer evidence는 client scenario가 HTTP endpoint로 확인한다. |
| `Client/Scenarios/SmE3Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE3Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/TimerScenarioSpot.kt` | scenario | done | idle close evidence는 통과한다. |
| `Client/Scenarios/SmE4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmE4Scenario.kt`, `Server/Play/src/main/kotlin/.../play/spots/TimerScenarioSpot.kt` | scenario | done | overrun policy evidence는 통과한다. |
| `Client/Scenarios/SmF1Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF1Scenario.kt` | scenario | done | route mesh marker는 Client module binary에서 통과하지만 route support 재분류는 아직 남아 있다. |
| `Client/Scenarios/SmF2Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF2Scenario.kt` | scenario | done | target spot egress channel marker는 Client module binary에서 통과하지만 route support 재분류는 아직 남아 있다. |
| `Client/Scenarios/SmF4Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmF4Scenario.kt` | scenario | gap | missing-route는 확인하지만 malformed raw-frame harness는 없다. |
| `Client/Scenarios/SmG1Scenario.cs` | 없음 | scenario | gap | play node crash/rejoin/rebind harness가 없다. |
| `Client/Scenarios/SmG2Scenario.cs` | 없음 | scenario | gap | scale-out owner remap harness가 없다. |
| `Client/Scenarios/SmG3Scenario.cs` | 없음 | scenario | gap | join/leave/request race 부하 harness가 없다. |
| `Client/Scenarios/SmG4Scenario.cs` | 없음 | scenario | gap | 다수 bound session push 부하와 오배달 검증 harness가 없다. |
| `Client/Scenarios/SmQ9Scenario.cs` | `Client/src/main/kotlin/.../client/scenarios/SmQ9Scenario.kt`, `Server/MultiNode/src/main/kotlin/.../multinode/*`, full runner `logs/20260630-013008-2203737` | scenario-extra | done | 공통 e2e와 `.NET feature-map`에는 없는 추가 multi-node route-to-spot 검증 파일이다. Kotlin client scenario와 runner `multi-node` mode가 MultiNode role의 `/spot/create-local`, `/spot/state/request`, `/evidence/wait` 흐름을 검증한다. |
| `Server/Registry/SpotService.Registry.csproj` | `Server/Registry/build.gradle.kts` | project | done | Registry Gradle module과 binary는 module-local `src/main/kotlin`을 사용한다. |
| `Server/Registry/Program.cs` | `Server/Registry/src/main/kotlin/.../registry/RegistryProgram.kt`, `registry/RegistryApplication.kt` | server-role | merged | Registry role source file은 Server/Registry module의 `registry` package로 재분류했다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/src/main/kotlin/.../registry/RegistryApplication.kt` | server-role | merged | registry host 구성 책임은 옮겼지만 파일 분리는 다르다. |
| `Server/Registry/RegistrySupport.cs` | `Shared/src/main/kotlin/.../Env.kt` | support | merged | registry option/support helper가 별도 파일로 나뉘지 않았다. |
| `Server/Play/SpotService.Play.csproj` | `Server/Play/build.gradle.kts` | project | done | Play Gradle module과 binary는 module-local `src/main/kotlin`을 사용한다. |
| `Server/Play/Program.cs` | `Server/Play/src/main/kotlin/.../play/PlayProgram.kt`, `play/PlayApplication.kt` | server-role | merged | Play role source file은 Server/Play module의 `play` package로 재분류했다. |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/src/main/kotlin/.../play/PlayApplication.kt` | server-role | merged | host 구성 책임은 옮겼지만 `.NET`과 같은 factory file은 아니다. |
| `Server/Play/PlaySupport.cs` | `Shared/src/main/kotlin/.../Env.kt`, `ScenarioState.kt`, `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | support | merged | Shared support와 Play endpoint로 분리했다. `.NET` PlaySupport와 1:1 파일 대응은 아니다. |
| `Server/Play/Endpoints/OperationalEndpoints.cs` | `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | endpoint | merged | operational evidence endpoint 일부를 Kotlin HTTP server가 담당하며 `play/endpoints` package에 있다. |
| `Server/Play/Endpoints/SpotFailureEndpoints.cs` | `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | endpoint | merged | failure evidence endpoint 일부를 Kotlin HTTP server가 담당하며 `play/endpoints` package에 있다. |
| `Server/Play/Endpoints/SpotInteractionEndpoints.cs` | `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | endpoint | merged | interaction evidence endpoint 일부를 Kotlin HTTP server가 담당하며 `play/endpoints` package에 있다. |
| `Server/Play/Endpoints/SpotLifecycleEndpoints.cs` | `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | endpoint | merged | lifecycle evidence endpoint 일부를 Kotlin HTTP server가 담당하며 `play/endpoints` package에 있다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/src/main/kotlin/.../play/handlers/EntryActorEchoHandler.kt`, `EntryActorJoinHandler.kt`, `UserActorEchoHandler.kt` | handler | done | actor handlers는 Play module의 `play/handlers` package에 있다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | `Server/Play/src/main/kotlin/.../play/handlers/IngressCommandHandler.kt`, `NoopIngressHandler.kt`, `RoutePingHandler.kt` | handler | done | control/ingress handler 책임은 Play module로 옮겼지만 파일 대응은 다르다. |
| `Server/Play/Handlers/PlaySessionHandlers.cs` | `Server/Play/src/main/kotlin/.../play/handlers/ScenarioSession.kt`, `ActorAuthHandler.kt`; `Server/Session/src/main/kotlin/.../session/handlers/*` | handler | merged | Play module에는 기존 compatibility handler가 남아 있고, actor/session 실행 경로는 Session role package로 분리했다. Play 쪽 중복 제거는 후속 정리다. |
| `Server/Play/Handlers/PlaySpotRouteHandlers.cs` | `Server/Play/src/main/kotlin/.../play/handlers/StateRequestHandler.kt`, `StateCommandHandler.kt`, `SlowRequestHandler.kt`, `OutboundRequestHandler.kt`, `OutboundCommandHandler.kt`, `SpotEventHandler.kt` | handler | done | route/user spot handlers는 Play module의 `play/handlers` package에 있다. |
| `Server/Play/Handlers/PlayStageHandlers.cs` | 없음 | handler | gap | Kotlin public wrapper 계층과 stage scenario가 없다. |
| `Server/Play/Spots/PlayActorModel.cs` | `Server/Play/src/main/kotlin/.../play/spots/ScenarioActor.kt`, `ScenarioActorFactory.kt`, `ScenarioEntrySpot.kt`, `UserSpot.kt` | spot | done | actor model/factory와 spot class는 Play module의 `play/spots` package에 있다. |
| `Server/Play/Spots/PlayMultiNodeScenario.cs` | 없음 | spot | gap | multi-node scenario support가 없다. |
| `Server/Gateway/SpotService.Gateway.csproj` | `Server/Gateway/build.gradle.kts` | project | done | Gateway Gradle module과 `spot-service-kotlin-gateway` binary를 추가했고 full runner `gateway-publish` mode에서 실행했다. |
| `Server/Gateway/Program.cs` | `Server/Gateway/src/main/kotlin/.../gateway/GatewayProgram.kt` | server-role | done | Gateway role entrypoint를 추가했고 full runner가 해당 binary를 시작한다. |
| `Server/Gateway/GatewayHostFactory.cs` | `Server/Gateway/src/main/kotlin/.../gateway/GatewayApplication.kt` | server-role | done | spot mesh pub/sub, HTTP `/health`, `/evidence`, `/spot/publish`, `/shutdown` 대응을 public Kotlin framework API로 구성했다. full runner `gateway-publish` mode가 publish evidence를 확인한다. |
| `Server/MultiNode/SpotService.MultiNode.csproj` | `Server/MultiNode/build.gradle.kts` | project | done | MultiNode Gradle module과 `spot-service-kotlin-multi-node` binary를 추가했고 `:Server:MultiNode:installDist`가 통과했다. |
| `Server/MultiNode/Program.cs` | `Server/MultiNode/src/main/kotlin/.../multinode/MultiNodeProgram.kt` | server-role | done | MultiNode role entrypoint를 추가했다. |
| `Server/MultiNode/MultiNodeHostFactory.cs` | `Server/MultiNode/src/main/kotlin/.../multinode/MultiNodeApplication.kt` | server-role | done | route mesh, spot mesh, HTTP `/health`, `/evidence`, `/evidence/wait`, `/spot/create-local`, `/spot/state/request`, `/shutdown`, `/crash` 대응을 public Kotlin framework API로 구성했고 full runner `multi-node` mode에서 확인했다. |
| `Server/MultiNode/MultiNodeSupport.cs` | `Server/MultiNode/src/main/kotlin/.../multinode/MultiNodeApplication.kt` | support | merged | options, evidence store, node kind support를 application file 안에 두었다. `.NET`과 같은 별도 support file 분리는 아직 없다. |
| `Server/MultiNode/Handlers/MultiNodeControlHandlers.cs` | `Server/MultiNode/src/main/kotlin/.../multinode/MultiNodeApplication.kt`, `MultiNodeSpots.kt` 일부 | handler | done/gap | local create/state route-to-spot 흐름은 구현했다. ensure actor, close spot, type mismatch, user spot actor join 등 control handler parity는 아직 gap이다. |
| `Server/MultiNode/Handlers/MultiNodeSessionHandlers.cs` | `ScenarioSession.kt`, `ActorAuthHandler.kt` 일부 | handler | gap | 전용 multi-node session handler package가 없다. |
| `Server/MultiNode/Handlers/MultiNodeStageHandlers.cs` | 없음 | handler | gap | multi-node stage handler 대응이 없다. |
| `Server/MultiNode/Spots/MultiNodeActorModel.cs` | `ScenarioActor.kt`, `ScenarioActorFactory.kt`, `ScenarioEntrySpot.kt`, `UserSpot.kt` 일부 | spot | gap | 전용 multi-node actor model file이 없다. |
| `Server/MultiNode/Spots/MultiNodeMultiNodeScenario.cs` | `Server/MultiNode/src/main/kotlin/.../multinode/MultiNodeSpots.kt` 일부 | spot | done/gap | `MultiNodeSpotA/B`와 `StateReq` handler를 추가했고 full runner `multi-node` mode에서 state evidence를 확인했다. stage support와 retry helper parity는 아직 gap이다. |
| `Server/Session/SpotService.Session.csproj` | `Server/Session/build.gradle.kts` | project | done | Session Gradle module과 `spot-service-kotlin-session` binary를 추가했다. |
| `Server/Session/Program.cs` | `Server/Session/src/main/kotlin/.../session/SessionProgram.kt` | server-role | done | Session role entrypoint를 분리했다. |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/src/main/kotlin/.../session/SessionApplication.kt` | server-role | done/gap | stream node, spot mesh, actor factory, evidence endpoint를 Session application으로 분리했다. `.NET`과 같은 control route surface는 아직 없다. |
| `Server/Session/SessionSupport.cs` | `Shared/src/main/kotlin/.../Env.kt`, `ScenarioState.kt`, `Server/Session/src/main/kotlin/.../session/endpoints/SessionEvidenceHttpServer.kt` | support | merged/gap | Session evidence endpoint와 `/evidence/wait`는 분리했다. `.NET` option/support object와 control option parity는 아직 남아 있다. |
| `Server/Session/Handlers/SessionControlHandlers.cs` | 없음 | handler | gap | Session 전용 control route handler package가 아직 없다. |
| `Server/Session/Handlers/SessionSessionHandlers.cs` | `Server/Session/src/main/kotlin/.../session/handlers/ScenarioSession.kt`, `ActorAuthHandler.kt`, `MultiBindHandler.kt` | handler | done/gap | session/auth/multi-bind handler는 Session role package로 분리했다. remote/user-spot auth handler는 아직 없다. |
| `Server/Session/Handlers/SessionStageHandlers.cs` | 없음 | handler | gap | session stage handler 대응이 없다. |
| `Server/Session/Spots/SessionActorModel.cs` | `Server/Session/src/main/kotlin/.../session/spots/ScenarioActor.kt`, `ScenarioActorFactory.kt`, `ScenarioEntrySpot.kt`, `UserSpot.kt` | spot | done/gap | actor model, entry spot, user spot은 Session role package로 분리했다. `.NET` 세부 evidence와 alternate spot parity는 아직 없다. |
| `Server/Session/Spots/SessionMultiNodeScenario.cs` | 없음 | spot | gap | session multi-node scenario spot이 없다. |

## 기존 Kotlin 파일 처리

| Kotlin 파일 또는 그룹 | 판단 | 다음 작업 |
|-----------------------|------|-----------|
| `src/main/kotlin/.../Program.kt` | 제거 | `.NET`처럼 role별 module entrypoint만 사용한다. |
| `Server/Play/src/main/kotlin/.../PlayProgram.kt` | 유지 후 재분류 | Play module binary 진입점이다. 후속 정리에서 `.NET` 기준 package 이름과 `Program.kt` 위치를 맞춘다. |
| `Client/src/main/kotlin/.../client/ClientProgram.kt` | 유지 후 재분류 | Client module binary 진입점이며 `client` package로 재분류했다. |
| `Client/src/main/kotlin/.../client/ClientApplication.kt` | 제거 | Client는 framework runtime을 띄우지 않는다. |
| `Client/src/main/kotlin/.../client/ClientScenario.kt` | 유지 | runner mode를 `.NET` 기준 scenario file로 연결하는 dispatcher로 둔다. |
| `Client/src/main/kotlin/.../client/scenarios/*Scenario.kt` | 유지 후 보강 | 구현된 marker는 일부 `.NET` 기준 scenario ID별 Kotlin file로 나눴고, runner-only marker와 없는 scenario는 후속 작업에서 보강한다. |
| `Client/src/main/kotlin/.../client/support/ScenarioAssert.kt`, `Client/src/main/kotlin/.../client/support/WaitSupport.kt`, `Client/src/main/kotlin/.../client/support/StreamConnectorSupport.kt`, `Client/src/main/kotlin/.../client/support/ClientHttpSupport.kt` | 유지 후 분리 | `.NET` 기준 `Client/Support/*` 책임 중 assert, wait, stream connector, HTTP evidence helper를 파일별로 나눴다. option object와 package 이름은 후속 정리에서 맞춘다. |
| `Shared/src/main/kotlin/.../Contracts.kt` | 유지 | `Shared` project의 message Kotlin file로 옮겼고, 후속 정리에서 shared package 이름을 맞춘다. |
| `Server/Registry/src/main/kotlin/.../registry/RegistryProgram.kt`, `Server/Registry/src/main/kotlin/.../registry/RegistryApplication.kt` | 유지 후 재분류 | Registry module의 `registry` package로 재분류했다. support file 분리는 후속 작업이다. |
| `Server/Play/src/main/kotlin/.../play/PlayApplication.kt` | 유지 후 분리 | Play module source tree로 옮겼고, 후속 정리에서 `Server/Play`, `Server/Session`, `Server/MultiNode` 책임을 분리한다. |
| `Server/Publisher/src/main/kotlin/.../publisher/PublisherProgram.kt`, `Server/Publisher/src/main/kotlin/.../publisher/PublisherApplication.kt` | 제거 | `.NET`에 없는 extra role이다. `SM-C4` publish 검증은 Gateway role의 public publisher endpoint가 담당한다. |
| `Client/src/main/kotlin/.../client/ClientDriverSpot.kt` | 제거 | Client spot runtime을 제거하고 plain HTTP/stream driver로 바꿨다. |
| `Server/Play/src/main/kotlin/.../play/spots/ScenarioActor.kt`, `Server/Play/src/main/kotlin/.../play/spots/ScenarioActorFactory.kt` | 유지 후 재분류 | Play module source tree로 옮겼고, role별 project를 만들 때 actor support package로 나눈다. |
| `Server/Play/src/main/kotlin/.../play/spots/UserSpot.kt`, `Server/Play/src/main/kotlin/.../play/spots/ScenarioEntrySpot.kt`, `Server/Play/src/main/kotlin/.../play/spots/TimerScenarioSpot.kt`, `Server/Play/src/main/kotlin/.../play/spots/MismatchedSpot.kt` | 유지 후 재분류 | Play module의 `play/spots` package로 재분류했다. role별 project 분리는 후속 작업이다. |
| `Server/Play/src/main/kotlin/.../play/handlers/EntryActorEchoHandler.kt`, `Server/Play/src/main/kotlin/.../EntryActorJoinHandler.kt`, `Server/Play/src/main/kotlin/.../UserActorEchoHandler.kt`, `Server/Play/src/main/kotlin/.../SpotEventHandler.kt` | 유지 후 재분류 | Play module의 `play/handlers` package로 재분류했다. role별 project 분리는 후속 작업이다. |
| `Server/Session/src/main/kotlin/.../session/SessionProgram.kt`, `Server/Session/src/main/kotlin/.../session/SessionApplication.kt`, `Server/Session/src/main/kotlin/.../session/endpoints/SessionEvidenceHttpServer.kt` | 유지 후 보강 | Session module binary와 evidence endpoint, `/evidence/wait`를 추가했다. control/stage/multi-node endpoint와 `.NET` option/support parity는 후속 작업이다. |
| `Server/Session/src/main/kotlin/.../session/handlers/ScenarioSession.kt`, `Server/Session/src/main/kotlin/.../session/handlers/ActorAuthHandler.kt`, `Server/Session/src/main/kotlin/.../session/handlers/*Actor*Handler.kt` | 유지 후 보강 | stream auth, stream relay, entry/user actor request handler를 Session module로 분리했다. remote/multi-bind/user-spot auth handler는 후속 gap이다. |
| `Server/Session/src/main/kotlin/.../session/spots/ScenarioActor.kt`, `Server/Session/src/main/kotlin/.../session/spots/ScenarioActorFactory.kt`, `Server/Session/src/main/kotlin/.../session/spots/ScenarioEntrySpot.kt`, `Server/Session/src/main/kotlin/.../session/spots/UserSpot.kt` | 유지 후 보강 | actor model과 entry/user spot을 Session module로 분리했다. stage/multi-node/alternate spot parity는 후속 gap이다. |
| `Shared/src/main/kotlin/.../Env.kt` | 유지 후 분리 | 현재 공통 helper로 Shared에 있다. role별 option/support class를 만들 때 공통 support 또는 role별 support로 나눈다. |
| `Shared/src/main/kotlin/.../ScenarioState.kt` | 유지 후 분리 | 현재 Shared에 있다. role별 project를 만들 때 infrastructure package로 옮긴다. |
| `Server/Play/src/main/kotlin/.../play/endpoints/EvidenceHttpServer.kt` | 유지 후 재분류 | Play module의 `play/endpoints` package로 재분류했다. role별 endpoint 분리는 후속 작업이다. |
| `Shared/src/main/kotlin/.../SpotRouteResolver.kt` | 유지 후 분리 | 현재 Shared에 있다. role별 project를 만들 때 route/support package로 옮긴다. |
| `Server/Play/src/main/kotlin/.../play/handlers/IngressCommandHandler.kt`, `Server/Play/src/main/kotlin/.../NoopIngressHandler.kt`, `Server/Play/src/main/kotlin/.../RoutePingHandler.kt` | 유지 후 재분류 | Play module의 `play/handlers` package로 재분류했다. role별 project 분리는 후속 작업이다. |
| `Server/Play/src/main/kotlin/.../play/handlers/StateRequestHandler.kt`, `Server/Play/src/main/kotlin/.../StateCommandHandler.kt`, `Server/Play/src/main/kotlin/.../SlowRequestHandler.kt`, `Server/Play/src/main/kotlin/.../OutboundRequestHandler.kt`, `Server/Play/src/main/kotlin/.../OutboundCommandHandler.kt` | 유지 후 재분류 | Play module의 `play/handlers` package로 재분류했다. role별 project 분리는 후속 작업이다. |
| `Server/Play/src/main/kotlin/.../play/handlers/StateTimerHandler.kt`, `Server/Play/src/main/kotlin/.../TimerActivityHandler.kt`, `Server/Play/src/main/kotlin/.../TimerStatusHandler.kt`, `Server/Play/src/main/kotlin/.../TimerOverrunHandler.kt`, `Server/Play/src/main/kotlin/.../IdleCloseTimerHandler.kt` | 유지 후 재분류 | Play module의 `play/handlers` package로 재분류했다. role별 project 분리는 후속 작업이다. |
