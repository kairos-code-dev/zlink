# Kotlin Framework E2E .NET 기준 포팅 계획

## 목적

이 계획은 `framework/languages/dotnet/e2e`를 기준 구현으로 삼아
`framework/languages/java/e2e-kotlin`의 Kotlin e2e를 같은 폴더 형태, 역할 분리, 파일 분류, 실행 검증
수준으로 다시 정렬하는 절차를 정의한다.

Kotlin e2e는 Java runtime 위의 Kotlin 사용 표면을 검증한다. Java e2e를 그대로 복사하는 것이 아니라,
`.NET` 기준 구조와 공통 e2e scenario를 Kotlin DSL과 Kotlin 사용자 코드 형태로 포팅한다.

## 완료 기준

1. `framework/languages/java/e2e-kotlin/<Config>/`가 대응하는 `.NET` config와 같은 의미의 구조를 가진다.
2. Gradle 파일과 Kotlin package 배치는 Kotlin 관례에 맞출 수 있지만, 역할과 파일 책임은 `.NET` 기준과
   대응된다.
3. `.NET`에 있는 scenario, server role, shared message, support 책임이 빠지지 않는다.
4. Kotlin public 사용 표면으로 구현할 수 없는 항목은 Java internal helper나 테스트 전용 adapter로
   메우지 않고 `feature-map.ko.md`에 gap으로 남긴다.
5. 포팅 중 버그가 발생하면 scenario만 통과시키는 우회 코드를 넣지 않는다. 실패 원인을 public runtime,
   framework, stream connector, zlink http client, e2e harness 중 책임 계층까지 추적하고, 같은 문제가
   다시 생기지 않도록 회귀 테스트를 먼저 추가하거나 함께 추가한 뒤 수정한다.
6. 한 config의 build, `run_e2e.sh` 실행, feature-map 갱신, Codex 에이전트 리뷰가 모두 끝나기 전에는
   다음 config 작업을 시작하지 않는다.
7. Codex 에이전트 리뷰에서 이슈 없음이 나와야 해당 config를 완료로 본다.

## 기준

1. 공통 e2e 문서:
   - `framework/doc/framework/common/e2e/README.ko.md`
   - `framework/doc/framework/common/e2e/config-*.ko.md`
2. `.NET` 기준 구현:
   - `framework/languages/dotnet/e2e/<Config>/`
   - `framework/languages/dotnet/e2e/<Config>/feature-map.ko.md`
3. Kotlin/Java framework public surface:
   - `framework/doc/framework/kotlin/`
   - `framework/doc/framework/java/`
   - `framework/languages/java/zlink-framework-core/`
   - `framework/languages/java/zlink-framework-spring-boot-starter/`
4. Kotlin e2e 대상:
   - `framework/languages/java/e2e-kotlin/<Config>/`

공통 e2e 문서는 검증 기준이고, 새 public API 추가 근거가 아니다. Kotlin에 없는 기능이 `.NET`에 있더라도
spec 또는 공통 framework 계약 근거를 먼저 확인한다.

`.NET` e2e는 포팅의 기준 구현이지만, 모든 config가 공통 e2e 완료 기준을 이미 완전히 만족한다는 뜻은
아니다. 포팅 전에 `.NET`의 `feature-map.ko.md`를 읽고 완료, 부분 구현, public contract gap, harness
gap을 구분한다. `.NET`에서 부분 구현인 항목을 Kotlin에서 그대로 완료로 표시하지 않는다. 공통 e2e 문서가
`.NET` 구현보다 더 강한 완료 기준을 요구하면 공통 e2e 문서를 우선하고, Kotlin에서 바로 구현할 수 없으면
`feature-map.ko.md`와 `porting-inventory.ko.md`에 gap으로 남긴다.

## 현재 Kotlin 작업물 처리 원칙

Kotlin e2e에는 이미 여러 config의 runner, Kotlin source, 일부 Java support source, feature-map이
있다. 이 작업물은 삭제 기준이 아니라 `.NET` e2e inventory와 공통 e2e 문서에 맞춰 분류할 입력이다.
작업자는 기존 Kotlin 파일이 어떤 `.NET` 파일과 scenario에 대응하는지 먼저 확인하고, 유지할 구현과
버릴 구조를 분리한다.

현재 상태:

- `framework/languages/java/e2e-kotlin`의 기존 파일은 검토 입력이다.
- 기존 Kotlin e2e는 Kotlin code path가 있는 config와 Java support source가 섞인 config가 있다. Kotlin
  사용자가 보는 설정, handler 등록, client scenario 흐름은 보존하거나 Kotlin으로 끌어올리고, 순수
  support 역할만 Java에 남긴다.
- 기존 `Monitoring` 이름은 `.NET` 기준 `RuntimeMonitoring`과 이름이 맞지 않는다. 완료 config로
  인정하기 전에 rename 또는 새 config 작성 여부를 inventory에서 결정한다.
