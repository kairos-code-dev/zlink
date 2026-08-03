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
- [`service-wire-protocol`](../../../framework/common/internals/service-wire-protocol.ko.md): relay와
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
