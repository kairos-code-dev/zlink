# 2026-08-02 runtime·unit phase 완료 기록

## 범위와 현재 판정

이번 기록은 Node.js Framework의 production runtime gap과 unit·production contract test만 다룬다.
process E2E와 sample 구현·실행은 사용자가 지정한 후속 범위이므로 완료 판정에 포함하지 않았다.

현재 판정은 다음과 같다.

- ND-IMP-001의 Nest `configureInboundDispatch` runtime·declaration 경로를 구현하고 contract test를
  통과했다.
- ND-IMP-002의 13개 public error kind와 내부 detailed failure mapping을 분리하고, exception과
  terminal error mapping을 unit/runtime test로 확인했다.
- ND-IMP-003의 unknown non-JSON content type을 public `ProtocolError`로 종료하도록 수정하고,
  handler가 payload를 받지 않는 경로를 회귀 test로 확인했다.
- Mesh endpoint-only admission·upgrade, liveness Ready, mailbox claim/release, Location·Spot owner
  recovery와 relocation runtime은 M5/M6 test 범위에서 통과했다.
- ND-IMP-004 package consumer parity, process E2E, 공통 E2E inventory와 sample gap은 후속 조건이다.

## 구현 candidate의 주요 책임 경계

runtime 수정은 호출부 우회나 raw frame 해석을 추가하지 않고 다음 production 경로에 반영했다.

| 책임 | 주요 source | 확인한 결과 |
|---|---|---|
| endpoint-only peer admission과 liveness | `packages/framework/src/runtime/foundation/raw-service-mesh-runtime.ts`, `service-liveness-registry.ts` | one-sided·bilateral endpoint-only route가 peer RID를 학습하고 Ready 전환을 완료한다. |
| bounded mailbox release | `packages/framework/src/runtime/foundation/service-mailbox.ts`, `node-raw-mesh-backend.ts` | claim 후 남은 raw record가 release에서 복구되고 infrastructure claim 진행을 막지 않는다. |
| public error mapping | `packages/framework/src/contracts/Errors/ZLinkFrameworkException.ts`, `packages/framework/src/runtime/framework-errors-internal.ts` | public 13-kind enum과 내부 failure kind mapping을 분리했다. |
| unknown content type | `packages/framework/src/runtime/channels/channel-envelope.ts`, `channel-dispatch-pipeline.ts` | 등록되지 않은 non-JSON content type을 ProtocolError로 완료한다. |
| Nest inbound dispatch contract | `packages/nestjs/src/contracts.ts`, `packages/nestjs/src/options-builder.ts` | declaration과 runtime builder가 동일한 `configureInboundDispatch` member를 제공한다. |
| IPC listener normalization | `packages/framework/src/contracts/Configuration/RegistrationNormalizers.ts` | `ipc://` endpoint가 TCP listener host validation으로 잘못 거부되지 않는다. |

## Fresh gate 결과

모든 명령은 `/home/hep7/project/kairos/zlink/framework/languages/node`에서 현재 working tree를
대상으로 실행했다.

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| `npm run verify:m5-foundation` | 5/5 PASS |
| `npm run verify:m6a-runtime` | 26/26 PASS |
| `npm run verify:m6b-runtime` | 43/43 PASS |
| `npm run verify:m6c-runtime` | 79/79 PASS |
| M5/M6 합계 | 153/153 PASS |

## Unit·production contract 범위

초기 candidate의 `test/**/*.test.js` inventory 123개에서 다음을 후속 범위로 제외했다.

- `test/browser/`와 `test/integration/`
- 경로 이름에 `e2e-` 또는 `sample-`이 포함된 test
- source에서 `e2e/` 또는 `samples/` 아래 구현을 읽는 static gate
- `node-sample-client-bundle.test.js`와 `user-spot-native-two-process.test.js`

마지막 항목은 각각 sample build·실행과 native child process 두 개를 직접 기동하므로 unit gate에
포함하지 않았다. 초기 candidate의 선택 수는 58개였고, 각 파일을 독립 Node test process로 실행한
결과는 `UNIT_SELECTED=58`, `UNIT_COMPLETED=58/58`이다. 이후 추가된 `application-hwm.test.js`를
포함한 최신 선택 수와 결과는 아래 최신 결과 절에 기록했다. 초기 포함 범위에는 `channel-client`,
`channel-envelope-error`, `contract-surface`, `location-*`, `nestjs-module`, `runtime-*`,
`stream-*`, `spot-*`, `topology-runtime-projection`과 binding smoke가 있다.

