# Node runtime 수정·unit·ZoneWorld sample 및 common internals 대조

작성일: 2026-08-04

## 범위와 판정

이번 기록의 작업 범위는 Node.js Framework runtime 수정, 해당 unit·contract test, 그리고
ZoneWorld sample의 실제 process 동작이다. 공통 E2E 374개 inventory 전체와 일곱 sample aggregate는
이번 작업의 범위로 확장하지 않았다.

runtime의 visible failure는 두 가지였다. `SpotWide`에서 deferred Actor Join이 target Spot을
기다리는 동안 source Spot의 공유 실행 권한을 계속 보유하면 서로 다른 zone으로 이동하는 두
Actor가 서로의 target admission을 기다리는 cycle이 생겼다. 이때 source 요청은 `NotConnected`
결과로 끝났고 target admission은 source timeout 뒤에 나타났다. 또 client가 STREAM을 닫은 뒤
늦게 도착한 one-way Actor push가 `SubmitError(InvalidState)`로 Gateway process의 poll 경계를
벗어났다.

현재 범위의 판정은 다음과 같다.

- deferred Join은 `SpotWide` 공유 gate를 기다릴 때 권한을 반납하고 lifecycle lane의 새 turn으로
  재개한다.
- 닫힌 Session으로 향하는 늦은 one-way delivery는 닫힌 session으로 정리하고 runtime poll을
  종료시키지 않는다.
- 관련 unit·contract test와 M6 runtime contract가 통과했다.
- ZoneWorld clean process 실행은 `PASS ZoneWorld`로 종료했다.
- 전체 `npm test`는 runtime unit failure가 아니라 공통 E2E inventory gate에서 중단된다. Node
  scenario file 224개와 common inventory 374개의 차이로 154개 ID가 아직 없다.

## runtime 수정과 owning layer

### Deferred Actor Join의 실행 권한

`packages/framework/src/runtime/actors/actor-context.ts`의 deferred `joinSpot`와
`joinEntrySpot`는 호출 시점의 `ZLinkSpotSerialTurn`을 보관한다. `SpotWide` 또는 Instance Spot의
turn이면 coordinator의 Join promise를 `yieldPromise`로 감싸므로 대기 중 source gate가 열리고,
완료 뒤에는 executor가 새 lifecycle turn으로 재개한다. PerActor turn에는 이 반납 경로를 적용하지
않는다.

이 수정은 sample의 timeout을 늘리거나 이동 순서를 바꾸는 방식이 아니다. Join의 실행 권한을
소유한 Framework runtime에서 대기와 재개 순서를 고쳤고, 새 회귀 test는 실제
`ZLinkSpotSerialExecutor`와 `DefaultZLinkActorContext`를 사용해 두 번째 Spot turn이 target Join
완료 전에 실행되는지 확인한다.

### 닫힌 STREAM session의 one-way delivery

`packages/framework/src/runtime/backend/node/node-raw-mesh-backend.ts`의
`RawStreamSessionService.deliver`는 binding target을 조회한 뒤 send가 `InvalidState`로 실패하면
target mapping을 제거하고 `false`를 반환한다. 이미 닫힌 client를 다시 보내거나 transport retry를
추가하지 않는다. 이 경계에서 one-way delivery의 실패를 닫힌 session 상태로 분류해 runtime poll과
host process가 예외로 종료되지 않도록 했다.

### RouteMesh source identity와 HWM

channel dispatcher는 `routeMesh === true`인 경우에만 source routing ID를
`ZLinkRouteMessageContext`에 넣고, mesh dispatch는 native source RID를 보존한다. remote-bound
session relay는 stale `ActorRef.acceptedHighWater`를 authoritative binding registry의 HWM과
비교하지 않고, active 또는 remembered seal과 binding identity를 확인한다. 이 경계는 호출부가
route 또는 HWM 내부 표현을 조립하지 않도록 유지했다.

### Sample 변경의 책임 범위

ZoneWorld는 typed public contract를 그대로 사용한다. bot의 초기 actor 생성과 placement weight
변경 순서를 분리하고, deferred Join 중인 Actor의 중복 tick을 막으며, Ops 보고는 runtime event와
명시적인 node report를 합쳐 registered·connected 상태를 구분한다. F client는 F4 완료를 확인한
뒤 종료하고 B4 process failure를 시작한다.

common internals/08의 규칙에 따라 기존 User Spot을 process restart 뒤 일반 message로 다시 만들지
않는다. 그래서 정상 E 시나리오는 원래 zone owner가 준비된 동안 실행하고, owner process를 종료한
뒤의 replacement는 maintenance 상태 복원과 isolated routing probe만 확인한다. sample에 raw frame
해석, private API, reflection, 메시지별 codec helper를 추가하지 않았다.

