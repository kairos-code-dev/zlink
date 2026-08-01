# .NET Framework spec gap audit와 수정 ledger

> 상태: 2차 implementation audit 완료, 구현 수정 전
>
> 기준: `b2d0e3b19a56e9bfb8edc1ff42a8548fc53b602a`와 2026-08-02 working tree
>
> 범위: `.NET` server framework. HTTP client와 client용 Stream Connector는 공통 server 계약이
> 직접 요구하는 연결 지점만 포함한다.

## 1. 목적과 완료 조건

이 문서는 현재 `.NET` Framework 구현이
[`framework/doc/framework/common/spec/`](../../framework/common/spec/README.ko.md)의 목표 계약과
[.NET exact interface](../../framework/common/spec/server/languages/dotnet/README.ko.md)를 충족하는지
다시 확인하고, 확인된 차이를 수정하는 순서를 고정한다. 모든 판정은 현재 source, 실행 가능한 test와
공통 E2E를 직접 대조한 결과만 사용한다.

이 작업은 다음 조건을 모두 만족해야 완료된다.

1. `.NET` exact interface의 모든 public declaration을 실제 source assembly와 package export에 대조한다.
2. 공통 spec의 관찰 가능한 동작뿐 아니라 상태 전이, commit 경계, callback 순서, queue 소유권과
   실패 복구 범위를 production call path와 대조한다. 같은 이름의 기능이 있다는 사실만으로 충족으로
   판정하지 않는다.
3. 각 동작을 contract test, unit test 또는 실제 process E2E 가운데 적합한 계층에서 검증한다.
4. 아래에서 `확인`으로 분류한 implementation gap을 모두 수정한다.
5. 공통 E2E scenario와 `.NET` feature-map의 누락과 잘못된 상태를 모두 제거한다.
6. 마지막 전체 재검사에서 기록하지 않은 `.NET` gap이 0개다.
7. Core, bindings 또는 공통 contract 변경이 필요한 항목은 `.NET` 완료로 처리하지 않고 선행 조건을
   별도 항목으로 남긴다.

## 2. 판정 기준과 조사 범위

공통 spec은 언어와 무관한 동작, 완료 조건과 오류 의미를 소유한다. `.NET` exact interface는 C# 타입,
signature, nullable, generic constraint와 기본값을 소유한다. 구현은 두 계약에 맞춘다. 다른 언어 구현이나
공통 E2E만으로 public API를 추가하지 않는다.

조사는 다음 위치를 기준으로 수행했다.

| 구분 | 기준 위치 |
|---|---|
| 공통 계약 | `framework/doc/framework/common/spec/00`~`32` |
| .NET exact interface | `framework/doc/framework/common/spec/server/languages/dotnet/interfaces/` |
| Production source | `framework/languages/dotnet/src/Zlink.Framework*` |
| Public API snapshot | `framework/languages/dotnet/contract/api/` |
| Contract test | `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/` |
| Runtime regression | `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/` |
| Process 검증 | `framework/languages/dotnet/e2e/`와 공통 E2E Config 1~13 |

판정은 다음 네 종류를 사용한다.

| 판정 | 의미 |
|---|---|
| `확인` | 계약과 production source의 차이를 직접 확인했다. |
| `test gap` | 구현 여부를 판정하는 회귀 test가 없거나 현재 test가 계약을 직접 검증하지 않는다. |
| `contract 선행` | 공통 계약을 .NET public interface로 표현하는 방법이 부족하여 exact spec을 먼저 고쳐야 한다. |
| `충족` | 현재 source와 실행 결과가 해당 계약을 직접 증명한다. |

Working tree에는 Actor relocation과 관련 E2E 수정이 이미 존재한다. 이 ledger는 해당 변경을 보존한 상태에서
작성했다. 이후 실행 log에는 각 단계의 candidate commit과 working tree diff를 함께 기록해야 한다.

## 3. 현재 검증 결과

| 검증 | 결과 | 현재 판단 |
|---|---|---|
| `dotnet test tests/Zlink.Framework.ContractTests/...` | 72/72 통과 | Source assembly와 고정 API snapshot은 일치한다. Markdown exact interface와의 일치는 아직 증명하지 않는다. |
| `.NET` unit test 전체 | 1385 통과, 1 실패 | Runtime test는 통과했고 documentation inventory test 한 건이 실패한다. |
| documentation regression 단독 | 18 통과, 1 실패 | 첫 failure는 Config 3 PubSub inventory 누락이다. 전체 inventory 대조 결과 Config 3, 7, 10과 12에 누락이 있다. |
| `verify-framework-doc-contracts.sh` | 중단 | Service wire 검사는 통과했다. C++ member override의 target signature 누락에서 중단되어 `.NET` 문서 전체 CLEAN 증거로 사용할 수 없다. |

`ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`는 이름과 달리 Markdown
spec을 읽지 않는다. `framework/languages/dotnet/contract/api/*.api.txt`와 reflection 결과만 비교한다.
따라서 이 test의 통과를 exact interface와 구현이 일치한다는 최종 증거로 사용하지 않는다.

### 3.1 Relocation 동작 방식 재검사 요약

다음 표는 public type이나 method의 존재 여부가 아니라 production call path의 실행 순서를 spec과 대조한
결과다. `부분 충족`은 정상 경로 일부가 같다는 뜻이며 완료 판정이 아니다.