- `YieldDispatch`는 기존 단일 Kotlin e2e 구현이 있지만 `.NET` 기준 role/project와 scenario 파일 분리가
  맞지 않는다. `.NET`과 공통 e2e 기준 inventory를 먼저 만들고 기존 구현을 재분류한다.

판단:

- 기존 Kotlin e2e를 그대로 완료로 인정하면 `.NET` 기준 폴더 분류와 scenario 파일 대응을 놓칠 수 있다.
- 반대로 기존 구현을 버리면 이미 작성된 runner, Kotlin public API 호출, feature-map의 gap 분류를 잃는다.
- 따라서 Kotlin은 config별로 **기존 구현 보존을 기본값**으로 두고, `.NET` 기준 inventory에서 불일치가
  확인된 파일만 이동, Kotlin 재작성, 삭제한다.
- `src/main/java`를 둘 수는 있지만, public contract gap을 숨기는 우회로 쓰지 않는다. Kotlin 사용자 코드가
  보는 설정, handler 등록, client scenario 흐름은 `src/main/kotlin`에 드러나야 한다.
- config별 첫 산출물은 `porting-inventory.ko.md`다. 이 파일에서 기존 Kotlin/Java support 파일의 유지,
  이동, Kotlin 재작성, 삭제 판단을 먼저 끝낸 뒤 코드 변경을 시작한다.

1차 분류:

| config | 현재 판단 | 작업 원칙 |
|--------|-----------|-----------|
| `RegistryMessaging` | public contract gap | `.NET` 기준 role/project 분리와 RM-A/B/C scenario 대부분은 완료했고 `logs/20260630-114719-3846562` runner가 통과했다. 다만 RM-C9 backpressure/HWM은 Kotlin/Java public channel builder에 low-HWM socket setup 표면이 없어 internal socket access 없이 완료 구현하지 않는다. |
| `SpotService` | public contract/harness gap | `.NET` 기준 `porting-inventory.ko.md`를 추가했고 현재 Kotlin 구현의 통과 marker와 gap을 파일 단위로 분류했다. `Shared`, `Client`, `Server/Registry`, `Server/Play`, `Server/Publisher`, `Server/Gateway`, `Server/MultiNode`, `Server/Session` Gradle module과 module별 binary를 추가했고, shared message contracts, env helper, route resolver, client scenario/support, Registry/Publisher/Gateway/MultiNode/Play role application, Play spot/handler/evidence endpoint, Session role entrypoint/application, stream session/auth handler, actor request handler, actor model, entry/user spot, evidence endpoint를 module-local `src/main/kotlin` 아래로 옮겼다. Gateway는 public `ZLinkSpotPublisherClient` 기반 `/spot/publish` HTTP endpoint와 evidence endpoint를 추가했고 full runner `gateway-publish` mode에서 `SM-C4-gateway` marker, gateway publish evidence, Play target spot evidence를 확인했다. MultiNode는 public route mesh, spot manager, spot factory로 `/spot/create-local`, `/spot/state/request`, `/evidence/wait`를 구현했고 `logs/20260630-013008-2203737` full runner에서 `.NET` 추가 검증 파일 `SM-Q9`의 local create/state/evidence 흐름과 두 노드의 `multi-state-request` evidence를 확인했다. `SM-A6`, `SM-A7`, `SM-E2`는 shell echo marker에서 Kotlin client scenario file로 옮겼고, `SM-A6`은 retry 시 이미 닫힌 bootstrap spot에 의존하지 않도록 Play-B HTTP admin endpoint가 public `ZLinkSpotManager.getOrCreate`로 만든 user spot을 close하는 흐름으로 바꿨다. `SM-A4`는 같은 `RoutingId`인 `room-a`의 반복 request가 같은 owner에 유지되는 의미로 맞췄다. actor-session mode는 `session-a`와 `session-b` 두 Session role을 시작하고, `SM-B1`, `SM-B3`, `SM-B5`, `SM-B6`, `SM-B7`, `SM-B8`, `SM-D1`, `SM-D3`, `SM-D4`, `SM-D5`, `SM-D6`, `SM-D7`, `SM-D8`, `SM-D9`, `SM-D10`, `SM-D11`, `SM-D13` client scenario file을 `.NET` 기준 파일명으로 분리해 실행한다. `SM-B5`는 public stream connector로 handler 없는 actor packet request를 보내 `SPOT_ACTOR/HANDLER_MISSING/REPLY_ERROR` flow evidence를 확인하고, `SM-B6`은 public `ZLinkSpotContext.leaveActor` 기반 explicit leave와 disconnect callback 차이를 확인하고, `SM-D3`는 public `ZLinkActorContext.joinSpot`으로 entry spot request와 user spot request의 relay/push 동등성을 비교하고, `SM-D4`는 public session actor bind와 `actor-id` metadata 분기를 확인하며, `SM-D5`는 public `ZLinkSessionActor.notifyDisconnected`가 선택 actor에게만 disconnect callback을 전달하는지 확인하고, `SM-D6`은 `session-a` bound session과 `session-b` shadow session 사이의 push 격리를 확인하며, `SM-D7`은 auth 전 actor request 실패와 auth 후 actor request/push dispatch를 확인하고, `SM-D8`은 끊긴 stream의 pending failure와 disconnect evidence 뒤 같은 actor의 재auth/rebind를 확인하고, `SM-D10`은 public stream connector의 bounded received-message queue와 session별 push 격리를 확인하고, `SM-D11`은 같은 client process의 stream actor request와 route-channel request 혼합을 확인하고, `SM-D13`은 heartbeat가 켜진 stream session의 연결 유지와 후속 actor request를 확인하고, `SM-B8`은 public entry spot actor destroy와 destroy 후 request failure를 확인하며, `SM-D9`는 public stream connector `observeInbound`로 stream reply packet name 관측을 확인한다. `SM-D12`는 두 Session role topology까지는 준비했지만 현재 Kotlin Session role이 연결 서버와 actor logic을 분리하지 않아 `session-b` 재접속 때 같은 actor id가 `session-b` 쪽 actor로 bind되므로 gap으로 유지한다. `logs/20260630-035320-2565912` full runner에서 `SM-B5`를 포함한 actor-session marker, `SPOT_ACTOR/HANDLER_MISSING/REPLY_ERROR/MissingActorReq` flow evidence, 최종 runner stdout `spot-service kotlin e2e result=passed` marker를 확인했다. runner에서는 Play가 stream endpoint를 소유하지 않고 actor-session mode에서 Session이 spot/stream/http endpoint를 새로 예약한다. Session evidence endpoint에는 `/evidence/wait`를 추가했고 actor-session client mode가 이 endpoint로 evidence marker를 기다린다. `SM-A8`은 worker follow-up evidence와 worker completion evidence를 모두 기다려 다음 mode와 겹치지 않게 했다. `logs/20260630-013008-2203737`에서 module별 `Registry.jar`, `Play.jar`, `Publisher.jar`, `Gateway.jar`, `Session.jar`, `MultiNode.jar`, `Client.jar` 실행, `SM-B1`, `SM-B3`, `SM-B7`, `SM-D1`, Gateway/publisher/type/timer/lifecycle/multi-node marker, worker evidence, actor/session evidence, 최종 runner stdout `spot-service kotlin e2e result=passed` marker를 확인했다. 이번 full runner `logs/20260630-014753-2253374`는 actor-session 전에 `SM-C3` spot-to-spot retry에서 timeout으로 실패했으므로 `SM-B6`/`SM-B8`/`SM-D3`/`SM-D4`/`SM-D5`/`SM-D6`/`SM-D7`/`SM-D8`/`SM-D9`/`SM-D10`/`SM-D11`/`SM-D13`의 full-sweep proof로 쓰지 않는다. Codex 자체 리뷰에서 `SmB5Scenario.kt`, `ZLinkBoundSessionRuntime`, `ZLinkSpotRuntime`의 SM-B5/bound-session 변경과 full runner proof를 대조했고 추가 이슈를 찾지 못했다. `porting-inventory.ko.md`의 scenario 표는 구현 marker가 확인된 항목을 `done`으로 정리했고, 미구현 항목은 `gap`으로 유지했다. file/role 표도 판단 보류 상태를 제거해 구현된 책임은 `done` 또는 `merged`로, 부분 구현은 `done/gap` 또는 `merged/gap`으로 재분류했다. 다만 Session의 control/stage/multi-node 세부 parity는 아직 gap이다. |
| `PubSub` | harness gap | `.NET` 기준 `Shared`, `Client`, `Server/Publisher`, `Server/Registry`, `Server/Subscriber` role project와 client scenario/support 분리를 완료했고 `logs/20260629-162342-449016` runner에서 `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1` marker와 각 client mode의 `pub-sub kotlin e2e result=passed`를 확인했다. 다만 `.NET feature-map`도 같은 HTTP evidence polling 기반 push 검증 gap을 부분 구현으로 남기므로 Kotlin도 완료로 과장하지 않고 feature-map에 gap을 유지한다. |
| `RegistrationCodec` | 완료 | `Shared`, `Client`, `Server/Main`, `Server/CodecRequester`, `Server/JsonOnlyPeer`, `Server/InvalidDuplicate` Gradle project와 runner process 분리, client scenario/support 파일 분리, server role package/CLI option 정리를 완료했다. `logs/20260629-165219-563294` runner에서 `RC-A1`, `RC-A2`, `RC-A3`, `RC-A4`, `RC-A5`, `RC-B1`, `RC-B2`, `RC-B3`, `RC-B4`, `RC-B5` marker, `RC-A6` duplicate packet registration startup failure, 최종 pass marker를 확인했다. |
| `DiscoveryRegistryHa` | 완료 | `.NET` 기준 `porting-inventory.ko.md`를 추가했고 `Client`, `Server/Registry`, `Server/Provider`, `Server/Consumer`, `Server/Probe`, `Server/Embedded` role project runner, CLI option 입력 전환, client scenario/support Kotlin split, shared message와 role support Kotlin 전환을 완료했다. `logs/20260629-180145-820678` full runner에서 `DR-A1`, `DR-A2`, `DR-A3`, `DR-A4`, `DR-B1`, `DR-B2`, `DR-B3`, `DR-C1`, `DR-C2`, `DR-C3`, `DR-D1`, `DR-D2`, `DR-D3`, `DR-D4` marker와 각 client mode의 `discovery-registry-ha kotlin e2e result=passed`를 확인했다. `DR-C3`는 same-consumer outage harness의 `consumer-DR-C3-flow.log` request/reply 흐름을 확인했고, `DR-D4`는 remote Probe process와 client mode의 provider 집합 비교를 확인했다. |
| `ResilienceLifecycle` | public contract/harness gap | `.NET` 기준 `porting-inventory.ko.md`를 추가했고 `Shared`, `Client`, `Server/Registry`, `Server/Provider` role project와 runner binary 분리, 구현된 client scenario/support Kotlin file 분리, Provider/Registry role application/support/handler와 shared message Kotlin 전환을 완료했다. `logs/20260629-183905-946498` runner에서 `RL-A1`, `RL-A2`, `RL-A3`, `RL-A5`, `RL-B1`, `RL-B3`, `RL-B4`, `RL-B5`, `RL-B6`, `RL-C1`, `RL-C3`, `RL-D1`, `RL-D3`, `RL-D5` marker와 각 client mode의 최종 통과를 확인했다. 다만 `Server/Consumer` role과 `RL-A4`, `RL-B2`, `RL-C2`, `RL-C4`, `RL-D2`, `RL-D4`는 public API 또는 harness 제어가 더 필요해 gap으로 남아 있다. |
| `RuntimeMonitoring` | 완료 | 기존 Kotlin `Monitoring` 디렉터리를 `.NET` 기준 이름인 `RuntimeMonitoring`으로 전환했고 package/app/cache 이름과 runner를 맞췄다. `Shared` Gradle module을 추가해 message/evidence contract를 분리했고, `Client`, `Server/Registry`, `Server/Service`, `Server/FailoverService`, `Server/FilteredService`, `Server/ThrowingService`, `Server/Trigger` Gradle module과 전용 binary를 추가했다. `MON-A1`, `MON-A2`, `MON-A3`, `MON-A4`, `MON-A5`, `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1` client scenario는 Kotlin 파일로 분리했다. `MON-A4`는 Service admin endpoint가 public runtime socket weight를 0으로 바꾸고 Trigger socket monitoring evidence에서 `PEER_ADMISSION_CHANGED`, 같은 `svc-a` routing id를 가진 FailoverService handler evidence, registry `TOPOLOGY_CHANGED` evidence를 함께 확인한다. `MON-B1`은 FilteredService evidence에서 `CONNECTION_READY` 필터만 관찰하는지 확인하고, `MON-C1`은 ThrowingService evidence에서 monitoring handler failure 뒤 messaging이 계속되는지 확인한다. `MON-B2` client scenario는 Trigger HTTP endpoint를 호출해 duplicate source, 비양수 interval, missing socket source, missing spot source 검증을 실행한다. `MON-D1`은 FilteredService service-b를 종료/재시작하고 Trigger transient service-b request, restarted service socket evidence, registry topology evidence를 확인한다. `logs/20260630-093115-3422310` full runner에서 Client/Registry/Service/FailoverService/FilteredService/ThrowingService/Trigger module binary 실행, marker, 최종 통과를 확인했다. Shared message/evidence contract와 env helper, Client Spring host/framework 설정, Registry host 설정, recorder, evidence store/evidence HTTP support와 Service host 설정, Service recorder/evidence store, Service work handler/spot timer, Service 계열 evidence/admin HTTP endpoint support, FailoverService host/handler, FilteredService host 설정, ThrowingService host 설정, Trigger host/HTTP/validation support는 module-local Kotlin class로 전환했다. Java/Kotlin `DefaultZLinkMonitoringOptions`도 duplicate monitoring source를 configuration error로 거부하도록 맞췄다. `.NET` Trigger의 disconnect, throw, log wait, invalid-handshake helper endpoint는 Kotlin client scenario의 직접 channel request, service별 evidence endpoint, raw TCP 연결로 공통 scenario 요구를 검증하므로 인벤토리에서 merged로 정리했다. |
| `YieldDispatch` | public contract/harness/scenario gap | `.NET` 기준 `porting-inventory.ko.md`를 추가했고 `Shared`, `Client`, `Server/Registry`, `Server/Delay`, `Server/Play`, `Server/Session` Gradle module과 module별 binary를 추가했다. Delay server는 Play role에서 분리했고 stream endpoint와 actor auth/relay runtime은 Session role로 분리했다. Play/Session 사이의 remote actor join은 registry-backed SPOT remote addresses와 explicit route mesh로 연결한다. 현재 구현된 client marker 중 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-C1`, `YD-C2`, `YD-E1`, `YD-D1`, `YD-D2`, `YD-D3`는 scenario file로 분리했고 `ScenarioAssert`/stream helper는 support file로 분리했다. `YD-A1`은 stream connector가 session gateway를 통해 같은 Spot에 `HoldCommand`/`ProbeCommand`를 보내고 public `requestToChannel(...).await(...)` 기본 terminator의 evidence 순서를 확인한다. `YD-A2`는 `YieldCommand`/`ProbeCommand`를 같은 경로로 보내 public `requestToChannel(...).yield(...)`가 turn을 반납하고 probe 뒤에 continuation을 재개하는 evidence 순서를 확인한다. `YD-A4`는 `WorkerYieldCommand`/`ProbeCommand`를 같은 경로로 보내 public `context.runWorker(...).yield()`가 turn을 반납하고 probe 뒤에 continuation을 재개하는 evidence 순서를 확인한다. `YD-B1`은 actor A가 public `requestToChannel(...).yield(...)`로 delay service를 기다리는 동안 actor B의 fast request가 actor A continuation보다 먼저 완료되는 evidence 순서를 확인한다. `YD-B2`/`YD-B3`는 scenario file은 남겼지만 `.NET` evidence marker와 actor message contract를 그대로 맞추지 못했고 full runner 안에서 route mesh timeout이 재현되어 runner 완료 marker에서 제외했다(`logs/20260630-113806-3810792`, `logs/20260630-114012-3819684`, `logs/20260630-112723-3766598`, `logs/20260630-113906-3813484`). `YD-C1`/`YD-C2`는 timer yield와 다음 tick 순서를 node-level evidence store로 확인하지만 `.NET` evidence HTTP surface와 timer command 전체 mode set은 아직 그대로 맞추지 않았다. `YD-C3`는 public stream actor request interleaving 시도에서 timeout이 나므로 gap으로 유지한다(`logs/20260630-071558-3077374`, `logs/20260630-071810-3084675`). `YD-D4`는 public `boundSession().send(...)`와 stream connector `waitFor(...)` 기반 구현을 시도했지만 full runner에 넣으면 기존 actor request sweep 또는 D4 actor join이 timeout되어 gap으로 유지한다(`logs/20260630-074648-3163279`, `logs/20260630-074614-3161019`, `logs/20260630-074527-3158860`). `YD-E1`은 `yield(...)` timeout 뒤 같은 Spot이 post-timeout probe packet을 처리하는지 evidence store로 확인하지만 `.NET` reply contract와 HTTP evidence surface를 그대로 맞추지는 않았다. `YD-E2`는 `.NET`의 `Yield<T>(CancellationToken)`에 대응하는 Java/Kotlin public request-call cancellation token overload가 없어 public contract gap으로 유지한다. `YD-E3`는 pending yield marker 확인, `play-a` 종료, public closed/cancelled error 확인, same-rid restart recovery probe를 묶는 shutdown-wait/recovery client mode와 runner supervision이 없어 harness gap으로 유지한다. `YD-D1`은 현재 single Play/Delay local topology에서 A/B/C/E1 marker 통과 뒤 aggregate marker를 남기지만, `.NET`처럼 local topology mode에서 evidence marker 전체를 다시 수집하지는 않는다. `YD-D2`는 기본 local sweep 뒤 D2 전용 mode에서 `play-b`를 추가 실행하고, `play-a` owner Spot의 public `requestToSpot(...).yield(...)`가 `play-b` target Spot을 기다린 뒤 owner/target evidence가 분리되는지 확인한다. `YD-D3`는 같은 D2 전용 topology에서 stream connector packet이 Session route mesh를 거쳐 `play-b` target Spot으로 전달되고, target Spot yield 중 probe marker가 먼저 실행되는지 확인한다. `YD-E4` 정적 검사 일부로 HTTP trigger/client 사용, Play handler와 Session Entry Spot join 예외 밖 `yield(...)` 사용, connector를 받지 않는 `Yd*.java` scenario file과 scenario file의 connector 생성/lifecycle 소유를 runner에서 거른다. `logs/20260630-114116-3823326` full runner에서 module binary 실행, `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-C1`, `YD-C2`, `YD-E1`, `YD-D1`, `YD-D2`, `YD-D3` marker, 정적 검사, 최종 통과를 확인했다. 기존 worker-yield `YD-C1` 표기는 `.NET` 기준 `YD-A4`로 재분류했다. 다만 `.NET` 구현 항목인 `YD-B2`, `YD-B3`, `YD-C3`, `YD-D4`, `YD-E2`, `YD-E3`는 Kotlin gap 또는 partial로 분류했고, `YD-E4`도 `.NET`처럼 helper-free connector usage까지는 아직 맞추지 않았다. `.NET`도 `YD-A3` metadata 보존과 `YD-E5` cross-language report aggregation은 partial이므로 Kotlin에서도 완료로 과장하지 않는다. |

## 표준 Kotlin E2E 구조

```text
framework/languages/java/e2e-kotlin/<Config>/
|-- Shared/
|   `-- src/main/kotlin/.../shared/
|-- Server/
|   |-- <Role>/
|   |   |-- build.gradle.kts
|   |   |-- src/main/kotlin/.../<role>/Program.kt
|   |   |-- src/main/kotlin/.../<role>/Configuration/
|   |   |-- src/main/kotlin/.../<role>/Endpoints/
|   |   |-- src/main/kotlin/.../<role>/Handlers/
|   |   |-- src/main/kotlin/.../<role>/Infrastructure/
|   |   `-- src/main/kotlin/.../<role>/Support/
|   `-- <OtherRole>/
|-- Client/
|   |-- build.gradle.kts
|   |-- src/main/kotlin/.../client/Program.kt
|   |-- src/main/kotlin/.../client/Scenarios/
|   `-- src/main/kotlin/.../client/Support/
|-- logs/
|   `-- .gitignore
|-- .gitignore
|-- build.gradle.kts
|-- settings.gradle.kts
|-- feature-map.ko.md
`-- run_e2e.sh
```

Kotlin에서 일부 Java helper를 함께 컴파일해야 하면 `src/main/java`를 둘 수 있다. 다만 Kotlin e2e의
검증 흐름과 사용 예시는 Kotlin code path가 중심이어야 한다. Java helper가 public contract gap을
숨기는 우회가 되면 완료로 보지 않는다.

## 파일 분류 규칙

| 위치 | 책임 |
|------|------|
| `Shared` | client와 server가 함께 쓰는 request, reply, event, evidence 타입 |
| `Client/.../Program.kt` | scenario 목록과 실행 순서 선언 |
| `Client/.../Scenarios/` | `.NET Client/Scenarios` 파일 하나에 대응하는 Kotlin scenario 파일 |
| `Client/.../Support/` | option parsing, assertion, process launcher, wait helper |
| `Server/<Role>/.../Program.kt` | role 실행 진입점 |
| `Server/<Role>/.../Configuration/` | role 실행 옵션과 환경 변수 해석 |
| `Server/<Role>/.../Endpoints/` | HTTP endpoint와 evidence/wait/shutdown endpoint |
| `Server/<Role>/.../Handlers/` | framework handler, observer, spot, actor handler |
| `Server/<Role>/.../Infrastructure/` | evidence store와 role 내부 상태 |
| `Server/<Role>/.../Support/` | 해당 role 내부에서만 쓰는 relay, wait, runtime helper |
| `run_e2e.sh` | Gradle build, 포트 할당, role process 시작과 종료, client 실행, 실패 로그 출력 |
| `feature-map.ko.md` | scenario ID별 구현 상태, gap, 검증 결과 |

`Program.kt`는 진입점만 담당한다. Kotlin DSL 설정과 endpoint 등록이 길어지면 role별 host factory와
configuration class로 나눈다.

## .NET 위치 복사 금지와 재분류 규칙

`.NET` e2e의 현재 파일 위치가 항상 목표 위치는 아니다. `.NET` role root에 option, endpoint, handler,
evidence 파일이 남아 있으면 Kotlin에서는 책임별 package와 폴더로 재분류한다.

- option, argument, endpoint 주소 설정: `Server/<Role>/.../Configuration/`
- HTTP endpoint mapping, evidence wait, shutdown endpoint: `Server/<Role>/.../Endpoints/`
- framework handler, dispatch filter, observer, spot, actor handler: `Server/<Role>/.../Handlers/`
- evidence store, runtime state, in-memory repository: `Server/<Role>/.../Infrastructure/`
- 해당 role 내부에서만 쓰는 relay, wait, runtime helper: `Server/<Role>/.../Support/`
- 여러 scenario가 함께 쓰는 client-side context나 helper: `Client/.../Support/`
- scenario ID 하나를 실행하는 파일: `Client/.../Scenarios/`

재분류한 파일은 `porting-inventory.ko.md` 비고에 원본 위치와 목표 위치를 함께 적는다.

## Scenario ID 판정 규칙

`Client/Scenarios/` 아래에 있다는 이유만으로 모두 scenario 파일로 보지 않는다. scenario 파일은 공통
e2e 문서의 scenario ID 하나를 직접 실행하고 marker를 검증하는 파일이다. context, shared record, fixture,
helper는 `.NET`에서 `Client/Scenarios/` 아래에 있더라도 Kotlin에서는 `Client/.../Support/`나 `Shared`로
옮긴다.

`.NET`에 별도 scenario 파일이 없지만 공통 e2e와 `.NET feature-map`에 scenario ID가 있으면
`porting-inventory.ko.md`에 공통 scenario ID 행을 추가하고 Kotlin 대응 scenario 파일을 명시한다.

## Inventory 매핑 산출물

각 config는 `.NET` 기준 파일 하나하나가 Kotlin에서 어디로 옮겨졌는지 기록하는 매핑 문서를 반드시 둔다.

```text
framework/languages/java/e2e-kotlin/<Config>/porting-inventory.ko.md
```

형식:

| .NET 기준 파일 | Kotlin 대응 파일 | 분류 | 상태 | 비고 |
|----------------|------------------|------|------|------|
| `Client/Scenarios/...Scenario.cs` | `Client/src/main/kotlin/.../Scenarios/...Scenario.kt` | scenario | done/gap | scenario ID와 marker |
| `Server/<Role>/...` | `Server/<Role>/src/main/kotlin/...` | server-role | done/gap | role 이름과 endpoint/handler 책임 |
| `Shared/Messages.cs` | `Shared/src/main/kotlin/.../shared/...` | shared | done/gap | payload field 대응 |
| `Client/Support/...` | `Client/src/main/kotlin/.../Support/...` | support | done/gap | 공통 helper 책임 |

규칙:

- `.NET` 기준 파일 목록은 아래 명령으로 생성한다.

```bash
find framework/languages/dotnet/e2e/<Config> -type f \
  ! -path '*/bin/*' \
  ! -path '*/obj/*' \
  ! -path '*/logs/*' \
  | sed 's#^framework/languages/dotnet/e2e/<Config>/##' \
  | sort