## common/internals 대조 결과

다음 문서를 현재 production call path와 sample runner에 대조했다.

- [`README`](../../../framework/common/internals/README.ko.md): common internals의 책임 경계를
  public contract와 구분하고, 구현 세부를 sample 호출부에 노출하지 않는지 확인했다.
- [`01-layering`](../../../framework/common/internals/01-layering.ko.md): Core·binding·Framework·sample의
  책임을 분리하고, runtime 내부 상태나 transport 결정을 sample로 밀어내지 않는지 확인했다.
- [`03-progress-isolation`](../../../framework/common/internals/03-progress-isolation.ko.md): 원격
  admission·relocation 대기가 다른 실행 lane의 진행을 막지 않고, 실패가 독립적인 operation에
  전파되지 않는지 확인했다.
- [`02-serialization`](../../../framework/common/internals/02-serialization.ko.md): Actor queue와
  Spot-wide shared gate를 분리하고, deferred Join 대기 중 gate를 반납한 뒤 새 작업으로 재개한다.
  `deferred-actor-join.test.js`와 수정된 `actor-manager.test.js`가 이 규칙을 고정한다.
- [`04-completion`](../../../framework/common/internals/04-completion.ko.md): completion 등록과
  correlation을 submit보다 먼저 수행하고, 완료 winner를 한 번만 확정한다. 닫힌 one-way
  session에는 재전송하지 않으며 callback을 lock 안에서 실행하지 않는다.
- [`05-relocation-continuity`](../../../framework/common/internals/05-relocation-continuity.ko.md):
  old owner address의 message가 Message Follow로 새 owner에 도달하도록 operation, generation,
  payload와 reply route를 보존한다. Actor packet relay와 session relay에서 이 경계를 확인했다.
- [`06-routing-and-cache`](../../../framework/common/internals/06-routing-and-cache.ko.md): Ready
  route와 필요한 fence만 cache하고 Missing·Creating·Store failure 결과를 cache하지 않는다.
  target route와 owner generation이 바뀌면 stale route를 사용하지 않는다.
- [`07-dispatch-loop`](../../../framework/common/internals/07-dispatch-loop.ko.md): readiness 이후
  즉시 깨우고, bounded batch와 시간 예산을 사용하며, timer callback이 Spot의 장시간 대기를
  보유하지 않도록 한다. ZoneWorld bot tick은 background task로 실행하고 timer turn을 붙잡지
  않는다.
- [`08-object-lifecycle`](../../../framework/common/internals/08-object-lifecycle.ko.md): 일반
  message가 User Spot을 생성하지 않으며, process loss 뒤 User Spot을 되살리지 않는다. runner의
  E/B4 순서와 replacement probe는 이 생명주기 규칙을 따른다.
- [`09-session-binding`](../../../framework/common/internals/09-session-binding.ko.md): Session gate와
  Actor gate를 별도로 유지하고 Actor 이동 시 Session을 재연결하지 않고 route만 갱신한다.
  binding identity와 active/remembered seal 검사가 이 책임 경계를 가진다.
- [`10-liveness-and-state`](../../../framework/common/internals/10-liveness-and-state.ko.md): runtime
  liveness 관측과 업무 message를 섞지 않고, status callback이 runtime 처리를 막지 않게 한다.
  Ops 보고 send의 rejected Promise를 process-level unhandled rejection으로 남기지 않는다.
- [`11-message-ownership`](../../../framework/common/internals/11-message-ownership.ko.md): 수락되지
  않은 message를 역직렬화하지 않고, handler가 받은 typed message의 소유권을 owning runtime에서
  닫는다. ZoneWorld 업무 코드에는 raw buffer decode 경로를 추가하지 않았다.
- [`12-service-wire-protocol`](../../../framework/common/internals/12-service-wire-protocol.ko.md): relay와
  relocation에서 operation, generation, payload, return route의 wire 의미를 보존하고, 업무 코드가
  control/data frame을 직접 조립하지 않는지 확인했다.

위 대조에서 현재 범위의 Critical·High·Medium 수준 위반은 확인하지 않았다. 다만 공통 구현 gap
목록의 W4인 Location Store `Active → Closing` 전이와 cross-node admission seal의 의미는 별도
설계·구현 조건으로 남아 있다. 이 기록은 W4 또는 전체 Framework spec gate를 완료로 승격하지
않는다.