초기 inventory 실행에는 static E2E gate와 sample bundle test가 규칙상 한 차례 포함되었다. 그중
`registry-messaging-public-errors-gate.test.js`는 E2E source의 오래된 정적 표현을 기대해 실패했고,
`user-spot-native-two-process.test.js`는 native process의 `RequestError`로 실패했다. 두 failure는
수정하지 않고 후속 E2E·package evidence로 분리했다. 최종 unit gate에서는 두 종류를 모두 제외했다.

## 남은 조건

- `ND-IMP-004`: package version pin, local archive, lockfile과 clean consumer가 아직 일치하지 않는다.
- `ND-E2E-IMP-*`: Config 1-14 inventory, selector, role-server evidence와 process runner는 후속이다.
- `NS-IMP-*`·`NS-TEST-*`: 공통 sample 7종과 Node sample process 검증은 후속이다.
- `npm test` 전체 gate와 browser·integration·process E2E를 green으로 표시하지 않는다. 초기 phase의
  58/58 unit 결과와 M5/M6 153/153은 해당 범위를 대체하지 않는다.

이번 phase에서는 commit·push를 수행하지 않았다. 기존 working tree의 변경은 보존했다.

## STREAM recv와 전체 Framework socket Poller 정렬 후 업데이트

2026-08-02 후속 작업에서 Node Framework가 직접 수신하는 모든 production socket 경로를 다시
대조했다. 각 경로는 `PollIn` readiness를 확인한 뒤 `DontWait` receive를 호출한다.

| 수신 경로 | readiness와 receive의 소유 위치 | 현재 확인 |
|---|---|---|
| STREAM application ingress | `streams/index.ts`가 socket별 Poller를 만들고 `stream-session-runtime.ts`가 `wait(0)` 뒤 public `recv(DONT_WAIT)`를 호출 | callback ingress 제거, routing id와 raw parts 보존, segmented·multiple frame 조립, HWM pause/resume 통과 |
| Channel·Route ROUTER | `channel-runtime-lifecycle.ts`가 Poller를 주입하고 `channel-receive-loops.ts`가 `wait(0)` 뒤 `recv(1)`을 호출 | readiness가 false이면 다음 receive를 발행하지 않음 |
| ClientServer control DEALER | `channel-socket-registry.ts`의 physical connection별 Poller | control drain의 각 receive 직전에 readiness 재확인 |
| Fanout SUB | `ZLinkSubscriberReceiveLoop`가 adapter Poller를 소유 | 기존 nonblocking receive 전에 Poller를 사용 |
| Framework 내부 raw RouteMesh ROUTER·DEALER | `node-raw-binding-port.ts`의 socket port별 Poller | `receiveRecord()`가 readiness 후 native nonblocking receive를 호출 |

Monitor는 Framework runtime에서 `onEvent` 경로로만 사용되며, Framework production call path에서
monitor `recv()` 호출은 확인되지 않았다. 해당 adapter의 raw compatibility `recv()`와 직접 binding
사용자 경로는 이번 Framework ingress 범위와 구분했다.

STREAM의 steady-state receive는 binding public `Received` envelope을 재사용한다. frame reassembler는
수신 part가 단독으로 완전한 frame인 경우 `Buffer` view를 사용하고, part 경계를 넘는 경우에만
reusable storage로 조립한다. decoded structural header를 별도 `Message`로 만들지 않고, payload만
application queue에 소유 객체로 넘긴다. Poller와 `PollEvents`도 socket별로 한 번 만들고 종료 시
소유 계층에서 해제한다. Core source·public header·native Core API/ABI와 binding public API는 수정하지
않았으며, 현재 package는 Core 11.1.0을 사용한다.

## 초기 후속 검증 결과(수정 전 candidate)

아래 결과는 Codex review에서 추가 수정하기 전 candidate의 기록이다. 이후
`start()` 중복 monitor 등록, stale buffered state, control frame HWM 회계와 segmented frame
allocation을 보완했으므로 최신 결과는 아래의 `Codex review 결과와 최신 검증` 절을 기준으로 한다.