```

- `.csproj`, `.gitignore`, `run_e2e.sh`, `feature-map.ko.md`, `README.ko.md`도 매핑에서 빠뜨리지 않는다.
- `.NET` 파일 하나가 여러 Kotlin 파일로 나뉘면 대응 파일 칸에 모두 적는다.
- Kotlin에서 해당 파일이 필요 없다고 판단해도 행을 삭제하지 않는다. 상태를 `gap` 또는 `not-needed`로
  두고 근거를 비고에 적는다.
- `pending` 상태가 하나라도 있으면 config 완료로 보지 않는다.

## 진행 순서

| 순서 | Config | 기준 문서 | 완료 조건 |
|------|--------|-----------|-----------|
| 1 | `RegistryMessaging` | `config-1-registry-messaging.ko.md` | `.NET`의 RM-* scenario, registry/provider/workflow/consumer role 전부 대응 |
| 2 | `PubSub` | `config-3-pubsub.ko.md` | publisher/subscriber/registry role과 pubsub scenario 전부 대응 |
| 3 | `RegistrationCodec` | `config-4-registration-codec.ko.md` | registration, codec variant, invalid registration scenario 전부 대응 |
| 4 | `DiscoveryRegistryHa` | `config-6-discovery-registry-ha.ko.md` | cluster, failover, embedded registry, direct endpoint scenario 전부 대응 |
| 5 | `ResilienceLifecycle` | `config-5-resilience-lifecycle.ko.md` | restart, remap, drain, crash, outage, observer failure scenario 전부 대응 |
| 6 | `RuntimeMonitoring` | `config-7-monitoring.ko.md` | monitoring event, filter, dispatch failure, recovery scenario 전부 대응 |
| 7 | `SpotService` | `config-2-spot-service.ko.md` | spot, actor, session, route, timer, multi-node scenario 전부 대응 |
| 8 | `YieldDispatch` | `config-8-yield-dispatch.ko.md` | YD-A/B/C/D/E 전체 scenario 대응. 특히 YD-D1 local topology, YD-E3 runtime shutdown, YD-E4 금지 표면 정적 검증, YD-E5 언어별 의미 동등성까지 확인 |

기존 Kotlin e2e의 `Monitoring` 이름은 `.NET`과 맞춰 `RuntimeMonitoring`으로 정렬한다. 기존에 없는
`YieldDispatch`도 공통 e2e와 `.NET`에 있으므로 별도 config로 다룬다.

## Config 단위 작업 절차

1. `.NET` inventory를 생성한다.
   - `find framework/languages/dotnet/e2e/<Config> -type f ! -path '*/bin/*' ! -path '*/obj/*' ! -path '*/logs/*'`
2. `.NET` scenario, role, shared message, support 목록을 `porting-inventory.ko.md`에 정리한다.
3. 공통 e2e 문서에서 scenario ID와 성공 조건을 확인한다.
4. `.NET feature-map.ko.md`의 완료/부분/gap 상태를 함께 기록한다.
5. Kotlin public API와 문서에서 같은 동작을 제공할 수 있는지 확인한다.
6. public contract gap이 있으면 구현하지 말고 `feature-map.ko.md`에 남긴다.
7. Kotlin Gradle 프로젝트와 package를 `.NET` 역할 구조에 맞춰 만들되, stale `.NET` root 파일은 목표 폴더로 재분류한다.
8. Shared 타입, server role, client scenario 순서로 포팅한다.
9. scenario ID가 없는 helper/context 파일은 `Client/.../Scenarios/`가 아니라 `Client/.../Support/` 또는
   `Shared`로 옮긴다.
10. `run_e2e.sh`가 실제 role process를 띄우고 readiness, cleanup, 실패 로그 출력을 처리하게 한다.
11. `porting-inventory.ko.md`의 모든 행에 Kotlin 대응 파일, 분류, 상태를 채운다.
12. 해당 config의 `run_e2e.sh`를 실제 실행한다.
13. 실패하면 같은 config 안에서 수정하고 다시 실행한다. 이때 실패 원인을 모른 채 sleep, retry 횟수 증가,
    runner-only adapter, raw frame 조작으로 덮지 않는다. 원인을 좁혀 framework, stream connector,
    zlink http client, e2e 중 책임 위치를 수정하고 회귀 테스트를 추가한다.
14. 버그를 수정했다면 feature-map 또는 README에 원인, 수정 계층, 추가한 회귀 테스트를 함께 기록한다.
15. Codex 에이전트 리뷰를 요청한다.
16. 리뷰 이슈가 있으면 수정, 재실행, 재리뷰를 반복한다.
17. 리뷰가 이슈 없음이면 config 완료로 기록하고 다음 config로 이동한다.

## Codex 에이전트 리뷰 요청

```text
READ-ONLY로 리뷰해줘.
대상: framework/languages/java/e2e-kotlin/<Config>
기준: framework/languages/dotnet/e2e/<Config>, framework/doc/framework/common/e2e/config-*.ko.md

