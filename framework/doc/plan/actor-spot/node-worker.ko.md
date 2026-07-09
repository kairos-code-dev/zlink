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
export interface ZLinkActorJoinAdmission {
  readonly actorId: string;
  readonly actorType: Type<ZLinkActor>;
  readonly sourceSpotRid: RoutingId;
  readonly targetSpotRid: RoutingId;
  readonly sourceNodeRid: RoutingId;
  readonly targetNodeRid: RoutingId;
}
// user Spot / Entry Spot(재진입) admission → ZLinkSpotActorJoinResponse
onActorJoin(admission: ZLinkActorJoinAdmission, request: ZLinkMessage, signal?): Promise<ZLinkSpotActorJoinResponse>;
onJoinedActor(actor: ZLinkActor, signal?): Promise<void>;   // join 완료 신호

// transfer adapter (actor type별)
export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
  transferOut(actor: TActor, signal?: AbortSignal): Promise<ZLinkMessage>;
  transferIn(actorId: string, state: ZLinkMessage, signal?: AbortSignal): Promise<TActor>;
}
// 등록: 기본(stateless) / custom adapter
addStatelessActorTransfer<TActor extends ZLinkActor>(actorType: Type<TActor>): this;
addActorTransferAdapter<TActor extends ZLinkActor>(actorType: Type<TActor>, adapterType: Type<ZLinkActorTransferAdapter<TActor>>): this;
```

> 주의: admission이 반환하는 `ZLinkSpotActorJoinResponse`와, actor-side `JoinSpot` 결과
> `ZLinkActorJoinResult<TReply>`(resultCode + ActorRef + reply)는 **다른 타입**이다.

Node의 무게중심은 실제 public source interface 정렬 + runtime 실동작 audit·보정 + 샘플 + e2e + P5다.

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) Node 열에 반영.

- [ ] SPOT runtime에서 local join이 `onActorJoin → onLeaveActor → onJoinedActor` 순서이고, join
      success가 `onJoinedActor` 완료 뒤에 resolve되는지.
- [ ] remote 이동이 admission/commit 분리 + `ZLinkActorTransferAdapter` 호출로 되는지.
- [ ] `addStatelessActorTransfer`/`addActorTransferAdapter` 등록·조회, 빈 state transfer vs 미등록 실패
      구분.
- [ ] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과를 이 문서 하단 `## 현황`에 정리하고 각 P를 gap 중심으로 좁힌다.

## P1. Interface / contract 정렬

- [ ] 실제 source public interface를 목표 정본으로 변경:
  `onActorJoin(admission, request, signal?)`, `ZLinkActorTransferAdapter<TActor>`,
  `addStatelessActorTransfer`, `addActorTransferAdapter`.
- [ ] 기존 `onActorJoin(actor, request)` 호출부와 샘플 compile break를 정본 시그니처로 변경.
- [ ] `onActorJoin(admission, request)`이 `ZLinkActorJoinAdmission`만 받고 actor instance를 받지
  않는지(정본 §3.1). 저수준 header로 admission 필드 노출 금지.
- [ ] `transferOut/transferIn`이 domain state 변환만(정본 §6). id/type/route 검사·admission·membership·
  location·bound session 책임 아님.
- [ ] adapter 등록 = remote transfer 지원, actor type별 1개, 미등록 시 remote 시작 전 실패. stateless
  등록 시 빈 `ZLinkMessage` + factory 경로.
- [ ] 필요 시 `handler-interfaces.ko.md` 서술을 정본 문구로 미세 정합(신규 계약 추가 아님).

## P2. Framework runtime 구현/보정

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `onActorJoin`(admission) accept → moving 표시 → source `onLeaveActor` → membership commit → target `onJoinedActor` → committed location → success. reject면 side effect 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. source `transferOut` → source `onLeaveActor` → commit(state) → target `transferIn` materialize → membership commit → `onJoinedActor` → committed location → commit ack → success. remote에서 `onCreateActor` 호출 안 함(정본 §7). |
| transfer adapter 미등록 | remote 시작 전 실패, source `onLeaveActor` 없음. |
| **stateless adapter** | `addStatelessActorTransfer`로 빈 `ZLinkMessage` + factory/public 생성. 빈 state transfer와 미등록 실패 구분(정본 §6). |
| moving dispatch 차단 | 정본 §3.4. |
| pending admission deadline | 정본 §5.2(down signal 없이 deadline 정리). |
| 멱등 source cleanup | 정본 §5.1. |
| location pending/committed + fencing | 정본 §8. |
| bound session transfer | 정본 §9. |
| 실패 분류 | 정본 §10 표(Promise reject/throw 시점별 결과). |