| 범위 | Spec이 정한 경계 | Live production path | 판정 |
|---|---|---|---|
| Host preflight | Host state와 admission을 바꾸기 전에 local workload, Store, policy와 target을 확인한다. | `PreflightRetireAsync(...)`가 `PublishRetiringAsync(...)`와 `Relocating` 전이보다 먼저 실행된다. | 충족 |
| Host workload handoff | Unit별 restore·commit을 끝내고 모두 source dispatch에서 분리한 뒤 `Relocated`가 된다. Infrastructure는 유지한다. | PerActor shell, Actor, aggregate 순서로 unit을 옮긴 뒤 admission을 seal하고 infrastructure teardown 없이 반환한다. | 부분 충족. 전체 process E2E 필요 |
| Host deadline | Target 탐색 실패와 다른 preflight cancellation을 서로 다른 reason으로 반환한다. | Preflight 내부 catch가 cancellation을 모두 `TargetUnavailable`로 바꿀 수 있다. | DN-IMP-005 |
| Host process failure | Process 종료 뒤 다른 runtime이 relocation을 이어받지 않는다. | Startup recovery와 takeover가 published relocation을 restore하고 계속 진행한다. | DN-IMP-004 |
| Host commit 뒤 failure | Commit한 unit은 target owner로 유지하고, 아직 옮기지 않은 source workload만 복원한 뒤 host를 `Serving`으로 전환한다. | 한 unit이라도 commit한 뒤 실패하면 `ForceStopAsync(...)`를 실행하여 runtime infrastructure를 종료하고 host를 `Error`로 전환한다. | DN-IMP-007 |
| Host terminal 관찰 | 느린 observer에서도 relocation·shutdown terminal status를 생략하지 않으며 표준 identifier로 structured log를 남긴다. | Observer channel이 모든 status에 `DropOldest`를 적용하고, 일반 `Blocked` 결과는 status에 넣지 않은 채 게시한다. 표준 host identifier도 기록하지 않는다. | DN-IMP-009 |
| Cross-node Join prepare·commit | Target admission 뒤 source를 seal·capture하고 target restore 뒤 authority를 commit한다. | Admission reservation, source capture, Relocation Store prepare, target import·restore와 authority publication 순서로 실행한다. | 부분 충족. failure E2E 필요 |
| Join deadline | Call의 absolute deadline을 위치 commit 전의 resolve, prepare, target restore와 authority commit 전체에 적용한다. | Relocation root 준비 뒤 target commit reconciliation은 call token 대신 `runtime.ShutdownToken`만 사용하여 deadline 뒤에도 계속 재시도한다. | DN-IMP-008 |
| Join lifecycle | `OnJoinedActor` 뒤 source leave를 one-way으로 보내되, leave 결과가 target completion을 막지 않는다. | Source가 `ReconcileCommittedSourceLeaveAsync(...)`를 끝낼 때까지 기다린 뒤 target completion request를 보낸다. | DN-IMP-006 |
| Bound Session Join | Join completion과 target dispatch 뒤 위치 갱신을 시작하며 ACK는 Actor 처리를 막지 않는다. | Session route commit ACK를 기다린 뒤 Join completion과 queue replay를 실행한다. | DN-IMP-006 |
| Join process failure | Completion cursor는 process memory에만 두며 restart 뒤 callback을 replay하지 않는다. | Store의 durable cursor와 published root로 restart 뒤 `Accepted` callback과 handoff를 복구한다. | DN-IMP-006 |

## 4. 확인된 implementation gap

### DN-IMP-001 — Application HWM의 host 전체 ingress 미적용

**판정: 확인. Relocation 계약 경계를 바로잡은 뒤 수정한다.**