| 명령 | 결과 |
|---|---:|
| `node --test test/contract/stream-session-runtime.test.js` | 42/42 PASS |
| `node --test test/contract/stream-runtime.test.js` | 89/89 PASS |
| `node --test test/contract/backend-contract.test.js` | 36/36 PASS |
| `node --test test/contract/inbound-dispatch-budget.test.js test/contract/application-hwm.test.js` | 12/12 PASS |
| `node --test test/contract/client-server-location-runtime.test.js` | 22/22 PASS |
| `node --test test/contract/startup-validation.test.js` | 22/22 PASS |
| `node --test test/contract/drain-control.test.js` | 25/25 PASS |
| `node --test test/contract/channel-client.test.js` | 92/93 PASS |
| `node --test test/contract/nestjs-module.test.js` | 58/60 PASS |
| `npm run verify:m6a-runtime` | 26/26 PASS |
| 변경 source targeted ESLint | PASS |
| `git diff --check` | PASS |

`channel-client.test.js`의 1건과 `nestjs-module.test.js`의 2건은 모두 기존
`ZLinkLocationWriteIntent is not defined` 오류로 실패했다. 오류 위치는
`runtime/locations/location-store-repository.ts`의 type-only import 사용이며, 이번 Poller·STREAM·HWM
변경이 원인이 아니다. 전체 TypeScript 명령도 변경 파일 오류 없이 종료했지만, 기존
`rewriteActorAuthorityOwner` export 누락, Location Store 결과 narrowing 6건과 같은
`ZLinkLocationWriteIntent` import 2건 때문에 exit code 2를 반환했다.

위 결과는 초기 candidate의 회귀 기록이며, 현재 완료 판정의 근거로 사용하지 않는다. E2E process와
sample은 사용자가 지정한 후속 범위이므로 실행하지 않았다.

## Codex review 결과와 최신 검증

현재 Node runtime의 STREAM·socket Poller·Application HWM 범위에서 Critical·High·Medium 수준의
미해결 finding은 없다. 이번 review는 현재 working tree의 production call path, binding public API,
ownership, queue admission과 관련 contract test를 Codex 기준으로 다시 대조한 결과다.

| Codex review finding | 수정 결과 | 선택한 이유 |
|---|---|---|
| `start()` 반복 호출 때 monitor handler가 중복 등록될 수 있음 | `stopped`와 기존 receive loop를 먼저 확인하고 한 번만 등록 | monitor callback 증폭과 불필요한 dispatch를 lifecycle 경계에서 차단 |
| buffered frame을 꺼내는 동안 session state가 교체되어도 session id 존재만 확인함 | `receiveStates.get(sessionId) === state` 객체 동일성 확인 | 이전 state가 새 session에 영향을 주는 stale dispatch를 차단 |
| segmented frame에서 header와 payload를 별도 할당할 수 있음 | 하나의 backing `Buffer`를 만들고 두 영역을 view로 사용 | 경계가 넘는 frame에서 allocation·copy를 한 번으로 제한 |
| control frame이 application HWM 회계에 포함될 수 있음 | control payload를 application payload budget에서 제외 | HWM 계약의 application payload 의미를 유지하고 heartbeat가 admission을 소비하지 않게 함 |
| ClientServer monitor setup 실패 때 새 Poller와 dealer가 남을 수 있음 | Poller·monitor·dealer 생성 단계별 cleanup을 소유 계층에서 수행 | reconnect 또는 반복된 startup failure에서 native 자원 누적을 방지 |

두 가지 설계 대안을 비교했다. Poller는 모든 socket을 하나의 공유 객체로 묶는 방식과 socket별로
소유하는 방식을 비교한 뒤, receive loop와 close 책임이 같은 계층에 있는 socket별 Poller를 선택했다.
HWM은 frame 시작 시 payload를 예약하는 방식과 frame 조립·header 확인 후 admission하는 방식을
비교한 뒤, 계약의 complete message application payload 순서와 segmented frame의 마지막 `recv`
요구를 함께 만족하는 후자를 선택했다. 이 선택으로 Framework가 Core callback을 우회하거나 private
binding API를 호출하지 않는다.

