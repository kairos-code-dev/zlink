# .NET worker — Spot Actor Join / Transfer 적용

> 이 문서 하나로 .NET framework의 Spot actor join/transfer 적용을 끝낼 수 있게 썼다.
> 계약 정본은 [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md),
> 검증 정본은 [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md),
> .NET interface 정본은 [dotnet/spec/handler-interfaces.ko.md](../../framework/common/spec/languages/dotnet/handler-interfaces.ko.md),
> 사용자 가이드는 [dotnet/guide/06-actor-spot.ko.md](../../framework/dotnet/guide/06-actor-spot.ko.md)다.
> 전 언어 현황은 [README.ko.md](README.ko.md).

## 0. .NET 완료 상태 (2026-07-10)

`ZLinkActorJoinAdmission` 제거, adapter 미등록 기본 빈 state transfer, joined·leave 기본 no-op API 삭제를
source, 샘플, e2e와 guide에 반영했다. 아래 interface는 현재 .NET public source와 일치한다.

```csharp
// admission — actor instance도 route metadata도 받지 않는다
ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
    string actorId, ZLinkMessage request, CancellationToken ct);   // Accept/Reject
ValueTask OnJoinedActorAsync(TActor actor, CancellationToken ct);   // join 완료 신호
ValueTask OnLeaveActorAsync(TActor actor, CancellationToken ct);
// ZLinkSpotActorJoinResult.Accept(reply?) / .Reject(reply?)

// transfer adapter (actor type별)
public interface IZLinkActorTransferAdapter<TActor> where TActor : IZLinkActor {
    ValueTask<ZLinkMessage> TransferOutAsync(TActor actor, CancellationToken ct);
    ValueTask<TActor> TransferInAsync(
        string actorId, IZLinkActorContext context, ZLinkMessage state, CancellationToken ct);
}
// 등록: state 이동이 필요한 actor type만 custom adapter 등록
void AddActorTransferAdapter<TActor, TAdapter>(string actorType)
    where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
```

> 주의: admission callback이 반환하는 `ZLinkSpotActorJoinResult`(Accept/Reject)와, actor-side
> `JoinSpot` 호출 결과인 `ZLinkActorJoinResult<TReply>`(`Accepted` + `Actor` + `Reply`)는 **다른 타입**이다.

기존 `ValueTask OnJoinedActorAsync(TActor actor, CancellationToken cancellationToken) { return
ValueTask.CompletedTask; }` 같은 interface 기본 구현은 제거했다. 해당 lifecycle interface 구현체는
처리할 일이 없더라도 메서드를 명시하고 완료된 `ValueTask`를 반환해야 한다.

완료 범위는 (a) public source interface 재정렬, (b) runtime audit·보정, (c) **guide 정합**,
(d) 샘플·e2e 재정렬, (e) P5 재검증이다.

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) .NET 열에 반영.

- [x] SPOT runtime(예: `ZLinkSpotRuntime` 등 실제 파일 확인)에서 local join이
      `OnActorJoinAsync → OnLeaveActorAsync → OnJoinedActorAsync` 순서이고, join success가
      `OnJoinedActorAsync` 완료 뒤에 반환되는지.
- [x] remote 이동이 admission/commit 분리 + `IZLinkActorTransferAdapter` 호출로 되는지, 구형 factory
      재생성 모델이 남아 있는지.
- [x] `AddActorTransferAdapter` 등록·조회, adapter 미등록 기본 빈 state transfer, 기존
      `AddStatelessActorTransfer` 제거 범위 확인.
- [x] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과는 바로 아래 P0 표에 정리했다.

### P0 audit 결과 (2026-07-10 current source)

`framework/languages/dotnet` source 기준으로 확인한 상태다.

| 항목 | 상태 | 근거/판단 |
| --- | --- | --- |
| local join admission surface | 완료 | public callback, descriptor, invoker, local/remote dispatch가 actor id와 request만 전달한다. `ZLinkActorJoinAdmission` public type은 제거했다. |
| local join success gate | 완료 | `ZLinkSpotActivationActors`와 `ZLinkEntrySpotActivation` 경로에서 accept 뒤 leave/joined를 호출한다. config-10 ST-A1/ST-A2/ST-A3/ST-D1이 success reply, location commit, moving 중 dispatch 차단을 배포형 evidence로 검증했다. |
| remote transfer adapter | 완료 | custom adapter는 source `TransferOutAsync`와 target `TransferInAsync`를 사용한다. 미등록 actor type은 source 빈 state와 target actor factory 경로를 사용한다. |
| adapter registration | 완료 | `AddStatelessActorTransfer`와 stateless flag를 제거했다. `AddActorTransferAdapter`가 adapter 등록과 scoped DI 등록을 함께 담당한다. |
| target materialize 설계 | 완료 | custom adapter는 `TransferInAsync(actorId, context, state, ct)`를 사용하고, 기본 빈 state transfer는 actor factory 경로로 materialize한다. |
| config-10 e2e | 완료 | ST-A1~ST-E2가 모두 통과했다. ST-B3은 adapter 미등록 기본 빈 state 성공, ST-B4는 custom adapter가 빈 state를 반환한 뒤 `OnJoinedActorAsync`에서 별도 domain state를 읽는 흐름을 검증한다. |
| verification | 완료 | solution build, unit/contract/sample regression, config-10 전체, sample 전체 runner와 e2e 전체 runner가 통과했다. |