## 실행 결과

모든 명령은 `/home/hep7/project/kairos/zlink/framework/languages/node`에서 실행했다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| 최종 `npm test`의 build·typecheck·lint 단계 | PASS |
| `node --test test/contract/deferred-actor-join.test.js test/contract/actor-manager.test.js` | 85/85 PASS |
| `node --test test/contract/channel-client.test.js test/contract/stream-runtime.test.js` | 187/187 PASS |
| `node --test test/contract/contract-surface.test.js` | 38/38 PASS |
| `node --test test/contract/sample-zoneworld-gate.test.js` | 9/9 PASS |
| `npm run verify:m6a-runtime` | 27/27 PASS |
| `npm run verify:m6b-runtime` | 51/51 PASS |
| `npm run verify:m6c-runtime` | 79/79 PASS |
| Node path-limited `git diff --check` | PASS |

`contract-surface`의 checked-in binding version fixture는 현재 package `11.2.0`과 달리 `11.1.0`을
가리키고 있어 `11.2.0`으로 정렬했다. 이는 public package snapshot과 실제 dependency가 같은
contract를 가리키게 하는 test fixture 수정이다.

최종 sample 명령은 다음과 같다.

```text
node samples/run-sample.mjs samples/ZoneWorld/Runner/sample-runner.mjs --keep-run-dir
```

최종 clean 실행 결과:

```text
scenario ZW-E1 passed
scenario ZW-E2 passed
scenario ZW-E3 passed
scenario ZW-E4 passed
scenario ZW-E6 passed
scenario ZW-E5 armed
scenario ZW-E5 passed
ZW-G3 rolling-replacement=ready
ZW-G4 crash-replacement=ready
ZW-G5 caller-fixed-routing-id=absent
topology=ready
zoneworld-transfer=completed
zoneworld-border-sync=completed
zoneworld-ops-observe=completed
zoneworld-ops-announce=completed
zoneworld-ops-maintenance=completed
zoneworld=completed
PASS ZoneWorld
runDir=/tmp/zlink-zoneworld-kuIIo2
```

첫 번째 clean 실행(`/tmp/zlink-zoneworld-XIVVUx`)은 초기 route readiness 변동으로 E1 관찰이
timeout됐지만 process와 runtime은 종료되지 않았고, 같은 build에서 즉시 재실행한 최종 run은
전체 marker를 통과했다. 이 timing 민감성은 aggregate sample reliability에서 다시 확인할 조건으로
남기며, 이번 범위의 최종 process evidence는 `kuIIo2` 실행을 사용한다.

전체 `npm test`는 `test/contract/e2e-scenario-header-gate.test.js`에서 중단됐다. 현재 Node
scenario file은 224개이고 common inventory는 374개이며 missing은 154개, extra ID는
`MON-A4`, `MON-D1`, `SM-D16`, `SM-Q9`이다. 이 결과는 runtime unit 실패가 아니라 사용자가
후속으로 지정한 E2E inventory gap이므로, scenario를 임의로 추가하거나 gate를 우회하지 않았다.

이번 기록에서는 commit·push를 수행하지 않았고, 다른 workstream의 dirty change를 수정하거나
정리하지 않았다.

## 2026-08-05 최신 runtime·PubSub 후속 검증

최신 working tree에서 manual fanout subscriber의 `subscriberConnections().connect()`와
`disconnect()`가 실제 receive loop와 socket 수명에 반영되도록 runtime lifecycle을 보강했다.
관련 regression은 첫 번째 publisher의 event 수신, 두 번째 endpoint 추가 후 수신, endpoint 제거 후
추가 event 미수신을 실제 `ZLinkFrameworkRuntimeHost` 세 개로 확인한다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `node --test test/contract/manual-fanout-lifecycle.test.js test/contract/channel-client.test.js test/contract/fanout-location-runtime.test.js test/contract/topology-runtime-projection.test.js` | 121/121 PASS |
| `TMPDIR=<isolated> ./e2e/PubSub/run_e2e.sh PS-D5` | PASS |
| Node path-limited `git diff --check` | PASS |
| 공개 문서 `plan/` 링크 검사 | 0건 |
| PS-D5 process log | `e2e/PubSub/log/20260805-041812-3949174` |

PS-D5는 public `/location/status` 응답을 통해 Store 상태가 정상에서 장애로 바뀌고 다시
복구되는지 확인한다. 장애 중과 복구 후에도 같은 publisher의 ready 상태와 event 전달을 확인한다.
`/location/status`는 기존 `ZLINK_LOCATION_RUNTIME_QUERY` public contract를 사용하며, E2E 전용
private runtime 접근이나 새 public API를 추가하지 않았다.

