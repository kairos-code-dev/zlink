# Java(+Kotlin) worker — Spot Actor Join / Transfer 적용

> 이 문서 하나로 Java framework와 Kotlin surface의 Spot actor join/transfer 적용을 끝낼 수 있게 썼다.
> 계약 정본은 [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md),
> 검증 정본은 [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md),
> Java interface 정본은 [java/spec/handler-interfaces.ko.md](../../framework/java/spec/handler-interfaces.ko.md)
> (Kotlin은 java surface 공유 + suspend/Flow idiom)다. 전 언어 현황은 [README.ko.md](README.ko.md).

Kotlin은 Java 런타임을 공유하므로 **런타임 구현은 Java 한 벌**, Kotlin은 surface 미러 + suspend/Flow
idiom + Kotlin 샘플(`:kotlin:...` gradle 모듈)/`e2e-kotlin` 검증이다.

## 0. Java 시작 상태 (문서 정본은 정렬됨, source는 P0에서 확인)

`handler-interfaces.ko.md`는 목표 정본과 정렬돼 있다. 하지만 실제
`framework/languages/java` public source가 아직 이 표면을 구현한다는 뜻은 아니다. P0에서 source를
확인하고, 구형 `onActorJoin(TActor actor, ...)` 또는 adapter 등록 API 부재가 확인되면 P1에서 Java
public source interface, Kotlin surface, 샘플 compile break를 함께 고친다.

```java
// user Spot / Entry Spot(재진입) admission → ZLinkSpotActorJoinResponse
ZLinkSpotActorJoinResponse onActorJoin(String actorId, /* request, ct */ ...);
void onJoinedActor(/* actor, ct */ ...);   // join 완료 신호

public interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
    ZLinkMessage transferOut(TActor actor, CancellationToken ct);
    TActor transferIn(String actorId, ZLinkMessage state, CancellationToken ct);
}
// 등록: state 이동이 필요한 actor type만 custom adapter 등록
void addActorTransferAdapter(String actorType, Class<? extends ZLinkActorTransferAdapter<?>> adapterType);
```

> 주의: admission이 반환하는 `ZLinkSpotActorJoinResponse`와, actor-side `JoinSpot` 결과
> `ZLinkActorJoinResult<TReply>`(resultCode + ActorRef + reply)는 **다른 타입**이다. `onActorJoin`은
> 전자를 반환한다.

Java/Kotlin의 무게중심은 실제 public source interface 정렬 + runtime 실동작 audit·보정 + Kotlin
surface 미러 + 샘플 + e2e + P5다.

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) Java·Kotlin 열에 반영.

- [ ] SPOT runtime(예: `ZLinkSpotRuntime` 등 실제 파일 확인)에서 local join이
      `onActorJoin → onLeaveActor → onJoinedActor` 순서이고, join success(`submit/await`)가
      `onJoinedActor` 완료 뒤에 완료되는지.
- [ ] remote 이동이 admission/commit 분리 + `ZLinkActorTransferAdapter` 호출로 되는지.
- [ ] `addActorTransferAdapter` 등록·조회, adapter 미등록 기본 빈 state transfer, 기존
      `addStatelessActorTransfer` 제거 범위 확인.
- [ ] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- [ ] Kotlin surface가 `onActorJoin/onJoinedActor/onLeaveActor` + transfer adapter를 suspend로 노출하고
      `<TActor extends ZLinkActor>` 제너릭이 단일 유지되는지(2-인터페이스 분리 금지).
- audit 결과를 이 문서 하단 `## 현황`에 정리하고 각 P를 gap 중심으로 좁힌다.

## P1. Interface / contract 정렬

- [ ] 실제 Java public source interface를 목표 정본으로 변경:
  `onActorJoin(String actorId, ZLinkMessage, CancellationToken)`,
  `ZLinkActorTransferAdapter<TActor>`, `addActorTransferAdapter`.