## P1. Public interface 정렬 + guide reconcile

- [x] 실제 source public interface를 목표 정본으로 변경:
  `OnActorJoinAsync(string actorId, ZLinkMessage, CancellationToken)`,
  `IZLinkActorTransferAdapter<TActor>`, `AddActorTransferAdapter<TActor, TAdapter>`.
- [x] `ZLinkActorJoinAdmission` public type과 이를 요구하는 descriptor/invoker/dispatch/샘플 코드를 제거.
- [x] `AddStatelessActorTransfer<TActor>` public API와 registration storage의 stateless flag를 제거한다.
- [x] `OnJoinedActorAsync`/`OnLeaveActorAsync`의 interface 기본 no-op 구현을 제거한다. 특히 아래 형태가
  public interface에 남지 않아야 한다.

```csharp
ValueTask OnJoinedActorAsync(
    TActor actor,
    CancellationToken cancellationToken)
{
    return ValueTask.CompletedTask;
}
```

- [x] `06-actor-spot.ko.md`의 "② 다른 노드 — actor migration" 다이어그램/표를 정본으로 교체:
  - remote path의 `OnCreateActorAsync` 제거(정본 §7: remote materialize는 새 actor 생성이 아님 →
    target Entry Spot create callback 호출 안 함).
  - "in-memory 상태 전송 안 됨(payload/DB)" 서술을 **transfer adapter로 state 이동**으로 교체.
    `IZLinkActorTransferAdapter<TActor>.TransferOutAsync(actor) → ZLinkMessage`,
    `TransferInAsync(actorId, context, state) → actor`로 복원. adapter가 없으면 framework 기본 빈
    `ZLinkMessage` + factory 경로임을 명시.
  - callback 순서: source `TransferOut` → source `OnLeaveActorAsync` → commit → target `TransferIn`
    → `OnJoinedActorAsync`(정본 §5 다이어그램과 동일 의미).
- [x] transfer adapter 등록 API를 guide §2 actor 등록 예제에서 `AddActorTransferAdapter`만 남기도록 수정.
- [x] admission이 actor instance와 route metadata를 받지 않고 actor id만 받는다는 규칙을 guide §3 콜백 표에 명시.

## P2. Framework runtime 구현/보정

정본 의미를 만족하도록 보정한다(P0에서 이미 만족하면 그대로).

> .NET 보정: custom `TransferInAsync`가 반환한 actor instance도 기존 actor factory 계약처럼
> `IZLinkActorContext`를 노출해야 한다. 그래서 `.NET` 언어별 interface는
> `TransferInAsync(actorId, context, state, ct)`로 고정한다. runtime은 이 context를 먼저 만들고 adapter에
> 넘긴 뒤, 반환 actor가 같은 context를 노출하는지 기존 bind 검증으로 확인한다.

| 항목 | 요구 |
| --- | --- |
| 같은 node join | `OnActorJoinAsync`(admission) accept → moving 표시 → source `OnLeaveActorAsync` → membership commit → target `OnJoinedActorAsync` → committed location → success. reject면 side effect 없음. adapter 미사용. |
| remote transfer | admission/commit 분리. source `TransferOutAsync` → source `OnLeaveActorAsync` → commit(state) → target `TransferInAsync` materialize → membership commit → `OnJoinedActorAsync` → committed location → commit ack → success. remote에서 `OnCreateActorAsync` 호출 안 함. |
| transfer adapter 미등록 | 실패가 아니다. source는 빈 `ZLinkMessage`로 이동하고 target은 actor factory/public 생성 경로로 materialize한다. |
| custom adapter 빈 state | `AddActorTransferAdapter<TActor, TAdapter>`가 등록되어 있고 `TransferOutAsync`가 빈 `ZLinkMessage`를 반환해도 정상 transfer다. |
| moving dispatch 차단 | 정본 §3.4(actor별 serial queue/moving flag/generation guard). |
| pending admission deadline | 정본 §5.2(down signal 없이 deadline 정리). |
| 멱등 source cleanup | 정본 §5.1. |
| location pending/committed + fencing | 정본 §8. |
| bound session transfer | 정본 §9. |
| 실패 분류 | 정본 §10 표. |

