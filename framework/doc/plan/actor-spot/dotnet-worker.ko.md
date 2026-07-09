# .NET worker — Spot Actor Join / Transfer 적용

> 이 문서 하나로 .NET framework의 Spot actor join/transfer 적용을 끝낼 수 있게 썼다.
> 계약 정본은 [common/spec/spot-actor.ko.md](../../framework/common/spec/spot-actor.ko.md),
> 검증 정본은 [common/e2e/config-10-spot-actor-transfer.ko.md](../../framework/common/e2e/config-10-spot-actor-transfer.ko.md),
> .NET interface 정본은 [dotnet/spec/handler-interfaces.ko.md](../../framework/dotnet/spec/handler-interfaces.ko.md),
> 사용자 가이드는 [dotnet/guide/06-actor-spot.ko.md](../../framework/dotnet/guide/06-actor-spot.ko.md)다.
> 전 언어 현황은 [README.ko.md](README.ko.md).

## 0. .NET 시작 상태 (문서 정본은 정렬됨, source는 P0에서 확인)

`handler-interfaces.ko.md`는 목표 정본과 정렬돼 있다. 하지만 실제 `framework/languages/dotnet/src`
public source가 아직 이 표면을 구현한다는 뜻은 아니다. P0에서 source를 확인하고, 구형
`OnActorJoinAsync(TActor actor, ...)` 또는 adapter 등록 API 부재가 확인되면 P1에서 public source
interface와 샘플 compile break까지 함께 고친다.

```csharp
// admission — actor instance가 아니라 identity(record)만 받는다
public sealed record ZLinkActorJoinAdmission(
    string ActorId, Type ActorType,
    RoutingId SourceSpotRid, RoutingId TargetSpotRid,
    RoutingId SourceNodeRid, RoutingId TargetNodeRid);

ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
    ZLinkActorJoinAdmission admission, /* request, ct */ ...);   // Accept/Reject
ValueTask OnJoinedActorAsync(TActor actor, ...);   // join 완료 신호
// ZLinkSpotActorJoinResult.Accept(reply?) / .Reject(reply?)

// transfer adapter (actor type별)
public interface IZLinkActorTransferAdapter<TActor> where TActor : IZLinkActor {
    ValueTask<ZLinkMessage> TransferOutAsync(TActor actor, CancellationToken ct);
    ValueTask<TActor> TransferInAsync(
        string actorId, IZLinkActorContext context, ZLinkMessage state, CancellationToken ct);
}
// 등록: 기본(stateless) / custom adapter
void AddStatelessActorTransfer<TActor>(string actorType);
void AddActorTransferAdapter<TActor, TAdapter>(string actorType)
    where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
```

> 주의: admission callback이 반환하는 `ZLinkSpotActorJoinResult`(Accept/Reject)와, actor-side
> `JoinSpot` 호출 결과인 `ZLinkActorJoinResult<TReply>`(`Accepted` + `Actor` + `Reply`)는 **다른 타입**이다.

따라서 .NET의 무게중심은 (a) 실제 public source interface 정렬, (b) runtime이 정본 의미대로 도는지
audit·보정, (c) **guide 정합**, (d) 샘플·e2e, (e) P5 리팩토링이다.

## P0. 현황 audit (먼저)