- [ ] Kotlin surface를 같은 의미의 suspend API로 미러링하고 기존 sample/e2e compile break를 정리.
- [ ] `onActorJoin(actorId, request)`이 actor id만 받고(정본 §3.1) actor instance나 route metadata를
  받지 않는지. 저수준 header로 admission 필드 노출 금지. 반환은 `ZLinkSpotActorJoinResponse`.
- [ ] `transferOut/transferIn`이 domain state 변환만(정본 §6).
- [ ] adapter 등록 = state 이동이 필요한 actor type별 1개. 미등록 actor type은 기본 빈
  `ZLinkMessage` + factory 경로.
- [ ] `ZLinkActorJoinAdmission`, `addStatelessActorTransfer`, lifecycle 기본 no-op API가 있으면 제거.
- [ ] Kotlin: 위 surface를 suspend 형태로 미러, 제너릭 단일 유지(런타임 raw+suppress는 기존 4곳만).

## P2. Framework runtime 구현/보정 (Java 한 벌)

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `onActorJoin`(admission) accept → moving 표시 → source `onLeaveActor` → membership commit → target `onJoinedActor` → committed location → success. reject면 side effect 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. source `transferOut` → source `onLeaveActor` → commit(state) → target `transferIn` materialize → membership commit → `onJoinedActor` → committed location → commit ack → success. remote에서 `onCreateActor` 호출 안 함(정본 §7). |
| transfer adapter 미등록 | 실패가 아니다. source는 빈 `ZLinkMessage`로 이동하고 target은 actor factory/public 생성 경로로 materialize한다. |
| custom adapter 빈 state | `addActorTransferAdapter`가 등록되어 있고 `transferOut`이 빈 `ZLinkMessage`를 반환해도 정상 transfer다. |
| moving dispatch 차단 | 정본 §3.4. |
| pending admission deadline | 정본 §5.2(down signal 없이 deadline 정리). |
| 멱등 source cleanup | 정본 §5.1. |
| location pending/committed + fencing | 정본 §8. |
| bound session transfer | 정본 §9. |
| 실패 분류 | 정본 §10 표(`CompletionStage` 예외/throw 시점별 결과). |

> 기동 레이스 주의: java auto-connect/기동 순서 감사가 진행 중이므로 remote transfer e2e에서 node
> 기동 후 첫 이동 전 route 준비를 확인한다(rid 뒤집기 실험법 참고).

## P3. 샘플 적용

- **local join**: `framework/languages/java/samples/java` 및 `framework/languages/java/samples/kotlin`의 Bingo/TicTacToe가
  정본 순서로 동작하는지 확인. admission에서 room membership 확정 코드가 있으면 `onJoinedActor`로 이동.
- **remote transfer**: 다중 node 샘플(DeliveryDispatch, 필요 시 SupportChat)에
  `ZLinkActorTransferAdapter` 구현 + `addActorTransferAdapter` 등록을 추가한다. 옮길 domain state가 없는
  actor는 기본 빈 state transfer를 사용한다.
- Kotlin 샘플(`framework/languages/java/samples/kotlin/...`, gradle 모듈 `:kotlin:Bingo:...` 등,
  `framework/languages/java/samples/settings.gradle.kts` 등록, kotlin 2.1.0)도 같은 시나리오로 미러.