확인할 것:
1. .NET 기준의 폴더 구조, role 분리, 파일 분류가 Kotlin에도 같은 의미로 반영되었는가.
2. .NET의 Client/Scenarios 파일과 공통 e2e scenario ID가 Kotlin에서 빠짐없이 대응되는가.
3. Shared message, server role, endpoint, handler, infrastructure, client support가 누락 없이 포팅되었는가.
4. porting-inventory.ko.md가 .NET 기준 파일을 빠짐없이 담고, 각 행의 Kotlin 대응 파일과 상태가 실제와 맞는가.
5. .NET feature-map의 부분 구현/gap 상태를 Kotlin feature-map에서 과장 없이 반영했는가.
6. .NET role root에 있던 option/endpoint/handler/evidence/support 파일을 목표 분류로 재배치했는가.
7. Client/Scenarios 아래 helper/context 파일을 scenario로 오분류하지 않았는가.
8. public contract에 없는 기능을 private API, raw frame, test-only adapter로 우회하지 않았는가.
9. run_e2e.sh가 실제 프로세스 경계, readiness, cleanup, 실패 로그 출력을 제대로 처리하는가.
10. feature-map.ko.md가 구현 완료와 gap을 과장 없이 기록하는가.
11. 버그 수정이 임시 우회가 아니라 원인 계층을 고친 변경이며, 재발을 막는 회귀 테스트가 함께 있는가.
12. 실제 실행 결과가 문서와 코드의 완료 주장과 일치하는가.