Codex Sol 독립 리뷰에서 확인한 manual fanout lifecycle의 stale termination callback, receive loop
정리 실패, dispose 중 재연결 문제를 runtime에 반영했다. generation token으로 이전 connection의
종료 callback이 새 connection을 닫지 않도록 제한했고, receive loop 정리가 실패해도 socket을 닫고
다음 endpoint 연결을 허용한다. 중복 endpoint를 한 번만 등록하고 dispose 뒤 retained handle의
재연결을 차단하는 regression도 추가했다.

최신 `e2e-scenario-header-gate.test.js`는 아직 common inventory 374개 중 Node exact scenario
header 242개만 일치해 132개 누락으로 실패한다. 따라서 PS-D5의 process 통과는 해당 scenario의
증거를 추가한 것이며, common E2E 전체 완료나 aggregate gate 통과를 의미하지 않는다.

## 2026-08-05 manual fanout ownership·dispose 최종 검증

이전 리뷰에서 확인한 endpoint ownership 분리와 cleanup race를 후속 수정했다. Builder registration과
`subscriberConnections()`가 같은 mutable endpoint set을 관찰하도록 했고, 중복 endpoint는 그 set에서
한 번만 유지한다. monitor termination 뒤 receive loop cleanup이 실패해도 socket registry를 정리하고
desired endpoint의 reconnect를 계속한다. active state가 없는 retained handle도 runtime dispose에서
detach하며, transition이 `loop.stop()`을 기다리는 동안 dispose가 시작되면 같은 loop를 일반 subscriber
cleanup에서 다시 stop하지 않는다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `node --test test/contract/manual-fanout-lifecycle.test.js` | 6/6 PASS |
| `node --test test/contract/manual-fanout-lifecycle.test.js test/contract/channel-client.test.js test/contract/fanout-location-runtime.test.js test/contract/topology-runtime-projection.test.js` | 125/125 PASS |
| Codex Sol final read-only review | CLEAN; Critical/High/Medium 0 |
| Node scoped `git diff --check` | PASS |

이 증거는 manual fanout runtime lifecycle 범위의 완료를 의미한다. Framework 전체 spec gate,
common sample, common E2E aggregate와 package/CI completion은 별도 조건으로 남아 있다.

## 2026-08-05 PS-D2 ChannelName 필터 process 검증

PS-D2의 공통 시나리오에 맞춰 `events`와 `audit` ChannelName을 각각 사용하는 두 publisher와
`events` subscriber를 Store-backed process로 실행했다. 두 publisher가 모두 ready 상태가 된 뒤
서로 다른 marker를 publish했고, subscriber는 `events` marker만 수신하면서
`/status/fanout`에 `pub-events`만 유지하는지 확인했다.

| 명령 | 결과 |
|---|---:|
| Node framework 및 PubSub publisher/subscriber/client build | PASS |
| `TMPDIR=<isolated> ./e2e/PubSub/run_e2e.sh PS-D2` | PASS |
| PS-D2 process log | `e2e/PubSub/log/20260805-044034-4039804` |
| `node --test test/contract/e2e-scenario-header-gate.test.js` | FAIL; common inventory 374개 중 131개 누락 |

초기 build 실패는 `createPublisherEndpoints`의 기본 인자가 `"events"` 리터럴 타입으로
추론되어 `audit`를 전달할 수 없었던 문제였다. 인자를 `string`으로 명시하고 다시 build한 뒤
PS-D2를 실행했다. 이 수정은 E2E 전용 우회가 아니라 기존 `addFanoutChannel(string)`과
`ZLinkFanoutClient.publish(channelName, ...)`의 public surface를 그대로 사용한다.

PS-D2 통과는 해당 process 증거만 추가하며, Framework spec gate와 common E2E 전체 완료를
의미하지 않는다. 현재 남은 exact scenario header는 IS 36개, OBS 9개, PS 3개, RC 1개,
RL 19개, RM 1개, SA 20개, SF 18개, SM 12개, ST 12개다.

## 2026-08-05 ST-E1A 새 Actor incarnation 재bind 검증

Destroy 뒤 같은 `ActorId`로 새 `ObjectGeneration`을 만든 뒤 이전 Session binding을
재사용하지 않고 명시적으로 다시 bind해야 하는 경로를 process로 검증했다. 첫 실행에서
확인한 실패 원인은 Actor destroy 뒤 이전 Session owner의 native unbind가
`RequestResult.NotFound`와 errno 21을 반환하는데 `ZLinkManagedStream`이 이 결과를
실패로 처리한 것이었다. 해당 결과는 이미 Actor registry가 정리된 뒤 도착한 idempotent
cleanup 결과이므로 `NotConnected`와 같은 종료 결과로 처리했다.