직접 socket을 수신하는 production 경로는 다음처럼 모두 `PollIn` readiness 확인 후 non-blocking
receive를 호출한다.

- STREAM application ingress: public `recv(DONT_WAIT)`
- Channel·Route ROUTER, ClientServer control DEALER, Fanout SUB: public socket receive/subscribe
- Framework 내부 raw RouteMesh ROUTER·DEALER: binding public raw receive surface

STREAM은 packet callback ingress를 등록하지 않는다. binding public `Received`는 stream socket에서만
재사용하고, Framework가 다음 receive 전에 이전 envelope을 닫는다. 단독 frame은 입력 `Buffer` view를
사용하며, 경계를 넘는 frame만 조립 storage를 사용한다. Spot의 `recvRoute`·lifecycle·actor 관련
메서드는 socket receive가 아니라 binding-owned high-level service queue surface이므로, 이를 Poller
socket 경로로 바꾸기 위해 private API나 reflection을 추가하지 않았다.

최신 candidate 검증 결과는 다음과 같다.

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `node --test test/contract/stream-session-runtime.test.js` | 48/48 PASS |
| Poller·HWM·backend·channel·startup·Nest targeted contract suite | 360/360 PASS |
| `npm run verify:m6a-runtime` | 26/26 PASS |
| Framework TypeScript `--noEmit` | PASS |
| `npm run lint` | PASS |
| Node·progress 범위 `git diff --check` | PASS |

위 candidate 검증 뒤 `actor-manager`의 remote takeover 두 실패를 owning layer에서 수정했다. 해당
실패는 dirty worktree의 Location owner lease 판정과 release 경로가 explicit takeover 및 현재 lease
generation을 구분하지 못한 문제였다. 최신 결과와 남은 조건은 다음 절에 기록한다.

Core package provenance는 native Core 11.1.0이다. Core source·public header·ABI와 binding public
API는 이번 작업에서 수정하지 않았다. E2E process와 sample은 후속 작업으로 남겨 두었다.

## 미해결 actor·Location 회귀 수정 후 최신 결과

remote actor takeover에서 이전 owner lease가 아직 유효하더라도 명시적인 `Takeover`는 row/provider
CAS와 다음 owner lease가 동시 변경을 차단하므로 허용해야 한다. `NewClaim`만 기존 owner lease의
만료 여부를 확인하도록 분리했고, actor·Spot release는 row generation이 아니라 현재 owner lease
generation을 사용하도록 수정했다. 같은 owner의 재시작 cleanup도 현재 유효한 lease token일 때만
수행하며, legacy fixture에는 기존 descriptor generation을 fallback으로 적용했다. 이로써 stale
runtime이 새 owner row를 제거하지 않으면서, 새 lease가 이전 row를 정리할 수 있다.

| Gate | 결과 |
|---|---:|
| `node --test test/contract/actor-manager.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` | 111/111 PASS |
| E2E·sample source와 browser·integration을 제외한 현재 Node unit inventory | 59 files, 987/987 PASS |
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| `npm run verify:m6a-runtime` | 26/26 PASS |
| Node·progress 범위 `git diff --check` | PASS |

수정 후 `npm test` 전체 gate는 이전 actor takeover 실패를 더 이상 보고하지 않고,
`test/contract/e2e-scenario-header-gate.test.js`에서 중단된다. 이 static gate는 현재 scenario
inventory 171개를 읽었지만 verification header inventory는 빈 목록을 기대한다. 이는 사용자가
후속으로 지정한 E2E·sample 범위의 계약 inventory 문제이므로 이번 unit 단계에서 E2E source나
sample을 수정하지 않았다. 따라서 현재 판정은 Node unit 59/59와 production runtime gate 완료이며,
repository-wide `npm test`와 process E2E·sample 완료 판정은 보류한다.

## Codex 재리뷰 후 runtime 수정 및 검증