## P3. 샘플 적용

- [x] **local join**: `framework/languages/dotnet/samples/Bingo`, `framework/languages/dotnet/samples/TicTacToe`가 정본 순서로 동작하는지 확인.
  admission에서 room membership 확정 코드가 있으면 `OnJoinedActorAsync`로 이동.
- [x] **remote transfer**: actor domain state가 있는 `Bingo`, `TicTacToe`, `SupportChat`에
  `IZLinkActorTransferAdapter<TActor>` 구현 + `AddActorTransferAdapter` 등록을 추가한다.
  `DeliveryDispatch`의 `CustomerActor`는 보존할 domain state가 없고, `CourierActor`의 `_pending`은 진행 중
  request 대기 상태라 transfer state로 옮기지 않는다. 이 actor들은 기본 빈 state transfer를 사용한다.
- [x] guide §4 Bingo 예제와 실제 샘플 코드 동기화.
- [x] 언어별 sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인. 2026-07-10
  `./framework/languages/dotnet/samples/run_samples.sh` 통과.

## P4. e2e config-10 구현

신규 디렉토리 `framework/languages/dotnet/e2e/SpotActorTransfer`(기존 `ToActorMessaging`=config-9,
`LocationMessaging`, `StoreFailure` 구조 참고).

- 서버 역할(config-10 §2): location store(공유 Redis, 전용 prefix) · actor 노드 2 · session
  gateway 2 · transfer controller(실제 app HTTP endpoint) · consumer.
- client는 `ZLinkHttpClient` 사용(raw `HttpClient` 금지), 상태 변경 관찰은 stream connector.
  framework 내부 API(`IZLinkChannelClient`, `AddZLinkFramework`, `Host.CreateDefaultBuilder`,
  reflection) 직접 사용 금지(e2e README 코드 규칙).