출력:
- 심각도 순 findings만 먼저 적어줘.
- 파일:라인 근거를 반드시 붙여줘.
- 이슈가 없으면 "이슈 없음"이라고 명시해줘.
```

## 리뷰 기록

- `YieldDispatch`: `logs/20260630-114116-3823326` runner proof와 현재 `run_e2e.sh`, `ClientScenario.java`,
  `feature-map.ko.md`, `porting-inventory.ko.md`를 대조했다. `YD-B2`/`YD-B3`는 full runner 안에서
  route mesh timeout이 재현되어 완료 marker에서 제외했고, 문서의 runner 통과 범위와 gap 표기가 현재
  코드와 일치함을 확인했다. 추가 이슈 없음.
- `RegistryMessaging`: `logs/20260630-114719-3846562` runner proof와 현재 `run_e2e.sh`,
  `feature-map.ko.md`, `porting-inventory.ko.md`를 대조했다. runner marker는 `RM-A1`, `RM-A2`,
  `RM-A4`, `RM-A6`, `RM-B1`, `RM-B2`, `RM-C1`, `RM-C2`, `RM-C3`, `RM-C4`, `RM-C5`, `RM-C7`,
  `RM-C8`이며, `RM-C9`는 public contract gap으로 제외된 상태가 문서와 일치한다. 추가 이슈 없음.

## 누락 방지 체크리스트

- `.NET` inventory와 Kotlin 파일 목록을 나란히 비교했다.
- `porting-inventory.ko.md`에 `.NET` 기준 파일이 모두 있고 `pending` 상태가 없다.
- `.NET Client/Scenarios` 아래 파일을 scenario ID 파일과 helper/context 파일로 구분했고, scenario ID 파일만
  Kotlin `Client/.../Scenarios/`에 대응된다.
- `.NET`의 모든 server role이 Kotlin `Server/<Role>/`에 대응된다.
- 공통 e2e 문서의 scenario ID가 `feature-map.ko.md`와 client scenario 파일에 모두 나타난다.
- `.NET feature-map.ko.md`의 부분 구현/gap 항목을 완료로 과장하지 않았다.
- `.NET role root의 option/endpoint/handler/evidence/support 파일을 목표 폴더로 재분류했다.
- scenario ID가 없는 helper/context 파일을 `Client/.../Scenarios/`에 두지 않았다.
- Kotlin 사용 표면 검증을 Java helper나 internal API로 대체하지 않는다.
- 버그를 발견했을 때 scenario를 통과시키는 임시 우회 대신 원인을 수정하고 회귀 테스트를 추가했다.
- Gradle build output, `.gradle`, `.kotlin`, `build`, 임시 로그는 커밋하지 않는다.
- Codex 에이전트 리뷰가 이슈 없음으로 끝났다.