추가 재리뷰에서 byte HWM만으로는 payload가 0바이트인 STREAM frame의 직렬 dispatch 작업
metadata를 제한할 수 없는 경로를 확인했다. Framework는 host 내부 공용
`ZLinkStreamDispatchCapacity`를 두고 STREAM node 전체의 in-flight dispatch를 1,024개로
제한한다. capacity가 가득 차면 Poller readiness를 확인하더라도 다음 `recv`를 발행하지 않으며,
작업이 terminal 상태가 된 뒤에만 receive loop를 재개한다. Application HWM의 byte 회계에는
계속 application payload만 포함하므로 이 내부 guard가 Core HWM이나 Application HWM의 의미를
바꾸지 않는다. 해당 경로에 zero-byte frame 회귀 테스트를 추가했다.

같은 재리뷰에서 다음 항목도 다시 확인하고 보완했다.

- 알 수 없는 channel `contentType`은 빈 body인 reply에서도 JSON으로 fallback하지 않고
  `ProtocolError`로 처리한다.
- channel dispatch task의 rejected Promise는 detached rejection으로 남지 않으며, task runner
  error sink로 보고한 뒤 tracker가 소유한 task 집합에서 제거한다.
- Fanout infrastructure classification이 이미 decode한 header를 subscriber dispatcher에
  전달하여 같은 envelope header를 다시 JSON decode하지 않는다.
- segmented STREAM frame은 조립 storage에서 header와 payload view의 ownership을 넘겨 두 번째
  header+payload allocation/copy를 만들지 않는다.
- STREAM idle wait와 session monitor lookup에서 반복적인 Promise, timer closure, session
  snapshot allocation을 줄였다.
- owner cleanup은 scan version과 owner lease version을 함께 조건으로 하는 bounded atomic
  batch로 수행하고, scan cursor가 만료되면 해당 prefix를 첫 페이지부터 다시 읽는다.
- subscriber dispatch의 Application HWM accounting은 start가 실패한 경우에도 queued byte를
  취소하도록 ownership 경계를 명확히 했다.

최신 Node runtime·unit 검증 결과는 다음과 같다.

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| STREAM session·HWM·channel·fanout·location targeted suite | 245/245 PASS |
| `git diff --check` | PASS |

E2E와 sample은 사용자가 지정한 후속 범위이므로 이번 수정과 검증에 포함하지 않았다. 전체
`npm test`의 남은 실패는 runtime unit failure가 아니라 scenario header inventory gate이며,
현재 working tree의 171개 scenario와 gate가 기대하는 빈 inventory가 일치하지 않는 문제다.
문서 계약과 E2E/sample을 수정해 이 gate를 닫는 작업은 별도 범위로 남긴다.

## 비-E2E runtime·contract 재검증 — 2026-08-02 후속

이번 후속 범위는 사용자가 지정한 대로 E2E scenario source, sample과 process runner를 제외하고
Node Framework production runtime, public contract, package와 unit·contract test에 한정했다.

### 반영한 runtime·설계 변경

- 반복적인 `shift()`, `unshift()`와 `splice(0, ...)`로 큐를 앞에서 재배열하던 경로를 head/count와
  tombstone 방식으로 바꿨다. 대상은 async submit, dispatch budget, admission, stream session,
  inbound observer, received message, browser websocket, topology projection, mesh status,
  stateful mailbox follow, event-loop resource, raw mesh completion, stream capacity, actor
  post-commit queue와 SpotNode publish waiter다. compaction은 누적 head가 충분히 커진 경우에만
  실행한다.
- `runCpuWorker`는 작업마다 Worker를 만들고 버리는 방식에서 bounded elastic pool로 바꿨다.
  `minThreads`를 warm baseline으로 유지하고 `maxThreads`까지 필요할 때 확장하며,
  `idleTimeoutMs` 후 baseline을 초과한 유휴 Worker를 종료한다. queued cancellation은 slot을
  소비하지 않고, 실행 중 timeout/cancellation은 해당 Worker를 종료해 비협조적인 CPU 작업이
  자원을 점유하지 않도록 했다.
- worker 등록·runtime 양쪽에서 `minThreads`, `maxThreads`, `idleTimeoutMs`, `maxQueueLength`를
  같은 기본값과 검증 규칙으로 정규화했다. 기본값은 `0`, `max(2, availableParallelism())`,
  `30000`, `1024`이며 `maxThreads >= minThreads`를 검증한다.
