# .NET Framework spec gap audit와 수정 ledger

> 상태: Phase A (`.NET Framework` spec gap) 진행 중. 현재는 G1 사전 검토 단계이며, Phase B (common
> sample gap)는 Phase A 완료 gate를 통과하기 전에는 시작하지 않는다.
>
> 기준: `3291e338f4f700484780560cd81345a647ef0948`와 2026-08-02 G0 candidate working tree
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
7. Core, bindings 또는 공통 contract 변경이 필요한 항목은 선행 card로 분리한다. 원인을 소유한
   layer에서 regression test와 수정을 끝내고 package를 다시 배포해 Framework가 그 package를 사용하는
   것까지 확인하기 전에는 `.NET` 완료로 처리하지 않는다.
8. Phase A의 모든 implementation·E2E·regression·package gap을 닫고, G7 독립 audit에서 unresolved
   `Critical`·`High`·`Medium` finding이 0건이어야 Phase B를 시작한다.
9. Phase B의 common sample gap은 이 문서의 sample phase에서만 진행한다. Phase A 완료 전에는 sample
   source 수정, sample card 완료 판정과 sample process evidence 수집을 시작하지 않는다.

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
| Process 검증 | `framework/languages/dotnet/e2e/`와 공통 E2E Config 1~14 |

판정은 다음 네 종류를 사용한다.

| 판정 | 의미 |
|---|---|
| `확인` | 계약과 production source의 차이를 직접 확인했다. |
| `test gap` | 구현 여부를 판정하는 회귀 test가 없거나 현재 test가 계약을 직접 검증하지 않는다. |
| `contract 선행` | 공통 계약을 .NET public interface로 표현하는 방법이 부족하여 exact spec을 먼저 고쳐야 한다. |
| `충족` | 현재 source와 실행 결과가 해당 계약을 직접 증명한다. |

Working tree에는 Actor relocation과 관련 E2E 수정이 이미 존재한다. 이 ledger는 해당 변경을 보존한 상태에서
작성했다. 이후 실행 log에는 각 단계의 candidate commit과 working tree diff를 함께 기록해야 한다.

### 1.1 작업 log와 phase 전환

이 ledger의 진행 상황과 검증 결과는 이 문서가 있는 폴더의 `log/` 아래에 기록한다. 각 log는
`log/YYYYMMDD-HHMMSS-<card>.ko.md` 형식을 사용하고, 기록 시점의 card, working tree 조건,
실행한 명령, 통과·실패 수치, Sol review 결과와 남은 조건을 적는다. 진행 상황은 본문을 매번
늘어놓지 않고 해당 log 경로를 기준으로 확인한다. Formal public contract spec은 이 작업 log의
대상이 아니며, 구현 전 설계나 실행 증거는 plan ledger와 그 `log/`에서만 관리한다.

이 문서의 Phase A card와 G7 최종 audit가 모두 완료되면 같은 문서의 `## 10. Phase B — common sample
gap`에서 sample 작업을 이어서 진행한다. Phase B card를 Phase A 완료 판정 전에 섞지 않으며, 전환 시점과
시작 조건은 `log/`에 기록한다. 별도의 sample ledger 문서는 두지 않는다.

### 2.1 Model 배치와 card review gate

이 작업은 Luna Max가 구현을 수행하고 Sol이 독립적으로 검토하는 직렬 흐름으로 진행한다. Luna를
main agent로 선택하고, subagent로 호출할 수 있는 Sol을 reviewer로 사용한다. Model 비용을 줄이기 위해
review 범위를 생략하거나 Luna의 자체 점검으로 대체하지 않는다.

| 역할 | Model과 reasoning | 책임 | Source 수정 |
|---|---|---|---|
| Main implementer | Luna Max | 한 번에 ledger card 하나를 조사하고 test를 먼저 고정한 뒤 구현·검증한다. | 허용 |
| Card reviewer | Sol Medium | 실제 spec, candidate 전체, diff와 test 결과를 읽고 contract·POSD·DDD 관점에서 검토한다. | 금지 |
| Final auditor | Sol Medium | G7 뒤 `.NET` production과 공개 계약 전체를 이전 review와 독립적으로 다시 검사한다. | 금지 |

지정한 model이나 reasoning level을 사용할 수 없으면 임의의 model로 바꾸지 않는다. 해당 review를
`차단`으로 기록하고 사용할 수 있을 때 다시 실행한다. Main implementer와 reviewer는 같은 working tree를
동시에 수정하지 않는다. Reviewer가 동작하는 동안 candidate를 바꾸지 않으며, review가 끝난 뒤에만
Luna가 finding을 수정한다.

각 card는 다음 gate를 순서대로 통과한다.

1. Luna는 card가 따라야 하는 공통 spec, .NET exact interface, production owner와 기존 test를 먼저
   확인한다. Card 범위를 벗어난 gap은 우회해서 함께 고치지 않고 이 ledger에 별도 항목으로 등록한다.
2. Public contract, lifecycle, ownership, state transition 또는 module 경계를 바꾸는 card는 구현 전에
   사건, command, 상태 owner, failure 의미와 서로 다른 설계 대안 두 가지 이상을 적고 Sol Medium의
   사전 review를 받는다. G1~G4에는 이 사전 review를 필수로 적용한다.
3. Luna는 실패를 재현하는 test를 추가하거나 기존 test가 계약을 직접 검증한다는 근거를 남긴 뒤
   production source를 수정한다. Targeted test와 해당 변경이 영향을 주는 regression을 실행한다.
4. Review 직전에 기준 commit, candidate commit 또는 working tree manifest, `git status --short`, 전체
   diff, 실행한 test 명령과 결과를 고정한다. Commit하지 않은 candidate라면 review가 끝날 때까지
   working tree를 변경하지 않는다.
5. Sol Medium은 Luna의 요약만 읽지 않고 정식 spec, exact interface, production call path, 전체 candidate와
   test evidence를 직접 대조한다. Reviewer는 finding만 반환하며 source·test·문서를 수정하지 않는다.
6. `Critical`, `High`, `Medium` finding은 모두 blocking이다. Luna가 원인과 책임 경계를 고치고 관련
   test를 다시 실행한 뒤 같은 범위로 Sol review를 다시 요청한다. `Low` finding도 수용·기각·후속 분리
   가운데 하나와 근거를 기록한다.
7. Sol이 `CLEAN`으로 판정하고 필수 test가 모두 통과한 뒤에만 card를 완료로 표시하고 다음 card로
   이동한다. Reviewer 부재, 미실행 test, unresolved finding은 완료 증거가 아니다.

#### POSD·DDD review 기준

Sol reviewer는 contract 일치 여부와 함께
[`software-design-principles.ko.md`](../../../../doc/principal/software-design-principles.ko.md)를 기준으로
다음 질문에 답한다.

1. POSD 관점에서 shallow module, information leakage, pass-through method·variable, temporal
   decomposition, 중복, 특수·범용 경로 혼합 또는 호출자에게 전달한 복잡성이 새로 생겼는가.
2. 기존 public interface와 표준 호출 경로로 해결할 수 있는데 새 helper, option, wrapper 또는 parallel
   abstraction을 추가했는가. 인터페이스 변경이 있다면 호출자가 알아야 하는 개념과 순서가 실제로
   줄었는가.
3. DDD 관점에서 lifecycle, ownership, state transition, commit phase, deadline과 failure invariant의
   owner가 한 경계에 모여 있는가. 같은 용어가 spec·source·test에서 같은 의미로 쓰이는가.
4. Host Application HWM의 byte accounting과 payload ownership transfer, host·Actor relocation의 실행
   권한과 commit, Session 위치 갱신과 STREAM endpoint처럼 이 ledger가 다루는 상태를 둘 이상의
   module이 서로 다른 기준으로 결정하는가.
5. Framework의 상태 규칙에 transport·codec·storage detail이 섞이거나, adapter가 도메인 상태를
   결정하는가. Relocation Store의 durable data를 새 runtime의 실행 권한으로 해석하는 것처럼 저장된
   사실과 lifecycle authority를 혼동하는가.
6. 새 DDD 이름이나 계층이 요청을 전달만 하는가. Domain 경계를 지킨다는 이유로 mapper, service,
   wrapper를 늘려 변경 증폭과 인지 부하를 키웠는가.

비자명한 구조 finding에는 서로 다른 대안 두 가지 이상을 제시하고 단순성, 일반성, 성능, 호출자 부담과
책임 경계를 비교한 뒤 권장안을 선택한다. Finding에는 ID, severity, category(`contract`, `POSD`, `DDD`,
`test/evidence`, `leftover`), file·line, 근거, 위반한 계약이나 원칙, 영향, 대안, 권장안과 재검토 결과를
기록한다. 근거가 없는 선호나 style 차이는 blocking finding으로 분류하지 않는다.

### 2.2 Core·bindings 선행 수정과 package 배포 gate

Framework를 구현하거나 E2E를 추가하는 과정에서 Core 또는 bindings bug가 확인되면 Framework에서
우회하지 않는다. 호출자가 raw frame을 해석하거나, private·internal API를 reflection으로 호출하거나,
Framework 전용 helper·adapter·상태 복제·retry 경로를 추가해서 하위 layer의 실패를 숨기는 변경은
완료로 인정하지 않는다.

하위 layer 문제는 다음 순서로 처리한다.

1. Public contract, production call path와 최소 재현으로 원인을 소유한 layer를 확정한다. 필요한 public
   contract가 spec에 없다면 구현하지 않고 `contract 선행`으로 등록한 뒤 이 저장소의 target-first 또는
   draft 규칙에 따라 spec review부터 진행한다.
2. Core bug는 Core test에, bindings bug는 해당 bindings test에 실패를 재현하는 regression을 먼저
   추가한다. Framework test만 실패하는 상태는 하위 library 수정의 완료 증거가 아니다.
3. 원인을 소유한 layer에서 수정한다. Core의 lifecycle·socket·transport 책임을 bindings나 Framework로,
   bindings의 public projection·ownership 책임을 Framework로 옮기지 않는다.
4. 수정한 Core 또는 bindings candidate에도 2.1절의 Luna Max 구현과 Sol Medium read-only review gate를
   동일하게 적용한다. Reviewer는 해당 layer의 public contract, 전체 diff, regression과 package 입력을
   직접 확인하고 POSD·DDD 책임 경계가 상위 layer로 누출되지 않았는지 판정한다.
5. Core를 수정했으면 `core/build`를 다시 build하고 관련 Core test를 통과시킨다. 이어서
   [`scripts/local-package/README.ko.md`](../../../../scripts/local-package/README.ko.md)의 version 정책에
   따라 Core와 bindings version을 맞추고, `scripts/local-package/native/sync-local-core-libs.sh`로 새
   runtime을 bindings workspace에 동기화한 뒤 필요한 local package를 다시 만든다.
6. .NET bindings만 수정했으면 .NET package version을 올리고
   `scripts/local-package/build-wsl.sh dotnet`으로 NuGet package를 만든다. Core를 수정했으면 README가
   정한 모든 bindings version·package 조건을 적용하며, `.NET` package만 새 Core에 맞춘 상태를 전체
   bindings 배포 완료로 기록하지 않는다.
7. 새 package를 만든 뒤 `framework/languages/dotnet/Directory.Packages.props`의
   `ZLinkBindingsPackageVersion`을 검증한 version으로 갱신한다. 같은 version package를 개발 중 다시
   만들었다면 `~/.nuget/packages/systems.zlink/<version>/runtimes/`에 풀린 이전 native cache를 정확한
   경로 확인 없이 재사용하지 않는다.
8. Package 안의 native runtime, public export와 version을 확인하고
   `framework/languages/dotnet/scripts/verify_packaged_contract.sh`, 관련 Framework regression과 실제
   process E2E를 새 package로 다시 실행한다. Source tree의 수정본을 직접 참조하거나 이전에 추출된
   package cache로 통과한 결과는 증거로 인정하지 않는다.

선행 card에는 원인 layer, 재현 test, fix candidate, Sol review 결과, Core·bindings version, package 경로와
hash, Framework 참조 version, consumer test를 기록한다. 이 증거가 하나라도 없으면 원래 Framework card는
`선행 조건 미충족`으로 유지한다.

## 3. 현재 검증 결과

| 검증 | 결과 | 현재 판단 |
|---|---|---|
| 기존 `dotnet test tests/Zlink.Framework.ContractTests/...` | 74/74 통과 | Roslyn full declaration test를 추가하기 전 snapshot·contract test 결과다. Source assembly와 고정 API snapshot만 확인하므로 exact-interface 일치의 단독 증거로 사용하지 않는다. |
| 현재 exact-interface declaration·owner test | 2/2 + 2/2 통과 | 14개 exact interface 문서의 고정 owner FQN과 source·compiled package contract를 양방향으로 대조한다. Positional record projection과 SemanticModel 기반 assembly/FQN binding을 포함한다. |
| 현재 전체 `.NET` ContractTests | 76/76 통과 | Exact-interface declaration·owner test와 public snapshot, error·record·package contract를 포함한 전체 contract suite다. |
| 현재 전체 `.NET` UnitTests | 1388/1388 통과 | `--logger 'console;verbosity=normal'`로 2분 51초 실행했다. Negative configuration test가 예상된 host-start error log를 남겼지만 testhost crash·timeout 없이 전체 suite가 종료되었다. |
| 현재 targeted relocation unit test | 248/248 통과 | `DrainCoordinatorTests`, `MaintenanceRuntimeTests`, `ActorHandoffTests`, `EntrySpotActorDispatchTests`, `LocationRuntimeQueryTests`를 현재 dirty tree에서 실행했다. 이전 `DrainSpots` delegate compile blocker는 해소되었다. |
| 현재 documentation regression 단독 | 20/20 통과 | Exact interface 14개 문서의 owner matrix와 Config 1~14 feature-map inventory, 중복·unknown ID 검사가 현재 G0 candidate에서 통과한다. |
| 현재 sample regression | 134/134 통과 | Phase B baseline evidence다. Scenario canonical ID/name 검사와 common TicTacToe의 Entry Spot destroy ownership 문장을 포함하지만 Phase A 완료나 Phase B 전체 완료를 증명하지 않는다. |
| `verify-framework-doc-contracts.sh` | 중단 | Service wire 검사는 통과했다. C++ member override의 target signature 누락에서 중단되어 `.NET` 문서 전체 CLEAN 증거로 사용할 수 없다. |

`ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`는 이름과 달리 Markdown
spec을 읽지 않는다. `framework/languages/dotnet/contract/api/*.api.txt`와 reflection 결과만 비교한다.
따라서 이 test의 통과를 exact interface와 구현이 일치한다는 최종 증거로 사용하지 않는다.

### 3.1 Relocation 동작 방식 재검사 요약

다음 표는 public type이나 method의 존재 여부가 아니라 production call path의 실행 순서를 spec과 대조한
결과다. `부분 충족`은 정상 경로 일부가 같다는 뜻이며 완료 판정이 아니다.

| 범위 | Spec이 정한 경계 | Live production path | 판정 |
|---|---|---|---|
| Host preflight | Host state와 admission을 바꾸기 전에 local workload, Store, policy와 target을 확인하고 이미 수락한 operation을 정리한다. | `PreflightRetireAsync(...)`가 state 전이보다 먼저 실행되지만 `WaitForAcceptedOperationsForDrainAsync()`는 Relocate preflight에서 호출하지 않는다. | DN-IMP-005, DN-IMP-017 |
| Host workload handoff | Unit별 restore·commit을 끝내고 모두 source dispatch에서 분리한 뒤 `Relocated`가 된다. Infrastructure는 유지한다. | PerActor shell, Actor, aggregate 순서로 unit을 옮긴 뒤 admission을 seal하고 infrastructure teardown 없이 반환한다. | 부분 충족. 전체 process E2E 필요 |
| Host deadline | Target 탐색 실패와 다른 preflight cancellation을 구분하고 unit callback·cleanup까지 같은 absolute deadline을 적용한다. | Preflight cancellation은 `TargetUnavailable`로 합쳐질 수 있고, Spot unit은 activation timeout으로 별도 deadline을 계산하며 post-commit source cleanup은 `CancellationToken.None`을 사용한다. | DN-IMP-005, DN-IMP-011 |
| Host process failure | Process 종료 뒤 다른 runtime이 relocation을 이어받지 않는다. | Startup recovery와 takeover가 published relocation을 restore하고 계속 진행한다. | DN-IMP-004 |
| Host commit 뒤 failure | Commit한 unit은 target owner로 유지하고, 아직 옮기지 않은 source workload만 복원한 뒤 host를 `Serving`으로 전환한다. | 한 unit이라도 commit한 뒤 실패하면 `ForceStopAsync(...)`를 실행하여 runtime infrastructure를 종료하고 host를 `Error`로 전환한다. | DN-IMP-007 |
| Host terminal 관찰 | 느린 observer에서도 relocation·shutdown terminal status를 생략하지 않으며 표준 identifier로 structured log를 남긴다. | Observer channel이 모든 status에 `DropOldest`를 적용하고, 일반 `Blocked` 결과는 status에 넣지 않은 채 게시한다. 표준 host identifier도 기록하지 않는다. | DN-IMP-009 |
| Cross-node Join prepare·commit | Target admission 뒤 source를 seal·capture하고 target restore 뒤 authority를 commit한다. Entry Spot target은 admission 없이 commit한다. | User Spot admission과 relocation 순서는 구현하지만 Entry Spot도 admission descriptor가 없으면 reject하고, target reservation 결과와 post-commit reason을 일부 잃는다. | DN-IMP-010, DN-IMP-012, DN-IMP-016, DN-IMP-018 |
| Join deadline | `Defer()` 시점의 absolute deadline을 admission, resolve, prepare, restore와 authority commit 전체에 적용한다. | Commit reconciliation은 `runtime.ShutdownToken`을 사용하고, admission request는 `DefaultRequestTimeout`으로 새 deadline을 계산한다. | DN-IMP-008, DN-IMP-015 |
| Join lifecycle | `OnJoinedActor` 뒤 source leave를 one-way으로 보내되, leave 결과가 target completion을 막지 않는다. | Source가 `ReconcileCommittedSourceLeaveAsync(...)`를 끝낼 때까지 기다린 뒤 target completion request를 보낸다. | DN-IMP-006 |
| Bound Session Join | Join completion과 target dispatch 뒤 위치 갱신을 시작하며 ACK는 Actor 처리를 막지 않는다. | Session route commit ACK를 기다린 뒤 Join completion과 queue replay를 실행한다. | DN-IMP-006 |
| Join process failure | Completion cursor는 process memory에만 두며 restart 뒤 callback을 replay하지 않는다. | Store의 durable cursor와 published root로 restart 뒤 `Accepted` callback과 handoff를 복구한다. | DN-IMP-006 |