- 시나리오(P1은 ST-C3·ST-D2뿐, 나머지 전부 P0): ST-A1/A2/A3, ST-B1/B2/B3/**B4**, ST-C1/C2/C3, ST-D1/D2, ST-E1/E2.
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

- .NET 특유 관심: `ZLinkSpotRuntime`류 god-file 분할, adapter 등록/조회 응집, admission/commit/transfer
  책임 분리, moving-dispatch·generation fencing·bound session owner 단일화, 구 factory-recreate 잔재
  제거, 핫패스 주석 게이트.
- hot 경로 변경은 baseline vs patched 벤치 증거 첨부.
- 라운드별 반영·수렴 기록.

## 체크리스트 (.NET)

### 계약 항목(§11)
- [x] 1~12 (README §4 .NET 열). runtime audit와 config-10 evidence로 확정.
- [x] 13~17 (in-flight handoff, publish 전 queue 적재, bound-session FIFO, bounded forwarding, mapping 축출).

### interface/문서
- [x] 실제 source public interface를 actor id admission/adapter 모델로 변경
- [x] `ZLinkActorJoinAdmission`, `AddStatelessActorTransfer`, joined·leave 기본 no-op API 삭제
- [x] 기존 샘플/e2e compile break 정리
- [x] `06-actor-spot.ko.md` remote 다이어그램·표를 새 transfer-adapter 모델로 reconcile
- [x] transfer adapter 등록 API를 guide에서 custom adapter만 남기도록 수정
- [x] admission=actor id 규칙 guide 명시

### 샘플
- [x] Bingo/TicTacToe `IZLinkActorTransferAdapter` 등록·순서 정합
- [x] SupportChat `IZLinkActorTransferAdapter` 등록·순서 정합
- [x] DeliveryDispatch 기본 빈 state transfer 정합
- [x] sample 전체 runner 통과

### e2e config-10 (`framework/languages/dotnet/e2e/SpotActorTransfer`)
- [x] ST-A1 · [x] ST-A2 · [x] ST-A3
- [x] ST-B1 · [x] ST-B2 · [x] ST-B3 · [x] ST-B4
- [x] ST-C1 · [x] ST-C2 · [x] ST-C3
- [x] ST-D1 · [x] ST-D2
- [x] ST-E1 · [x] ST-E2
- [x] ST-F1 · [x] ST-F2 · [x] ST-F3 · [x] ST-F4 · [x] ST-F5
- [x] e2e 전체 runner 통과

### P5
- [x] 새 스펙 반영 뒤 codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

### P5 수렴 기록

- Round 1: remote transfer commit 뒤 같은 runtime의 bound session context가 있으면 target actor ref로
  local rebind하도록 보정했다. source session binding에는 session rid가 아니라 session owner SpotNode rid를
  저장하도록 고쳐 remote target이 올바른 session node로 push할 수 있게 했다.
- Round 1 추가 보정: session actor rebind 시 같은 actor id의 이전 binding token을 registry와 runtime
  state에서 정리하도록 `ZLinkSessionActorBindingRegistry` 책임 안에서 처리했다.
- Round 2: adapter 미등록 기본 동작을 nullable registration으로 runtime 안에 흡수하고, custom adapter
  DI 등록도 framework service registrar가 담당하도록 했다. 호출자는 state가 필요할 때만 adapter를
  등록한다. admission 경로에서는 route metadata 전달 객체를 제거해 actor id와 request만 전달한다.
- Round 2 재리뷰: adapter 등록/조회, transfer materialize, admission/commit, bound session 책임이 기존
  runtime 모듈에 유지되어 새 public helper나 샘플 우회가 없다. remote transfer는 빈도가 낮은 lifecycle
  경로이므로 별도 hot-path benchmark 대상이 아니다. 추가로 의미있는 POSD/DDD 항목은 남기지 않는다.

### 최종 검증 (2026-07-10)

- `dotnet build Zlink.Framework.sln --no-restore`: 경고 0개, 오류 0개.
- unit test: 300개 통과. contract test: 36개 통과. sample regression test: 28개 통과.
- `e2e/SpotActorTransfer/run_e2e.sh`: ST-A1~ST-E2 전체 통과.
- `samples/run_samples.sh`: TicTacToe, Bingo, SupportChat, ShoppingMall, DeliveryDispatch, GameQuest 통과.
- `e2e/run_e2e_all.sh`: 10개 config 전체 통과, `total PASS (1090s)`.

### In-flight handoff 추가 검증 (2026-07-10)

- `ActorHandoffTests`: 신규 계약 테스트 5개 통과.
- `e2e/SpotActorTransfer/run_e2e.sh ST-F1 ST-F2 ST-F3 ST-F4 ST-F5`: Track F 전체 통과.
- `ActorTransferForwardWindow` 기본값 5초와 배포별 override 계약을 public options에 반영했다.
- ST-F5는 actor-a→actor-b→actor-c 연쇄 이동으로 hop별 forwarding을 확인하고, window 뒤 두 source
  mapping이 각각 `ActorLocationStale`로 fail-fast하는지 확인한다.

### In-flight handoff POSD 재점검

- 대안 1은 source Spot queue에 남은 작업을 commit 뒤 개별 forward하는 방식이다. 구현은 작지만 target의
  direct packet이 backlog를 추월하고, bound session rebind 경계를 닫지 못해 제외했다.
- 대안 2는 actor runtime state가 moving ingress frame과 forwarding mapping을 함께 소유하고, remote join
  protocol이 backlog와 target barrier를 전달하는 방식이다. 호출자에게 새 packet buffer나 codec 등록을
  요구하지 않고 순서 결정과 retained state를 framework 내부에 숨길 수 있어 이 방식을 선택했다.
- 재점검에서 사용하지 않는 별도 commit 경로와 임시 barrier marker를 제거했다. ingress capture,
  commit payload, target replay, cutoff timer의 책임은 기존 actor state·remote join·Spot ingress 경계에
  각각 남겼고, 새 public 표면은 공통 계약에 필요한 window 설정 하나뿐이다. 추가로 의미있는 POSD/DDD
  항목은 남지 않는다.

## 함정 (.NET)

- guide의 factory-recreate 서술을 지우지 않으면 샘플/e2e 구현이 옛 모델로 흘러간다 — reconcile을 먼저.
- `OnActorJoinAsync`는 actor id와 request만 받고 `ZLinkSpotActorJoinResult`(≠
  `ZLinkActorJoinResult<TReply>`)를 반환한다. 여기서 membership·location·client event·instance 접근 금지.
- adapter 미등록은 실패가 아니라 기본 빈 state transfer다. 실패 시나리오로 되살리지 않는다.
- success reply는 `OnJoinedActorAsync` 완료 뒤에만. join 성공 후 `actor` 객체를 다시 만지지 않는다
  (remote면 이 노드 인스턴스는 retire).