- Java/Kotlin sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/java/e2e/SpotActorTransfer`(Java) + `framework/languages/java/e2e-kotlin/...`(Kotlin).
기존 `ToActorMessaging`=config-9 구조 참고.

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2 · session
  gateway 2 · transfer controller(실제 app HTTP endpoint, Spring Boot + ZLink 채널) · consumer.
- client는 HTTP client wrapper + stream connector. framework host 구성·내부 client·test-only helper
  직접 사용 금지(e2e README 코드 규칙).
- 시나리오(P1은 ST-C3·ST-D2뿐, 나머지 전부 P0): ST-A1/A2/A3, ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2.
  - ST-B3 = adapter 미등록 actor type의 기본 빈 state transfer 성공.
  - ST-B4 = custom adapter가 빈 state를 반환해도 성공(`actor-empty-state`, target joined 이후 별도 store에서 domain state 로드 marker).
  - Kotlin은 같은 시나리오를 suspend/Flow idiom으로 미러.
- evidence: callback order marker(`admission, transfer_out(_empty), leave, commit_request,
  transfer_in(_empty), joined, domain_state_loaded, location_committed, commit_ack, source_cleanup`),
  admission input snapshot(instance 없음), transfer state marker, packet handler marker, bound session
  snapshot. 로그 `log/` 파일 + message flow `key_transitions`.
- config-10 단독 runner가 통과한 뒤 Java/Kotlin e2e 전체 runner를 실행해 기존 config가 깨지지 않는지 확인.

## P5. POSD/DDD 리팩토링 루프

config-10 P0 전부 + §12 contract 테스트(README §3.1 매핑)가 그린이 된 뒤 시작. **codex 에이전트 리뷰 → 의미있는 항목 반영
→ 회귀 그린 → 재리뷰**를 CONVERGED까지 반복(README §6).

- Java 특유 관심: `ZLinkSpotRuntime`류 god-file 분할(현재 최대 파일), adapter 등록/조회 응집,
  admission/commit/transfer 책임 분리, moving-dispatch·generation fencing·bound session owner 단일화,
  구 codec/factory-recreate 잔재 제거, Kotlin surface 중복.
- hot 경로 변경은 baseline vs patched 벤치 증거 첨부.
- 라운드별 반영·수렴 기록. 리팩토링은 Java 한 벌 + Kotlin surface 반영.

## 체크리스트 (Java / Kotlin)

### 계약 항목(§11) — Java / Kotlin 각각
- [ ] 1~12 Java (README §4 Java 열). runtime audit로 `✅`/`🚫` 확정.
- [ ] 1~12 Kotlin surface 미러 확인 (README §4 Kotlin 열).

### interface/문서
- [ ] 실제 Java public source interface를 actor id admission/adapter 모델로 변경
- [ ] Kotlin surface와 기존 샘플/e2e compile break 정리
- [ ] `onActorJoin`=actor id admission(instance/route metadata 없음), 반환 `ZLinkSpotActorJoinResponse` 확인
- [ ] `ZLinkActorTransferAdapter` 책임 경계 + `addActorTransferAdapter`/기본 빈 state transfer 정책
- [ ] Kotlin surface suspend 미러 + 단일 제너릭 유지

### 샘플
- [ ] Java/Kotlin Bingo·TicTacToe local join 순서 정합
- [ ] DeliveryDispatch(+SupportChat) remote transfer adapter 등록·순서 정합(Java+Kotlin)
- [ ] Java/Kotlin sample 전체 runner 통과

### e2e config-10
- [ ] Java `framework/languages/java/e2e/SpotActorTransfer`: ST-A1/A2/A3 · ST-B1/B2/B3/B4 · ST-C1/C2/C3(P1) · ST-D1/D2(P1) · ST-E1/E2
- [ ] Kotlin `framework/languages/java/e2e-kotlin/...`: 위 시나리오 미러
- [ ] Java/Kotlin e2e 전체 runner 통과

### P5
- [ ] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

## 함정 (Java/Kotlin)

- 런타임은 한 벌(Java). Kotlin은 surface/샘플/e2e만 — 런타임을 두 번 고치지 않는다.
- `onActorJoin`은 actor id와 request만 받고 `ZLinkSpotActorJoinResponse`(≠
  `ZLinkActorJoinResult<TReply>`)를 반환한다. membership·location·client event·instance 접근 금지.
- `submit/await` success 완료 시점이 `onJoinedActor` 완료 뒤인지 확인(early complete 금지).
- adapter 미등록은 실패가 아니라 기본 빈 state transfer다.
- 같은 node join에서 transfer adapter 호출 금지(인스턴스 그대로 이동).
- Kotlin 제너릭은 단일 `<TActor extends ZLinkActor>` 유지(2-인터페이스로 쪼개지 말 것).