### 3.2 이번 추가 implementation-level 재검토

다음 항목은 이전 표의 정상 경로 요약만으로는 드러나지 않아 production call path를 다시 따라가며
확인한 차이다. 각 항목은 기능의 존재 여부가 아니라 실패 시점, owner 변경 경계, callback 대상과
public contract를 비교한 결과다.

| 범위 | 확인한 구현 차이 | 판정 |
|---|---|---|
| Spot terminal result | `ZLinkSpotNodeCatalog`가 unit 결과를 `bool`로 축약하고, `ZLinkRelocationWorkloadCoordinator`가 aggregate `TerminalReason`을 버린다. | DN-IMP-010 |
| Maintenance deadline | Spot unit deadline을 host option에서 받지 않고 activation default로 새로 계산한다. Post-commit source cleanup 일부는 `CancellationToken.None`을 사용한다. | DN-IMP-011 |
| Actor Join admission | Caller가 `Timeout(...)`으로 고정한 deadline 대신 target admission packet에 `DefaultRequestTimeout` 기반 시각을 넣는다. | DN-IMP-015 |
| Entry Spot Join | Local·remote target 모두 `OnActorJoin` descriptor를 찾고, 없으면 `Rejected`를 반환한다. Entry Spot exact interface에는 이 callback이 없다. | DN-IMP-016 |
| Preflight gate | Relocate preflight가 actor handoff admission만 기다리고 `_activeOperations` 전체를 기다리지 않는다. | DN-IMP-017 |
| Actor failure mapping | Capture·restore·store 오류와 commit 뒤 오류를 동일한 `RelocationFailed` 경로로 축약할 수 있다. | DN-IMP-018 |
| Metrics | `CreateRelocation(...)` 호출이 Actor 경로에만 있고 Spot relocation은 object kind별 metric을 생성하지 않는다. | DN-IMP-014 |
| Public completion shape | 이전 source에 `IsRetriable`가 있었으나 G0 candidate는 `OperationId`와 `Kind`만 노출하도록 exact interface와 맞췄다. | DN-IMP-013 |

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

### DN-IMP-010 — Spot relocation이 terminal reason과 commit 경계를 잃는다

**판정: 확인. Spot 결과를 `bool` 하나로 축약하면서 owner 변경 뒤 실패를 commit 전 실패처럼 처리할 수 있다.**

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
첫 owner 변경 뒤 실패하면 target owner를 유지하고 `Blocked/RelocationFailed`를 반환하도록 한다. 아직
commit하지 않은 source workload만 복원해야 하며, target owner를 source로 되돌리면 안 된다.

현재 production path에는 다음 손실이 있다.

1. `ZLinkSpotNodeCatalog.TryRelocateForRetireAsync(...)`는 각 unit의 예외를 잡아 `false`로 바꾸고,
   성공한 unit 수만 `CommittedUnitCount`로 계산한다. `RelocationDisabled`, Store 오류, callback 오류와
   post-commit 오류가 같은 값으로 축약된다.
2. `ZLinkRelocationWorkloadCoordinator.DrainAsync(...)`는 aggregate 결과를 만들 때
   `aggregates.TerminalReason`을 `null`로 고정한다. Aggregate가 반환한 terminal reason이 host까지 전달되지
   않는다.
3. `ZLinkSpotRetireScheduler.TryRelocateAsync(...)`는 target stage 뒤 `committed = true`로 바꾼다. 그
   뒤 `CompleteCommittedAsync(...)`가 실패하면 catalog가 해당 unit을 `false, 0`으로 보고할 수 있다.
   `ZLinkActorDrainCoordinator`와 `ZLinkStandaloneActorRelocationRuntime`에도 같은 post-commit 경계가 있다.
4. `ZLinkFrameworkDrainExecutor`는 terminal reason과 committed count가 모두 0이면
   `RollBackBlockedRetireAsync(...)`를 호출한다. Location authority가 이미 target을 가리키는 상황에서도
   source admission과 descriptor를 다시 열 수 있다.

**수정 범위**

1. Spot·Actor unit 결과에 terminal reason, commit 전후 단계와 실제 committed count를 함께 전달한다.
2. Aggregate의 terminal reason을 workload coordinator와 host result까지 보존한다.
3. Owner commit 뒤 failure는 source rollback이나 source route 재개 없이 `Blocked/RelocationFailed`로
   끝낸다. 미commit unit만 source queue와 admission을 복원한다.
4. Commit 경계를 확인할 수 없으면 Location Store authority를 재확인한 뒤에만 rollback 여부를 결정한다.

### DN-IMP-011 — Host relocation의 absolute deadline이 Spot unit과 cleanup callback에 전달되지 않는다

**판정: 확인. Host option의 deadline과 unit 내부 deadline이 서로 다른 clock과 timeout을 사용한다.**

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
callback 취소, owner 변경 전 작업과 cleanup을 포함한 operation 전체가 deadline 안에 완료되어야 한다고
정한다. Deadline을 넘긴 단계는 원인에 맞게 `DeadlineExceeded` 또는 commit 뒤
`RelocationFailed`로 끝나야 한다.

현재 `ZLinkSpotNodeCatalog.TryRelocateForRetireAsync(...)`는 host에서 받은 absolute deadline을 받지 않고
모든 activation의 `DefaultRequestTimeout` 최댓값으로 새 deadline을 만든다. 따라서 여러 phase를 순서대로
실행하는 host deadline과 Spot callback·reply·cleanup의 deadline이 다르다. 또한
`ZLinkSpotRetireScheduler.CompleteCommittedAsync(...)`의 source completion과
`ZLinkStandaloneActorRelocationRuntime.CompleteCommittedSourceAsync(...)`의 post-commit 단계 일부가
`CancellationToken.None`으로 실행된다. Host deadline이 끝나도 이 callback과 source cleanup이 계속 대기할 수
있다.

**수정 범위**

1. `RelocateAsync`의 monotonic 또는 absolute deadline을 every unit, target reservation, restore,
   lifecycle callback과 source cleanup에 전달한다.
2. Unit을 시작하기 전에 남은 시간을 검사하고, 각 retry와 delayed cleanup도 같은 deadline token을 사용한다.
3. Owner commit 여부를 확인한 뒤 deadline failure를 `DeadlineExceeded`와 `RelocationFailed`로 구분한다.

### DN-IMP-012 — Actor Join target reservation의 `TargetUnavailable`를 `NotFound`로 변환한다

**판정: 확인. Requested Spot이 없는 경우와 target capacity·owner fence가 사용할 수 없는 경우의 오류가
같아진다.**

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)은 요청한 User
Spot ID 자체가 없으면 `NotFound`, 이동할 Entry Spot이나 호환 target node가 없거나 owner/membership
fence를 사용할 수 없으면 `Unavailable`을 반환하도록 정한다.

`ZLinkFrameworkRuntimeActors.AdmitRoutedActorJoinAsync(...)`의 capacity reservation switch는
`ZLinkRelocationCapacityReserveResult.TargetUnavailable`를 `ZLinkFrameworkErrorKind.NotFound`로 만든다.
반면 `ZLinkCanonicalRelocationReservationOwner`는 같은 결과를 `Unavailable`로 매핑한다. 동일한 Store
결과가 target 경로에 따라 다른 public error가 된다.

**수정 범위**

1. Requested Spot lookup 실패와 reservation target unavailable을 서로 다른 mapping으로 유지한다.
2. Local·remote Join과 retry reply가 모두 `Unavailable`을 사용하도록 공통 mapper를 둔다.

### DN-IMP-013 — `ZLinkActorJoinCompletion.Failed`의 public shape가 exact interface와 다르다

**이전 판정: contract 선행. Source가 exact interface 문서보다 큰 public record를 제공했다.**

.NET exact interface
[`06-actors.ko.md`](../../framework/common/spec/server/languages/dotnet/interfaces/06-actors.ko.md)는
`Failed(OperationId, Kind)`만 정의한다. 실제 source
`framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorContext.cs`는 여기에
`bool IsRetriable`를 추가하고, `ZLinkDeferredActorJoin`과 sample은 이 값을 사용했다. 이 문장은 이전
audit의 source 상태를 설명한다.

API snapshot test가 source assembly와 snapshot만 비교하므로 Markdown exact interface가 이 extra field를
허용하는지 검증하지 않는다. 이 값이 필요한지 여부를 source 사용 사례와 공통 error contract로 먼저 결정한
뒤, exact interface·guide·package·test를 한 번에 맞춰야 한다.

**G0 candidate 수정 결과 (2026-08-02):**

- `Failed`는 `OperationId`와 `Kind`만 노출하도록 source, API snapshot, contract test를 맞췄다.
- `ZLinkDeferredActorJoin`은 `RetryAdvice`를 public completion field로 복사하지 않는다. 재시도 여부는
  오류 종류와 업무 상태, idempotency를 확인하는 caller/application policy가 결정한다.
- Bingo, TicTacToe, SupportChat sample payload에서 `IsRetriable`를 제거했고, 기존 protobuf field 3은
  호환성을 위해 reserved로 남겼다. Common server guide도 retry hint를 public completion 계약처럼
  설명하지 않는다.
- Sol Medium의 사전 review에서 제기된 `IsRetriable` High finding은 이 candidate에서 처리했다.
- Exact-interface declaration test는 14개 문서의 public declaration을 Roslyn syntax로 source와 대조하고,
  constructor는 source declaration과 일대일로 비교한다. Compiled package export는 API snapshot의 전체
  public surface 비교로 별도 확인하고, type owner uniqueness도 별도 assertion으로 유지한다. 현재 DN-IMP-019~023의
  known gap을 정확히 보고하며, 이 실패를 G0 PASS로 세지 않는다.
- `ST-A2` process E2E는 통과했지만 `ST-C3`는 두 번째 source-leave failure subcase에서 기존
  `join_failed` completion ordering gap으로 실패했다. 이는 DN-IMP-006의 열린 문제이며 G0에서 우회하지
  않는다.

### DN-IMP-014 — Object relocation metrics의 종류와 terminal outcome이 구현되지 않았다

**판정: 확인. Host-wide metric과 별개인 Actor·User Spot·Instance Spot relocation metric이 일부 경로에서
누락된다.**