- Nest options builder에서 runtime 내부 codec/duplicate-name helper를 prototype 표면에서
  제거하고 module-local helper로 숨겼다. contract test는 허용 member의 exact set을 검사한다.
- generated `ActorRefWire`에 의존하던 stream connector codec test를 현재 generated contract인
  `AuthenticateReq`/`AuthenticatePlayerRes` round-trip으로 정렬했다.
- Node workflow의 common guide 변경 경로를 push와 pull request filter에 포함했다.

### fresh evidence

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| E2E·sample·native integration을 제외한 contract inventory 59 files | 1001/1001 PASS |
| `contract-surface`, `nestjs-module`, `documentation-regression`, `stream-connector-codecs` | 121/121 PASS |
| `npm ls @zlink-systems/zlink --all` | 11.1.0 전 workspace clean |
| `bash scripts/verify_packaged_contract.sh` | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs` |
| Node·progress 범위 `git diff --check` | PASS |

`npm run verify:ci`는 build·typecheck·lint·Chromium과 비-E2E test를 통과한 뒤
`test/contract/e2e-scenario-header-gate.test.js`에서 common scenario 171개 누락을 보고하며
exit 1로 끝났다. 이 결과는 이번 비-E2E 변경의 실패가 아니라 사용자가 제외한 E2E inventory
gate가 아직 열려 있음을 뜻한다. E2E source·runner·sample을 수정해 이 gate를 우회하지 않았다.

## Worker slot reference 수명 보정 — 2026-08-02 후속

`entry-spot-serial-dispatch.test.js`의 24개 assertion은 통과했지만, CPU Worker의
`unref()`를 listener 등록 전에 호출하면 첫 `message` 뒤 `MessagePort`가 다시 참조되어
idle worker가 test process를 유지하는 문제가 확인됐다. 반대로 생성 직후 무조건 `unref()`하면
활성 작업의 Promise가 event loop 종료로 취소될 수 있다.

따라서 slot 생성 시 listener를 먼저 등록하고 baseline slot은 `unref()`하며, 작업을 할당할 때
`ref()`하고 결과·오류·정상 cancellation 뒤 idle 상태로 전환할 때 다시 `unref()`하도록 수명
경계를 조정했다. 작업 중에는 process 종료를 막고, 작업이 없을 때는 worker와 idle timer가
불필요한 process retention을 만들지 않는다. Worker를 작업마다 생성·종료하지 않는 bounded
pool 구조와도 일치한다.

| Gate | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| `timeout 10s node --test test/contract/entry-spot-serial-dispatch.test.js` | 24/24 PASS, exit 0 |
| 6개 비-E2E contract 파일 combined run | 152/152 PASS, exit 0 |

이 수정은 E2E·sample·process runner를 변경하지 않는다. 전체 CI의 E2E scenario inventory gate는
기존과 같이 사용자 제외 범위의 후속 조건으로 남긴다.

## 최종 비-E2E 재검증 — 2026-08-02

마지막 sample 정적 문서 변경 뒤 Node 대상 범위를 다시 확인했다.

| 명령 | 결과 |
|---|---:|
| `npm run build` | PASS |
| `npm run typecheck` | PASS |
| `npm run lint` | PASS |
| 6개 핵심 contract 파일 combined run | 148/148 PASS, exit 0 |
| `sample*.test.js` 중 `sample-regression.test.js` 제외 | 35/35 PASS, exit 0 |
| `npm ls @zlink-systems/zlink --all` | 11.1.0 clean |
| `bash scripts/verify_packaged_contract.sh` | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs` |
| Node 대상 `git diff --check` | PASS |

sample static 변경은 `9fb68d71e7a`, 해당 변경의 push 기록은 `c2d6c264666`으로 각각
path-limited commit하고 push했다. 이 최종 재검증 log도 Node 대상 경로만 다음 commit으로
추가한다. E2E scenario inventory, browser sample process와 native process는 실행·수정하지 않았다.

변경 source와 이 수명 검증 log는 `4f67c0eb47` 커밋으로, ledger의 현재 판정 보강은
`b2c5cfaeae` 커밋으로 각각 path-limited commit하고 `origin/agent/framework-contract-runtime-update`에
push했다. 두 commit 모두 다른 언어 workstream의 변경을 포함하지 않는다.