## P3. 샘플 적용

- **local join**: `framework/languages/node/samples/Bingo.Ts`, `framework/languages/node/samples/TicTacToe.Ts`가 정본 순서로 동작하는지 확인.
  admission에서 room membership 확정 코드가 있으면 `onJoinedActor`로 이동.
- **remote transfer**: 다중 node 샘플(`framework/languages/node/samples/DeliveryDispatch.Ts`, 필요 시
  `framework/languages/node/samples/SupportChat.Ts`)에 `ZLinkActorTransferAdapter` 구현 + 등록(state 옮기면
  `addActorTransferAdapter`, 안 옮기면 `addStatelessActorTransfer`) 추가, node 간 이동 정본 순서 정합.
- 언어별 sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/node/e2e/SpotActorTransfer`(기존 `ToActorMessaging`=config-9,
`SpotService`, `YieldDispatch` 구조 참고).

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2 · session
  gateway 2 · transfer controller(실제 app HTTP endpoint) · consumer(HTTP client + stream connector).
- client는 언어별 HTTP client wrapper, 상태 변경 관찰은 stream connector. framework host 구성·내부
  client·test-only helper 직접 사용 금지(e2e README 코드 규칙).
- 시나리오(P1은 ST-C3·ST-D2뿐, 나머지 전부 P0): ST-A1/A2/A3, ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2.
  - ST-B3 = adapter 미등록 실패(`actor-no-adapter`), ST-B4 = **stateless adapter + 빈 state transfer
    성공**(`actor-empty-state`, target joined 이후 별도 store에서 domain state 로드 marker).
- evidence: callback order marker(`admission, transfer_out(_empty), leave, commit_request,
  transfer_in(_empty), joined, domain_state_loaded, location_committed, commit_ack, source_cleanup`),
  admission input snapshot(instance 없음), transfer state marker, packet handler marker, bound session
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

### 계약 항목(§11)
- [ ] 1~10 (README §4 Node 열). runtime audit로 각 항목 `✅`/`🚫` 확정.

### interface/문서
- [ ] 실제 source public interface를 admission/adapter 모델로 변경
- [ ] 기존 샘플/e2e compile break 정리
- [ ] `onActorJoin`=admission identity(instance 없음), 반환 `ZLinkSpotActorJoinResponse` 확인
- [ ] `ZLinkActorTransferAdapter` 책임 경계 확인
- [ ] adapter 등록(stateless/custom)/미등록 정책 확인

### 샘플
- [ ] Bingo.Ts/TicTacToe.Ts local join 순서 정합
- [ ] DeliveryDispatch.Ts(+SupportChat.Ts) remote transfer adapter 등록·순서 정합
- [ ] sample 전체 runner 통과

### e2e config-10 (`framework/languages/node/e2e/SpotActorTransfer`)
- [ ] ST-A1 · [ ] ST-A2 · [ ] ST-A3
- [ ] ST-B1 · [ ] ST-B2 · [ ] ST-B3 · [ ] ST-B4
- [ ] ST-C1 · [ ] ST-C2 · [ ] ST-C3(P1)
- [ ] ST-D1 · [ ] ST-D2(P1)
- [ ] ST-E1 · [ ] ST-E2
- [ ] e2e 전체 runner 통과

### P5
- [ ] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

## 함정 (Node)

- `onActorJoin`은 `ZLinkActorJoinAdmission`만 받고 `ZLinkSpotActorJoinResponse`(≠
  `ZLinkActorJoinResult<TReply>`)를 반환한다. membership·location·client event·instance 접근 금지.
- Promise 기반이라 success resolve 시점이 `onJoinedActor` 완료 뒤인지 특히 주의(early resolve 금지).
- **미등록 adapter 실패(ST-B3)와 빈 state transfer(ST-B4)는 다른 결과다.**
- 같은 node join에서 `transferOut/transferIn`을 호출하면 안 된다(인스턴스 그대로 이동).
- Track D·E는 실제 location store/stream connector로 관찰(내부 store key 직접 읽어 판정 금지).