[Runtime metrics §5](../../framework/common/spec/25-runtime-metrics.ko.md#5-host-relocation과-shutdown)은
`zlink.relocation.started`, `completed`, `duration`, `bytes`에
`object_kind=actor|user_spot|instance_spot`와 `outcome=completed|aborted|failed|shutdown`을 요구한다.

현재 `CreateRelocation(...)` 호출은 `ZLinkActorRemoteJoiner`와
`ZLinkStandaloneActorRelocationRuntime`에만 있다. `ZLinkSpotNodeCatalog`와
`ZLinkSpotRetireScheduler`에는 Spot relocation metric 생성이 없으므로 User Spot과 Instance Spot
relocation을 object kind별로 집계하지 않는다. 또한 standalone Actor catch 경로는 commit 여부만 보고
`Completed` 또는 `Aborted`를 기록하여 post-commit completion failure와 runtime shutdown을
`Failed`·`Shutdown`으로 구분하지 못한다.

**수정 범위**

1. Actor maintenance, User Spot aggregate, PerActor shell과 Instance Spot 각각에서 동일한 metric
   lifecycle을 시작하고 terminal outcome을 한 번만 기록한다.
2. Commit 전 abort, commit 뒤 failure와 shutdown cancellation을 metric outcome으로 분리한다.
3. Host-wide operation metric과 object relocation metric을 섞지 않고, spec에 없는 ID label을 추가하지
   않는다.

### DN-IMP-015 — Actor Join admission이 caller의 absolute deadline을 사용하지 않는다

**판정: 확인. `Defer()`에서 계산한 deadline과 target admission·reservation의 deadline이 다르다.**

[Spot Actor §3](../../framework/common/spec/15-spot-actor.ko.md#3-entry-spot과-user-spot의-actor-membership)은
`Defer()` 시점에 absolute deadline을 고정하고 handler tail, admission, restore와 completion에 같은
deadline을 적용하도록 한다.

`ZLinkDeferredActorJoin`은 caller timeout에서 남은 시간을 계산하고 linked token을 만든다. 그러나
`ZLinkActorRemoteJoiner.SubmitRoutedJoinActorTransactionAsync(...)`는
`DateTimeOffset.UtcNow + registration.DefaultRequestTimeout`으로 `admissionDeadline`을 새로 만든다.
Target의 `ZLinkActorHandoffAdmissions.AdmitReservedAsync(...)`는 callback과 capacity reservation을 먼저
실행하고 `RegisterReservedAsync(...)`에서야 request deadline을 확인한다. Caller deadline이 끝난 뒤에도
target callback, Store reservation과 pending admission이 생성될 수 있다.

**수정 범위**

1. `Defer()`에서 고정한 absolute deadline을 admission packet, target callback, reservation과 commit retry에
   전달한다.
2. Target은 callback과 capacity reservation 전에 deadline을 확인하고, 만료된 reservation은
   `DeadlineExceeded`로 cleanup한다.
3. Caller deadline 만료 뒤 source admission을 다시 열기 전에 current authority와 reservation 상태를
   확인한다.

### DN-IMP-016 — Entry Spot Join이 지원하지 않는 admission callback을 실행한다

**판정: 확인. Entry Spot의 public contract와 local·remote implementation path가 다르다.**

[Spot Actor §4.1](../../framework/common/spec/15-spot-actor.ko.md#41-entry-spot과-user-spot의-callback-비교)과
[Spot model](../../framework/common/spec/11-spot-model.ko.md#3-actor-membership)은 Entry Spot에
`OnActorJoin`이 없으며, User Spot target에서만 admission callback을 실행한다고 정한다. Entry Spot으로
복귀할 때는 admission 없이 membership을 commit하고 target `OnJoinedActor`, source `OnLeaveActor`만 실행한다.
같은 규칙은 cross-node Join과 host maintenance에서 모두 적용된다.

현재 `ZLinkActorEntrySpotJoinCoordinator.JoinLocalEntrySpotAsync(...)`는 Entry activation에서
`TryResolveActorJoin`을 호출한다. Descriptor가 없으면 `Reject()`를 반환하므로 정상적인 Entry Spot Join이
거부된다. `ZLinkActorRemoteJoiner.JoinEntrySpotAsync(...)`는 routed admission을 사용하고,
`ZLinkFrameworkRuntimeActors.AdmitRoutedActorJoinAsync(...)`도 Entry Spot에 `AdmitActorJoinAsync`를
호출하거나 descriptor가 없으면 reject한다. `ZLinkSpotDescriptorFactory`는 exact Entry interface에 없는
concrete `OnActorJoinAsync` method까지 reflection으로 발견한다.

**수정 범위**

1. Entry Spot descriptor와 target admission에서 `OnActorJoin` 경로를 제거한다.
2. Local·cross-node Entry Join은 capacity·restore·authority commit을 수행하되 admission callback 없이
   Accepted를 만들고, commit 뒤 Entry `OnJoinedActor`와 source `OnLeaveActor`만 실행한다.
3. Entry Spot concrete type에 우연히 같은 이름의 method가 있어도 public contract로 노출하거나 실행하지
   않는다.

### DN-IMP-017 — Relocate preflight가 일반 accepted operation을 기다리지 않는다

**판정: 수정 완료. Sol Medium read-only review `CLEAN`.**

[Host Relocate §4](../../framework/common/spec/28-graceful-drain-handoff.ko.md#4-target을-선택하기-전에-확인하는-조건)는
target을 고르기 전에 Create, Join, Instance placement, session binding, inbound relocation과
infrastructure operation을 확인하고 먼저 끝낼 작업을 확정하도록 한다. 이 검사가 끝난 뒤에만 host
state·descriptor와 admission 경계를 바꾼다.

`ZLinkFrameworkRuntime.PreflightRetireAsync(...)`는 Spot preflight 뒤
`WaitForAcceptedActorHandoffsAsync(...)`만 호출하고 `_activeOperations`를 감시하는
`WaitForAcceptedOperationsForDrainAsync()`를 호출하지 않는다. 같은 method는 Shutdown 경로에서만
`ZLinkFrameworkDrainExecutor`를 통해 사용된다. 따라서 accepted Create/Join·session/inbound operation이
남아 있어도 target preflight가 끝나고 Relocating publication과 unit seal이 시작될 수 있다.

**수정 범위**

1. Relocate preflight에 일반 operation gate와 actor handoff gate를 모두 포함한다.
2. Host absolute deadline 안에 끝나지 않은 gate를 `OperationInProgress` 또는 `DeadlineExceeded`로 구분하고,
   source state를 `Serving`으로 유지한다.
3. Preflight가 끝난 뒤 publish와 seal 사이에 새 accepted operation이 들어가지 않도록 현재 admission
   protocol과 fence를 함께 검증한다.

**2026-08-02 구현 및 검증 evidence**

- `PreflightRetireAsync(...)`는 일반 accepted operation의 zero transition과 actor handoff의 safe 상태를
  함께 기다린다. operation·actor admission·handoff epoch와 count를 다시 비교한 뒤에만 relocation
  admission fence와 owner generation을 commit한다.
- Relocation rollback은 동일 generation의 rollback lease를 획득한 경우에만 실행한다. Shutdown이 owner를
  넘겨받으면 lease reference를 gate lock 안에서 먼저 제거하고 cancellation은 lock 밖에서 실행하므로,
  rollback 복원이 Shutdown admission을 다시 열지 않는다. 복원 중 owner가 바뀌거나 post-restore 검증이
  실패하면 `TeardownFailed`로 fail-closed 처리한다.
- 추가한 회귀 test는 zero-transition waiter, stale operation baseline, Shutdown takeover, stale fence,
  invalidated rollback lease와 partial publication failure를 각각 검증한다. 관련 targeted unit test는
  `6/6 PASS`다.
- Sol Medium 최종 read-only review는 `Critical 0`, `High 0`, `Medium 0`인 `CLEAN`을 판정했다.
  formal exact-interface 문서와 public contract diff는 없다. 다음 G1 card로 진행한다.

### DN-IMP-018 — Actor maintenance relocation의 failure mapping이 구현 단계 정보를 잃는다

**판정: 확인. Capture·restore·Store failure와 owner commit 뒤 failure가 같은 `RelocationFailed`로 축약될 수
있다.**

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
commit 전 Store failure를 `StoreUnavailable`, adapter·factory·restore incompatibility를
`StateIncompatible`, deadline을 `DeadlineExceeded`, commit 뒤 target runtime failure를
`RelocationFailed`로 구분한다.

`ZLinkStandaloneActorRelocationRuntime.RelocateSourceAsync(...)`는 `Committed`, `Deferred`,
`TargetRejected`만 반환하고 예외에는 commit 단계와 원인 정보를 함께 싣지 않는다.
`ZLinkActorDrainCoordinator.MoveActorAsync(...)`는 retriable이 아닌 `ZLinkFrameworkException`과 기타 예외를
대부분 `RelocationFailed`로 반환한다. 따라서 Capture exception, payload incompatibility, pre-commit Store
failure와 post-commit cleanup failure를 caller가 구분할 수 없다. Actor preflight가 일부 policy와 capacity를
검사한다는 사실만으로 runtime failure mapping이 충족되지는 않는다.

**수정 범위**

1. Actor relocation result에 commit phase와 typed terminal reason을 포함시킨다.
2. Capture·factory·restore·Store·deadline failure를 owner commit 전후에 따라 정확히 매핑한다.
3. Target rejection과 retryable availability를 terminal failure와 구분하고, source rollback은 commit 전
   결과에만 허용한다.

### DN-IMP-019 — Location runtime status의 exact interface와 source public shape가 다르다

**판정: 충족. Formal exact-interface 문서는 유지하고 source·package의 public shape를 네 개의 필드로 맞췄다.**

[.NET Location 설정과 운영 공개 인터페이스](../../framework/common/spec/server/languages/dotnet/interfaces/08-location-maintenance.ko.md#3-readiness와-운영-query)는
`ZLinkLocationRuntimeStatus`에 `StoreHealthy`, `OwnerLeaseHealthy`, `LastRefreshAt`과
`OwnerLeaseRenewedAt`만 정의한다. 현재 source의
`framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Diagnostics.cs`도 이 네 필드만
public으로 export한다. Store failure E2E와 backend mapping은 readiness와 timestamp를 사용하며, 제거된
진단 필드는 public 관찰 계약으로 남아 있지 않다.

`Diagnostics.cs`에서 세 개의 public 상태를 제거하고 Store failure 관찰에는 readiness와 timestamp만 사용하도록
E2E·backend mapping을 맞췄다. Roslyn exact declaration test, source reflection과 package snapshot이 같은
public shape를 확인한다.

변경한 정식 기준은 없으며, formal exact-interface 파일은 working tree에서 수정하지 않았다.

### DN-IMP-020 — `ZLinkPageRequest`의 첫 페이지 기본값이 exact interface와 다르다

**판정: 충족. Source constructor default를 `100`으로 맞추고, runtime policy는 legacy `0` 입력을 `100`으로
정규화하며 음수와 상한 초과를 거부한다.**

같은 `.NET Location` exact interface는 `ZLinkPageRequest(PageSize = 100, ContinuationToken = null)`을
정의한다. `ZLinkPageRequestPolicy`를 runtime owner로 두어 Store와 operational query의 첫 페이지 의미를
한 곳에서 정규화한다.

Contract test와 `Page_Request_Policy_Normalizes_Default_And_Rejects_Invalid_Sizes` unit test가 default·범위
계약을 직접 확인한다.

Formal exact-interface 파일은 수정하지 않았다. `0`은 runtime compatibility input으로만 처리하고 public
constructor의 계약 기본값은 `100`으로 고정한다.

### DN-IMP-021 — Framework error의 retry advice export가 exact interface에 없다

**판정: 충족. `ZLinkRetryAdvice`와 `RetryAdvice`를 internal runtime policy로 제한하고 public exception은
`ErrorKind`만 export한다.**

[.NET Framework 오류 공개 인터페이스](../../framework/common/spec/server/languages/dotnet/interfaces/10-monitoring-errors.ko.md)는
application이 오류 종류와 업무 상태를 확인해 다음 operation을 결정하며 public exception에서 retry hint를
받지 않는다고 명시한다. Source와 package snapshot에서 retry advice export를 제거했고, runtime 내부 호출은
같은 assembly 또는 friend assembly에서만 사용한다.

Formal exact-interface 파일을 수정하지 않고 source, package snapshot, caller test를 public error contract에
맞췄다.

### DN-IMP-022 — `ZLinkConfigurationException` public constructor가 exact interface에 없다

**판정: 충족. Constructor를 internal로 제한하고 `Zlink.Framework.AspNetCore`에만 friend access를 부여했다.**

현재 `Zlink.Framework.Contracts.Errors.ZLinkConfigurationException`은 Framework와 ASP.NET Core runtime이
configuration failure를 만들 때 사용하는 public type이다. Application이 직접 생성하지 못하도록 public
constructor를 추가하지 않았으며, bidirectional declaration gate와 package export가 이를 확인한다.

### DN-IMP-023 — `ActorRef` constructor parameter name이 exact interface와 source에서 다르다

**판정: 충족. Source를 positional record projection으로 맞춰 constructor와 generated `Deconstruct`의 parameter
name을 `ActorId`, `ObjectGeneration`, `MeshName`, `NodeRid`로 고정했다.**

`ActorRef`는 named argument를 사용할 수 있는 public constructor이므로 parameter name도 public signature의
일부다. Positional property의 custom `init` accessor에 기존 validation을 유지했으며 constructor·property
accessor·generated `Deconstruct` parameter name을 reflection과 API snapshot으로 검증한다.

## 5. 확인된 regression test와 inventory gap

### DN-TEST-001 — Markdown exact interface와 export를 직접 비교하지 않는다

**판정: 충족.**

기존 public surface test는 reflection 결과와 별도 API snapshot만 비교했다. Exact interface 문서가 바뀌고
snapshot을 갱신하지 않거나, snapshot과 문서가 서로 다르게 바뀌어도 한쪽 차이를 직접 검출할 수 없었다.

G0 candidate는 다음 두 검증으로 exact interface와 compiled export를 양방향으로 고정했다.

- `DotNetExactInterfaceDeclarations_Match_Source_And_Package_Exports`는 `interfaces/*.ko.md`의 C#
  declaration을 Roslyn syntax로 읽고 server contract source의 public declaration과 양방향으로 비교한다.
  Type owner 비교 key는 고정된 expected assembly와 namespace-qualified FQN이며, constructor는 source
  declaration과 일대일로 비교한다. Provider interface가 소유하는 구현 method는 실제 고정 FQN interface의
  동일 signature일 때만 연결한다. Record projection과 member referenced-type identity도 compiler projection과
  Roslyn SemanticModel을 통해 비교하며, 그 밖의 source extra는 실패로 남긴다. Public API snapshot 비교는
  전체 package member surface가 compiled export와 같은지 계속 확인한다.
- `DotNetExactInterfaceTypes_Have_A_Single_DocumentOwner`는 문서에서 시작하지 않고 server package의
  exported public type inventory를 만든 뒤, 각 type이 exact interface 문서 하나에만 소유되는지 확인한다.

현재 full declaration test와 owner test는 exact-interface 문서, source, package export를 모두 통과한다. Formal
exact-interface 문서는 수정하지 않았고, owner identity는 고정 expected assembly·FQN으로 검증한다.

### DN-TEST-007 — positional record projection을 exact declaration 분모로 확장하지 않는다

**판정: 충족.**

Exact interface는 `record struct Type(Parameter...)`처럼 property와 `Deconstruct`를 compiler projection으로
표현할 수 있다. Gate는 positional record parameter에서 public constructor, `get; init;` property와 generated
`Deconstruct(out ... ParameterName)`을 materialize하고, source reflection과 package API snapshot의 같은
projection을 대조한다.

Source extra를 무조건 제외하지 않으며, projection으로 설명할 수 없는 public member는 계속 실패한다. `ActorRef`
source는 validation을 유지하는 custom `init` accessor를 사용하고 positional constructor parameter name은
formal interface와 동일하게 유지한다.

### DN-TEST-008 — member signature의 referenced type FQN canonicalization이 불완전하다

**판정: 충족.**

Exact owner type은 고정 expected assembly·namespace mapping으로 확인하고, member type은 각 source/document의
Roslyn `SemanticModel`에서 `ITypeSymbol`을 얻어 assembly name과 fully-qualified type identity로 정규화한다.
따라서 parameter·return·property type이 같은 이름의 다른 namespace로 이동하면 양쪽 identity가 달라져 실패한다.

각 문서와 source project를 별도 compilation으로 binding하고 unresolved 또는 ambiguous type은 test failure로
처리한다. Record projection test도 같은 semantic resolver를 사용하므로 단순 token 이름 fallback이 없다.

### DN-TEST-002 — documentation regression이 exact interface 디렉토리를 분모로 사용하지 않는다

**판정: 충족.**

`RegressionTests.DotNetExactInterfaceDocuments_Have_An_Explicit_Regression_Owner`가
`server/languages/dotnet/interfaces/`의 각 문서를 regression matrix의 실행 가능한 owner와 대조한다.
`ContractSurfaceCoverage.DotNetExactInterfaceTypes_Have_A_Single_DocumentOwner`는 package exported type이
정확히 하나의 문서 owner를 갖는지 확인한다. 현재 documentation regression `20/20`과 owner test `2/2`가
이 분모를 통과한다.

### DN-TEST-003 — 공통 E2E scenario inventory와 feature-map이 다르다

**판정: 충족.**

`RegressionTests.CommonE2EConfigsHaveCompleteDotNetFeatureMapInventories`가 Config 1~14의 공통 E2E
문서 ID와 `.NET` feature-map ID를 중복·누락·unknown까지 비교한다. 현재 documentation regression에서
이 검사가 통과하고, `SM-G5A`·`SM-G5B`와 분리된 Config 3·7·10·11·12 ID도 각 feature-map에 존재한다.
실제 process selector와 evidence가 없는 항목은 inventory gap이 아니라 해당 Config의 E2E implementation
gap으로 유지한다.

### DN-TEST-004 — 기존 feature-map의 일부 상태가 live source와 반대다

**판정: 충족. Feature-map 상태를 live source와 현재 process evidence 수준에 맞춰 보정했다.**

다음 항목은 feature-map 설명과 production source가 일치하지 않는 확인 사례다.

| 보정한 기록 | live source와 현재 상태 |
|---|---|
| SubmitAdmission `SA-E2E-10`은 `source 구현·process 미검증`으로 기록한다. | `IZLinkClientServerChannelRoleBuilder`와 ClientServer runtime이 존재하고 contract/unit test가 통과한다. 실제 three-process evidence는 아직 없다. |
| PubSub `PS-D1`은 `source 구현·process 미검증`으로 기록한다. | `ZLinkAutomaticFanoutSubscriberRuntime`과 `ZLinkFanoutDiscovery`가 descriptor별 connection을 구현한다. Automatic actual-process evidence는 아직 없다. |

Source type의 존재만으로 `구현`으로 바꾸지 않고, scenario의 모든 관찰 조건을 process test가 직접 확인할
때만 `구현`으로 판정한다. `SA-E2E-10`의 process evidence는 DN-E2E-IMP-015가, `PS-D1`의 automatic
fanout evidence는 DN-E2E-IMP-006이 소유한다.

### DN-TEST-005 — 중앙 regression matrix의 여러 행에 실행 가능한 test가 없다

**판정: test gap.**

`framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md`에는 `unit`,
`integration-single-process`처럼 계층만 적힌 행이 많다. 현재 참조 검사도 backtick으로 적힌 이름만
확인하므로, 구체적인 test 이름이 없는 행은 검증 대상에서 빠진다.

각 행은 다음 중 하나를 가져야 한다.

- 현재 test tree에 존재하는 정확한 test method
- 공통 E2E와 feature-map에 모두 존재하는 scenario ID
- 실행 경로가 고정된 verification script

### DN-TEST-006 — relocation fixture delegate 계약의 재발 방지 확인

**판정: 이전 test wiring blocker는 현재 working tree에서 해소되었고, 재발 방지 항목으로 유지한다.**

`ZLinkFrameworkDrainExecutor`의 `DrainSpots` delegate는 host shutdown 여부를 구분하기 위해
`Func<bool, bool, CancellationToken, ValueTask<ZLinkSpotDrainResult>>`를 사용한다. 현재 fixture는
`relocate`, `hostShutdown`, `cancellationToken`을 명시적으로 받고, 현재 targeted test에서 이 계약과
Shutdown·Relocate 경로가 함께 실행된다.

2026-08-02 현재 확인 결과는 다음과 같다.

- 이전 CS1593 compile blocker는 재현되지 않았다.
- targeted relocation test는 248개가 모두 통과했다.
- source delegate를 바꾸면 fixture와 shutdown/relocate close-reason assertion을 같은 변경에서 갱신하고,
  이 targeted command를 다시 실행한다.

### 5.1 .NET E2E spec와 실제 구현의 추가 gap

다음 항목은 공통 E2E 문서의 scenario ID와 실제 `.NET` E2E의 role server, client, selector,
feature-map, aggregate runner를 대조한 결과다. 이름이나 source type이 있다는 사실만으로 통과시키지
않고, 공통 E2E가 요구하는 process 경계, client-visible 결과, role server evidence, terminal assertion과
실패 의미까지 확인한다. 아래 항목은 구현·E2E test를 이번 작업에서 수정했다는 뜻이 아니라, 후속 수정
목록을 고정한 것이다.

#### DN-E2E-IMP-001 — Config 14 Instance Spot process E2E가 없다

**판정: 확인.**

`framework/doc/framework/common/e2e/config-14-instance-spot.ko.md`는 `IS-E2E-01`부터
`IS-E2E-36`까지 cold activation, concurrent first call, owner process 종료 뒤 takeover 금지,
generation 경계, 정상 relocation, close/reactivate, store outage, capacity conflict, deadline과
handler capability를 요구한다. 현재 `framework/languages/dotnet/e2e/InstanceSpot/feature-map.ko.md`는
전체 ID inventory를 갖지만 role server·client와 actual-process runner가 없다.

**수정 목록**

1. Instance Spot role server와 `ZLinkHttpClient` 기반 client를 추가하고, 각 scenario ID를 독립 selector로
   고정한다.
2. cold activation과 concurrent first call의 owner 수·generation·single materialization을 role
   server evidence로 남긴다.
3. owner process 종료 뒤 다른 runtime이 takeover하거나 callback을 replay하지 않는지, 정상
   relocate·close/reactivate와 store/capacity/deadline 실패의 terminal 결과를 client와 role server에서
   함께 확인한다.

#### DN-E2E-IMP-002 — aggregate runner가 Config 12와 Config 14를 실행하지 않는다

**판정: 확인.**

`framework/languages/dotnet/e2e/run_e2e_all.sh`의 `CONFIGS`에는 `ChannelEgressRouting`과
`InstanceSpot`이 없다. Config 12 feature-map과 Config 14 `InstanceSpot/feature-map.ko.md`는 존재하지만
각 role process runner가 준비되지 않았으므로 현재 `all` 결과는 공통 E2E 전체를 대표하지 않는다.

**수정 목록**

1. Config 12와 Config 14의 exact selector와 role server evidence가 준비된 뒤 `CONFIGS`와 결과
   denominator에 두 구성을 포함한다.
2. 실행하지 못한 구성은 성공으로 세지 말고, config·scenario ID·누락 이유를 결과에 남긴다.
3. Config 수와 scenario 수를 공통 E2E inventory에서 계산해 runner의 고정 목록 drift를 회귀 test로
   차단한다.

#### DN-E2E-IMP-003 — 공통 scenario ID와 .NET selector/feature-map의 분할이 맞지 않는다

**판정: 확인. Inventory는 충족하고 process selector/evidence가 남아 있다.**

현재 documentation regression `20/20`은 Config 1~14의 공통 문서와 feature-map에 대해 duplicate,
missing, unknown ID를 검사한다. `SM-G5A`, `SM-G5B`, `PS-D7A`~`PS-E2C`, `MON-A4A`~`MON-D1B`,
`ST-E1B`·`ST-E1C`, `OBS-C9A`·`OBS-C9B`, `CH-E2E-04A`~`CH-E2E-07C`는 현재 각 feature-map에
존재한다. 남은 문제는 ID inventory가 아니라 exact selector, role server와 actual-process evidence다.

| Config | 현재 남은 process gap |
|---|---|
| 2 SpotService | `SM-G5A`·`SM-G5B`의 독립 selector와 actual-process evidence |
| 3 PubSub | automatic fanout, liveness와 observer matrix의 process evidence |
| 7 RuntimeMonitoring | split selector와 role server evidence |
| 10 SpotActorTransfer | `ST-E1B`·`ST-E1C`·`ST-F3A`의 exact selector와 process evidence |
| 11 ObservabilityOps | `OBS-C9A`·`OBS-C9B`의 topology별 process evidence |
| 12 ChannelEgressRouting | split selector, role server와 central runner 연결 |

**수정 목록**

1. 공통 문서와 feature-map의 split ID inventory는 현재 regression으로 고정되어 있으므로 다시 누락으로
   표시하지 않는다.
2. `Client/Program.cs`, scenario dispatch와 `run_e2e.sh`가 같은 ID를 선택하도록 연결하고, aggregate
   alias를 쓰는 경우 어떤 분할 ID를 포함하는지 명시한다.
3. ID가 source-only, partial 또는 diagnostic-only이면 `구현`으로 표시하지 않고 필요한 process evidence를
   추가한다.

#### DN-E2E-IMP-004 — Config 1의 RM-A7 실행 경로가 없고 RM-C10의 계약 소유자가 없다

**판정: 확인.**

`LocationMessaging/feature-map.ko.md`의 `RM-A7`은 Actor와 Spot이 같은 global ID를 동시에 예약할 때
공유 실패 terminal과 exactly-once callback을 요구하지만 role server와 selector가 없다. `RM-C10`은 현재
Config 1 공통 문서와 feature-map의 active inventory에 없으므로 현재 E2E 누락 ID로 세지 않는다. 해당
capability가 public contract가 되어야 한다면 다른 언어 구현만으로 추가하지 않고 별도 spec/draft 검토를
거친다.

**수정 목록**

1. RM-A7에 reservation collision role server, selector, terminal reason과 callback count evidence를
   추가한다.
2. RM-C10은 공통 spec/E2E에 계약이 정식으로 추가되기 전까지 완료 inventory에서 제거하거나 draft로
   분리한다. 다른 언어 구현을 근거로 .NET public API를 추가하지 않는다.

#### DN-E2E-IMP-005 — Config 2 SpotService scenario와 현재 public 의미가 불완전하다

**판정: 확인.**

`SpotService/feature-map.ko.md`에는 `SM-A9`, `SM-A10`, `SM-A12`, `SM-A13`, `SM-B0A`, `SM-B10`,
`SM-B11`, `SM-G5A`, `SM-G5B`가 현재 `미구현`으로 기록되어 있고, `SM-C6`은 result-free one-way
publish 계약에 맞춘 `부분 구현`으로 기록되어 있다. Inventory는 존재하지만 각 항목의 role server,
selector와 actual-process evidence가 없다.

**수정 목록**

1. feature-map에 이미 있는 미구현 operation을 실제 role server endpoint와 독립 selector로 연결하고, success·reject·timeout·
   cancellation의 client result와 server terminal evidence를 함께 남긴다.
2. SM-C6은 제거된 publish result API를 되살리지 말고, 현재 one-way publish의 backpressure와 최종
   delivery 관찰 조건으로 시나리오를 다시 연결한다.
3. feature-map에 이미 분리된 `SM-G5A`·`SM-G5B`를 exact client selector와 runner evidence에 연결한다.

#### DN-E2E-IMP-006 — Config 3의 automatic fanout, liveness와 observer matrix가 process에서 검증되지 않는다

**판정: 확인.**

현재 `PubSub/run_e2e.sh`는 명시적 endpoint를 사용하는 manual A/B/C 경로만 실행한다. common Config 3의
`PS-D1`~`PS-D7`, `PS-E2A`~`PS-E2C`, `PS-F1`~`PS-F5`에 필요한 Redis-backed automatic publisher/subscriber,
descriptor discovery, liveness, snapshot/event coalescing, resync과 cancellation의 process evidence가
없다. feature-map의 source type 존재는 자동 completion의 증거가 아니다.

**수정 목록**

1. publisher와 subscriber role server가 Redis descriptor와 advertised port를 실제로 사용하도록
   process fixture를 추가한다.
2. automatic discovery, publisher별 connection, liveness terminal, snapshot/event coalescing, resync,
   observer capacity와 cancellation을 client-visible 결과와 server evidence로 각각 확인한다.
3. manual `PS-E1`과 automatic 시나리오를 분리하고 exact selector를 `all` denominator에 반영한다.

#### DN-E2E-IMP-007 — Config 4의 RC-B6 typed DTO process roundtrip이 없다

**판정: 확인.**

`RegistrationCodec/feature-map.ko.md`의 `RC-B6`은 별도 process에서 default JSON으로 typed DTO를 왕복하되
message-specific codec registration을 사용하지 않는 동작을 요구한다. 현재 map은 이를 `미구현`으로
기록한다.

**수정 목록**

1. role server와 client를 분리한 RC-B6 process scenario를 추가한다.
2. DTO roundtrip, unknown/non-JSON rejection과 default serializer 사용을 evidence로 확인한다.
3. 메시지별 codec 등록 함수나 호출부 우회는 추가하지 않는다.

#### DN-E2E-IMP-008 — Config 5의 ResilienceLifecycle 후반 matrix가 runner에서 제외된다

**판정: 확인.**

`ResilienceLifecycle/run_e2e.sh`는 RL-A1~D5만 실행한다. 공통 Config 5의 RL-E1~E5와 RL-F1~F14가 요구하는
orderly liveness, half-open, stale ACK, terminal-once, admission seal, owner ABA, cross-language parity,
ClientServer topology, host relocation readiness·queue·timer·frozen hold·metrics 증거는 role server와
runner에 등록되지 않았다.

**수정 목록**

1. RL-E/F의 exact selector와 process fixture를 추가하고 A~D의 historical log를 후반 matrix 완료로
   세지 않는다.
2. half-open·stale ACK·owner ABA와 preflight/admission seal의 state transition 및 terminal callback count를
   양쪽 process에서 확인한다.
3. relocation readiness, queue, timer, frozen hold와 metrics 조건을 bounded assertion으로 고정한다.

#### DN-E2E-IMP-009 — Config 6 StoreFailure의 fault matrix가 부분 실행된다

**판정: 확인.**

`StoreFailure/run_e2e.sh`는 SF-A1, A2, B1, B2, D1, D3, C2, C1, D2, E1만 실행한다. `SF-B3`, `SF-C3`,
`SF-C4`, `SF-C5`, SF-F1~F11과 SF-G1~G3의 Redis fault, generation fencing, lease, bounded reconcile,
relocation renew/orphan cleanup, manifest/chunk, reservation fence, owner-token cleanup, provider
cancellation/buffer lifetime와 atomic capacity vector 조건은 누락되어 있다.

**수정 목록**

1. 각 fault를 주입하는 Store role과 실제 framework role server를 분리하고 exact selector를 추가한다.
2. generation·lease fence와 bounded reconcile, relocation cleanup, manifest/chunk, reservation/owner token
   cleanup의 terminal evidence를 양쪽 process에서 확인한다.
3. provider cancellation과 buffer lifetime이 timeout 뒤에도 stale commit이나 double cleanup을 만들지
   않는지 검증한다.

#### DN-E2E-IMP-010 — Config 7 monitoring split과 placement 관찰 조건이 맞지 않는다

**판정: 확인.**

공통 Config 7의 `MON-A4A/B`, `MON-D1A/B`는 feature-map에 존재하고 aggregate scenario class도 있다.
현재 없는 것은 각 split ID의 독립 selector와 fresh role server/process evidence다. `MON-A6`의 placement
snapshot·reservation/commit/release·capacity reject process evidence도 없다. `MON-B1/B2`는 publish
monitoring 부재와 local/zero target 일부만 확인하며 blocked+normal target, rollback/no-retry와 message-flow
trace 조건을 확인하지 않는다.

**수정 목록**

1. 이미 존재하는 split ID를 exact selector와 role server 실행 경로에 연결한다.
2. placement role server가 snapshot, reservation, commit/release와 capacity reject를 실제로 관찰하게
   한다.
3. B1/B2에 blocked·normal target, rollback/no-retry와 message-flow trace를 포함한 terminal assertion을
   추가한다.

#### DN-E2E-IMP-011 — Config 8 execution-turn 후반 selector가 없다

**판정: 확인.**

`AutomaticTurnDispatch/Client/Program.cs`는 기존 TD-A1~D3, E1~E3, F1~F6, G1만 등록한다. 공통 Config 8의
`TD-D4`, `TD-D5`, `TD-D6`, `TD-E2A`, `TD-F5A`가 selector·scenario dispatch에서 빠져 있다.

**수정 목록**

1. 각 ID를 독립 selector로 등록하고 `all`에서 실제 실행한다.
2. PerActor async의 같은 actor 내부 blocking 범위, unsupported Yield의 pre-submit reject, same-gate
   self-await 검증, handler failure 뒤 deferred Join barrier 정리와 host shutdown 중 대기 종료를
   process evidence로 확인한다.

#### DN-E2E-IMP-012 — Config 10 relocation E2E의 기대 의미가 공통 spec과 충돌한다

**판정: 확인. 기능 수가 아니라 process failure 의미와 시나리오 범위의 차이다.**

`SpotActorTransfer/feature-map.ko.md`는 `ST-B2`에서 source lease 손실 뒤 target recovery와 completion
callback replay를 성공 조건으로 기록하지만, common Spot Actor/relocation spec은 process 종료 뒤 다른
runtime takeover·replay를 제공하지 않고 object를 unavailable로 유지하도록 한다. 반대로 `ST-B3`와
`ST-B4`는 전환 대상으로 표시되어 있으나 common Config 10은 `RecreateOnRelocation`과 empty-state
restore 성공을 요구한다. `ST-E1B/C`, `ST-F3A`, ST-G/H/I 계열도 exact process evidence가 없거나
diagnostic-only로 분류되어 있다.

**수정 목록**

1. ST-B2의 성공 조건을 no takeover/no replay/unavailable로 고치고, source·target 종료 시 owner와
   callback terminal을 양쪽 process에서 확인한다.
2. ST-B3/B4는 공통 spec의 RecreateOnRelocation과 empty-state 조건을 구현·검증하거나, spec 변경이
   승인될 때까지 open gap으로 남긴다. feature-map의 `전환 대상`을 완료로 세지 않는다.
3. ST-E1B/C, ST-F3A, ST-G1/G2/G4/G5, ST-H2~H5와 ST-I1~I6의 exact selector, cleanup·route switch·
   ACK·steady normalization·authority visibility·Message Follow evidence를 추가한다.
4. `diagnostic_only`와 unit/runtime evidence만으로 process E2E 완료를 표시하지 않는다.

#### DN-E2E-IMP-013 — Config 11 ObservabilityOps의 C9 split과 process evidence가 부족하다

**판정: 확인.**

공통 Config 11은 `OBS-C9A`(Automatic topology)와 `OBS-C9B`(Manual topology)로 분리되어 있고
feature-map에도 두 ID가 존재한다. 다만 `Client/Program.cs`는 `OBS-C9`와 `OBS-C9-MANUAL` aggregate alias를
사용한다. OBS-A5와 OBS-C1~C8, C10~C12도 source implemented·process unverified이며, C9는 source
implemented·process 미검증이다.

**수정 목록**

1. C9A/C9B와 기존 aggregate alias의 관계를 명시하고 exact ID를 selector와 runner 결과에 연결한다.
2. readiness gate, concurrent shutdown과 각 observability output을 실제 role process에서 수집하고
   source-only 상태를 `구현`으로 올리지 않는다.

#### DN-E2E-IMP-014 — Config 12 ChannelEgressRouting의 split selector와 부분 assertion이 부족하다

**판정: 확인.**

공통 Config 12는 `CH-E2E-04A/B/C`와 `CH-E2E-07A/B/C`를 요구하고 feature-map에도 split ID가 존재한다.
하지만 client selector는 각각 aggregate `CH-E2E-04`, `CH-E2E-07`만 사용한다. Accepted drain/shutdown/new RID restart, protocol
unsolicited injection, known-but-not-ready `Unavailable`, STREAM, drain, timeout/cancel/disconnect,
generation/late reply와 topology count assertion도 부분 상태다. 중앙 runner에는 Config 12 자체가 없다.

**수정 목록**

1. 이미 존재하는 split ID를 exact selector와 scenario file에 연결하고 aggregate alias가 결과에서 숨기지 않도록 한다.
2. 각 partial row에 role server endpoint, client-visible result, route/ACK/generation/terminal evidence를
   추가한다. protocol unsolicited injection은 raw frame 우회가 아니라 허용된 test transport 경계에서
   검증한다.
3. 모든 selector와 evidence가 준비된 뒤 central runner에 Config 12를 등록한다.

#### DN-E2E-IMP-015 — Config 13 SubmitAdmission의 gate matrix가 부분 구현이다

**판정: 확인.**

`SubmitAdmission/feature-map.ko.md`는 `SA-E2E-01`, 02, 03, 05, 07, 08, 09, 20과 `SA-REG-04`를
부분으로 기록하고, `SA-E2E-04`, 06, 11~13, 15~19를 미구현으로 기록한다. `SA-E2E-10`은 source
구현·process 미검증으로 기록한다. `SA-E2E-11~13`, `15~19`는 미구현으로 기록한다. 공통 계약이 요구하는 RID,
ChannelName, ClientServer, Spot, Actor, multicast, Session/STREAM, reply token, generation,
timeout/shutdown의 pending admission gate와 observer 조건을 polling·retry로 대체해서는 안 된다.

**수정 목록**

1. 각 topology와 operation 종류에 대해 deterministic gate와 observer role을 추가한다.
2. pending·accepted·rejected·timeout·shutdown terminal과 generation fence를 client와 role server에서
   함께 확인한다.
3. `SA-REG-04`의 timeout/cancel/disconnect/Spot shutdown 조건을 별도 process scenario로 고정한다.

#### DN-E2E-IMP-016 — 일부 .NET E2E client가 role server 계약을 우회한다

**판정: 확인. 공통 E2E 실행 규칙 위반이다.**

공통 E2E README는 client가 실제 role server HTTP endpoint를 호출하고 `ZLinkHttpClient`로 결과를
확인하며, `AddZLinkFramework`, `Host.CreateDefaultBuilder`, framework runtime/client 호출,
reflection과 private/internal API를 사용하지 않도록 정한다. 그러나
`ResilienceLifecycle/Client/Support/EphemeralRouteClient.cs`와 `StormClientProcessFleet.cs`는 client에서
host를 만들고 `AddZLinkFramework`, `IZLinkRouteMeshRuntime`, `IZLinkLocationRuntimeQuery`,
`IZLinkRouteClient`를 직접 사용한다. `RuntimeMonitoring/Client/Scenarios/MonBPublishMonitoringAbsenceScenario.cs`
와 `ChannelEgressRouting/Client/Program.cs`는 reflection/assembly scan으로 계약을 검사한다.

**수정 목록**

1. framework 호출은 role server의 endpoint와 server-side evidence로 이동하고, client는
   `ZLinkHttpClient`만 사용한다.
2. public surface 부재 검사는 reflection을 E2E client에서 제거하고 contract/source regression 계층으로
   이동한다.
3. process lifecycle 제어만 runner/client support에 남기고, framework messaging 호출은 client support에
   두지 않는다.

#### DN-E2E-IMP-017 — `all` 완료 분모가 partial/diagnostic-only를 숨길 수 있다

**판정: 확인.**

여러 feature-map이 `부분`, `미구현`, `source 구현·process 미검증` 또는 `diagnostic_only`를 기록한다.
`SpotActorTransfer/Client/Program.cs`는 다수 selector를 `excludedFromAll` 또는 diagnostic-only로 분류하고,
`ChannelEgressRouting`도 일부 누락 selector 때문에 aggregate 실행을 등록하지 않았다. 현재 구조는
공통 scenario ID, feature-map, 실제 selector, scenario 파일/role endpoint와 `all` denominator의 차이를
한 번에 실패시키지 않는다.

**수정 목록**

1. 공통 E2E inventory를 source로 삼아 ID·feature-map·selector·scenario file/endpoint를 일대일로
   검사하는 중앙 manifest regression을 추가한다.
2. `부분`, `미구현`, `diagnostic_only`는 완료 분모에서 제외하되 성공으로 표시하지 않고 열린 gap으로
   출력한다.
3. `all`은 실행한 ID, 제외한 ID, 제외 사유와 terminal 결과를 모두 출력하고, 누락 ID가 있으면 실패한다.

## 6. 현재 충족 판정

다음 항목은 live source와 test를 다시 확인했으며 새 implementation gap으로 등록하지 않는다.

| 범위 | 현재 증거 |
|---|---|
| ClientServer public surface와 runtime | Builder, discovery, local·remote selection, liveness와 monitoring 구현 및 contract/unit test가 존재한다. |
| Automatic classic fanout | 전용 publisher descriptor, automatic subscriber connection과 discovery runtime이 존재한다. Process E2E coverage는 별도 test gap으로 재검사한다. |
| Framework error surface | 13개 `ZLinkFrameworkErrorKind`는 contract test와 일치한다. `RetryAdvice`는 internal runtime policy로 제한하고 public exception에는 export하지 않는다. `Failed` completion에도 retry hint를 넣지 않는다. |
| 알 수 없는 non-JSON content type | `EnvelopeCodecTests`가 decode 전에 거부하는 경로를 검증한다. |
| API snapshot | Source·package assembly와 고정 API snapshot assertion이 통과한다. Markdown exact-interface는 DN-TEST-001의 양방향 gate가 별도로 확인한다. |

## 7. 작업 순서

G0~G6의 각 항목은 2.1절의 card review gate를 독립적으로 통과해야 한다. 한 단계 안에 여러 gap이 있어도
Luna가 동시에 구현하지 않고 card 하나씩 완료한다. G1~G4에서 lifecycle, ownership, public contract 또는
module 경계를 바꾸기 전에는 Sol Medium 사전 review를 먼저 통과한다. G7의 마지막 audit은 이전 card를
검토하지 않은 새 Sol Medium reviewer가 전체 범위를 read-only로 검사한다.

### G0 — audit 기준과 회귀 분모를 먼저 고친다

1. 이 ledger의 candidate commit과 working tree manifest를 저장한다.
2. DN-TEST-001과 DN-TEST-002 구현을 완료해 exact interface 전체를 audit 분모로 고정한다.
3. DN-TEST-004에 해당하는 feature-map 전체를 source와 test로 다시 판정하고 상태를 보정한다.
4. DN-IMP-013의 `Failed` exact interface shape를 contract review에서 확정한다.
5. 발견한 차이는 이 ledger에 추가한 뒤 다음 구현 단계로 이동한다.

완료 조건은 exact interface 파일, public export와 regression evidence 사이에 소유자가 없는 항목이 0개인
상태다.

#### G0 실행 기록 — 2026-08-02

- 기준은 `HEAD 3291e338f4f700484780560cd81345a647ef0948`이다. Working tree는 이미 dirty였으므로
  사용자 작업과 다른 ledger 변경을 reset하거나 checkout하지 않았다. 이번 G0 candidate에서 직접 확인한
  범위는 `Failed` contract, join failure sample/guide, exact-interface owner inventory, Config 1~14
  feature-map inventory, package snapshot과 관련 regression test다.
- [수정 전 증거] 기존 Contract test는 `dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-restore --logger 'console;verbosity=minimal'`로 `74/74 PASS`였다. Roslyn full declaration test를 추가한 직후에는 `73/75 PASS`였고, DN-IMP-019~023과 DN-TEST-007/008의 gap을 발견했다. 이 결과는 현재 상태가 아니라 수정 전 기준이다.
- [수정 전 증거] Documentation regression은 `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --filter 'FullyQualifiedName~Documentation.RegressionTests' --logger 'console;verbosity=minimal'`로 `20/20 PASS`였다.
- [수정 전 증거] Bingo, TicTacToe, SupportChat sample build는 각각 `0 warning, 0 error`였다.
- [수정 전 증거] `framework/languages/dotnet/scripts/verify_packaged_contract.sh`는 9개 package, source/package public API,
  clean consumer와 standalone HTTP consumer 검사를 모두 통과했고 당시 public API hash는
  `2be6d1b704046b12f4190809e10a9527d9c73479ddce5c377f4bf554c6f92e59`였다.
- `SpotActorTransfer/run_e2e.sh st-a2`는 PASS다. Log는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260802-033146-2144048`에 있다.
- `st-c3` 첫 실행은 Redis lease startup `TaskCanceledException`으로 readiness에 도달하지 못했다.
  재실행은 실제 source-leave failure branch까지 도달했지만 `join_failed` marker를 기다리는 기존
  completion ordering 문제로 실패했다. 두 번째 log는
  `framework/languages/dotnet/e2e/SpotActorTransfer/logs/20260802-033252-2146189`에 있다. 이 결과는
  G0 PASS로 세지 않으며 DN-IMP-006의 후속 evidence로 남긴다.
- C++ common guide의 `is_retriable` 예제는 C++ exact interface의 `error_kind`를 전달하고 application이
  업무 상태와 idempotency를 확인하도록 수정했다. RuntimeMonitoring의 MON-A4A/B·MON-D1A/B와
  ObservabilityOps의 OBS-C9A/B는 source selector가 존재하므로 `source 구현·process 미검증`으로 보정했다.
- [수정 전 기록] `git diff --check`는 통과했다. Full declaration test가 발견한 DN-IMP-019~023·DN-TEST-007/008과
  `ST-C3`의 열린 조건 때문에 당시 G0 card는 닫지 않고 Sol High post-review를 기다렸다.
- 후속 수정에서 formal exact-interface 문서는 변경하지 않고 DN-IMP-019~023을 source·runtime policy·test·package
  snapshot에 반영했다. `DotNetExactInterfaceDeclarations_Match_Source_And_Package_Exports`와
  `DotNetExactInterfaceTypes_Have_A_Single_DocumentOwner`는 각각 `2/2 PASS`가 되었다. Record projection은
  public `init` accessor와 PascalCase `Deconstruct` parameter name까지 reflection·snapshot으로 확인한다.
- DN-TEST-008의 resolver는 source project와 exact-interface document를 별도 Roslyn compilation으로 binding하고,
  `ITypeSymbol`의 assembly identity와 fully-qualified name을 비교한다. unresolved 또는 ambiguous type은 fallback
  없이 실패한다.
- [수정 전 기록] Sol High post-review는 `Critical 0`, `High 2`(`DN-TEST-007`, `DN-TEST-008`), Medium 4건을 판정했다. 두 High는
  위 gate 보강으로 수정했으며, common/.NET backpressure guide의 retry hint 예제와 feature-map 표 정리는 반영했다.
- 첫 Sol Medium read-only review는 `Critical 0`, `High 0`, `Medium 3`으로 `NOT CLEAN`을 판정했다. SubmitAdmission·PubSub
  feature-map의 source/process 상태를 보정하고, DN-TEST-002~004의 현재 판정과 과거 evidence 표기를 갱신했다.
  전체 UnitTests는 상세 logger로 실제 marker contract failure를 식별해 root source를 수정한 뒤 `1388/1388 PASS`가
  되었다.
- 새 package verifier는 9개 package, source/package public API, clean consumer와 standalone HTTP consumer를 통과했고
  public API hash는 `230512e60b75dd29d94dda48a53c62145fb16d3b01280fd92ff01fb56a8b01a5`다.
- 후속 sample regression은 feature-map의 `SM-G5A`·`SM-G5B` 분리에 맞춰 scenario 파일을
  `SmG5AAndG5BPlacementScenario`로 정렬하고, TicTacToe 문서에 Entry Spot의 `destroyActor` 소유 문장을
  복원한 뒤 `134/134 PASS`가 되었다. 이 변경은 formal exact-interface 문서를 수정하지 않았다.
- 두 번째·세 번째 Sol Medium read-only review는 각각 ledger의 stale inventory와 selector 상태를 추가로
  정리하도록 `NOT CLEAN`을 판정했다. Feature-map inventory와 aggregate selector를 분리하고, 마지막 한 줄의
  `SA-E2E-10` 상태 충돌까지 수정했다.
- 최종 Sol Medium read-only review는 `CLEAN`을 판정했다. Formal exact-interface diff 0, `git diff --check`
  통과와 fresh Contract/Unit/Documentation/Sample/Package evidence를 확인했으며, 남은 ST-C3와 Config별
  process selector gap은 G1 이후 열린 작업으로 유지한다.
- 같은 candidate의 초기 90초 실행은 hosting bind·negative configuration 로그 뒤 `timeout 124`로 끝났고,
  normal logger 재실행에서 `ActorHandoffTests.MessageFollowOperationalMarkers_DoNotExposeObjectIdentityOrGeneration`
  failure를 식별했다. `ZLinkActorHandoffState`가 `message_follow_registered` marker에 actor identity와
  generation을 추가한 것이 원인이었으며, object identity를 포함하지 않도록 root source를 수정했다.
  해당 class는 `63/63 PASS`, fresh 전체 UnitTests는 `1388/1388 PASS`(2분 51초)로 종료했다. Negative test의
  예상된 host-start error log와 test result를 구분해 기록한다.

### G1 — Host relocation과 Actor Join relocation의 계약 경계를 바로잡는다

1. DN-IMP-017의 일반 accepted operation gate와 actor handoff gate를 모두 preflight에 포함한다.
2. DN-IMP-011의 host absolute deadline을 모든 Spot unit, target reservation, callback과 cleanup에 전달한다.
3. DN-IMP-010의 terminal reason과 commit 경계를 Spot·Actor aggregate까지 보존한다.
4. DN-IMP-007의 commit 뒤 failure를 `Blocked/RelocationFailed`로 끝내고 uncommitted source workload와
   `Serving` descriptor만 복원한다.
5. DN-IMP-004의 startup recovery와 takeover 진입점을 제거하고, process 종료 뒤 unavailable 상태를
   유지하는 회귀 test를 먼저 고정한다.
6. DN-IMP-005의 preflight 결과에서 target 탐색 실패와 cancellation을 구분한다.
7. DN-IMP-008의 absolute deadline을 target commit reconciliation까지 전달하고 authority 재확인으로
   commit 전 timeout과 결과 미확정을 구분한다.
8. DN-IMP-015의 caller absolute deadline을 Actor Join admission callback과 reservation에 전달한다.
9. DN-IMP-006의 Join completion, queue replay와 Session 위치 갱신 순서를 spec에 맞춘다.
10. Durable Join completion cursor와 restart replay를 제거하되 같은 process 안에서의 commit 뒤 retry와
    idempotency는 유지한다.
11. DN-IMP-012의 target reservation error mapping을 requested Spot lookup과 분리한다.
12. DN-IMP-016의 Entry Spot Join에서 admission callback과 concrete method reflection 경로를 제거한다.
13. DN-IMP-018의 Actor relocation failure mapping에 commit phase와 typed reason을 보존한다.
14. DN-IMP-014의 object relocation metrics와 terminal outcome을 모든 relocation 종류에 연결한다.
15. DN-IMP-009의 terminal-preserving status stream과 표준 structured log를 구현한다.
16. Host relocation과 Actor Join relocation 각각에 production runtime 두 개를 사용하는 process E2E를
   추가한다. Mock coordinator의 호출 횟수만으로 완료 판정하지 않는다.

완료 조건은 정상 경로의 단계 순서, commit 전 rollback, commit 뒤 failure, deadline, concurrent shutdown과
process 종료 경계가 spec과 일치하는 상태다.

#### G1 사전 검토 — DN-IMP-017 설계 경계

- 첫 Sol Medium 사전 review는 `NOT CLEAN`이었다. 기존 owner를 유지하는 설계 A는 권고됐지만,
  initial operation wait와 최종 fence 사이의 workload 변경을 검출하지 못하고, concurrent Shutdown이
  relocation rollback 뒤에 admission을 다시 열 수 있다는 High 2건과 shared deadline/caller cancellation을
  구분해야 한다는 Medium 1건이 남았다.
- 구현 전 보강안은 operation·actor admission·actor handoff의 epoch를 함께 기록하고, target preflight 성공
  뒤 고정된 lock order로 relocation owner generation을 가진 fence를 원자적으로 설정하는 방식이다.
  epoch가 바뀌었거나 counter가 0이 아니면 plan을 폐기하고 deadline 안에서 전체 preflight를 다시 수행한다.
- rollback은 현재 relocation owner generation일 때만 Serving admission을 복원한다. Shutdown이 owner를
  넘겨받은 뒤에는 admission을 다시 열지 않고 `ShutdownRequested` 경계로 수렴한다. caller cancellation은
  shared relocation을 취소하지 않으며, fence wait에는 shared absolute deadline과 Shutdown token만 사용한다.
- 두 번째 Sol Medium review는 `NOT CLEAN`으로, Serving admission이 열린 상태에서 기존
  `_operationsDrained`를 기다리면 zero transition이 signal되지 않는 High 1건과 active actor lease를
  rollback에서 허용하지 않아야 한다는 Medium 1건을 판정했다. 따라서 일반 zero-transition waiter를
  Shutdown waiter와 분리하고, owner-aware reopen은 actor admission count 0과 handoff safe를 함께 요구한다.
- 세 번째 Sol Medium review는 `Critical 0`, `High 0`, `Medium 0`으로 `CLEAN`을 판정했다. 구현 검증은
  zero-transition waiter, 모든 routed admission의 동일 lock order, epoch 증가 지점, preflight cancellation
  reason 전파와 Shutdown ownership takeover를 직접 회귀 검사해야 한다.

#### G1 사전 검토 — DN-IMP-011과 DN-IMP-010 의존성

- DN-IMP-011의 첫 Sol Medium 사전 review는 `NOT CLEAN`이었다. executor에서 deadline을 새로 만들면
  MaintenanceRuntime의 preflight와 descriptor publication에서 이미 사용한 시간이 사라지는 High finding이
  있었다. Relocate 시작 시 고정한 absolute deadline을 preflight부터 executor, Spot unit과 callback까지
  그대로 전달해야 한다.
- 같은 review에서 DN-IMP-010의 post-commit result 손실이 DN-IMP-011의 선행 조건으로 확인됐다. 현재
  `ZLinkSpotNodeCatalog`가 scheduler의 post-commit 예외를 `false`로 축약하므로, deadline만 전달하면
  target authority를 유지해야 하는 `RelocationFailed`와 committed count를 잃는다. DN-IMP-010을 먼저
  unit result와 commit phase owner로 고정한 뒤 DN-IMP-011을 구현한다.
- pre-commit abort와 post-commit authority-safe cleanup은 deadline token과 별도의 bounded
  terminalization 경계를 사용해야 한다. detached Message Follow의 실제 duration과 activation disposal은
  host relocation deadline이 아니라 runtime detached-task owner와 Shutdown token이 관리한다.

#### G1 사전 검토 — DN-IMP-010과 DN-IMP-007 의존성

- DN-IMP-010의 첫 Sol Medium 사전 review는 `NOT CLEAN`이었다. typed unit result 자체는 예외 wrapper보다
  적합하지만, `StageAsync` 반환을 authority commit의 확정 지점으로 사용할 수 없다는 High finding이
  있었다. ACK를 관찰하지 못한 실패는 `Unknown` commit knowledge로 두고 exact aggregate fence와 Location
  authority를 재확인해야 한다.
- committed unit이 하나라도 있으면 현재 executor와 MaintenanceRuntime이 force-stop/Error로 끝난다.
  그러나 Host Relocate §10은 committed target owner를 유지하고 미commit workload만 복원한 뒤
  `Blocked/RelocationFailed`와 `Serving`을 유지하도록 한다. 이 High finding 때문에 DN-IMP-007의
  partial-commit state transition을 먼저 구현한다.
- Catalog의 parallel `Task.WhenAll`은 한 unit 예외가 다른 unit의 committed 결과를 잃게 할 수 있다. 결과는
  `Pending`·`Completed`·`TerminalFailure` outcome과 `NotCommitted`·`Committed`·`Unknown` commit
  knowledge를 조합하되 factory로 유효한 상태만 만들고, 모든 unit 결과를 회수한 뒤 count와 terminal
  reason을 합산해야 한다.

#### G1 사전 검토 — DN-IMP-007 partial-commit state transition

- DN-IMP-007의 첫 Sol Medium 사전 review는 `NOT CLEAN`이었다. 정상적인 concurrent Shutdown takeover를
  `TeardownFailed`로 분류하면 §11과 다르므로, Shutdown owner가 확인되면 Serving 복원 없이
  `Blocked/ShutdownRequested`로 종료해야 한다. Stale generation, owner 불명과 descriptor 복원 불일치만
  `TeardownFailed`로 남긴다.
- Admission을 다시 열기 전에 모든 미commit unit의 source dispatch·queue·session route 복원이 끝났다는
  terminalization barrier가 필요하다. `CommittedUnitCount`와 `BlockedReason`만으로는 이 조건을 증명할 수
  없으므로 DN-IMP-010 result가 terminalization 완료를 함께 소유해야 한다.
- executor/runtime이 descriptor와 admission 복원을 소유하고 MaintenanceRuntime은 결과를 받아 host state와
  public `Blocked` result만 갱신한다. `ForceReason`와 `BlockedReason`을 nullable 두 개로 병렬 표현하지
  않고, `Completed`·`Blocked`·`ForceStop` 중 하나만 허용하는 internal result factory를 사용한다.

- 구현 후 Sol Medium review는 `NOT CLEAN`으로, production `ZLinkRelocationWorkloadCoordinator`가
  `SourceTerminalized`를 실제 Spot/Actor result에서 만들지 않아 성공 partial restore가 test fixture에서만
  가능하다는 High finding을 판정했다. 이 wiring과 aggregate terminal reason 보존은 DN-IMP-010에서
  처리한다.
- 같은 review는 full rollback의 lease 획득·restore·reopen 경합에서 Shutdown takeover가
  `TeardownFailed`로 분류될 수 있다는 High finding을 판정했다. lease ownership loss를
  `Blocked/ShutdownRequested`로 분리하는 수정은 반영했으며, 실제 gate와 executor를 함께 사용하는
  회귀 검사를 추가해야 한다. DN-IMP-010의 production result wiring이 끝난 뒤 DN-IMP-007을 다시 review한다.

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

1. DN-E2E-IMP-001~003에 따라 Config 1~14 inventory, exact selector, feature-map과 aggregate runner의
   분모를 먼저 고정한다.
2. DN-E2E-IMP-004~015의 구성별 누락 scenario와 process evidence를 공통 spec의 terminal·callback·
   ownership 조건에 맞춰 추가한다.
3. DN-E2E-IMP-016의 client 우회 호출과 reflection 검사를 role server 또는 contract/source regression
   계층으로 이동한다.
4. DN-E2E-IMP-017의 중앙 manifest gate를 추가해 `부분`, `미구현`, `diagnostic_only`를 완료로 세지
   않는다.
5. Config 1~14의 모든 scenario ID가 `.NET` feature-map에 정확히 한 번 존재하는지 검사한다.
6. `부분` 또는 `미구현` 행은 source gap과 evidence gap을 분리한다.
7. 구현된 selector만 `run_e2e_all.sh`의 완료 분모에 넣고, gap selector를 직접 요청하면 성공으로
   건너뛰지 않도록 유지한다.

### G7 — 최종 회귀와 package 검증

다음 순서로 실행한다.

1. Contract test
2. Framework unit test
3. Redis provider test
4. Stream Connector와 HTTP client test
5. `framework/languages/dotnet/scripts/verify_packaged_contract.sh`
6. Config 1~14 process E2E
7. `verify-framework-doc-contracts.sh`
8. 독립 read-only 전체 audit

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
| `DrainCoordinatorTests`의 `DrainSpots` fixture | 새 `(relocate, hostShutdown, cancellationToken)` delegate shape와 Shutdown·Relocate close-reason assertion을 유지한다. 현재 targeted 248개가 통과했으며 source delegate 변경 때 함께 재검증한다. |
| `ZLinkActorJoinCompletion` contract/snapshot test | `Failed`가 `OperationId`와 `Kind`만 노출하는 exact interface 합의 shape를 고정한다. |
| Actor Join admission test | Target callback이 caller deadline 뒤에는 실행되지 않고 reservation이 durable cleanup으로 남지 않는지 검증한다. |
| Entry Spot Join test | Entry target descriptor가 없어도 admission reject가 아니라 membership commit을 수행하는지 검증하고 concrete method reflection도 차단한다. |
| Actor relocation failure test | Capture·Restore·Store failure를 commit phase와 함께 주입해 public `Failed.Kind`와 host reason mapping을 검증한다. |

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
| DN-REG-025 | unit | Spot unit이 `RelocationDisabled`, Store/callback failure와 post-commit failure의 terminal reason 및 committed count를 workload coordinator까지 보존한다. |
| DN-REG-026 | unit | Spot 또는 Actor가 owner commit 뒤 실패해도 target authority는 유지되고 source의 미commit workload와 host `Serving` 상태만 복원된다. Source rollback과 runtime force-stop은 발생하지 않는다. |
| DN-REG-027 | integration-single-process | Host의 짧은 absolute deadline이 여러 Spot phase, target reservation, lifecycle callback과 source cleanup 전체에 적용되고 만료 시 정확한 reason을 반환한다. |
| DN-REG-028 | unit | Actor Join target reservation의 `TargetUnavailable`은 `Unavailable`로, 존재하지 않는 requested Spot은 `NotFound`로 반환된다. |
| DN-REG-029 | contract | `ZLinkActorJoinCompletion.Failed`가 `OperationId`와 `Kind`만 노출하며 exact interface·source·package·sample 사이에서 같은 shape를 사용한다. |
| DN-REG-030 | unit | Actor maintenance, User Spot aggregate, PerActor shell과 Instance Spot relocation이 object kind별 started/completed/duration/bytes와 `completed|aborted|failed|shutdown` outcome을 한 번씩 기록한다. |
| DN-REG-031 | process E2E | `.Timeout(...)`으로 고정한 Actor Join deadline이 target admission callback과 capacity reservation보다 먼저 만료되며 target에 stale pending reservation을 남기지 않는다. |
| DN-REG-032 | integration-single-process | Local·cross-node Entry Spot Join은 Entry `OnActorJoin` callback 없이 Accepted가 되고, target `OnJoinedActor`와 source `OnLeaveActor`만 정해진 순서로 실행된다. |
| DN-REG-033 | unit | Relocate preflight가 accepted Create/Join/Instance placement/session/inbound operation과 actor handoff를 모두 기다린 뒤 publish한다. Deadline 만료 전에는 `Serving`을 유지한다. |
| DN-REG-034 | unit | Actor Capture·Restore·Store·deadline failure가 owner commit 전후에 따라 `StateIncompatible`, `StoreUnavailable`, `DeadlineExceeded`, `RelocationFailed`로 구분된다. |
| DN-REG-035 | documentation | 공통 E2E Config 1~14의 scenario ID가 `.NET` feature-map에 누락·중복 없이 존재하고 Config 14 Instance Spot도 분모에 포함된다. |
| DN-REG-036 | E2E manifest | 각 scenario ID가 feature-map, client selector, scenario dispatch, role server endpoint와 일대일로 연결되며 aggregate alias가 split ID를 숨기지 않는다. |
| DN-REG-037 | process E2E | `구현` 또는 `actual 통과`로 표시한 feature-map 행에 현재 process 실행 log, client-visible 결과, role server evidence와 bounded terminal assertion이 있다. Historical log와 source type 존재만으로 통과하지 않는다. |
| DN-REG-038 | E2E runner | `all`은 실행·제외 ID와 제외 사유를 출력하고, `부분`·`미구현`·`diagnostic_only`를 성공 분모로 세지 않으며 누락 ID에서 실패한다. |
| DN-REG-039 | E2E architecture | Client에는 `AddZLinkFramework`, `Host.CreateDefaultBuilder`, framework runtime/client 호출, reflection/private/internal API가 없고 framework 호출은 role server endpoint에서 수행된다. |
| DN-REG-040 | process E2E | ST-B2는 process 종료 뒤 no takeover/no replay/unavailable을, ST-B3/B4는 `RecreateOnRelocation`과 empty-state restore를 공통 spec과 같은 terminal 의미로 검증한다. |

## 9. Phase A Spec 완료 판정 checklist

- [ ] DN-IMP-001을 수정하고 DN-REG-003~008, 013을 통과했다.
- [ ] DN-IMP-002를 수정하고 DN-REG-009~010을 통과했다.
- [ ] DN-IMP-003의 exact interface를 먼저 확정하고 source와 package를 맞췄다.
- [ ] DN-IMP-004~018을 수정하고 DN-REG-014~034를 통과했다.
- [x] DN-TEST-001~004와 DN-TEST-007~008을 모두 닫았다.
- [x] DN-TEST-006의 이전 compile blocker를 확인하고 현재 targeted test 248/248 통과를 기록했다.
- [ ] DN-E2E-IMP-001~017을 수정하고 DN-REG-035~040을 통과했다.
- [ ] Config 1~14의 공통 scenario와 `.NET` feature-map·selector·aggregate runner 차이가 0개다.
- [ ] Contract, unit, provider, connector와 HTTP client regression이 모두 통과했다.
- [ ] Sample regression은 Phase B에서 별도로 실행하며 Phase A 완료를 대신하지 않는다.
- [ ] 실제 NuGet package export가 source와 exact interface에 일치한다.
- [ ] Process E2E 전체에서 `부분`이나 `미구현`으로 남은 항목은 이 ledger의 열린 gap과 일대일로 연결된다.
- [ ] 모든 구현 card가 Luna Max 구현, Sol Medium read-only review, finding 수정과 재검토 gate를 통과했다.
- [ ] 모든 POSD·DDD finding에 원칙, 책임 경계, 대안, 처리 결과와 Sol 재검토 판정이 기록되었다.
- [ ] G1~G4의 비자명한 설계 변경은 구현 전에 두 가지 이상 대안과 Sol Medium 사전 review를 기록했다.
- [ ] Core·bindings bug를 Framework에서 우회한 코드가 없고, 모든 선행 수정에 하위 layer regression과
      Sol review 결과가 있다.
- [ ] Core·bindings 선행 수정의 version, local package와 Framework 참조를 갱신하고 새 package를 사용한
      contract·regression·process 검증을 기록했다.
- [ ] 마지막 독립 audit에서 기록하지 않은 `.NET` gap이 0개다.
- [ ] 마지막 독립 audit을 새 Sol Medium reviewer가 수행했고 unresolved `Critical`·`High`·`Medium` finding이 0개다.

## 10. Phase B — common sample gap (Phase A 완료 후 시작)

Phase B는 Phase A의 `## 9. Phase A Spec 완료 판정 checklist`와 G7 독립 audit의 모든 항목이 통과된 뒤에만 시작한다.
Phase A 완료 전에는 아래 sample card를 구현 완료로 표시하거나 sample process evidence를 수집하지 않는다.

### 10.1 목적과 완료 조건

이 Phase B section은
[`framework/doc/framework/common/sample/`](../../framework/common/sample/README.ko.md)의 공통 sample
계약과 `framework/languages/dotnet/samples/`의 `.NET` 구현을 비교하여 gap을 확인하고, 확인된 차이를
수정하는 순서를 정한다. 비교 대상은 sample 이름이나 public API의 존재 여부에 한정하지 않는다. 다음
항목을 같은 기준으로 대조한다.

- message 이름, field 이름과 타입, nullable·optional 의미, enum 또는 named string 값
- request/reply, one-way send, notify, publish의 transport 의미와 handler 등록 방식
- payload가 wire에서 표현되는 방식과 typed JSON 또는 Protobuf codec의 선택
- topology, route, actor·spot ownership, session binding과 relocation의 실행 순서
- state commit, idempotency, retry·deadline, failure와 cleanup의 책임 경계
- Domain/Application/Infrastructure 분리와 application message에 내부 식별자를 노출하지 않는 규칙
- runner의 build, Redis 격리, readiness, client self-check, evidence와 cleanup 순서
- shell·PowerShell runner, 실제 process E2E, browser client와 회귀 test의 범위

다음 조건을 모두 만족해야 이 ledger의 sample 작업이 완료된다.

1. 공통 sample 7종의 정식 문서와 `.NET` shared contract를 한 행씩 대조한 inventory가 있고, 각 행의
   판정이 `충족` 또는 `수정 완료`로 닫힌다. `contract 선행`은 계약을 확정하기 전까지 열린 차단
   상태이며 완료 판정에 포함하지 않는다.
2. 공통 문서에 없는 `.NET` public 또는 client-facing message를 다른 언어 구현만으로 정당화하지
   않는다. 필요한 계약 변경은 먼저 공통 sample 문서와 관련 spec/guide의 review 대상으로 분리한다.
3. 확인된 wire shape와 runtime path gap을 소유한 계층에서 수정한다. sample 호출부에 raw frame, private
   API, reflection, message별 codec registry 또는 임시 adapter를 추가하지 않는다.
4. 모든 공통 sample의 직접적인 client assertion과 server evidence가 실제 process 실행에서 남는다.
   로그 문자열만으로 성공을 판정하지 않는다.
5. 지원하는 실행 환경의 shell·PowerShell runner가 같은 sample inventory와 완료 조건을 사용한다.
6. Source working tree를 사용하는 sample process E2E와 package artifact를 검증하는 clean consumer
   lane을 구분하고 둘 다 통과한다. `Systems.Zlink` bindings package와 Framework NuGet package는 서로
   다른 artifact로 기록하며 한쪽 결과로 다른 쪽을 통과 처리하지 않는다.
7. 마지막 독립 재검토에서 기록되지 않은 `.NET` sample spec·구현 gap이 0개다.

이 Phase B 계획 단계에서는 구현과 test source를 바꾸지 않는다. 구현 phase는 이 ledger의 선행 조건을
충족하고 모든 `contract 선행` 항목의 계약을 확정한 뒤 시작한다.

### 10.2 기준 문서와 조사 범위

공통 sample 문서는 실행 가능한 workflow, topology, message 계약, self-check와 완료 evidence를
소유한다. Framework public API의 계약 자체는 공통 Framework spec과 언어별 exact interface가 소유한다.
공통 sample 문서나 다른 언어 구현만으로 새 public API를 추가하지 않는다.

| 구분 | 기준 위치 | 이번 비교에서 확인할 내용 |
|---|---|---|
| 공통 sample index | `framework/doc/framework/common/sample/README.ko.md` | sample 목록, 공통 금지사항, 실행·완료 규칙 |
| 공통 sample 계약 | `bingo/README.ko.md`, `tictactoe/README.ko.md`, `supportchat/README.ko.md`, `deliverydispatch/README.ko.md`, `event/shoppingmall.ko.md`, `event/gamequest.ko.md`, `zoneworld/README.ko.md` | topology, message schema, flow, self-check, evidence |
| 공통 fixture | `framework/doc/framework/common/sample/fixtures/channel-topology.json` | 채널·mesh 이름과 역할의 공통 source |
| 공통 runner template | `framework/doc/framework/common/sample/runner-templates/` | readiness, Redis 격리, cleanup, 종료 marker |
| `.NET` sample source | `framework/languages/dotnet/samples/{Bingo,TicTacToe,SupportChat,DeliveryDispatch,ShoppingMall,GameQuest,ZoneWorld}/` | shared contract, server path, client assertion, runner |
| `.NET` sample runner | `framework/languages/dotnet/samples/run_samples.sh`, `run_samples.ps1`, 각 sample의 `run_sample.*` | 통합 inventory와 OS별 실행 순서 |
| `.NET` sample regression | `framework/languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/` | 정적 구조 검증, runner 정책, sample별 regression |
| 공통 documentation regression | `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Documentation/Regression.cs` | 공통 문서와 `.NET` guide의 최소 동기화 조건 |
| 선행 framework ledger | `framework/doc/plan/dotnet-framework-spec-gap-ledger/dotnet-framework-spec-gap-ledger.ko.md` | Framework runtime·contract·E2E가 sample 작업의 선행 조건을 충족했는지 |
| package 정책 | `scripts/local-package/README.ko.md`, `framework/languages/dotnet/Directory.Packages.props` | sample이 실제로 사용하는 bindings/Core package와 version |
| sample audit baseline log | [`log/20260802-090836-sample-regression-baseline.ko.md`](log/20260802-090836-sample-regression-baseline.ko.md) | 기준 commit, working tree, 실행 명령과 baseline 결과 |

대상 sample은 다음 일곱 가지다.

| Sample | 공통 핵심 흐름 | `.NET` 실행 경로에서 확인할 결과 |
|---|---|---|
| Bingo | authentication, actor binding, match·room join, card·draw, reward와 relocation | Protobuf payload, room ownership, session·actor binding, room cleanup |
| TicTacToe | HTTP game 생성, authenticate·join, turn·milestone, leave와 actor destroy | JSON request/reply, `LeaveGame` one-way semantics, Entry Spot cleanup |
| SupportChat | agent availability, conversation join, greeting·typing, idle close와 reconnect | metadata, one-way typing, close·reconnect state와 session route |
| DeliveryDispatch | delivery 생성, courier offer·decision, status push, timeout·reassign | status 순서, timestamp wire shape, retry·deadline과 client assertion |
| ShoppingMall | order start, inventory·payment workflow, event·projection, idempotency·failure | 접수 응답, durable event, workflow compensation과 재시도 |
| GameQuest | session join, gameplay action, event·projection, replay·reconcile | action response inventory, typed payload, domain event와 stream/Redis path |
| ZoneWorld | browser/stream session, zone 이동, bot·ops, border relocation과 message follow | actor·spot ownership, 이동 message boundary, Chromium/browser evidence |

#### 10.2.1 Model 배치와 card review gate

이 sample 작업의 model과 review gate는 선행 Framework ledger의 과거 실행 표기와 별도로 이 절을
따른다. Luna Max가 구현하고 Sol Medium이 candidate를 수정하지 않는 독립 review를 수행한다.

| 역할 | Model과 reasoning | 책임 | Source 수정 |
|---|---|---|---|
| Main implementer | Luna Max | Ledger card를 한 번에 하나씩 조사하고 regression을 먼저 고정한 뒤 구현·검증한다. | 허용 |
| Card reviewer | Sol Medium | 공통 sample 문서, Framework spec, 실제 diff와 test·process evidence를 contract·POSD·DDD 관점에서 검토한다. | 금지 |
| Final auditor | Sol Medium | G7에서 이전 card review와 독립적으로 sample 7종과 artifact evidence 전체를 다시 검사한다. | 금지 |

지정한 model이나 reasoning level을 사용할 수 없으면 다른 model로 바꾸지 않고 해당 card를 `차단`으로
기록한다. Main implementer와 reviewer는 같은 candidate를 동시에 수정하지 않는다. Review 직전에 기준
commit 또는 working tree manifest, `git status --short`, 전체 diff, 실행 명령과 결과를 고정한다.

각 card는 다음 gate를 통과해야 한다.

1. Luna는 공통 sample 문서, 관련 Framework spec·exact interface, production owner와 기존 test를 먼저
   확인한다. Card 밖에서 발견한 gap은 별도 card나 선행 조건으로 기록한다.
2. Public message, lifecycle, ownership, state transition, package boundary 또는 지원 host 범위를 바꾸는
   결정은 서로 다른 대안 두 가지 이상을 비교하고 Sol Medium의 사전 review를 받는다.
3. Luna는 실패 regression이나 현재 계약을 직접 검증하는 기존 test 근거를 먼저 고정한 뒤 source를
   수정하고 targeted regression과 필요한 process E2E를 실행한다.
4. Sol Medium은 Luna의 요약만 사용하지 않고 정식 문서, public contract, 전체 candidate와 evidence를
   직접 대조한다. `Critical`, `High`, `Medium` finding은 모두 blocking이다.
5. Luna가 blocking finding의 원인과 책임 경계를 수정하고 test를 다시 실행한 뒤 Sol Medium의 재검토를
   받는다. `Low`도 수용·기각·후속 분리 가운데 하나와 근거를 기록한다.
6. Sol Medium이 `CLEAN`으로 판정하고 필수 test와 process gate가 모두 통과한 뒤에만 다음 card로 이동한다.

##### POSD·DDD review 기준

Sol Medium은
[`software-design-principles.ko.md`](../../../../doc/principal/software-design-principles.ko.md)를 기준으로
다음 내용을 함께 검토한다.

- 새 helper, wrapper, mapper 또는 adapter가 요청을 전달만 하는 shallow module인지 확인한다.
- Message·topology·version 지식이 문서, fixture와 test에 독립적으로 복사되어 information leakage와
  변경 증폭을 만드는지 확인한다.
- Framework transport·codec·routing detail이 Domain이나 Application으로 올라오지 않는지 확인한다.
- Lifecycle, state transition, idempotency, deadline, retry와 cleanup의 owner가 한 경계에 모여 있는지
  확인한다.
- In-process port DTO와 실제 ZLink wire message, durable domain event와 transport event를 구분한다.
- 비자명한 finding은 두 가지 이상 대안을 단순성, 일반성, 성능, 호출자 부담과 책임 경계로 비교한다.

Finding에는 ID, severity, category(`contract`, `POSD`, `DDD`, `test/evidence`, `leftover`), file·line,
근거, 영향, 대안, 권장안과 재검토 결과를 기록한다.

#### 10.2.2 Artifact와 Core·bindings 선행 수정 gate

현재 `.NET` sample project는 Framework project를 `ProjectReference`로 사용하고 `Systems.Zlink` 같은
하위 library는 package로 참조한다. 따라서 다음 검증 lane을 섞지 않는다.

| Lane | 실제 검증 대상 | 완료 evidence |
|---|---|---|
| Source sample lane | 현재 working tree의 Framework source와 sample source | sample build, regression, 실제 process E2E와 cleanup |
| Core·bindings package lane | version이 고정된 `Systems.Zlink` package와 package 안의 native runtime | package version·hash, native version·hash, cache 경로와 consumer 결과 |
| Framework package lane | pack한 Framework NuGet의 public export와 dependency | `framework/languages/dotnet/scripts/verify_packaged_contract.sh`와 clean consumer 결과 |

Source sample E2E는 Framework NuGet을 사용했다는 증거가 아니다. Framework NuGet으로 sample process
동작까지 보장하려면 G6에서 별도 package-only clean consumer runner를 추가한다. 이 runner를 추가하지
않으면 완료 조건을 Framework package의 export·dependency 검증과 source sample process 검증으로
분리하여 기록하며, packaged Framework process E2E를 통과했다고 표현하지 않는다.

Sample 작업에서 Core 또는 bindings bug가 확인되면 sample이나 Framework에서 우회하지 않는다. Core
bug는 Core regression에, bindings bug는 해당 bindings regression에 실패를 먼저 재현하고 원인을 소유한
layer에서 수정한다. Raw frame 해석, reflection, private API, 상태 복제, retry helper 또는 sample 전용
adapter로 하위 layer 실패를 숨기는 변경은 완료로 인정하지 않는다.

하위 layer를 수정한 card는 다음 조건을 모두 충족한다.

1. 원인 layer, 최소 재현, public contract 근거와 regression 경로를 기록한다.
2. Luna Max 구현과 Sol Medium의 read-only POSD·DDD review를 같은 candidate에 적용한다.
3. Core를 수정했으면 `core/build`와 관련 Core test를 다시 실행하고
   [`scripts/local-package/README.ko.md`](../../../../scripts/local-package/README.ko.md)의 version 정책을
   적용한다.
4. `scripts/local-package/native/sync-local-core-libs.sh`로 새 runtime을 bindings workspace에 동기화하고
   필요한 package를 다시 만든다. .NET bindings만 수정했으면 version을 올린 뒤
   `scripts/local-package/build-wsl.sh dotnet`을 실행한다.
5. `framework/languages/dotnet/Directory.Packages.props`의 `ZLinkBindingsPackageVersion`을 새 package로
   갱신하고 이전 NuGet extraction cache를 새 artifact evidence로 재사용하지 않는다.
6. 새 package의 version·hash, native runtime version·hash와 consumer regression·process 결과를
   기록한 뒤 원래 sample card의 선행 조건을 닫는다.

### 10.3 선행 조건과 gap 상태

#### 10.3.1 선행 framework ledger gate

sample 실행이 의존하는 Framework runtime 의미를 sample source에서 우회해서는 안 된다. 따라서 다음
조건을 충족하기 전에는 이 ledger의 구현 card를 시작하지 않는다.

| 선행 조건 | 완료 판정 |
|---|---|
| Framework implementation audit | 기존 ledger의 `DN-IMP-001`~`DN-IMP-018`이 source, targeted test와 reviewer evidence로 닫힘 |
| Framework process E2E | 기존 ledger의 `DN-E2E-IMP-001`~`DN-E2E-IMP-017`에 실제 process 결과와 failure semantics가 기록됨 |
| Framework regression | 기존 ledger의 contract·unit·package·E2E regression card, 특히 `DN-REG-035`~`DN-REG-040`이 통과함 |
| Core·bindings package | 수정한 하위 layer가 있으면 package version, native runtime hash, consumer test와 `.NET` package cache가 일치함 |
| 독립 review | 선행 ledger의 final auditor가 sample이 사용할 public contract와 runtime 경계를 `CLEAN`으로 판정함 |

선행 card가 `선행 조건 미충족`, `contract 선행`, `test gap` 또는 unresolved finding이면 sample
구현에서는 raw frame, reflection, private member, 내부 상태 복제 또는 retry로 이를 숨기지 않는다. 원인을
소유한 ledger로 돌려보내고 이 문서에는 의존성만 기록한다.

#### 10.3.2 baseline log와 gap 판정

sample regression의 기준 commit, working-tree 조건, 실행 명령과 결과는
[`log/20260802-090836-sample-regression-baseline.ko.md`](log/20260802-090836-sample-regression-baseline.ko.md)에
기록한다. 본문은 실행 output을 반복하지 않고, baseline이 닫지 못한 gap을 `DS-IMP-*`와 `DS-REG-*`로
관리한다. 기존 정적 regression이 통과해도 공통 sample의 모든 message·flow·process evidence가 일치한
것으로 판정하지 않는다.

#### 10.3.3 이미 확인된 inventory·version 차이

다음은 별도의 실행을 통해 확인한 차이다.

| ID | 현재 근거 | 초기 판정 |
|---|---|---|
| `DS-IMP-008` | `run_samples.sh`의 `SAMPLES`에는 `ZoneWorld`가 있지만 `run_samples.ps1`의 `$knownSamples`에는 없다. | 확인된 runner parity gap |
| `DS-IMP-007` | `framework/languages/dotnet/samples/README.md`는 Framework `10.0.0` contract를 설명하지만 정식 Framework public contract governance는 `11.0.0`을 기준으로 한다. `Directory.Packages.props`의 `11.1.0`은 별도 `Systems.Zlink` bindings dependency version이다. | Framework 문서 version gap 확인, bindings version과의 직접 비교는 제외 |
| `DS-IMP-007` | `.NET` guide의 integrated runner 설명은 여섯 sample과 ZoneWorld 별도 실행을 말하지만 현재 shell runner는 일곱 sample을 포함한다. | 확인된 guide·runner inventory gap |
| `DS-IMP-009` | sample regression은 여러 구조 규칙을 검증하지만, 공통 sample의 모든 message·field·flow·evidence와 `.NET` client/server path를 한 번에 대조하는 inventory test가 없다. | 확인된 test/evidence gap |

### 10.4 gap 판정 규칙

각 finding은 다음 상태 가운데 하나로 관리한다.

| 상태 | 의미 | 다음 행동 |
|---|---|---|
| `확인` | 공통 문서와 `.NET` source 또는 process path의 차이를 재현 가능한 근거로 확인했다. | 원인 owner를 정하고 regression을 먼저 고정한 뒤 수정한다. |
| `contract 선행` | 공통 문서 자체에 응답 유무, field shape 또는 public 범위가 모호하거나 서로 충돌하는 열린 차단 상태다. | 구현을 바꾸지 말고 공통 문서·spec·guide review에서 계약을 먼저 확정한다. 확정 전에는 card를 닫지 않는다. |
| `test gap` | 구현이 맞을 가능성이 있어도 현재 test가 해당 계약을 직접 판정하지 않는다. | exact inventory, wire assertion 또는 process evidence를 추가한다. |
| `documentation gap` | source와 실행 path는 같지만 문서·runner 목록·version 설명이 현재 상태와 다르다. | 정식 문서 owner에서 근거를 갱신하고 regression을 추가한다. |
| `충족` | source, wire 결과, 실행 evidence와 test가 같은 계약을 직접 증명한다. | 근거 경로와 명령을 기록하고 다시 열지 않는다. |
| `수정 완료` | 확인된 gap을 원인 owner에서 수정하고 regression·process·review gate를 통과했다. | Candidate와 evidence를 기록하고 최종 audit에서 다시 확인한다. |
| `차단` | 필요한 model, contract 결정, package 또는 실행 환경이 없어 안전하게 진행할 수 없다. | 조건을 충족할 때까지 완료 판정에서 제외한다. |

기능 이름이 같거나 build가 성공했다는 사실만으로 `충족`으로 분류하지 않는다. handler의 transport
종류, response의 field, state commit 시점, failure 후 cleanup까지 실제 path를 따라간다.

### 10.5 구현 수준에서 확인된 gap

#### DS-IMP-001 — one-way message의 이름과 의미가 공통 문서와 다름

**현재 판정: `확인`; 공통 문서 변경 diff를 먼저 동결한 뒤 최종 수정 방향을 정한다.**

공통 TicTacToe 문서는 `LeaveGameMsg`를 선언하고, actor가 보내는 one-way message이며 response를
기다리지 않는다고 설명한다. 현재 `.NET` shared contract, handler, client와 runner는
`LeaveGameReq`를 사용한다. 공통 SupportChat 문서는 같은 규칙으로 `SetTypingMsg`를 선언하지만 `.NET`
은 `SetTypingReq`를 사용한다.

근거:

- 공통 문서: `framework/doc/framework/common/sample/tictactoe/README.ko.md`의 message 선언과 leave flow,
  `framework/doc/framework/common/sample/supportchat/README.ko.md`의 typing contract
- `.NET`: `TicTacToe/Shared/Contracts/Messages.cs`, `TicTacToe/.../PlayActorLeaveGameHandler.cs`,
  `TicTacToe/Client/TicTacToeClientScenario.cs`, `SupportChat/Shared/Contracts/Messages.cs`,
  `SupportChat/.../SetTypingHandler.cs`

수정 전에 확인할 내용은 다음과 같다.

1. 공통 sample의 `Req/Res`, `Msg`, `Notify`, `Event` 명명 규칙이 public wire contract인지, 문서상의
   역할 설명인지 확인한다.
2. one-way send가 실제로 response subscription이나 implicit completion을 사용하지 않는지 확인한다.
3. 계약이 확정되면 모든 `.NET` shared contract, handler attribute, client scenario, runner evidence와
   회귀 test를 같은 이름·transport 의미로 맞춘다. 이름만 바꾸고 request/reply path를 남기지 않는다.

#### DS-IMP-002 — GameQuest action inventory와 `GameplayMsg.payload`가 공통 계약과 다름

**현재 판정: `contract 선행`과 `확인`이 함께 있다. action 범위는 계약 review 후 수정하고, payload wire
shape는 실제 serializer 결과까지 확인한다.**

공통 GameQuest 문서는 `KillMonsterReq/Res`, `CollectItemReq`, `EnterAreaReq`,
`GetQuestProgressReq/Res`, `SyncQuestProgressReq/Res`를 선언하며 `GameplayMsg.payload`를 `object`로
설명한다. 현재 `.NET`은 `CollectItemRes`, `CompleteMissionReq/Res`, `EnterAreaRes`,
`UnlockFeatureReq/Res`를 추가하고, client가 이 응답을 요청한다. `GameplayMsg.Payload`는 `byte[]`이며
`GameplayEventOwnerDispatcher`와 `QuestContractMapper`가 UTF-8 byte payload를 직접 직렬화·역직렬화한다.
또한 공통 문서의 durable domain record 이름과 `.NET`의 `QuestProgressedEvent` 등 이름에 `Event` 접미어
차이가 있다.

공통 문서의 action 설명 일부는 각 action response가 EventId를 만든다고 서술하므로 declaration과
본문이 완전히 일치하지 않는다. 따라서 `.NET`의 추가 action을 바로 제거하거나 공통 문서에 바로 추가하지
않는다.

수정 순서:

1. action별 request/reply 유무, EventId 반환, public client surface 여부를 공통 sample contract에서
   하나의 표로 확정한다.
2. `GameplayMsg.payload`가 typed JSON object인지, envelope 안의 bytes인지, domain record의 저장
   payload와 transport payload를 어떻게 구분하는지 결정한다.
3. 계약 확정 뒤 `.NET` shared message, handler, client scenario와 mapper를 한 경계에서 수정한다.
   호출부에 `encode`, `decode`, `serialize`, `parse`를 추가하는 우회는 허용하지 않는다.
4. durable domain record 명칭은 transport message와 분리해 문서·source·store mapper의 용어를 같은
   의미로 맞춘다.

#### DS-IMP-003 — ShoppingMall 접수 응답의 field shape가 공통 계약과 다름

**현재 판정: `확인`; `StartOrderRes` wire shape만 gap이며 in-process port DTO 이름은 `충족`이다.**

공통 ShoppingMall 문서는 `StartOrderRes { orderId: string; state: OrderState }`를 선언한다. 현재 `.NET`
`ShoppingMall/Shared/Contracts/Messages.cs`의 `StartOrderRes`는 `OrderId`와 `Status` 문자열을 가진다.

`.NET`의 `ReserveInventoryCommand/Result`, `ReleaseInventoryCommand/Result`와
`AuthorizePaymentCommand/Result`는 `Server/Shared/Ports/Outbound`를 통해 같은 process의 application
port를 호출하는 DTO다. ZLink로 dispatch되는 wire message가 아니므로 공통 sample index의 in-process
port 예외에 따라 `Command/Result` 이름을 유지한다. 이 이름을 `Req/Res`로 바꾸거나 새 application
message로 노출하지 않는다.

확인할 항목:

- `StartOrderRes`가 실제 client wire payload에서 `state` enum 또는 named string을 보내는지
- `Status`가 public contract의 `OrderState`와 같은 값 집합·nullable 의미를 갖는지
- idempotency 재호출, workflow failure와 compensation에서 접수 응답과 event 순서가 공통 flow와 같은지

수정은 `StartOrderRes` public wire shape와 client assertion에 한정한다. Regression은 application port
DTO가 ZLink handler나 shared wire contract로 이동하지 않는 조건도 함께 유지한다.

#### DS-IMP-004 — DeliveryDispatch timestamp의 wire type이 다름

**현재 판정: `확인`; JSON 또는 Protobuf의 실제 wire 값을 직접 캡처해 최종 판정한다.**

공통 DeliveryDispatch 문서는 `DeliveryStatusChangedReq`, `DeliveryStatusNotify`,
`DeliveryStatusUpdatedMsg`의 `occurredAtUnixMs: int64`를 요구한다. 현재 `.NET`
`DeliveryDispatch/Shared/Contracts/Messages.cs`는 해당 값에 `DateTimeOffset OccurredAt`을 사용한다.
server evidence에서 `ToUnixTimeMilliseconds()`를 호출하는 부분이 있어 저장·로그 표현과 transport
payload가 다를 가능성이 있다.

수정 순서:

1. client가 수신한 raw application payload를 업무 코드에서 해석하지 않는 별도 test harness로 확인한다.
2. 공통 계약의 `int64`와 `.NET` serializer가 실제로 같은 JSON number를 만드는지 확인한다.
3. 다르면 shared contract와 모든 handler·client assertion을 Unix milliseconds로 맞추고, 같다면
   `DateTimeOffset` 표현을 내부 type으로 명시하여 public wire contract와 혼동되지 않도록 문서화한다.
4. status ordering, late decision과 reassign test에 timestamp 비교를 포함한다.

#### DS-IMP-005 — Bingo client-facing message와 optional field가 공통 목록과 다름

**현재 판정: `확인`; extra message의 public 범위는 contract review가 필요하다.**

공통 Bingo message 목록에는 `BingoJoinFailedNotify`와 `BingoActorEntrySpotNotify`가 명시되어 있지
않지만 `.NET` Protobuf schema에는 두 message가 있고 `PlayerActor`와 Entry Spot이 session으로
전송한다. 공통 `AuthenticatePlayerRes`는 `actor_id`, `display_name`, `reason`을 optional로
설명하지만 `.NET` proto는 plain `string` field로 선언한다.

확인할 항목:

- 두 notify가 client가 관찰해야 하는 public push인지, sample 내부 evidence인지
- optional field가 미설정·빈 문자열·null을 구분해야 하는지
- 모든 언어가 같은 field presence와 default 값을 제공해야 하는지
- notify를 공통 계약에 추가할지, client-facing surface가 아니면 `.NET` shared contract에서 분리할지

계약이 확정되기 전에는 다른 언어 구현을 근거로 새 public message를 추가하지 않는다. 확정 후 Protobuf
schema, client assertion, handler와 공통 문서의 message·flow·completion 조건을 함께 변경한다.

#### DS-IMP-006 — ZoneWorld 이동이 공통 message 경계를 우회함

**현재 판정: `확인`; 기능 존재가 아니라 Actor → Zone Spot 전달 방식의 차이다.**

공통 ZoneWorld 문서는 `UpdatePositionMsg`를 선언하고, 같은 zone의 이동은 Actor가 이 message를 Zone
Spot에 보내 state를 변경한다고 설명한다. 현재 `.NET` `ZoneWorld/Shared/Contracts/ZoneWorldMessages.cs`
에는 `UpdatePositionMsg`가 없으며, `PlayerMoveHandlers.cs`가 `actor.MoveTo(...)` 뒤 `ZoneSpot.UpdatePosition(...)`
을 직접 호출한다. `ZoneSpot.UpdatePosition`은 internal method다.

이 차이는 단순한 이름 차이가 아니다. message route, queue·handler 순서, owner commit과 state
publication이 Framework runtime의 public path를 거치지 않게 된다.

수정 순서:

1. `UpdatePositionMsg`의 sender, target, one-way semantics와 same-zone·border relocation의 commit
   경계를 공통 문서에서 고정한다.
2. Framework public message handler를 통해 Zone Spot state를 갱신하도록 production path를 바꾼다.
3. application code가 internal `ZoneSpot.UpdatePosition`이나 actor reference를 직접 호출하지 않는지
   정적 test로 고정한다.
4. same-zone move, border crossing, actor relocation과 message follow를 실제 process E2E에서 확인한다.

#### DS-IMP-007 — `.NET` sample 문서·guide·version 설명이 현재 실행 경로와 다름

**현재 판정: `documentation gap`; source 수정과 섞지 않고 문서 owner에서 별도로 닫는다.**

현재 `.NET` sample README는 Framework `10.0.0` contract를 설명하지만
`framework/doc/framework/common/spec/00-public-contract-governance.ko.md`는 Framework `11.0.0` 공개
계약을 기준으로 한다. `Directory.Packages.props`의 `ZLinkBindingsPackageVersion=11.1.0`은 sample이
참조하는 `Systems.Zlink` bindings package version이며 Framework contract version과 같은 값일 필요가
없다. `.NET` server guide는 integrated runner가 여섯 sample을 처리하고 ZoneWorld를 별도로 실행한다고
설명하지만 shell integrated runner는 ZoneWorld를 포함한 일곱 sample을 선택한다.

수정 전에 다음을 확인한다.

- README가 historical contract 예제인지 현재 package contract 안내인지
- guide가 generated output인지 source owner가 어디인지
- Framework contract version을 문서에 고정할지 정식 governance를 링크할지
- `Systems.Zlink` bindings version은 dependency evidence로 별도 표시할지
- ZoneWorld browser runner를 integrated sample 완료 조건에 포함할지

확정 후 README의 Framework contract 기준, guide·runner 목록과 bindings dependency evidence를 각각
해당 owner에서 맞춘다. Framework contract와 bindings package version의 숫자 일치를 강제하지 않는다.
Generated guide라면 생성 source를 고친다.

#### DS-IMP-008 — PowerShell 통합 runner가 ZoneWorld를 제외함

**현재 판정: `확인`; 지원 OS 범위를 먼저 결정한다.**

`run_samples.sh`의 기본 sample 목록은 `TicTacToe Bingo SupportChat ShoppingMall DeliveryDispatch
GameQuest ZoneWorld`다. `run_samples.ps1`의 `$knownSamples`에는 `ZoneWorld`가 없다. 따라서 shell과
PowerShell의 기본 실행 범위가 다르고, 공통 sample 7종을 모두 실행했다는 판정을 Windows runner가
증명하지 못한다.

지원 범위가 동일하다는 결정을 내리면 다음을 수정한다.

1. PowerShell integrated runner에 ZoneWorld를 추가한다.
2. ZoneWorld의 PowerShell per-sample runner와 browser/static configuration 전달 경로가 없으면
   공통 runner 규칙에 맞춰 추가한다.
3. Windows에서 dedicated Redis, readiness, browser self-check, cleanup을 실제로 실행한다.

Windows에서 ZoneWorld를 지원하지 않기로 결정하면 공통 sample 문서와 `.NET` guide에 그 제한과 대체
검증 명령을 명시하고, runner regression이 제한을 누락으로 오인하지 않도록 계약을 갱신한다.

#### DS-IMP-009 — 공통 sample 전체를 덮는 exact inventory와 process evidence가 없음

**현재 판정: `test gap`.**

현재 sample regression은 topology, codec 사용, runner 문자열, 일부 sample flow와 금지 API를 검사한다.
하지만 다음 관계를 한 번에 검사하는 기준이 없다.

- 공통 문서의 모든 message·field·transport kind와 `.NET` shared contract·handler의 대응
- 공통 flow의 각 단계와 `.NET` client scenario의 response·push·ordering assertion
- state commit, actor/spot ownership, relocation과 cleanup의 server evidence
- shell·PowerShell runner의 sample inventory와 실제 completion marker
- 공통 fixture의 topology와 `.NET` source가 사용하는 mesh·channel 값

이 gap은 source를 형식적으로 스캔하는 test 하나로 끝내지 않는다. static inventory와 최소 한 번의 실제
process smoke를 모두 추가한다.

baseline regression의 통과 여부와 무관하게 `DS-REG-004`의 Entry Spot destroy 문서·실행 의미 대조와
`DS-REG-001`의 전체 message inventory가 필요하다. 해당 실행 명령과 결과는 baseline log에서 확인한다.

### 10.6 Sample별 구현 path 검토 matrix

다음 matrix는 각 sample에서 이름 일치 외에 확인할 method-level 검토 범위를 고정한다. `검증 path`는
구현 phase에서 실제 line과 실행 log를 추가한다.

| Sample | 계약·동작 검토 범위 | `.NET`에서 먼저 읽을 path | 완료 evidence |
|---|---|---|---|
| Bingo | Protobuf field presence, authentication·actor binding, match reservation, room join·yield, reward publish, room relocation·cleanup | `Shared/Contracts/bingo_messages.proto`, `Server/Play`, `Server/Session`, `Client`, `run_sample.*` | 두 client의 card·draw·result assertion, room owner·actor relocation log, Redis cleanup |
| TicTacToe | HTTP create response, manual topology, join admission, turn order, milestone publish, `LeaveGameMsg` one-way과 Entry Spot destroy | `Shared/Contracts/Messages.cs`, `Server/Play/.../Handlers`, `Client/TicTacToeClientScenario.cs`, `run_sample.*` | response·GameState·milestone payload assertion, leave completion, destroy evidence |
| SupportChat | agent availability, conversation join, `SetTypingMsg` one-way, metadata propagation, idle close, reconnect와 session route | `Shared/Contracts/Messages.cs`, `Server/Support`, `Client/SupportChatClientScenario.cs`, `run_sample.*` | greeting·typing·close·reconnect ordering과 conversation owner evidence |
| DeliveryDispatch | offer/decision transport kind, status order, `occurredAtUnixMs` wire number, retry·deadline·reassign, late decision | `Shared/Contracts/Messages.cs`, `Server/Tracking`, `Server/CustomerGateway`, `Client`, `run_sample.*` | Assigned→Accepted→PickedUp→Delivered 또는 Reassigned sequence와 timestamp assertion |
| ShoppingMall | `StartOrderRes` state shape, in-process port DTO와 wire message 분리, durable event name, projection rebuild, idempotency, compensation | `Shared/Contracts/Messages.cs`, `Server/Shared/Ports`, `Server/CommerceApi`, `Server/OrderWorkflow`, `Client`, `run_sample.*` | 접수 응답 field, port DTO 비노출, event/projection 상태, 재호출·failure 결과와 store cleanup |
| GameQuest | action inventory, EventId response 의미, typed `GameplayMsg` payload, replay/reconcile, domain event와 store mapper 분리 | `Shared/Messages.cs`, `Server/GameApi`, `Server/QuestMission`, `Client/GameQuestClientScenario.cs`, `run_sample.*` | 각 action response, progress notify, replay/reconcile 결과와 no-transport-domain dependency |
| ZoneWorld | `UpdatePositionMsg` route, same-zone state update, border relocation·message follow, bot backpressure, ops replay, browser ws/wss/reconnect | `Shared/Contracts/ZoneWorldMessages.cs`, `Server/ZoneNode`, `Server/Gateway`, `Server/Ops`, browser client, `run_sample.sh` | browser self-check, same-zone/border state, owner·route evidence, process cleanup |

이 matrix에서 공통 문서와 `.NET` source의 용어가 다르면 먼저 `contract 선행`으로 이동한다. `.NET`
source를 기준으로 공통 문서를 축소하지 않는다.

### 10.7 수정 순서와 card gate

G0~G7의 각 card는 2.1절의 Luna Max 구현과 Sol Medium review gate를 독립적으로 통과한다. Core 또는
bindings 문제가 확인되면 2.2절의 하위 layer regression·수정·package gate를 먼저 닫는다.

#### G0 — 선행 Framework ledger 완료 확인

기존 `.NET Framework` ledger의 implementation·E2E·regression·package gate를 다시 실행한다. Framework
NuGet은 `verify_packaged_contract.sh`의 clean consumer로 public export와 dependency를 확인하고,
`Systems.Zlink`는 bindings package version·native hash·consumer 결과를 별도로 확인한다. 현재 sample
runner가 Framework `ProjectReference`를 사용한다는 사실도 manifest에 기록한다. 이 단계에서는 sample
source를 수정하지 않는다.

#### G1 — 공통 sample 계약 동결과 모호성 분리

현재 working tree의 common sample 문서 diff를 별도 manifest로 보존한다. `DS-IMP-001`, `DS-IMP-002`,
`DS-IMP-005`, `DS-IMP-008`처럼 public message 범위나 지원 OS를 바꾸는 항목은 공통 문서·guide·다른
언어 구현을 함께 읽고 다음을 기록한다.

- 확정된 public message와 internal-only type
- transport kind와 response completion의 의미
- field type, optionality, enum/named string 값
- 지원 runner와 browser path
- 계약 변경이 필요한 경우 관련 spec/guide owner와 review 결과

계약이 확정되지 않은 항목은 구현 card로 이동하지 않는다.

#### G2 — exact contract inventory와 실패 regression 고정

공통 sample 7종의 정식 문서를 inventory의 단일 계약 owner로 유지한다. 우선 문서의 structured message
declaration과 table을 직접 읽어 `.NET` shared contract와 비교하고, 추출 결과는 기준 문서 path·hash를
가진 generated evidence로만 저장한다. Generated evidence를 사람이 독립적으로 수정하는 두 번째 계약
fixture로 사용하지 않는다.

Markdown 구조를 안정적으로 읽을 수 없다면 별도 test fixture를 바로 복사해 만들지 않는다. 대안은
공통 sample 아래에 machine-readable contract fixture를 새 단일 기준으로 두고 문서와 언어별 test가
함께 사용하도록 공통 sample 정책을 먼저 변경하는 것이다. 기본 선택은 정식 문서를 직접 읽는 방식이며,
fixture 승격은 공통 정책 변경 review를 통과한 경우에만 사용한다. Inventory의 한 행은 sample, message,
direction, transport kind, response, field shape, nullable, owner, 기준 위치와 evidence를 포함한다. 이
단계의 test가 먼저 실패해야 G3 이후 source 수정의 범위를 알 수 있다.

#### G3 — wire contract와 public sample path 정렬

`DS-IMP-001`~`DS-IMP-005`를 계약 결정에 따라 수정한다. shared message와 client/server handler를 함께
변경하고, serializer가 만드는 실제 payload를 assertion한다. `DS-IMP-003`의 in-process
`Command/Result`는 변경 대상에서 제외하고 public `StartOrderRes`만 정렬한다. 내부 workflow 이름이나
domain event 이름은 public wire contract와 분리한다. 새 public API, raw frame, reflection 또는 호출부
codec은 추가하지 않는다.

#### G4 — runtime method path와 ownership 정렬

`DS-IMP-006`을 우선 수정하고 각 sample의 relocation, state commit, cleanup, retry·deadline 경계를
실제 call path로 다시 검사한다. ZoneWorld처럼 internal method 직접 호출이 공통 message boundary를
우회하는 경우 Framework public handler와 sample application 책임을 분리한다. G3의 wire 수정이
runtime semantics에 영향을 주면 같은 card에서 process E2E를 다시 실행한다.

#### G5 — runner·guide·package 설명 정렬

`DS-IMP-007`과 `DS-IMP-008`을 지원 OS 결정 뒤 수정한다. Shell·PowerShell 목록, per-sample runner,
ZoneWorld browser configuration, dedicated Redis와 cleanup을 같은 template 규칙으로 맞춘다. Framework
contract version은 정식 governance, `Systems.Zlink` version은 `Directory.Packages.props`, Framework
NuGet artifact는 package verification evidence를 각각 기준으로 사용한다. Generated 문서는 생성 source를
수정한다.

#### G6 — 회귀 test와 실제 process evidence 추가

G2에서 정식 문서로부터 읽은 inventory를 regression으로 고정하고, sample별 source runner를 실제로
실행한다. 각 실행은 build → dedicated Redis/resource → server start → readiness → client self-check →
evidence 수집 → cleanup 순서를 지켜야 한다. 한 sample의 로그가 남아 있다는 사실만으로 다른 sample의
완료를 추론하지 않는다.

이어 `Systems.Zlink` package version·native hash가 source runner의 실제 restore 결과와 일치하는지
확인하고 Framework NuGet은 `framework/languages/dotnet/scripts/verify_packaged_contract.sh`로 clean
consumer를 검증한다. Framework NuGet을 사용하는 package-only sample runner를 추가한 경우에만 packaged
Framework process E2E를 별도 결과로 기록한다.

#### G7 — 최종 독립 audit

이전 card를 검토하지 않은 새 Sol Medium reviewer가 선행 Framework ledger와 이 ledger를 다시 읽는다.
Source, common sample 문서, `.NET` guide, runner, test, Framework contract, Core·bindings package와
Framework NuGet evidence를 교차 대조한다. `확인`·`test gap`·`contract 선행`·`차단` 항목이나 unresolved
`Critical`·`High`·`Medium` finding이 남아 있으면 완료로 표시하지 않는다.

각 card에는 기준 commit 또는 working tree manifest, 변경 파일, 실행 명령, 결과, reviewer finding과
재검토 결과를 기록한다. 구현 중 unrelated dirty change를 되돌리거나 덮어쓰지 않는다.

### 10.8 기존 회귀 test의 유지와 변경 목록

#### 10.8.1 계속 유지할 test

기존 test는 범위를 줄이지 않고 유지한다. 다음 test들이 이미 보장하는 topology·runner 정책·codec
금지·sample별 flow를 새 inventory test로 대체하지 않는다.

| Test 영역 | 현재 보장하는 내용 | sample ledger에서의 역할 |
|---|---|---|
| `Regression.cs` | public connector assertion surface, sample discovery, payload codec 정책, actor destroy 문서 조건 | 공통 문서의 최소 규칙을 유지하고 exact message inventory와 연결 |
| `BingoRegressionTests` | topology, relocation adapter, room join·dedupe, client card/draw와 Redis 격리 | Bingo 실행 방식의 기존 regression 유지 |
| `TicTacToeRegressionTests` | handler registration, manual topology, relocation, runner/evidence, lifecycle와 payload lifetime | `LeaveGame` semantics와 destroy evidence를 확장 |
| `SupportChatRegressionTests` | one mesh, handler scan, rejection, relocation과 Redis 격리 | typing·metadata·reconnect assertion을 확장 |
| `DeliveryDispatchRegressionTests` | topology, status order, location store, binder/response, readiness/no retry | timestamp wire type와 late decision을 확장 |
| `ShoppingMallRegressionTests` | owner topology, isolated stores, Domain boundary | `StartOrderRes`와 workflow event shape를 확장 |
| `GameQuestRegressionTests` | isolated Redis/stream, Domain dependency boundary | action·payload inventory와 replay/reconcile를 확장 |
| `ZoneWorldTopologyRegressionTests`, `ZoneWorldOpsConsoleRegistryTests` | physical mesh, global route, relocation gate, ops registry | `UpdatePositionMsg` boundary와 browser process evidence를 확장 |
| `SampleConfigurationPolicyRegressionTests` | config provider 금지, readiness, shell sample 목록, browser static config, backpressure | shell·PowerShell inventory parity와 실제 completion gate를 추가 |
| `ExecutionTurnRegressionTests` | scenario inventory, typed packet names, bounded evidence, canonical ID | 공통 flow 단계별 assertion과 연결 |

#### 10.8.2 수정 또는 추가할 regression

아래 ID는 구현 phase에서 추가할 regression 목록이다. 현재 문서 작성 단계에서는 test source를 수정하지
않는다.

| ID | 대상 test | 추가·변경할 판정 |
|---|---|---|
| `DS-REG-001` | `CommonSampleContractInventoryMatchesDotNetSharedTypes` | 공통 7종의 message·field·direction·transport kind가 `.NET` shared contract와 일치하는지 확인 |
| `DS-REG-002` | `CommonSampleMessageSemanticsMatchDotNetHandlers` | `Msg`, `Req/Res`, `Notify`, `Event`가 실제 send/request/publish handler와 같은 의미인지 확인 |
| `DS-REG-003` | `CommonSampleOptionalAndEnumValuesMatchWireContract` | optional/null/default와 enum 또는 named string 값을 실제 serialized payload로 확인 |
| `DS-REG-004` | `TicTacToeLeaveUsesOneWayMessage` | `LeaveGameMsg`의 response subscription과 request/reply 우회를 금지하고 Entry Spot destroy evidence를 확인 |
| `DS-REG-005` | `SupportChatTypingUsesOneWayMessage` | typing send가 one-way이며 metadata·conversation route를 유지하는지 확인 |
| `DS-REG-006` | `GameQuestActionInventoryMatchesCommonContract` | extra action과 response를 계약 review 결과에 따라 허용하거나 실패시키고, 임의 public API를 금지 |
| `DS-REG-007` | `GameQuestGameplayPayloadMatchesTypedContract` | `GameplayMsg.payload`의 object/JSON wire shape와 store mapper의 domain conversion을 분리해 확인 |
| `DS-REG-008` | `ShoppingMallStartOrderResponseMatchesCommonShape` | `orderId`와 `state`의 타입·값 집합·idempotent 재호출 결과를 확인 |
| `DS-REG-009` | `DeliveryDispatchTimestampsUseCommonWireEncoding` | status request/notify/update의 `occurredAtUnixMs`가 같은 wire number와 ordering을 사용하는지 확인 |
| `DS-REG-010` | `BingoClientFacingMessagesMatchCommonInventory` | extra notify의 public 여부와 Protobuf optional presence를 공통 목록과 대조 |
| `DS-REG-011` | `ZoneWorldMoveUsesUpdatePositionMessageBoundary` | Actor 이동이 public message handler를 통해 Zone Spot state를 변경하고 internal method를 직접 호출하지 않는지 확인 |
| `DS-REG-012` | `IntegratedSampleRunnerIncludesEveryCommonSampleOnAllSupportedHosts` | shell·PowerShell sample 목록이 공통 7종과 같고, 지원하지 않는 host 제한은 문서화되었는지 확인 |
| `DS-REG-013` | `CommonSampleRunnerUsesIsolatedRedisAndCleanup` | sample별 dedicated Redis, readiness 실패 처리, process 종료와 cleanup을 양 OS에서 확인 |
| `DS-REG-014` | `CommonSampleCompletionRequiresClientAndServerEvidence` | payload·ordering self-check와 server ownership/cleanup evidence가 모두 있어야 성공으로 판정 |
| `DS-REG-015` | `CommonSampleTopologyUsesSharedFixture` | mesh·channel role이 공통 fixture와 일치하고 sample마다 임의 상수를 복사하지 않는지 확인 |
| `DS-REG-016` | `DotNetSampleDocsMatchRunnerAndVersionOwners` | sample README의 Framework contract, generated guide·runner 목록, `Systems.Zlink` dependency version이 각각 정식 owner와 일치하며 서로 같은 version을 강제하지 않는지 확인 |
| `DS-REG-017` | `AllCommonSamplesRespectApplicationBoundaryRules` | Domain의 Framework/storage 의존, application message의 NodeRid·ActorRef·raw frame·reflection·message별 codec을 전 sample에서 금지 |
| `DS-REG-018` | `ZoneWorldBrowserAndProcessEvidenceIsComplete` | Chromium/browser ws/wss/reconnect, static config 전달, same-zone·border relocation 결과를 실제 실행으로 확인 |

현재 `DotNet_Docs_Keep_Actor_Destroy_Entry_Owned`는 통과한다. `DS-REG-004`를 추가할 때 기존 문자열
assertion을 삭제하거나 약화하지 않고 `LeaveGameMsg`의 실제 one-way handler, Entry Spot destroy와
process evidence를 추가로 검증한다. 공통 문서가 의도적으로 바뀌면 정식 표현을 먼저 확정하고 test와
문서를 함께 갱신한다.

### 10.9 실행·증거 수집 계획

구현 phase의 최소 검증 순서는 다음과 같다.

1. 공통 문서에서 직접 읽은 inventory와 `.NET` source를 static 비교하고, 실패하는 `DS-REG`을 확인한다.
2. shared contract와 server/client project를 build한다. build 성공은 sample 완료 증거가 아니다.
3. `dotnet test tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj`
   와 영향받은 Framework contract/unit regression을 실행한다.
4. 지원 host마다 `run_samples.sh` 또는 `run_samples.ps1`로 공통 sample 목록을 실행한다. 각 sample은
   dedicated Redis와 고유 resource prefix를 사용한다.
5. ZoneWorld는 `.NET` process와 공통 TypeScript browser client를 함께 실행하고 Chromium 결과,
   static configuration, ws/wss/reconnect와 border relocation evidence를 보관한다.
6. client가 response·push payload와 ordering을 직접 assertion했는지 확인한다. runner log는 assertion과
   server evidence를 찾는 보조 자료로만 사용한다.
7. 종료 뒤 server, client, Redis와 temporary resource가 정리되었는지 확인한다. 이전 실행 log나 stale
   completion marker를 새 실행 결과로 사용하지 않는다.
8. Source sample process가 restore한 `Systems.Zlink` package version, package hash와 native runtime hash를
   기록하고 이전 NuGet extraction cache를 사용하지 않았는지 확인한다.
9. Framework NuGet은 `framework/languages/dotnet/scripts/verify_packaged_contract.sh`로 clean consumer를
   검증한다. Package-only sample runner를 실행하지 않았다면 packaged Framework process E2E 결과로
   기록하지 않는다.
10. 마지막으로 `git diff --check`, 변경 manifest, test 결과와 process evidence 경로를 card에 기록한다.

실행 결과에는 명령, host/runtime version, Framework contract version, `Systems.Zlink` package
version·hash, native runtime version·hash, Framework NuGet clean consumer 결과, 시작 시각, sample별 exit
code, assertion 수, evidence 파일과 cleanup 결과를 포함한다. 실패한 sample을 제외한 나머지 성공만으로
전체 완료를 표시하지 않는다.

### 10.10 완료 checklist

- [ ] 선행 `.NET Framework` ledger의 implementation·E2E·regression·package gate가 닫혔다.
- [ ] 모든 card가 Luna Max 구현과 Sol Medium read-only review·재검토 gate를 통과했다.
- [ ] 모든 POSD·DDD finding에 원칙, 책임 경계, 두 가지 이상 대안과 처리 결과가 기록되었다.
- [ ] 현재 common sample 문서 diff와 기존 dirty source를 manifest로 보존했다.
- [ ] 공통 7종의 message·field·transport·flow inventory가 정식 문서를 단일 기준으로 읽어 작성되었고,
      독립적으로 수정하는 중복 contract fixture가 없다.
- [ ] 모든 `contract 선행`·`차단` 항목이 해결되어 열린 상태가 0개다.
- [ ] `DS-IMP-001`~`DS-IMP-009`의 상태와 owner가 정해졌다.
- [ ] TicTacToe와 SupportChat one-way message semantics가 이름과 실행 path 모두 일치한다.
- [ ] GameQuest action·payload·domain event 계약이 공통 문서와 source에서 같은 의미를 갖는다.
- [ ] ShoppingMall 접수 응답과 DeliveryDispatch timestamp wire shape가 직접 검증되고, ShoppingMall의
      in-process `Command/Result` port DTO는 wire message로 노출되지 않는다.
- [ ] Bingo extra notify와 optional field의 public 범위가 review로 확정됐다.
- [ ] ZoneWorld가 공통 `UpdatePositionMsg` 경계, relocation/message follow와 browser flow를 충족한다.
- [ ] shell·PowerShell runner의 sample inventory와 ZoneWorld 지원 범위가 일치한다.
- [ ] `DS-REG-001`~`DS-REG-018` 중 적용 대상이 통과하고, 제외한 항목에는 근거가 있다.
- [ ] sample 7종의 실제 process 실행에서 client self-check, server evidence와 cleanup이 모두 확인됐다.
- [ ] Core·bindings bug는 원인 layer regression으로 재현·수정하고 version·package·native artifact와
      Framework 참조를 갱신했으며 sample이나 Framework에 우회가 없다.
- [ ] Source sample regression·process E2E, fresh `Systems.Zlink` package evidence와 Framework NuGet clean
      consumer 결과가 각각 통과했다.
- [ ] Package-only sample runner를 실행하지 않았다면 packaged Framework process E2E를 완료로 기록하지 않았다.
- [ ] 새 Sol Medium reviewer의 독립 final audit에서 기록되지 않은 sample spec·구현 gap과 unresolved
      `Critical`·`High`·`Medium` finding이 0개다.

## 11. 통합 ledger 완료 판정

이 문서 전체는 Phase A Spec gap과 Phase B common sample gap을 순서대로 닫아야 완료된다.

- [ ] Phase A의 implementation·E2E·regression·package gap이 모두 닫혔다.
- [ ] Phase A의 G7 독립 audit이 `CLEAN`이고 unresolved `Critical`·`High`·`Medium` finding이 0건이다.
- [ ] Phase B의 sample 7종 inventory, wire/runtime gap, runner와 process evidence가 모두 닫혔다.
- [ ] Phase B의 실제 process E2E와 clean package consumer 결과가 sample card별로 기록되었다.
- [ ] 두 phase에서 기록되지 않은 `.NET` Framework·common sample gap이 0개다.
- [ ] 마지막 독립 audit이 Phase A와 Phase B를 함께 확인했고, 통합 ledger가 `CLEAN`이다.