[Framework API §2](../../framework/common/spec/06-framework-api.ko.md#2-framework-설정)와
[Runtime 상태](../../framework/common/spec/24-runtime-monitoring.ko.md#2-application이-한-번에-읽는-상태)는
Framework가 받은 application payload를 host 전체에서 합산하도록 요구한다. RouteMesh, ClientServer,
fanout, Spot, Actor와 STREAM이 같은 `ApplicationHwmBytes`를 공유해야 한다.

현재 `ZLinkInboundDispatchBudget`은 ClientServer와 fanout의 `ZLinkChannelReceiveLoop`에만 전달된다.
`ZLinkSpotNodeInitializer`, Spot·Actor inbound pipeline과 `ZLinkStreamRuntimeManager`는 이 budget을 받지
않는다. 따라서 RouteMesh/Spot/Actor와 STREAM payload는 `PendingPayloadBytes`, `QueuedPayloadBytes`와
`ActivePayloadBytes`에 포함되지 않으며, HWM에 도달해도 해당 ingress의 새 receive가 계속될 수 있다.

현재 unit test는 budget 자체와 classic channel 연결만 검증한다.

- `InboundDispatchBudgetTests` 7개는 accounting primitive를 검증한다.
- `InboundDispatchOptionsTests`는 Auto 계산과 classic listener의 `MaxMessageSize` validation을 검증한다.
- ClientServer와 automatic fanout test는 channel receive 경로를 검증한다.
- RouteMesh/Spot/Actor와 STREAM을 같은 host budget으로 묶는 test는 없다.

**수정 범위**

1. Host가 만든 `ZLinkInboundDispatchBudget` 하나를 RouteMesh/Spot/Actor와 STREAM ingress에도 전달한다.
2. Complete message를 받은 뒤 queue에 넣기 전에 payload bytes를 한 번 더하고, handler가 성공, 실패 또는
   cancellation으로 끝나는 공통 terminal 경로에서 한 번 뺀다.
3. HWM에 도달하면 새 application receive만 중단한다. Completion, liveness와 relocation control은 계속
   처리한다.
4. Runtime status의 세 byte 값이 모든 ingress의 합계를 나타내도록 고친다.

### DN-IMP-002 — STREAM listener의 advertised endpoint 미확정

**판정: 확인. DN-IMP-001의 STREAM 연결 뒤 수정한다.**

[Network listener identity §4](../../framework/common/spec/10-network-listener-identity.ko.md#4-port를-확정하는-방법)는
port `0`으로 bind한 뒤 실제 port를 읽고 `AdvertiseHost`와 결합하도록 요구한다. .NET exact interface도
`IZLinkStreamNodeBuilder.SetAdvertiseHost(...)`를 공개한다.

현재 builder는 `ZLinkStreamNodeRegistration.AdvertiseHost`에 값을 저장한다. 그러나
`ZLinkStreamRuntimeManager.InitializeStreamNodesAsync(...)`는 bind 뒤 실제 endpoint를 읽지 않고,
`AdvertiseHost`도 사용하지 않는다. STREAM backend contract에는 `GetLastEndpoint()`도 없다. 현재 test는
builder가 문자열을 registration에 저장하는지만 확인한다.

**수정 범위**

Bindings의 public `IStreamSocket.Options.LastEndpoint`가 실제 bound endpoint를 제공한다. 따라서 bindings
변경이나 reflection은 필요하지 않다.

1. `IZLinkBackendStreamSocket`과 `ZLinkBackendStreamSocketWrapper`를 이 public property에 연결한다.
2. 실제 bound endpoint와 listener override를 결합한다.
3. Wildcard `BindHost`에서 `AdvertiseHost`가 없으면 socket bind 전에 startup configuration error를 낸다.
4. STREAM endpoint를 MeshNode, ClientServer 또는 fanout descriptor에 기록하지 않는다.

### DN-IMP-003 — STREAM ingress의 유한한 message 상한을 위한 public projection 부재

**판정: contract 선행. DN-IMP-001과 함께 결정한다.**

[Framework API §2](../../framework/common/spec/06-framework-api.ko.md#2-framework-설정)는 Auto 또는 양수
Application HWM을 사용할 때 모든 application listener의 `MaxMessageSize`가 유한한 양수여야 한다고
정한다. .NET의 `IZLinkStreamNodeBuilder`에는 STREAM listener의 `MaxMessageSize`를 설정하거나 확인하는
member가 없고, runtime도 STREAM socket에 같은 설정을 적용하지 않는다.

이 항목은 구현에 private option을 추가해서 해결하지 않는다. 다음 두 대안을 exact spec에서 먼저
검토해야 한다.

1. 모든 listener가 공유하는 좁은 socket configuration interface를 STREAM builder에도 제공한다.
2. STREAM listener의 공개 최대 message 크기를 별도 STREAM configuration member로 제공한다.

호출자가 topology별로 서로 다른 개념을 배워야 하는 범위가 작은 대안을 선택한다. Exact interface와
contract test를 먼저 고친 뒤 production source를 수정한다.

### DN-IMP-004 — Process 종료 뒤 다른 runtime의 host relocation 인계

**판정: 확인. 상태 전이와 정상 경로가 일부 구현되어 있다는 이유로 충족 처리할 수 없다.**

[Host Relocate §8.8](../../framework/common/spec/28-graceful-drain-handoff.ko.md#88-중간에-실패하면-어느-위치를-유지하는가)는
source나 target process가 종료되면 다른 runtime이 relocation을 이어받지 않도록 정한다. Owner 변경 뒤
target process가 종료되면 object를 unavailable 상태로 유지하며, 11.1.0은 자동 복구를 제공하지 않는다.

현재 runtime startup은 `ZLinkFrameworkRuntime.StartAsync(...)`에서
`RecoverPublishedRelocationsAsync(...)`를 호출한다. 이 경로는 Relocation Store의 published root를 읽고
Spot과 Actor를 다시 restore하며, `ZLinkStandaloneActorRelocationTakeoverCoordinator`는 이전 recovery owner가
종료된 뒤 다른 target으로 takeover할 수 있다. 기존
`StandaloneActorRelocationRuntimeTests`도 target owner lease가 끝난 뒤 새 runtime이 Actor를 만드는 동작을
성공 조건으로 검증한다. 이는 기능 누락이 아니라 spec보다 넓은 recovery 의미를 구현한 차이다.

**수정 범위**

1. Startup에서 published relocation을 자동으로 이어받는 경로를 11.1.0 host relocation에서 제거한다.
2. Commit 뒤 target process가 종료된 object는 source rollback이나 다른 target takeover 없이 unavailable로
   유지한다.
3. 같은 target process가 실행 중인 동안의 retry와 process restart 뒤 recovery를 구분한다.
4. Relocation Store의 durable payload는 실행 중 handoff와 검증에만 사용하고 새 runtime의 실행 권한으로
   해석하지 않는다.

### DN-IMP-005 — Host relocation preflight cancellation reason 손실

**판정: 확인. DN-IMP-004와 함께 host lifecycle regression을 고친다.**

`ZLinkFrameworkMaintenanceRuntime`은 preflight에서 발생한 deadline cancellation을
`Blocked/DeadlineExceeded`로 변환한다. 그러나 실제 callback인
`ZLinkFrameworkRuntime.PreflightRetireAsync(...)`가 내부의 모든 `OperationCanceledException`을 먼저 잡아
`TargetUnavailable`로 반환한다. 따라서 Store 검사, accepted handoff 대기 또는 unit 호환성 검사 중
deadline이 끝나면 상위 runtime이 cancellation 원인을 관찰하지 못하고 `Blocked/TargetUnavailable`을 반환할
수 있다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은 target
탐색 자체가 deadline까지 실패한 경우만 `TargetUnavailable`로 분류한다. Framework가 callback을
cancellation하거나 owner 변경 전 작업이 deadline을 넘으면 `DeadlineExceeded`여야 한다.

Preflight 결과는 blocker와 cancellation 원인을 구분해 전달한다. Target 탐색 deadline과 다른 preflight
단계의 deadline을 같은 catch에서 합치지 않는다.

### DN-IMP-006 — Actor Join relocation의 commit 뒤 실행 순서와 recovery 범위 차이

**판정: 확인. public API 유무가 아니라 commit 뒤 실행 순서와 recovery 범위의 차이다.**

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)는 cross-node Join의
정상 경로를 target admission, source seal과 capture, target restore, Location Store commit,
`OnJoinedActor`, source `OnLeaveActor`, target Join completion, 기존 작업과 temporary 작업 replay 순서로
정한다. 현재 production path는 이 정상 순서의 일부를 구현한다. 그러나 다음 세 지점은 계약과 다르다.

1. Source는 `ReconcileCommittedSourceLeaveAsync(...)`가 성공할 때까지 기다린 뒤 target completion
   request를 보낸다. Spec에서 source leave는 one-way notification이며 완료나 실패가 target completion을
   막지 않아야 한다.
2. `CompleteRoutedActorHandoffAsync(...)`는 `CommitCompletedSessionRouteAsync(...)`를 호출하고 ACK를 기다린
   뒤 `OnJoinCompletedAsync(...)`와 queue replay를 실행한다. Spec은 Join completion callback이 끝난 뒤
   Session 위치 갱신을 시작하며, 갱신 응답이 Actor message 처리를 막지 않도록 정한다.
3. `ZLinkDeferredActorJoinCompletionJournal`, `RecoverPublishedRelocationsAsync(...)`와
   `RecoverDeferredJoinCompletionAsync(...)`는 completion cursor, `OperationId`와 reply를 Store에 보존하고
   target process restart 뒤 `Accepted` callback을 다시 실행한다. Spec은 이 값들을 현재 source와 target
   process가 실행되는 동안만 보존하며, process 종료 뒤 completion을 다른 runtime에서 replay하지 않도록
   정한다.

**수정 범위**

1. `OnJoinedActor` 뒤 source leave notification을 보내되 그 결과를 target completion barrier로 사용하지
   않는다.
2. 저장된 기존 작업과 temporary 작업을 실제 queue로 옮겨 application dispatch를 연다.
3. Bound Session 위치 갱신은 completion 뒤 별도 retry 작업으로 시작하고 ACK를 Actor dispatch barrier로
   사용하지 않는다.
4. Process restart 뒤 completion callback과 cross-node Join을 자동 복구하는 durable cursor와 startup
   recovery 경로를 제거한다. 같은 process 안에서의 idempotent retry는 유지한다.

### DN-IMP-007 — 첫 commit 뒤 host relocation failure가 runtime 전체를 종료한다

**판정: 확인. DN-IMP-004보다 먼저 failure state machine을 바로잡는다.**

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은 첫
owner 변경 뒤 failure가 발생하면 commit한 owner를 유지하고 다른 target을 선택하지 않도록 정한다. 아직
옮기지 않은 source workload만 다시 처리한 뒤 host는 `Serving`으로 돌아가며 caller는
`Blocked/RelocationFailed`를 받는다. 이미 commit한 object는 target에서 사용할 수 없더라도 source로
rollback하지 않는다.

현재 `ZLinkFrameworkDrainExecutor.ExecuteWithProgressAsync(...)`는 `committedUnitCount > 0`인 상태에서 unit
failure를 받으면 `RelocationFailed` force reason을 반환한다. `ZLinkDrainCoordinator.ExecuteSharedAsync(...)`는
이를 `ForceStopAsync(...)`로 전달하여 runtime, auto-connect와 Location owner resource를 종료한다.
`ZLinkFrameworkMaintenanceRuntime.ExecuteRelocationAsync(...)`도 결과를 `ForceStopped`로 해석하여 host를
`Error`로 전환한다. 기존
`DrainCoordinatorTests.RetireFailureAfterCommitForceStopsWithDurableProgress`는 이 동작을 성공 조건으로
고정하므로 spec과 반대다.

**수정 범위**

1. Relocation의 commit 뒤 unit failure를 shutdown force reason과 분리하여 `DrainBlocked(RelocationFailed)`로
   전달한다.
2. Commit하지 않은 source unit만 admission과 dispatch를 복원한다. Commit한 authority와 membership은
   변경하지 않는다.
3. Descriptor를 `Serving`으로 되돌리고 application admission을 다시 열되, committed object의 source
   route는 다시 열지 않는다.
4. Runtime infrastructure와 Location owner lease는 유지한다. Descriptor rollback 확인에 실패한 경우에만
   spec의 bounded teardown 조건을 적용한다.

### DN-IMP-008 — Actor Join target commit reconciliation이 call deadline을 사용하지 않는다

**판정: 확인. DN-IMP-006의 callback·replay 순서 수정과 함께 처리한다.**

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)는 Join call의
deadline을 필요한 relocation 전체에 적용한다. Deadline까지 위치 변경을 commit하지 못하면
`Failed/DeadlineExceeded`이며, 다음 call이 Store의 current authority를 확인하여 중단된 attempt를
정리하거나 이어간다.

현재 `SubmitRoutedJoinActorCoreAsync(...)`는 source capture와 Relocation Store prepare까지 call token을
사용한다. 그 다음 `ReconcileTargetJoinCommitAsync(...)`는 token을 인자로 받지 않고
`runtime.ShutdownToken`으로 `ZLinkReconciliationRunner`를 실행한다. Target request가 계속 실패하면 public
Join call의 deadline과 caller cancellation은 더 이상 관찰되지 않고 runtime이 종료될 때까지 재시도할 수
있다. 각 request에 `DefaultRequestTimeout`이 있어도 전체 reconciliation deadline을 제한하지는 않는다.

**수정 범위**

1. Source가 받은 monotonic absolute deadline을 target commit reconciliation까지 전달한다.
2. Deadline이 끝나면 authority를 다시 읽어 owner commit 여부를 확정한다. Commit 전이면 prepared root와
   target staging을 정리하고 source queue를 복원한 뒤 `DeadlineExceeded`를 반환한다.
3. Commit 결과를 알 수 없으면 source rollback을 추측하지 않는다. Current authority를 확인할 때까지
   source admission을 닫아 둔다.
4. Commit 뒤 callback과 completion retry는 DN-IMP-006의 같은-process 규칙을 따르며 caller cancellation로
   owner를 되돌리지 않는다.

### DN-IMP-009 — Host relocation terminal status와 structured log 보장 누락

**판정: 확인. Public monitoring interface는 있으나 terminal 전달 방식이 계약과 다르다.**

[Runtime monitoring §3](../../framework/common/spec/24-runtime-monitoring.ko.md#3-변화를-연속으로-관찰한다)는
중간 status를 합칠 수 있어도 가장 최근 `Sequence`와 relocation·shutdown terminal status를 생략하지
않도록 정한다. [Structured log §5](../../framework/common/spec/24-runtime-monitoring.ko.md#5-structured-log)는
host relocation에 `zlink.runtime.host.relocation_changed`, shutdown에
`zlink.runtime.host.termination_changed` identifier를 사용하도록 정한다.

현재 `ZLinkFrameworkMaintenanceRuntime.ObserveAsync(...)`는 1024개 bounded channel에
`BoundedChannelFullMode.DropOldest`를 사용한다. Intermediate와 terminal을 구분하지 않으므로 느린 observer가
terminal status를 잃을 수 있다. 일반 `Blocked`는 최종 snapshot에 저장하지 않는 계약이 맞지만,
`CompleteBlocked(...)`는 transient terminal result도 status에 넣지 않은 채 `PublishUnderLock()`을 호출한다.
따라서 observer는 해당 `Blocked` 결과를 한 번도 받지 못한다.

`ZLinkDrainCoordinator`의 현재 log는 `ZLink host lifecycle changed` 문자열과 `State`만 기록한다. Production
source에는 두 host identifier가 없으며 relocation mode, effective target version, outcome과 reason을
표준 structured field로 기록하는 경로도 없다.

**수정 범위**

1. Observer별 buffer에서 intermediate status만 합치고 terminal status는 제거하지 않는 queue 정책을
   구현한다.
2. 일반 `Blocked` 결과는 current `Status.RelocationResult`에 보존하지 않더라도 observer에 complete status로
   한 번 게시한다.
3. Host state, relocation result와 termination result 변화에 공통 identifier와 필요한 structured field를
   기록한다.
4. 느린 observer, observer cancellation과 logger provider failure가 lifecycle 결과를 바꾸지 않도록 한다.

## 5. 확인된 regression test와 inventory gap

### DN-TEST-001 — Markdown exact interface와 export를 직접 비교하지 않는다

**판정: test gap.**

현재 public surface test는 reflection 결과와 별도 API snapshot을 비교한다. Exact interface 문서가 바뀌고
snapshot을 갱신하지 않거나, snapshot과 문서가 서로 다르게 바뀌어도 한쪽 차이를 직접 검출하지 못한다.

다음 회귀 test를 추가한다.

- `ExactInterfaceMarkdown_matches_source_and_package_exports`
  - `interfaces/*.ko.md`의 C# declaration을 모두 읽는다.
  - source assembly와 실제 NuGet package export를 각각 비교한다.
  - nullable, 기본값, generic constraint와 overload를 포함한다.
- `ExactInterfaceInventory_covers_every_exported_contract_type`
  - export된 public contract type마다 owner 문서가 정확히 하나인지 확인한다.

### DN-TEST-002 — documentation regression이 exact interface 디렉토리를 분모로 사용하지 않는다

**판정: test gap.**

`RegressionTests.DotNetContractDocuments_AllExposeRegressionTestSection`과 regression matrix는 과거 top-level
문서 목록을 중심으로 검사한다. 현재 계약 owner인 `server/languages/dotnet/interfaces/` 전체를 재귀적으로
분모에 넣지 않는다. 그 결과 exact interface 문서가 추가되거나 삭제되어도 matrix 누락을 안정적으로
검출하지 못한다.

다음 변경이 필요하다.

- exact interface README의 문서 표에서 inventory를 읽거나 해당 디렉토리를 재귀적으로 열거한다.
- 각 exact interface 문서가 최소 한 개의 실행 가능한 contract test와 연결되도록 검사한다.
- `integration-single-process` 같은 계층 이름만 적은 행은 증거로 인정하지 않는다.

### DN-TEST-003 — 공통 E2E scenario inventory와 feature-map이 다르다

**판정: test gap이며 현재 test 실패 원인이다.**

현재 공통 E2E 문서와 `.NET` feature-map을 같은 정규식으로 전수 대조하면 다음 ID가 빠져 있다.

| Config | Feature-map | 누락 ID |
|---:|---|---|
| 3 | `PubSub` | `PS-D7A`, `PS-D7B`, `PS-E2A`, `PS-E2B`, `PS-E2C` |
| 7 | `RuntimeMonitoring` | `MON-A4A`, `MON-A4B`, `MON-D1A`, `MON-D1B` |
| 10 | `SpotActorTransfer` | `ST-E1B`, `ST-E1C` |
| 12 | `ChannelEgressRouting` | `CH-E2E-04A`, `CH-E2E-04B`, `CH-E2E-04C`, `CH-E2E-07A`, `CH-E2E-07B`, `CH-E2E-07C` |

Feature-map에 상태만 추가해서 닫지 않는다. 현재 working tree의 Actor handoff 변경과 함께 실제 selector,
runner registration, server evidence와 terminal assertion을 추가해야 한다.

### DN-TEST-004 — 기존 feature-map의 일부 상태가 live source와 반대다

**판정: inventory gap. 구현 gap으로 사용하지 않는다.**

다음 항목은 feature-map 설명과 production source가 일치하지 않는 확인 사례다.

| 기존 기록 | live source |
|---|---|
| SubmitAdmission `SA-E2E-10`은 ClientServer builder와 runtime이 없다고 기록한다. | `IZLinkClientServerChannelRoleBuilder`와 ClientServer runtime이 존재하고 contract/unit test가 통과한다. |
| PubSub `PS-D1`은 automatic subscriber와 publisher descriptor가 없다고 기록한다. | `ZLinkAutomaticFanoutSubscriberRuntime`과 `ZLinkFanoutDiscovery`가 descriptor별 connection을 구현한다. |

모든 feature-map 행을 `구현`, `부분`, `미구현` 가운데 하나로 다시 판정한다. Source type의 존재만으로
`구현`으로 바꾸지 않는다. Scenario의 모든 관찰 조건을 test가 직접 확인할 때만 `구현`으로 바꾼다.

### DN-TEST-005 — 중앙 regression matrix의 여러 행에 실행 가능한 test가 없다

**판정: test gap.**

`framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md`에는 `unit`,
`integration-single-process`처럼 계층만 적힌 행이 많다. 현재 참조 검사도 backtick으로 적힌 이름만
확인하므로, 구체적인 test 이름이 없는 행은 검증 대상에서 빠진다.

각 행은 다음 중 하나를 가져야 한다.

- 현재 test tree에 존재하는 정확한 test method
- 공통 E2E와 feature-map에 모두 존재하는 scenario ID
- 실행 경로가 고정된 verification script

## 6. 현재 충족 판정

다음 항목은 live source와 test를 다시 확인했으며 새 implementation gap으로 등록하지 않는다.

| 범위 | 현재 증거 |
|---|---|
| ClientServer public surface와 runtime | Builder, discovery, local·remote selection, liveness와 monitoring 구현 및 contract/unit test가 존재한다. |
| Automatic classic fanout | 전용 publisher descriptor, automatic subscriber connection과 discovery runtime이 존재한다. Process E2E coverage는 별도 test gap으로 재검사한다. |
| Framework error surface | 13개 `ZLinkFrameworkErrorKind`와 현재 exact interface의 `RetryAdvice`가 contract test와 일치한다. |
| 알 수 없는 non-JSON content type | `EnvelopeCodecTests`가 decode 전에 거부하는 경로를 검증한다. |
| API snapshot | Source assembly와 고정 API snapshot 72개 contract test가 통과한다. Markdown 직접 비교는 DN-TEST-001에서 보강한다. |

## 7. 작업 순서

### G0 — audit 기준과 회귀 분모를 먼저 고친다

1. 이 ledger의 candidate commit과 working tree manifest를 저장한다.
2. DN-TEST-001과 DN-TEST-002를 구현해 exact interface 전체를 audit 분모로 고정한다.
3. DN-TEST-004에 해당하는 feature-map 전체를 source와 test로 다시 판정한다.
4. 발견한 차이는 이 ledger에 추가한 뒤 다음 구현 단계로 이동한다.

완료 조건은 exact interface 파일, public export와 regression evidence 사이에 소유자가 없는 항목이 0개인
상태다.

### G1 — Host relocation과 Actor Join relocation의 계약 경계를 바로잡는다

1. DN-IMP-007의 commit 뒤 failure를 `Blocked/RelocationFailed`로 끝내고 uncommitted source workload와
   `Serving` descriptor만 복원한다.
2. DN-IMP-004의 startup recovery와 takeover 진입점을 제거하고, process 종료 뒤 unavailable 상태를
   유지하는 회귀 test를 먼저 고정한다.
3. DN-IMP-005의 preflight 결과에서 target 탐색 실패와 cancellation을 구분한다.
4. DN-IMP-008의 absolute deadline을 target commit reconciliation까지 전달하고 authority 재확인으로
   commit 전 timeout과 결과 미확정을 구분한다.
5. DN-IMP-006의 Join completion, queue replay와 Session 위치 갱신 순서를 spec에 맞춘다.
6. Durable Join completion cursor와 restart replay를 제거하되 같은 process 안에서의 commit 뒤 retry와
   idempotency는 유지한다.
7. DN-IMP-009의 terminal-preserving status stream과 표준 structured log를 구현한다.
8. Host relocation과 Actor Join relocation 각각에 production runtime 두 개를 사용하는 process E2E를
   추가한다. Mock coordinator의 호출 횟수만으로 완료 판정하지 않는다.

완료 조건은 정상 경로의 단계 순서, commit 전 rollback, commit 뒤 failure, deadline, concurrent shutdown과
process 종료 경계가 spec과 일치하는 상태다.

### G2 — STREAM configuration 계약을 완성한다

1. DN-IMP-003의 두 대안을 검토하고 exact interface를 먼저 수정한다.
2. Contract test와 public API snapshot을 목표 interface에 맞춘다.
3. Production builder, registration validation과 backend wrapper를 구현한다.
4. Bindings public API가 부족하면 bindings 작업으로 분리하고 Framework에서 우회하지 않는다.

### G3 — RouteMesh·Spot·Actor ingress를 host Application HWM에 연결한다

1. RouteMesh node direct와 ChannelName receive가 host budget을 획득하도록 한다.
2. Spot과 Actor queue에 저장한 payload와 실행 중 payload를 같은 budget으로 계산한다.
3. Relocation temporary queue와 replay는 원본 payload를 두 번 계산하지 않도록 ownership 전환을 한 곳에서
   처리한다.
4. Completion, liveness와 relocation control 경로가 application pause에 막히지 않는지 검증한다.

### G4 — STREAM ingress를 host Application HWM에 연결한다

1. Session packet 수신과 Actor relay가 같은 host budget을 사용하도록 한다.
2. Session close, reply token과 transport control은 application pause와 분리한다.
3. Handler failure, cancellation, disconnect와 shutdown에서 payload bytes를 정확히 한 번 반환한다.

### G5 — STREAM actual endpoint를 확정한다

1. DN-IMP-002를 구현한다.
2. Port `0`, listener override, wildcard host와 restart generation을 unit 및 process test로 검증한다.
3. STREAM endpoint가 다른 descriptor 종류에 기록되지 않는 negative test를 추가한다.

### G6 — 공통 E2E와 feature-map을 현재 계약에 맞춘다

1. DN-TEST-003의 Config 3, 7, 10과 12 누락 ID를 feature-map에 등록하고, 실제 구현과 selector 증거를
   각각 판정한다.
2. Config 1~13의 모든 scenario ID가 `.NET` feature-map에 정확히 한 번 존재하는지 검사한다.
3. `부분` 또는 `미구현` 행은 source gap과 evidence gap을 분리한다.
4. 구현된 selector만 `run_e2e_all.sh`의 완료 분모에 넣고, gap selector를 직접 요청하면 성공으로
   건너뛰지 않도록 유지한다.

### G7 — 최종 회귀와 package 검증

다음 순서로 실행한다.

1. Contract test
2. Framework unit test
3. Redis provider test
4. Stream Connector와 HTTP client test
5. Sample regression
6. `verify_packaged_contract.sh`
7. Config 1~13 process E2E
8. `verify-framework-doc-contracts.sh`
9. 독립 read-only 전체 audit

Repo-wide 문서 검사가 다른 언어 오류에서 중단되면 `.NET` 완료로 숨기지 않는다. `.NET` 범위 통과와
repo-wide blocker를 결과에서 분리한다.

## 8. 기존 회귀 test의 유지·변경·추가 목록

### 그대로 유지할 test

| Test | 유지하는 이유 |
|---|---|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | Source와 고정 API snapshot drift를 검출한다. DN-TEST-001을 대신하지는 않는다. |
| `SourceLayoutContracts.Server_application_contract_source_is_owned_by_the_server_project` | Public contract source owner가 runtime으로 이동하는 회귀를 막는다. |
| `InboundDispatchBudgetTests` 7개 | Host byte accounting primitive의 pause, resume와 terminal 반환을 검증한다. |
| `InboundDispatchOptionsTests` | Auto HWM 계산, 기본 message 상한과 설정 오류를 검증한다. |
| `ClientServerChannelRuntimeTests` | ClientServer transport, actual endpoint, liveness와 selection 회귀를 검증한다. |
| `FanoutAutomaticDiscoveryTests` | Automatic fanout descriptor와 publisher별 connection 회귀를 검증한다. |
| `EnvelopeCodecTests.DecodeBody_Rejects_Unregistered_NonJson_ContentType_Before_Json_Decode` | Content type을 decode 전에 거부하는 계약을 검증한다. |
| `DrainCoordinatorTests.Relocation_workload_coordinator_moves_shells_then_actors_then_aggregates` | 한 process 안에서 host relocation unit 실행 순서를 검증한다. Process 종료 뒤 recovery를 허용하는 근거로 사용하지 않는다. |
| `ActorHandoffTests`의 source seal·target import·replay test | 같은 process가 실행되는 동안 ingress hold와 temporary queue 순서를 검증한다. Restart recovery와 분리한다. |

### 변경할 test

| Test | 변경 내용 |
|---|---|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | 이름이 검증 범위를 과장하지 않도록 API snapshot 범위를 명시하거나 Markdown 비교 test와 역할을 분리한다. |
| `RegressionTests.DotNetContractDocuments_AllExposeRegressionTestSection` | Exact interface 디렉토리 전체를 분모로 사용한다. |
| `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods` | 구체적 test나 scenario가 없는 matrix 행도 실패하도록 강화한다. |
| `RegressionTests.CommonE2EConfigsHaveCompleteDotNetFeatureMapInventories` | 현재 누락을 고친 뒤 중복 scenario ID와 알 수 없는 ID도 거부한다. |
| `InboundDispatchOptionsTests.Memory_Limited_Mode_Requires_A_Finite_Listener_Maximum` | RouteMesh, ClientServer, fanout과 STREAM listener를 각각 포함한다. |
| `DeferredActorJoinDurabilityTests.Canonical_completion_cursors_survive_target_restart_without_losing_phase` | 삭제하거나 반대 계약을 검증하도록 변경한다. Process restart 뒤 Join completion cursor를 복구하면 실패해야 한다. |
| `StandaloneActorRelocationRuntimeTests`의 target owner 종료 뒤 recovery test | 다른 runtime의 restore·takeover 성공을 기대하지 않고, committed object가 unavailable로 남는지 검증한다. |
| Actor Join bound Session test | Session route ACK를 completion과 queue replay의 선행 조건으로 기대하는 assertion을 제거한다. Completion과 dispatch가 먼저 진행되고 위치 갱신이 별도로 retry되는지 검증한다. |
| `DrainCoordinatorTests.RetireFailureAfterCommitForceStopsWithDurableProgress` | Force-stop 기대를 제거한다. Commit한 unit은 target owner로 유지하고 uncommitted source workload만 복원하며 coordinator가 `DrainBlocked(RelocationFailed)`와 ready 상태를 반환해야 한다. |
| `MaintenanceRuntimeTests.Preflight_blocker_keeps_the_host_serving` | Current status에 일반 `Blocked` 결과를 저장하지 않는 assertion은 유지하되 observer가 transient terminal status를 받는 assertion을 추가한다. |

### 새로 추가할 test

| ID | 계층 | 확인 기준 |
|---|---|---|
| DN-REG-001 | contract | Markdown exact interface, source assembly와 package export가 일치한다. |
| DN-REG-002 | documentation | 모든 exact interface 문서가 owner test와 연결된다. |
| DN-REG-003 | unit | RouteMesh node direct와 ChannelName ingress가 같은 host budget에서 pause·resume한다. |
| DN-REG-004 | unit | Spot과 Actor queue·active handler bytes가 host status에 포함되고 terminal에서 한 번 반환된다. |
| DN-REG-005 | unit | Relocation capture·temporary queue·replay 사이 ownership 전환에서 payload를 중복 계산하지 않는다. |
| DN-REG-006 | unit | STREAM session ingress와 Actor relay가 같은 host budget을 사용한다. |
| DN-REG-007 | integration-single-process | ClientServer, fanout, RouteMesh/Spot과 STREAM을 동시에 사용해도 하나의 HWM 합계를 공유한다. |
| DN-REG-008 | unit | Application receive가 중단되어도 completion, liveness와 relocation control이 진행된다. |
| DN-REG-009 | unit | STREAM port `0`의 actual endpoint와 listener `AdvertiseHost` override가 일치한다. |
| DN-REG-010 | unit | Wildcard STREAM bind에서 advertised host가 없으면 bind 전에 실패한다. |
| DN-REG-011 | E2E | `ST-E1B`가 relocation 종류별 bound session 위치 snapshot을 검증한다. |
| DN-REG-012 | E2E | `ST-E1C`가 Session Actor 위치 갱신의 재전송과 exactly-once 적용을 검증한다. |
| DN-REG-013 | E2E | HWM에 도달한 mixed topology에서 handler loss·duplicate 없이 receive가 재개된다. |
| DN-REG-014 | unit | Host preflight의 target 탐색 timeout은 `TargetUnavailable`, 다른 단계 cancellation은 `DeadlineExceeded`로 구분된다. |
| DN-REG-015 | process E2E | Host relocation의 source 또는 target process가 commit 뒤 종료돼도 다른 runtime이 published relocation을 이어받지 않는다. |
| DN-REG-016 | unit | Cross-node Actor Join에서 `OnJoinedActor`, source leave 전송, Join completion과 queue replay 순서를 보존하며 source leave 결과는 completion을 막지 않는다. |
| DN-REG-017 | process E2E | Bound Session 위치 갱신 ACK가 지연되거나 유실돼도 Join completion과 target Actor message 처리가 먼저 진행된다. |
| DN-REG-018 | process E2E | Cross-node Join commit 뒤 target process가 종료되면 다른 runtime이 completion callback을 replay하거나 Actor를 자동 restore하지 않는다. |
| DN-REG-019 | unit | Commit 전 Actor Join 실패는 target temporary queue를 실행하지 않고 source queue를 원래 순서로 복원한다. |
| DN-REG-020 | unit | 첫 host unit commit 뒤 다음 unit이 실패하면 committed authority는 target에 남고 uncommitted source workload와 host `Serving` 상태만 복원된다. Runtime infrastructure는 종료되지 않는다. |
| DN-REG-021 | process E2E | Host relocation이 일부 commit된 뒤 target failure가 발생해도 source host의 ClientServer, fanout, RouteMesh와 STREAM infrastructure가 유지되고 남은 workload를 처리한다. |
| DN-REG-022 | unit | Cross-node Actor Join의 target commit reply가 계속 실패하면 call deadline에 `DeadlineExceeded`로 끝나며 authority 재확인 전에는 source admission을 열지 않는다. |
| DN-REG-023 | unit | 느린 observer가 중간 status를 합치더라도 relocation·shutdown terminal status와 가장 최근 `Sequence`를 모두 받는다. 일반 `Blocked`는 transient terminal status로 관찰되지만 current status에는 저장되지 않는다. |
| DN-REG-024 | unit | Host relocation과 shutdown 변화가 각각 표준 structured log identifier와 mode, state, outcome, reason을 기록하며 logger failure가 terminal result를 바꾸지 않는다. |

## 9. 완료 판정 checklist

- [ ] DN-IMP-001을 수정하고 DN-REG-003~008, 013을 통과했다.
- [ ] DN-IMP-002를 수정하고 DN-REG-009~010을 통과했다.
- [ ] DN-IMP-003의 exact interface를 먼저 확정하고 source와 package를 맞췄다.
- [ ] DN-IMP-004~009를 수정하고 DN-REG-014~024를 통과했다.
- [ ] DN-TEST-001~005를 모두 닫았다.
- [ ] Config 1~13의 공통 scenario와 `.NET` feature-map 차이가 0개다.
- [ ] Contract, unit, provider, connector, HTTP client와 sample regression이 모두 통과했다.
- [ ] 실제 NuGet package export가 source와 exact interface에 일치한다.
- [ ] Process E2E 전체에서 `부분`이나 `미구현`으로 남은 항목은 이 ledger의 열린 gap과 일대일로 연결된다.
- [ ] 마지막 독립 audit에서 기록하지 않은 `.NET` gap이 0개다.
