# Node worker — Spot Actor Join / Transfer 적용

> 이 문서 하나로 Node(NestJS) framework의 Spot actor join/transfer 적용을 끝낼 수 있게 썼다.
> 계약 정본은 [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md),
> 검증 정본은 [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md),
> Node interface 정본은 [node/spec/handler-interfaces.ko.md](../../framework/node/spec/handler-interfaces.ko.md)다.
> 전 언어 현황은 [README.ko.md](README.ko.md).

## 0. Node 시작 상태 (문서 정본은 정렬됨, source는 P0에서 확인)

`handler-interfaces.ko.md`는 목표 정본과 정렬돼 있다. 하지만 실제
`framework/languages/node/packages/framework/src` public source가 아직 이 표면을 구현한다는 뜻은 아니다.
P0에서 source를 확인하고, 구형 `onActorJoin(actor, request)` 또는 adapter 등록 API 부재가 확인되면
P1에서 public source interface와 샘플 compile break까지 함께 고친다.

```ts
// user Spot / Entry Spot(재진입) admission → ZLinkSpotActorJoinResponse
onActorJoin(actorId: string, request: ZLinkMessage, signal?): Promise<ZLinkSpotActorJoinResponse>;
onJoinedActor(actor: ZLinkActor, signal?): Promise<void>;   // join 완료 신호

// transfer adapter (actor type별)
export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
  transferOut(actor: TActor, signal?: AbortSignal): Promise<ZLinkMessage>;
  transferIn(actorId: string, state: ZLinkMessage, signal?: AbortSignal): Promise<TActor>;
}
// 등록: state 이동이 필요한 actor type만 custom adapter 등록
addActorTransferAdapter<TActor extends ZLinkActor>(actorType: Type<TActor>, adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;
```

> 주의: admission이 반환하는 `ZLinkSpotActorJoinResponse`와, actor-side `JoinSpot` 결과
> `ZLinkActorJoinResult<TReply>`(resultCode + ActorRef + reply)는 **다른 타입**이다.

Node의 무게중심은 실제 public source interface 정렬 + runtime 실동작 audit·보정 + 샘플 + e2e + P5다.

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) Node 열에 반영.

- [x] SPOT runtime에서 local join이 `onActorJoin → onLeaveActor → onJoinedActor` 순서이고, join
      success가 `onJoinedActor` 완료 뒤에 resolve되는지.
- [x] remote 이동이 admission/commit 분리 + `ZLinkActorTransferAdapter` 호출로 되는지.
- [x] `addActorTransferAdapter` 등록·조회, adapter 미등록 기본 빈 state transfer, 기존
      `addStatelessActorTransfer` 제거 범위 확인.
- [x] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과를 이 문서 하단 `## 현황`에 정리하고 각 P를 gap 중심으로 좁힌다.

## P1. Interface / contract 정렬

- [x] 실제 source public interface를 목표 정본으로 변경:
  `onActorJoin(actorId, request, signal?)`, `ZLinkActorTransferAdapter<TActor>`,
  `addActorTransferAdapter`.
- [x] 기존 `onActorJoin(actor, request)` 호출부와 샘플 compile break를 정본 시그니처로 변경.
- [x] `onActorJoin(actorId, request)`이 actor id만 받고 actor instance나 route metadata를 받지
  않는지(정본 §3.1). 저수준 header로 admission 필드 노출 금지.
- [x] `transferOut/transferIn`이 domain state 변환만(정본 §6). id/type/route 검사·admission·membership·
  location·bound session 책임 아님.
- [x] adapter 등록 = state 이동이 필요한 actor type별 1개. 미등록 actor type은 기본 빈
  `ZLinkMessage` + factory 경로.
- [x] `ZLinkActorJoinAdmission`, `addStatelessActorTransfer`, lifecycle 기본 no-op API가 있으면 제거.
- [x] 필요 시 `handler-interfaces.ko.md` 서술을 정본 문구로 미세 정합(신규 계약 추가 아님).

## P2. Framework runtime 구현/보정

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `onActorJoin`(admission) accept → moving 표시 → source `onLeaveActor` → membership commit → target `onJoinedActor` → committed location → success. reject면 side effect 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. source `transferOut` → source `onLeaveActor` → commit(state) → target `transferIn` materialize → membership commit → `onJoinedActor` → committed location → commit ack → success. remote에서 `onCreateActor` 호출 안 함(정본 §7). |
| transfer adapter 미등록 | 실패가 아니다. source는 빈 `ZLinkMessage`로 이동하고 target은 actor factory/public 생성 경로로 materialize한다. |
| custom adapter 빈 state | `addActorTransferAdapter`가 등록되어 있고 `transferOut`이 빈 `ZLinkMessage`를 반환해도 정상 transfer다. |
| moving packet handoff | 정본 §3.4·§10. moving 중 source dispatch를 차단하면서 packet을 순서대로 보존하고, target replay 뒤 location을 공개한다. old ref는 기본 5초 동안 다음 hop으로 forward한 뒤 fail-fast한다. |
| pending admission deadline | 정본 §5.2(down signal 없이 deadline 정리). |
| 멱등 source cleanup | 정본 §5.1. |
| location pending/committed + fencing | 정본 §8. |
| bound session transfer | 정본 §9. |
| 실패 분류 | 정본 §10 표(Promise reject/throw 시점별 결과). |