코드로 확인하고 [README 마스터표](README.ko.md#3-마스터-체크-표--config-10-시나리오--언어) .NET 열에 반영.

- [x] SPOT runtime(예: `ZLinkSpotRuntime` 등 실제 파일 확인)에서 local join이
      `OnActorJoinAsync → OnLeaveActorAsync → OnJoinedActorAsync` 순서이고, join success가
      `OnJoinedActorAsync` 완료 뒤에 반환되는지.
- [x] remote 이동이 admission/commit 분리 + `IZLinkActorTransferAdapter` 호출로 되는지, 아니면 아직
      factory 재생성(guide 다이어그램) 모델인지.
- [x] `AddStatelessActorTransfer`/`AddActorTransferAdapter` 등록·조회, 빈 state transfer vs 미등록 실패
      구분.
- [x] moving dispatch 차단, pending/committed location, generation fencing, pending admission
      deadline, 멱등 source cleanup, bound session A→B transfer.
- audit 결과를 이 문서 하단 `## 현황`에 정리하고 각 P를 gap 중심으로 좁힌다.

### P0 audit 결과 (2026-07-10 current source)

`framework/languages/dotnet` source 기준으로 확인한 상태다.

| 항목 | 상태 | 근거/판단 |
| --- | --- | --- |
| local join admission surface | 완료 | public source가 `ZLinkActorJoinAdmission` 기반 `OnActorJoinAsync`를 요구한다. handler invoker, descriptor factory, local activation dispatch가 admission 객체를 넘긴다. |
| local join success gate | 부분 충족 | `ZLinkSpotActivationActors`와 `ZLinkEntrySpotActivation` 경로에서 accept 뒤 leave/joined를 호출하는 구조다. Bingo/TicTacToe/SupportChat sample runner에서 joined 이후 side effect 흐름은 확인했다. 다만 config-10 ST-A1/A2/A3의 배포형 evidence가 아직 없다. |
| remote transfer adapter | 진행 | source `ZLinkActorRemoteJoiner`가 remote routed join 전에 actor type별 transfer registration을 확인하고, 미등록이면 source leave 전에 실패한다. 등록되어 있으면 `TransferOutAsync` 결과를 routed join request에 싣는다. |
| adapter registration | 진행 | `AddStatelessActorTransfer`/`AddActorTransferAdapter`와 `ActorTransfers` registration storage가 있고, runtime 조회가 remote routed join source/target 양쪽에 연결됐다. |
| target materialize 설계 | 보정됨 | `.NET` actor는 `IZLinkActorContext`를 생성자에서 받아야 하므로 `TransferInAsync(actorId, context, state, ct)`로 언어별 interface를 보정했다. target은 stateless이면 factory 경로, custom이면 adapter 경로로 actor를 materialize하고, 둘 다 location claim/native ref/context bind 검증을 통과한다. |
| config-10 e2e | 진행 | `framework/languages/dotnet/e2e/SpotActorTransfer` 골격을 추가했고, 단독 runner에서 ST-A1/ST-A2/ST-B1/ST-B3/ST-B4 초기 배포형 흐름이 통과했다. Track C/D/E, ST-A3, ST-B2, ST-C3, ST-D1/D2, ST-E1/E2와 source cleanup/leave evidence 강화는 남아 있다. |
| verification | 부분 완료 | `dotnet build Zlink.Framework.sln --no-restore` 통과(기존 e2e warning 7개), sample regression test 25개 통과, `./framework/languages/dotnet/samples/run_samples.sh` 통과. unit test 294개, contract test 35개도 직전 검증에서 통과했다. `./framework/languages/dotnet/e2e/SpotActorTransfer/run_e2e.sh`는 ST-A1/ST-A2/ST-B1/ST-B3/ST-B4 partial runner로 통과했다. e2e 전체 runner와 config-10 전체 시나리오는 아직 완료 전이다. |

## P1. Public interface 정렬 + guide reconcile

- [x] 실제 source public interface를 목표 정본으로 변경:
  `OnActorJoinAsync(ZLinkActorJoinAdmission, ZLinkMessage, CancellationToken)`,
  `IZLinkActorTransferAdapter<TActor>`, `AddStatelessActorTransfer<TActor>`,
  `AddActorTransferAdapter<TActor, TAdapter>`.
- [x] 기존 `OnActorJoinAsync(TActor actor, ...)` 호출부와 샘플 compile break를 정본 시그니처로 변경.
- [x] adapter 등록 API가 actor type별 1개 등록, stateless/custom 구분, 미등록 실패 정책을 표현하는지 확인.

- [x] `06-actor-spot.ko.md`의 "② 다른 노드 — actor migration" 다이어그램/표를 정본으로 교체:
  - remote path의 `OnCreateActorAsync` 제거(정본 §7: remote materialize는 새 actor 생성이 아님 →
    target Entry Spot create callback 호출 안 함).
  - "in-memory 상태 전송 안 됨(payload/DB)" 서술을 **transfer adapter로 state 이동**으로 교체.
    `IZLinkActorTransferAdapter<TActor>.TransferOutAsync(actor) → ZLinkMessage`,
    `TransferInAsync(actorId, context, state) → actor`로 복원. state가 필요 없으면 **stateless adapter**
    (`AddStatelessActorTransfer`)로 빈 `ZLinkMessage` + factory 경로임을 명시.
  - callback 순서: source `TransferOut` → source `OnLeaveActorAsync` → commit → target `TransferIn`
    → `OnJoinedActorAsync`(정본 §5 다이어그램과 동일 의미).
- [x] transfer adapter 등록 API(`AddStatelessActorTransfer`/`AddActorTransferAdapter`)를 guide §2
  actor 등록 예제에 추가.
- [x] admission이 actor instance를 받지 않는다는 규칙을 guide §3 콜백 표에 명시.

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
| transfer adapter 미등록 | remote transfer 시작 전 실패, source `OnLeaveActorAsync` 없음. |
| **stateless adapter** | `AddStatelessActorTransfer<TActor>`로 빈 `ZLinkMessage` + factory/public 생성. 빈 state transfer와 미등록 실패 구분(정본 §6). |
| moving dispatch 차단 | 정본 §3.4(actor별 serial queue/moving flag/generation guard). |
| pending admission deadline | 정본 §5.2(down signal 없이 deadline 정리). |
| 멱등 source cleanup | 정본 §5.1. |
| location pending/committed + fencing | 정본 §8. |
| bound session transfer | 정본 §9. |
| 실패 분류 | 정본 §10 표. |

## P3. 샘플 적용

- **local join**: `framework/languages/dotnet/samples/Bingo`, `framework/languages/dotnet/samples/TicTacToe`가 정본 순서로 동작하는지 확인.
  admission에서 room membership 확정 코드가 있으면 `OnJoinedActorAsync`로 이동.
- **remote transfer**: 다중 node 샘플(`framework/languages/dotnet/samples/DeliveryDispatch`, 필요 시
  `framework/languages/dotnet/samples/SupportChat`)에 `IZLinkActorTransferAdapter<TActor>` 구현 + 등록(state 옮기면
  `AddActorTransferAdapter`, 안 옮기면 `AddStatelessActorTransfer`) 추가, node 간 이동 정본 순서 정합.
- guide §4 Bingo 예제와 실제 샘플 코드 동기화.
- 언어별 sample 전체 runner를 실행해 actor/spot 변경이 다른 샘플을 깨지 않는지 확인. 2026-07-10
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

- .NET 특유 관심: `ZLinkSpotRuntime`류 god-file 분할, adapter 등록/조회 응집, admission/commit/transfer
  책임 분리, moving-dispatch·generation fencing·bound session owner 단일화, 구 factory-recreate 잔재
  제거, 핫패스 주석 게이트.
- hot 경로 변경은 baseline vs patched 벤치 증거 첨부.
- 라운드별 반영·수렴 기록.

## 체크리스트 (.NET)

### 계약 항목(§11)
- [ ] 1~10 (README §4 .NET 열). runtime audit로 각 항목 `✅`/`🚫` 확정.

### interface/문서
- [x] 실제 source public interface를 admission/adapter 모델로 변경
- [x] 기존 샘플/e2e compile break 정리
- [x] `06-actor-spot.ko.md` remote 다이어그램·표를 transfer-adapter 모델로 reconcile
- [x] transfer adapter 등록 API(stateless/custom)를 guide에 추가
- [x] admission=identity 규칙 guide 명시

### 샘플
- [x] Bingo/TicTacToe local join 순서 정합
- [x] DeliveryDispatch(+SupportChat) `IZLinkActorTransferAdapter` 등록·순서 정합
- [x] sample 전체 runner 통과

### e2e config-10 (`framework/languages/dotnet/e2e/SpotActorTransfer`)
- [x] ST-A1 · [x] ST-A2 · [ ] ST-A3
- [x] ST-B1 · [ ] ST-B2 · [x] ST-B3 · [x] ST-B4
- [ ] ST-C1 · [ ] ST-C2 · [ ] ST-C3(P1)
- [ ] ST-D1 · [ ] ST-D2(P1)
- [ ] ST-E1 · [ ] ST-E2
- [ ] e2e 전체 runner 통과

### P5
- [ ] codex POSD/DDD 리팩토링 루프 CONVERGED(회귀 그린 유지)

## 함정 (.NET)

- guide의 factory-recreate 서술을 지우지 않으면 샘플/e2e 구현이 옛 모델로 흘러간다 — reconcile을 먼저.
- `OnActorJoinAsync`는 `ZLinkActorJoinAdmission`만 받고 `ZLinkSpotActorJoinResult`(≠
  `ZLinkActorJoinResult<TReply>`)를 반환한다. 여기서 membership·location·client event·instance 접근 금지.
- **미등록 adapter 실패(ST-B3)와 빈 state transfer(ST-B4)는 다른 결과다.**
- success reply는 `OnJoinedActorAsync` 완료 뒤에만. join 성공 후 `actor` 객체를 다시 만지지 않는다
  (remote면 이 노드 인스턴스는 retire).