또한 binding generation이 0이거나 없는 ActorRef에 대해 native bound-session send와
disconnect를 호출하지 않도록 `ZLinkNativeFallbackBoundSession`의 조건을 정리했다.
이 상태의 public error는 공통 Session Actor dispatch spec에 맞춰 `InvalidOperation`으로
반환하며, 단위 테스트에서 native send와 native close가 모두 호출되지 않는 것을 확인했다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run verify:m6b-runtime` | 69/69 PASS |
| `node --test test/contract/stream-runtime.test.js` | 94/94 PASS |
| `bash e2e/SpotActorTransfer/run_e2e.sh ST-E1A` | PASS |
| ST-E1A process log | `e2e/SpotActorTransfer/log/20260805-052046-14643` |

process 증거에는 첫 generation의 `bound_push|before-destroy`, 재생성 뒤 pre-bind
`bound_push_rejected|10`, 명시적 rebind 뒤 `bound_push|after-rebind`가 각각 한 번씩
기록되었고 Session owner에도 generation 1과 generation 2의 bind가 기록되었다. 이전
binding으로의 push는 수신되지 않았으며 host stderr도 비어 있었다.

초기 stream runtime test의 실패는 runtime 결함이 아니라 테스트 double이 실제 bound-session
경로인 `submitInfrastructure` 대신 `submit`만 대체한 문제였다. 해당 double을 production
호출 표면과 맞춘 뒤 전체 94개 테스트를 다시 실행해 통과시켰다.

이 결과는 ST-E1A runtime·unit·process 범위의 완료를 뜻한다. common sample 전체,
common E2E inventory 전체, PS-D6 public listener status 계약, package와 aggregate gate는
아직 완료 조건으로 남아 있다.

## 2026-08-05 PS-D6 Port 0 listener status와 자동 재연결 검증

PS-D6를 위해 common network listener identity spec에 publisher가 실제 advertised endpoint를 조회하는
계약을 먼저 추가하고, Node exact interface의 `ZLinkFanoutClient.getListenerStatus(...)`로 고정했다.
Publisher가 이 public API를 호출하는 `/status/listener` endpoint를 제공하며, 내부 socket이나
`lastEndpoint`를 sample 코드가 직접 읽지 않는다. Runtime은 bind 결과와 listener의 `AdvertiseHost`를
결합해 remote process가 사용할 endpoint를 반환한다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `node --test test/contract/channel-client.test.js` 새 listener status 2건 | PASS |
| `node --test --test-name-pattern='CH-001' test/contract/channel-client.test.js` | 1/1 PASS |
| `bash e2e/PubSub/run_e2e.sh PS-D6` | PASS |
| PS-D6 process log | `e2e/PubSub/log/20260805-053438-69688` |
| PS-D6 process stderr | publisher 2개, subscriber, client 모두 0 bytes |
| Node scoped `git diff --check` | PASS |

PS-D6 process는 publisher와 subscriber를 모두 새 process로 시작하고, subscriber에는 publisher endpoint를
입력하지 않았다. 첫 publisher의 public listener status가 nonzero port를 반환한 뒤 publisher를 정상
종료하고 같은 Publisher RID로 port `0` 재시작했다. 두 번째 status의 port가 첫 번째와 다르고,
subscriber가 descriptor를 자동으로 갱신한 뒤 `before-port-zero-restart`와 `after-port-zero-restart`
event를 각각 한 번 수신했다.

실제 listener status 출력은 `first=tcp://127.0.0.1:30281`과
`second=tcp://127.0.0.1:25983`이었다.

전체 `channel-client.test.js`를 같은 시점에 실행했을 때 CH-001이 ClientServer ready 대기 timeout으로
한 번 실패했지만, 해당 테스트를 단독 실행하면 1/1 PASS였다. 이는 PS-D6 변경과 무관한 기존 timing
민감성으로 분리해 기록하며, aggregate unit gate를 완료로 판정하지 않는다.

PS-D6의 runtime·unit·process 범위는 완료했다. 남은 조건은 common sample 전체, 남은 common E2E exact
inventory, sample 이후 1차 POSD/DDD review, E2E 이후 2차 POSD/DDD review, package·clean-consumer와
최종 aggregate gate다.