## P3. 샘플 적용

- **local join**: `framework/languages/node/samples/Bingo.Ts`, `framework/languages/node/samples/TicTacToe.Ts`가 정본 순서로 동작하는지 확인.
  admission에서 room membership 확정 코드가 있으면 `onJoinedActor`로 이동.
- **remote transfer**: 다중 node 샘플(`framework/languages/node/samples/DeliveryDispatch.Ts`, 필요 시
  `framework/languages/node/samples/SupportChat.Ts`)에 `ZLinkActorTransferAdapter` 구현 +
  `addActorTransferAdapter` 등록을 추가한다. 옮길 domain state가 없는 actor는 기본 빈 state transfer를
  사용한다.
- 언어별 sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/node/e2e/SpotActorTransfer`(기존 `ToActorMessaging`=config-9,
`SpotService`, `YieldDispatch` 구조 참고).

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2 · session
  gateway 2 · transfer controller(실제 app HTTP endpoint) · consumer(HTTP client + stream connector).
- client는 언어별 HTTP client wrapper, 상태 변경 관찰은 stream connector. framework host 구성·내부
  client·test-only helper 직접 사용 금지(e2e README 코드 규칙).
- 시나리오(P1은 ST-C3·ST-D2·ST-F4/F5/F6, 나머지는 P0): ST-A1/A2/A3,
  ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2, ST-F1/F2/F3/F4/F5/F6.
  - ST-B3 = adapter 미등록 actor type의 기본 빈 state transfer 성공.
  - ST-B4 = custom adapter가 빈 state를 반환해도 성공(`actor-empty-state`, target joined 이후 별도 store에서 domain state 로드 marker).
- evidence: callback order marker(`admission, transfer_out(_empty), leave, commit_request,
  transfer_in(_empty), joined, domain_state_loaded, location_committed, commit_ack, source_cleanup`),
  admission input snapshot(actor id만, instance 없음), transfer state marker, packet handler marker, bound session
  snapshot. 로그 `log/` 파일 + message flow `key_transitions`.
- config-10 단독 runner가 통과한 뒤 언어별 e2e 전체 runner를 실행해 기존 config가 깨지지 않는지 확인.

## P5. POSD/DDD 리팩토링 루프

config-10 P0 전부 + §12 contract 테스트(README §3.1 매핑)가 그린이 된 뒤 시작. **codex 에이전트 리뷰 → 의미있는 항목 반영
→ 회귀 그린 → 재리뷰**를 CONVERGED까지 반복(README §6).

- Node 특유 관심: framework spot runtime god-file 분할, adapter 등록/조회 응집, admission/commit/
  transfer 책임 분리, Promise 기반 moving-dispatch·pending admission·bound session owner 단일화,
  구 codec 잔재 제거.
- hot 경로 변경은 baseline vs patched 벤치 증거 첨부.
- 라운드별 반영·수렴 기록.

## 체크리스트 (Node)

### 계약 항목(§12)
- [x] 1~18 (README §4 Node 열). runtime audit와 contract/E2E 증거로 모두 `✅` 확정.

### interface/문서
- [x] 실제 source public interface를 actor id admission/adapter 모델로 변경
- [x] 기존 샘플/e2e compile break 정리
- [x] `onActorJoin`=actor id admission(instance/route metadata 없음), 반환 `ZLinkSpotActorJoinResponse` 확인
- [x] `ZLinkActorTransferAdapter` 책임 경계 확인
- [x] `addActorTransferAdapter`/기본 빈 state transfer 정책 확인

### 샘플
- [x] Bingo.Ts/TicTacToe.Ts local join 순서 정합
- [x] DeliveryDispatch.Ts는 domain state가 없어 기본 빈 state transfer 사용, state가 있는 Bingo/TicTacToe는 adapter 등록·순서 정합
- [x] sample 전체 runner 통과

### e2e config-10 (`framework/languages/node/e2e/SpotActorTransfer`)
- [x] ST-A1 · [x] ST-A2 · [x] ST-A3
- [x] ST-B1 · [x] ST-B2 · [x] ST-B3 · [x] ST-B4
- [x] ST-C1 · [x] ST-C2 · [x] ST-C3(P1)
- [x] ST-D1 · [x] ST-D2(P1)
- [x] ST-E1 · [x] ST-E2
- [x] ST-F1 · [x] ST-F2 · [x] ST-F3
- [x] ST-F4(P1) · [x] ST-F5(P1) · [x] ST-F6(P1)
- [x] e2e 전체 runner 통과

### P5
- [x] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)
- [x] handoff H6 재리뷰 CONVERGED(request framing·replay·forwarding owner 단일화)

## 함정 (Node)

- `onActorJoin`은 actor id와 request만 받고 `ZLinkSpotActorJoinResponse`(≠
  `ZLinkActorJoinResult<TReply>`)를 반환한다. membership·location·client event·instance 접근 금지.
- Promise 기반이라 success resolve 시점이 `onJoinedActor` 완료 뒤인지 특히 주의(early resolve 금지).
- adapter 미등록은 실패가 아니라 기본 빈 state transfer다.
- 같은 node join에서 `transferOut/transferIn`을 호출하면 안 된다(인스턴스 그대로 이동).
- Track D·E는 실제 location store/stream connector로 관찰(내부 store key 직접 읽어 판정 금지).

## 현황

2026-07-10 현재 live source와 실제 runner를 기준으로 Node 적용을 완료했다.

- **public contract**: `onActorJoin(actorId, request)`은 admission만 담당한다. state가 필요한 actor는
  `ZLinkActorTransferAdapter`를 등록하고, 등록하지 않은 actor는 framework의 빈 state와 actor factory
  경로로 이동한다. core builder와 NestJS builder는 같은 등록 정책과 중복 검사를 사용한다.
- **두 단계 remote transfer**: remote 이동은 admission과 commit을 별도 request로 보낸다. 두 단계
  protocol을 사용할 수 없는 one-phase remote 경로는 target actor를 미리 만들지 않고 명시적으로
  실패한다. 같은 transfer id의 동시 재전송은 이미 진행 중인 admission/commit Promise를 공유하므로
  callback, `transferIn`, membership commit을 반복하지 않는다.
- **완료 경계와 rollback**: local·remote 모두 `onJoinedActor`가 끝난 뒤에만 location과 success를
  공개한다. local 실패는 membership을 되돌리고, remote target 실패는 materialize한 native actor와
  bound-session target까지 정리한다. native destroy가 일시적으로 실패하면 dispatch가 차단된 tombstone을
  남기고 정리가 성공할 때까지 재시도한다.
  commit 뒤 source-side binding 갱신 실패는 이미 끝난 transfer를 실패로 바꾸지 않고 별도 재시도로
  복구한다.
- **moving과 location**: source actor state와 source Spot membership guard는 begin/cancel 보상 경계에서
  함께 갱신한다. takeover 알림이 commit reply보다 늦게 도착해도 moving 또는 remote target으로 전환한
  source state를 지우지 않는다. source location release는 원래 generation을 유지한 채 재시도하므로
  target의 새 generation을 삭제할 수 없다.
- **bound session**: target은 location takeover 뒤 commit ack 전에 session gateway에 새 ownership
  generation과 ActorRef를 보낸다. 따라서 첫 target push보다 stale source push가 먼저 도착해도 폐기된다.
  E1/E2는 같은 stream connector binding이 성공한 이동 뒤 target push를 받고, 실패한 이동 뒤에는 기존
  binding을 유지하는지 확인한다.
- **in-flight handoff**: `ZLinkActorHandoffCoordinator`가 moving ingress, commit backlog, trailing
  forwarding, window와 mapping 축출을 한 곳에서 소유한다. target은 backlog를 actor queue에서 replay한
  뒤 location을 공개한다. request는 request sequence·flags·correlation 정보를 그대로 보존하고, caller
  timeout이 먼저 끝난 late reply는 기존 orphan 처리 경로를 따른다.
- **샘플**: Bingo와 TicTacToe는 domain state adapter를 등록했다. DeliveryDispatch는 옮길 domain state가
  없어 기본 빈 state transfer를 사용한다. SupportChat, GameQuest, ShoppingMall을 포함한 전체 sample
  runner가 통과했다.
- **config-10**: `framework/languages/node/e2e/SpotActorTransfer`에 ST-A1~ST-F6 20개 시나리오를 구현했다.
  pending deadline은 ST-C1의 공개 결과와 deadline 뒤 late commit을 거부하는 contract test로 함께
  증명한다. source cleanup은 ST-B2의 성공 유지 결과와 location remove가 한 번 실패한 뒤 같은
  generation으로 재시도되는 contract test로 검증한다. 검증 전용 public API나 application timer marker는
  추가하지 않았다. Track F 전체 증거는 `log/20260710-200221-3864800`이다.

## P5 수렴 기록

### 1차 리뷰 — NOT CONVERGED

local/native joined 실패 rollback, source cleanup 재시도, target rollback, moving 시작 보상,
bound-session stale ref, transfer id 멱등성, E2E의 application timer marker, lazy coordinator 책임을
의미 있는 항목으로 판정했다. 모든 항목을 runtime owner와 contract test에서 수정했다.

### 2차 리뷰 — NOT CONVERGED

남은 one-phase remote 경로, post-commit binder 오류 경계, native rollback 정리 실패, 동시 transfer id
재시도, 첫 target push 이전 generation fence, moving cancel 보상, wire schema 중복을 확인했다. one-phase
경로를 제거하고, post-commit binder와 rollback reconciler를 분리했으며, target commit control update와
공유 admission/commit task를 추가했다. sender/receiver가 packet 상수와 wire payload schema를 함께 쓰게
정리하고 shallow lazy wrapper는 native coordinator의 node provider로 대체했다.

### 3차 리뷰 — CONVERGED

2차 항목을 현재 source와 회귀 테스트로 다시 대조한 결과 의미 있는 P0/P1/P2 항목이 남지 않았다.
별도 리뷰 agent 재실행은 인증 토큰 갱신 오류로 시작하지 못해 main Codex가 같은 체크리스트로
최종 재검토했다. cleanup 내부 상태를 E2E application이 직접 읽는 helper를 만들지 않고, 배포형 공개
결과와 in-process failure-injection contract test를 조합하는 검증 경계를 유지했다.

두 가지 설계안을 비교한 결정은 다음과 같다.

- moving guard는 모든 dispatch owner를 새 lease 객체로 바꾸는 안과 현재 actor/Spot 상태를 하나의
  보상 transaction으로 묶는 안을 비교했다. hot dispatch 경계를 넓히지 않고 begin/cancel 실패를 모두
  보상하는 두 번째 안을 선택했다.
- stale bound push는 push마다 location store를 조회하는 안과 commit 시 ownership generation을 먼저
  전달하는 안을 비교했다. store 조회를 hot path에 넣지 않는 generation control update를 선택했다.
- rollback은 manager state를 즉시 버리는 안과 moving tombstone을 유지한 채 native cleanup을 재시도하는
  안을 비교했다. 남은 native actor의 owner 정보를 잃지 않는 두 번째 안을 선택했다.
- 중복 transfer는 duplicate를 오류로 끝내는 안과 transfer id별 작업 결과를 공유하는 안을 비교했다.
  transport timeout과 실제 처리 완료가 겹쳐도 side effect가 반복되지 않는 공유 task 방식을 선택했다.

bound-session receive hot path는 동일 build에서 unversioned 경로와 ownership generation 경로를 번갈아
8라운드, 라운드당 50,000회 실행했다. 중앙값은 각각 624,967 ops/s와 614,844 ops/s로, generation
검사 경로의 차이는 1.62%였다. 이 측정은 이전 commit의 binary 비교가 아니라 같은 runtime의 기존
unversioned 입력과 새 versioned 입력을 비교한 microbenchmark다.

현재 검증:

- `npm run build && npm run typecheck && npm run lint` — 통과
- handoff 인접 actor/spot/location/stream/NestJS contract tests — 263/263 통과
- `test/contract/actor-handoff.test.js` — 6/6 통과
- `e2e/SpotActorTransfer/run_e2e.sh all` — ST-A1~ST-E2 14/14 통과,
  `log/20260710-152609-2661347`
- `e2e/SpotActorTransfer/run_e2e.sh ST-F1,ST-F2,ST-F3,ST-F4,ST-F5,ST-F6` — 6/6 통과,
  `log/20260710-200221-3864800`
- `samples/run_samples.sh` — TicTacToe, Bingo, DeliveryDispatch, SupportChat, GameQuest,
  ShoppingMall 6/6 통과
- `e2e/run_e2e_all.sh` — 10개 config 전체 통과, 763초. RL-A3는 요청마다 만든 runtime의
  `app.close()`를 5초 뒤 버리던 lifecycle 누수를 제거한 뒤 단독 4회와 전체 누적 실행에서 통과했다.
- `npm test` — build, typecheck, lint, contract, sample regression, smoke gate 전체 통과. sample 전체
  runner를 포함한 기존 최종 gate다. 이번 handoff 변경 뒤에는 반복 실행 중 channel 파일이 간헐적으로
  대기해 channel 57/57, sample 6/6, 나머지 runtime gate를 분리 실행해 모두 통과시켰다.
