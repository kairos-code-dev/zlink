# .NET Framework spec gap audit와 수정 ledger

> 상태: 2026-08-03 09:28 KST live snapshot 기준 Phase A (`.NET Framework` spec gap) 부분 완료다.
> 현재 runtime 변경은 `FullyQualifiedName~Runtime` unit `740/740`, bound-session·entry
> targeted `136/136`, staged-route targeted `10/10`으로 통과했다. 기존 전체 solution
> `1890/1890`, package contract·clean consumer gate와 native Core 11.1.0 evidence도 유지한다.
> Actor transfer process는 ST-C1/C2/C3, ST-D2, ST-F1/F2/F3/F6가 통과했지만 aggregate가
> ST-B2 fixture의 낡은 source-cleanup barrier 기대에서 중단됐다. reconnect lifetime contract,
> 남은 공통 E2E process evidence, 중앙 matrix와 독립 최종 audit도 남아 있다.
> Phase B (common sample gap)는 Phase A 완료 gate를 통과하기 전에는 시작하지 않는다.
>
>
> 작업 시작 기준: `9efee01aa39ace3db8e0f50c46ba9c12864f2cc2`와 2026-08-02 working tree.
> 마지막 .NET runtime scoped commit은 `095dbdb2dc8ac1b752d07d32992118b6fffb10a6`이며,
> 현재 branch `HEAD`와 `origin/agent/framework-contract-runtime-update`는
> `ee1dbccbb5ba72a9defc4158206b4fd23fa36c62`에서 일치한다. 기준점과 working-tree manifest는
> [`log/20260802-092859-ledger-review.ko.md`](log/20260802-092859-ledger-review.ko.md)에 기록하고, 기존
> Phase A 실행 결과는 [`log/20260802-120542-phase-a-verification.ko.md`](log/20260802-120542-phase-a-verification.ko.md),
> 최신 poller/HWM 수정과 Codex 검증 결과는
> [`log/20260802-184053-poller-receive-progress.ko.md`](log/20260802-184053-poller-receive-progress.ko.md)에 기록한다.
> 현재 E2E working tree와 이 문서의 새 상태 판정은
> [`log/20260803-071702-current-progress.ko.md`](log/20260803-071702-current-progress.ko.md)에 기록한다.
> 현재 runtime unit와 Actor transfer process 실행 결과는
> [`log/20260803-092806-runtime-unit-and-e2e.ko.md`](log/20260803-092806-runtime-unit-and-e2e.ko.md)에 기록한다.
>
> 범위: `.NET` server framework. HTTP client와 client용 Stream Connector는 공통 server 계약이
> 직접 요구하는 연결 지점만 포함한다.

## 현재 진행 snapshot (2026-08-03)

마지막 green package gate와 현재 runtime·E2E 변경을 분리해 판정한다. 현재 `.NET` production
runtime source와 관련 unit test에는 dirty 변경이 있고, 이 변경은 runtime filter와 targeted unit
test로 재검증했다. Package version/export에는 변경이 없으므로 기존 clean consumer evidence를
유지한다. E2E source와 runner 변경은 각 process log가 있는 범위만 완료 증거로 사용한다.

| 범위 | 현재 상태 | 다음 조건 |
|---|---|---|
| Runtime·contract·package | 현재 runtime filter `740/740`, bound-session·entry targeted `136/136`, staged-route targeted `10/10`이 통과했다. 기존 UnitTests `1431/1431`, solution `1890/1890`, ContractTests `76/76`, package verifier exit 0도 유지한다. | reconnect lifetime contract와 target OS 실행 증거를 별도로 닫는다. |
| E2E source·selector·runner | ST-C1/C2/C3, ST-D2, ST-F1/F2/F3/F6의 실제 process log가 있다. Aggregate는 ST-B2의 source-cleanup barrier fixture 기대에서 중단됐다. | ST-B2를 공통 Actor Join 계약에 맞춰 수정하고 remaining process evidence를 수집한다. |
| Config 14 | `InstanceSpot/run_e2e.sh`는 feature-map만 확인하고 exit 2로 종료한다. role server와 client는 없다. | process fixture, role server, client와 36개 scenario evidence를 추가한다. |
| Phase A 완료 | 미완료. 현재 변경을 사용한 process E2E와 독립 final audit이 없다. | A-G6, A-G7 조건을 모두 통과하기 전에는 Phase B를 시작하지 않는다. |

## 공통 실행 규칙 — 네 ledger 동시 진행

이 문서의 .NET 작업은 C++, Java/Kotlin, Node.js 작업과 동시에 진행한다. 현재 시스템 시각
`2026-08-03 07:17 KST (+09:00)` 기준 마감은 `2026-08-03 10:00 KST (+09:00)`이다. 마감 시점에
완료하지 못한 항목은 완료로 표시하지 않고, 현재 조건과 blocker를 기록한 뒤 다음 결정을 기다린다.

이 절에서 고정하는 것은 작업 간 경계, 하위 layer bug 처리, CPU·마감·log 위치처럼 지켜야 하는
조건이다. 구체적인 test 순서와 범위, review model·reasoning level, 도움 요청 시점, commit 단위와
push 시점은 진행 중 evidence와 dependency를 보고 workstream owner가 정한다. 처음 정한 방식이
맞지 않으면 작업을 멈추기보다 이유와 새 선택을 `log/`에 남기고 조정한다. 한 항목의 결정이
끝나지 않아도 독립적으로 진행할 수 있는 조사·재현·test 준비는 계속한다.

### 작업 경계와 CPU 제한

- 네 작업은 서로 독립된 workstream으로 진행한다. 이 .NET workstream은 이 ledger와 .NET Framework의
  source, test, E2E, package와 그에 대응하는 진행 기록만 수정한다.
- 다른 workstream의 source, test, E2E, 문서, package version, lockfile 또는 진행 기록을 수정하거나
  정리하지 않는다. 비교를 위한 read-only 확인은 가능하지만, 다른 작업의 내용을 이 candidate와
  commit에 섞지 않는다.
- Core 또는 bindings처럼 여러 workstream이 사용하는 공통 파일을 수정해야 하면 먼저 해당 하위
  layer의 owner와 변경 manifest를 정한다. owner가 하나의 candidate로 수정·검증하고, 다른
  workstream은 그 의존성만 자기 log에 기록한다. 같은 Core·bindings 변경을 각 작업이 중복 수정하지
  않는다.
- 시스템의 20 CPU를 네 작업이 나누어 사용하므로 작업 하나가 build, test, review agent와 보조
  process를 합쳐 점유하는 CPU는 최대 5개를 넘지 않는다. 병렬 실행 옵션도 작업당 `5` 이하로
  제한하고, 추가 worker를 생성해 이 제한을 우회하지 않는다.

### Core·bindings bug 처리와 local package 배포

Core 또는 bindings 수준의 bug를 발견하면 Framework나 sample에서 회피하지 않는다. 호출부의 raw
frame 해석, private/internal API 호출, reflection, test-only adapter, 상태 복제, 별도 retry 경로를
추가해 하위 layer의 실패를 숨기는 방식은 완료로 인정하지 않는다.

1. 최소 재현으로 원인을 소유한 layer를 확정한다. 원인과 재현 조건이 더 분명해지는 방식이라면
   test와 fix candidate를 함께 준비할 수 있지만, 최종 candidate에는 수정 전 동작을 잡는
   regression test가 남아 있어야 한다.
2. Core bug는 Core test에, bindings bug는 해당 bindings test에 regression test를 추가하고, 그
   test가 확인하는 책임을 해당 layer의 수정으로 해결한다.
3. test·fix·영향받는 gate의 실행 순서는 이슈의 재현 조건과 의존성에 맞춰 정한다. 순서를 바꾸면
   그 이유와 아직 닫히지 않은 조건을 `log/`에 남긴다.
4. 수정한 Core 또는 bindings를 local package로 배포할 때는 반드시 package version을 올린다.
   `scripts/local-package/README.ko.md`의 절차에 따라 새 runtime·archive를 만들고, 필요한 Core
   library 동기화와 stale cache 제거를 끝낸다.
5. Framework는 새 version의 local package를 실제로 resolve하는지 clean consumer, package contract와
   관련 process E2E로 확인한다. source tree나 이전 version cache를 사용한 결과는 새 package의
   증거로 인정하지 않는다.

원인 layer, regression test, fix, 올린 version, package 경로·hash, consumer 결과는 이 문서 본문에
진행 log로 나열하지 않고 아래 `log/` 규칙에 기록한다. 공통 Core·bindings candidate의 commit은
owner workstream에서만 만들며, 이 .NET workstream의 commit에는 다른 언어 작업의 변경을 포함하지
않는다.

### Review agent 선택

Review agent의 model과 reasoning level은 언어별로 고정하지 않고, review를 요청하는 시점에
[OpenAI 공식 model guidance](https://developers.openai.com/api/docs/guides/latest-model)를 확인해
review 위험도에 따라 결정한다. 이 문서 갱신 시점의 guide는 `gpt-5.6-sol`을 frontier capability,
`gpt-5.6-terra`를 intelligence와 cost의 균형, `gpt-5.6-luna`를 효율적인 high-volume 작업의
예시로 설명한다. 이 model ID는 영구 pin이 아니며, guide가 바뀌면 새 선택을 따른다.

- heading·link·manifest 같은 기계 검사는 guide가 정한 balanced 또는 efficient model을 출발점으로
  삼는다. 실제 범위가 달라지면 더 적절한 model과 level을 선택할 수 있다.
- public contract, ABI, runtime semantics, lifecycle, concurrency, package와 process E2E를 판단하는
  review는 guide의 frontier model을 우선 검토한다. `high`, `xhigh`, `max` 중 어느 수준이 필요한지는
  candidate의 위험도와 실제 evidence에 따라 진행 중 정한다.
- 전체 closure와 어려운 cross-layer race의 최종 audit은 충분한 capability의 독립 reviewer가
  수행해야 한다. 특정 model ID나 level을 형식적으로 채우는 것보다 review 범위와 결과의 충분성을
  우선하며, 선택을 조정한 근거를 `log/`에 남긴다.
- 실제 model ID, guide 확인 URL와 날짜, 선택 근거, reasoning level과 결과는 이 문서의 `log/`에
  기록한다. 필요한 reviewer를 바로 사용할 수 없으면 해당 review만 pending으로 두고 독립적으로
  진행할 수 있는 작업을 계속한다. review가 필요한 완료 판정은 reviewer 결과 전까지 완료로
  표시하지 않는다.

### 미해결 이슈의 독립 도움 요청

작업 중 같은 이슈가 재현된 뒤에도 원인이나 수정 방향이 닫히지 않으면, 최신 guide가 정한 높은
수준의 Codex model 또는 `Claude Fable`에 독립 도움을 요청해 다음 선택을 정한다. 어느 시점에
어떤 경로로 요청할지는 재현 가능성, 영향 범위와 진행 dependency를 보고 owner가 정한다. 요청에는
판단에 필요한 candidate manifest, 재현 명령과 결과, 현재 가설, 책임 경계와 남은 제약을 포함한다.
도움은 설계·진단 입력이며 해결 판정이 아니다. 실제 owner layer의 regression test, fix, 새 package와
관련 gate가 닫힌 뒤에만 이슈를 해결로 표시한다. 도움 요청과 응답, 선택한 조치, 미해결 조건은
이 문서의 `log/`에만 기록한다. 지원 model을 사용하더라도 workstream 경계와 작업당 5 CPU 제한을
유지한다.

본문에 남은 model ID, level, round 순서와 표는 과거 evidence 또는 시작 profile이다. 별도 계약으로
고정하지 않은 세부 선택은 진행 중 candidate의 위험도와 evidence에 맞춰 조정할 수 있다. 이 공통
규칙과 review 요청 시점의 공식 guide를 기준으로 선택하고, 변경 이유만 `log/`에 남긴다.

### Commit·push와 진행 기록

- 하나의 bounded card 또는 하위 layer 수정의 commit 경계는 책임 범위, rollback 가능성과 review
  흐름을 보고 owner가 정한다. 필요한 검증을 마친 단위는 path-limited commit으로 남기고 적절한
  branch로 push한 뒤 다음 card로 진행한다. 중간 commit이 필요하면 candidate로 표시하고 최종
  완료와 구분한다.
- 최종·고위험 변경은 가능하면 push 전에 독립 review를 거치지만, review와 무관한 조사·재현·준비
  작업까지 기다리게 하지는 않는다.
- `git add -A`나 unrelated 변경을 포함한 commit은 금지한다. commit과 push 전후의 SHA, branch,
  package version과 gate 결과는 `log/`에 기록한다.
- 진행 log는 이 ledger 본문에 절대 남기지 않는다. 명령, exit code, review finding, commit·push,
  package 배포와 blocker는 이 문서가 있는 디렉토리의 `log/` 안에만 기록하고, 본문에는 현재 판정과
  필요한 log 링크만 둔다.

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
8. Phase A의 모든 implementation·E2E·regression·package gap을 닫고, A-G7 독립 audit에서 unresolved
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

현재 working tree에는 여러 언어·문서·E2E 변경이 함께 존재한다. 이 ledger review는 대상 문서와 해당 폴더의
review log artifacts만 수정하고 나머지 변경을 보존한다. 전체 `git status --short`, 대상 content hash와 diff hash는
[review log](log/20260802-092859-ledger-review.ko.md)에 기록한다.

### 2.1 작업 log와 phase 전환

이 ledger의 진행 상황과 검증 결과는 이 문서가 있는 폴더의 `log/` 아래에 기록한다. 각 log는
`log/YYYYMMDD-HHMMSS-<card>.ko.md` 형식을 사용하고, 기록 시점의 card, working tree 조건,
실행한 명령, 통과·실패 수치, Sol review 결과와 남은 조건을 적는다. 진행 상황은 본문을 매번
늘어놓지 않고 해당 log 경로를 기준으로 확인한다. Formal public contract spec은 이 작업 log의
대상이 아니며, 구현 전 설계나 실행 증거는 plan ledger와 그 `log/`에서만 관리한다.

이 문서의 Phase A card와 A-G7 최종 audit가 모두 완료되면 같은 문서의 `## 10. Phase B — common sample
gap`에서 sample 작업을 이어서 진행한다. Phase B card를 Phase A 완료 판정 전에 섞지 않으며, 전환 시점과
시작 조건은 `log/`에 기록한다. 별도의 sample ledger 문서는 두지 않는다.

### 2.2 Model 배치와 card review gate

이 작업은 구현자와 Codex reviewer를 분리해 진행한다. 아래의 Luna Max와 card 순서는 현재 작업을
시작할 때의 profile이며, 구현 model과 card 순서는 candidate의 위험도, 의존성과 사용 가능한 환경을
보고 진행 중 조정할 수 있다. Review 범위를 생략하거나 구현자의 자체 점검으로 대체하지 않는다.

| 역할 | Model과 reasoning의 참고 profile | 책임 | Source 수정 |
|---|---|---|---|
| Main implementer | Luna Max | 한 번에 ledger card 하나를 조사하고 test를 먼저 고정한 뒤 구현·검증한다. | 허용 |
| Card reviewer | 아래 risk routing에 따라 Terra Medium, Sol Medium 또는 Sol High | 실제 spec, candidate 전체, diff와 test 결과를 읽고 지정한 관점에서 검토한다. | 금지 |
| Final auditor | Sol Max | A-G7 뒤 `.NET` production과 공개 계약 전체를 이전 review와 독립적으로 다시 검사한다. | 금지 |

| Review 범위 | 시작 model과 reasoning의 참고 profile | 적용 기준 |
|---|---|---|
| 기계적 검증 | Terra Medium | Link·heading·generated evidence·명확한 단일 test처럼 계약 해석이나 설계 판단이 없는 범위 |
| 일반 code·test review | Sol Medium | 계약과 완료 조건이 명확한 multi-file 구현, regression completeness와 leftover 검사 |
| 고위험 설계 review | Sol High | Public contract, wire shape, runtime semantics, lifecycle, ownership, concurrency, package boundary, POSD·DDD 판단 |
| 최종 독립 audit | Sol Max | Phase 전체의 spec·source·test·package·process evidence와 누락을 처음부터 다시 검사하는 A-G7·B-G7 |

Review 요청 전에 candidate의 변경 축과 위험을 기록하고 위 표를 시작 profile로 참고한다. 실제 model과
reasoning level은 현재 guide, candidate 위험도와 사용 가능한 reviewer를 보고 정하며, 필요하면 진행
중 조정한다. reviewer를 바로 사용할 수 없으면 review만 `대기`로 두고 독립적으로 가능한 작업을
계속한다. Main implementer와 reviewer는 같은 working tree를 동시에 수정하지 않으며, reviewer가
동작하는 동안 candidate를 바꾸지 않는다.

각 card는 다음 gate를 순서대로 통과한다.

1. Luna는 card가 따라야 하는 공통 spec, .NET exact interface, production owner와 기존 test를 먼저
   확인한다. Card 범위를 벗어난 gap은 우회해서 함께 고치지 않고 이 ledger에 별도 항목으로 등록한다.
2. Public contract, lifecycle, ownership, state transition 또는 module 경계를 바꾸는 card는 구현 전에
   사건, command, 상태 owner, failure 의미와 서로 다른 설계 대안 두 가지 이상을 적고 Sol High 이상의
   사전 review를 받는다. A-G1~A-G4에는 이 사전 review를 필수로 적용한다.
3. Luna는 실패를 재현하는 test를 추가하거나 기존 test가 계약을 직접 검증한다는 근거를 남긴 뒤
   production source를 수정한다. Targeted test와 해당 변경이 영향을 주는 regression을 실행한다.
4. Review 직전에 기준 commit, candidate commit 또는 working tree manifest, `git status --short`, 전체
   diff, 실행한 test 명령과 결과를 고정한다. Commit하지 않은 candidate라면 review가 끝날 때까지
   working tree를 변경하지 않는다.
5. 선정한 Codex reviewer는 Luna의 요약만 읽지 않고 정식 spec, exact interface, production call path,
   전체 candidate와 test evidence를 직접 대조한다. Reviewer는 finding만 반환하며 source·test·문서를
   수정하지 않는다.
6. `Critical`, `High`, `Medium` finding은 모두 blocking이다. Luna가 원인과 책임 경계를 고치고 관련
   test를 다시 실행한 뒤 같은 범위로 Sol review를 다시 요청한다. `Low` finding도 수용·기각·후속 분리
   가운데 하나와 근거를 기록한다.
7. Sol이 `CLEAN`으로 판정하고 필수 test가 모두 통과한 뒤에만 card를 완료로 표시하고 다음 card로
   이동한다. Reviewer 부재, 미실행 test, unresolved finding은 완료 증거가 아니다.

#### POSD·DDD review 기준

POSD·DDD finding을 판정하는 reviewer는 Sol High 이상을 사용하며 contract 일치 여부와 함께
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

### 2.3 Core·bindings 선행 수정과 package 배포 gate

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
4. 수정한 Core 또는 bindings candidate에도 2.2절의 Luna Max 구현과 risk-based read-only review gate를
   동일하게 적용한다. Public contract, ABI, lifecycle, ownership, native package 또는 POSD·DDD 경계를
   다루면 Sol High 이상을 사용한다. Reviewer는 해당 layer의 전체 diff, regression과 package 입력을 직접
   확인하고 책임 경계가 상위 layer로 누출되지 않았는지 판정한다.
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

## 3. 검증 결과와 기준점

검증 결과는 실행 당시의 commit과 working tree 조건을 함께 보존한다. 아래 A-G0 candidate 결과는
Phase A의 과거 evidence이며, 현재 `HEAD`의 완료 증거로 다시 사용하지 않는다. 최신 기준점과 아직
재실행하지 않은 검증은 [`log/20260802-092859-ledger-review.ko.md`](log/20260802-092859-ledger-review.ko.md)에서
구분한다.

| 검증 | 결과 | evidence 범위 |
|---|---|---|
| 기존 `dotnet test tests/Zlink.Framework.ContractTests/...` | 74/74 통과 | Roslyn full declaration test를 추가하기 전 snapshot·contract test 결과다. Source assembly와 고정 API snapshot만 확인하므로 exact-interface 일치의 단독 증거로 사용하지 않는다. |
| A-G0 candidate exact-interface declaration·owner test | 2/2 + 2/2 통과 | 14개 exact interface 문서의 고정 owner FQN과 source·compiled package contract를 양방향으로 대조한다. Positional record projection과 SemanticModel 기반 assembly/FQN binding을 포함한다. |
| A-G0 candidate 전체 `.NET` ContractTests | 76/76 통과 | Exact-interface declaration·owner test와 public snapshot, error·record·package contract를 포함한 전체 contract suite다. |
| A-G0 candidate 전체 `.NET` UnitTests | 1388/1388 통과 | `--logger 'console;verbosity=normal'`로 2분 51초 실행했다. Negative configuration test가 예상된 host-start error log를 남겼지만 testhost crash·timeout 없이 전체 suite가 종료되었다. |
| A-G0 candidate targeted relocation unit test | 248/248 통과 | `DrainCoordinatorTests`, `MaintenanceRuntimeTests`, `ActorHandoffTests`, `EntrySpotActorDispatchTests`, `LocationRuntimeQueryTests`를 실행했다. 이전 `DrainSpots` delegate compile blocker는 해소되었다. |
| A-G0 candidate documentation regression 단독 | 20/20 통과 | Exact interface 14개 문서의 owner matrix와 Config 1~14 feature-map inventory, 중복·unknown ID 검사가 통과했다. |
| A-G0 candidate sample regression | 134/134 통과 | Phase B baseline evidence다. Scenario canonical ID/name 검사와 common TicTacToe의 Entry Spot destroy ownership 문장을 포함하지만 Phase A 완료나 Phase B 전체 완료를 증명하지 않는다. |
| `verify-framework-doc-contracts.sh` | 중단 | Service wire 검사는 통과했다. C++ member override의 target signature 누락에서 중단되어 `.NET` 문서 전체 CLEAN 증거로 사용할 수 없다. |
| 2026-08-02 최신 `.NET` UnitTests build | error 0, warning 0 | `Zlink.Framework.UnitTests.csproj`를 `--no-restore --no-incremental --maxcpucount:1`로 build했다. 현재 runtime source의 compiler warning이 없다. |
| 2026-08-02 최신 `.NET` ContractTests | 76/76 통과 | exact-interface declaration·owner, source/package export와 contract snapshot을 포함한다. |
| 2026-08-02 Documentation/runner regression | 21/21 통과 | `RegressionTests` filter로 exact-interface owner, matrix reference, Config 1~14 inventory, aggregate entry와 fail-closed runner reference를 확인했다. |
| 2026-08-02 이전 relocation·HWM targeted UnitTests | 305/305 통과 | `DrainCoordinatorTests`, `ActorHandoffTests`, `StandaloneActorRelocationRuntimeTests`, `MaintenanceRuntimeTests`, `EntrySpotActorDispatchTests`, `InboundDispatchOptionsTests`, `TopologyExactSurface`를 실행한 이전 evidence다. |
| 2026-08-02 최신 Application HWM targeted UnitTests | 16/16 통과 | `InboundDispatchOptionsTests`를 `--no-build --no-restore`로 재실행해 profile ratio, explicit limit, managed heap/OS candidate, physical fallback, unlimited, validation을 확인했다. |
| 2026-08-02 최신 전체 `.NET` UnitTests | 1431/1431 통과 | active multipart batch-boundary regression을 포함해 `--no-build --no-restore --logger 'console;verbosity=minimal'`로 실행했고 skipped 0이다. 상세 결과는 [`poller/HWM regression evidence`](log/20260802-184053-poller-receive-progress.ko.md)에 둔다. |
| 2026-08-02 최신 전체 `.NET` solution test | 1890/1890 통과 | Stream Connector 142, Framework Unit 1431, Contract 76, HTTP 63, SampleRegression 134, Locations.Redis 40, ObservabilityOps 4를 `Zlink.Framework.sln --no-build --no-restore`로 실행했고 skipped 0, exit 0이다. |
| 2026-08-02 최신 `.NET` bindings tests | 142/142 통과 | public STREAM `RecvPart` surface와 native Core 11.1.0 runtime을 포함해 bindings test project를 실행했다. |
| 2026-08-02 `M5FoundationTests` executable smoke | exit 0 | xUnit test project가 아닌 raw ROUTER lifecycle·multipart ownership smoke executable을 `dotnet run --no-build --no-restore`로 실행했다. |
| 2026-08-02 최신 `verify_packaged_contract.sh` | exit 0 | 9개 NuGet package의 assembly manifest, dependency, clean consumer와 standalone HTTP package consumer를 통과했다. `Systems.Zlink` package는 11.1.2이고 package 내 native Core는 11.1.0이다. public API snapshot hash는 `399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a`다. |
| 2026-08-02 `LocationMessaging:RM-A2` process E2E | `LocationMessaging PASS`, `total PASS`, exit 0, 15초 | aggregate runner로 실행했다. log는 `framework/languages/dotnet/e2e/LocationMessaging/logs/20260802-120542-77932/`에 있다. |
| 2026-08-02 Config 12·14 aggregate guard | 두 runner 모두 exit 2 | Config 12는 `CH-E2E-03`, `CH-E2E-08`, `CH-REG-02`, `CH-REG-05`가 없고, Config 14는 process fixture·role server·client evidence가 없어 fail-closed 된다. |
| 2026-08-03 current E2E worktree check | 변경된 E2E project build는 warning 0/error 0, 관련 sample regression은 `3/3`, `1/1`, `1/1`, 변경 runner `bash -n`은 exit 0 | Config 2·4·7·8·11·12의 source·selector·runner 진행만 확인했다. 현재 변경을 사용한 actual process E2E는 실행하지 않았으며 상세 상태는 [`current progress log`](log/20260803-071702-current-progress.ko.md)에 둔다. |
| 2026-08-03 runtime·Actor transfer follow-up | Runtime `740/740`, bound-session·entry `136/136`, staged-route `10/10`; ST-C1/C2/C3, ST-D2, ST-F1/F2/F3/F6 process PASS. Aggregate는 ST-B2에서 중단 | Runtime implementation과 unit gate는 통과했다. ST-B2 fixture가 source cleanup을 completion barrier로 요구해 공통 spec과 충돌하며, 상세 evidence는 [`runtime unit and E2E log`](log/20260803-092806-runtime-unit-and-e2e.ko.md)에 둔다. |
| 2026-08-02 `git diff --check` | 통과 | 현재 변경의 whitespace 오류가 없다. |

최신 Phase A 실행 세부 사항은 [`log/20260802-120542-phase-a-verification.ko.md`](log/20260802-120542-phase-a-verification.ko.md)에,
최신 poller/HWM source 수정과 전체 UnitTests·package gate 세부 사항은
[`log/20260802-184053-poller-receive-progress.ko.md`](log/20260802-184053-poller-receive-progress.ko.md)에 보존한다.
과거 A-G0 candidate 결과는 최신 결과와 합산하지 않는다.

`ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature`는 이름과 달리 Markdown
spec을 읽지 않는다. `framework/languages/dotnet/contract/api/*.api.txt`와 reflection 결과만 비교한다.
따라서 이 test의 통과를 exact interface와 구현이 일치한다는 최종 증거로 사용하지 않는다.

### 3.1 Relocation 동작 방식 재검사 요약

다음 표는 public type이나 method의 존재 여부가 아니라 production call path의 실행 순서를 spec과 대조한
결과다. `부분 충족`은 정상 경로 일부가 같다는 뜻이며 완료 판정이 아니다.

| 범위 | Spec이 정한 경계 | Live production path | 판정 |
|---|---|---|---|
| Host preflight | Host state와 admission을 바꾸기 전에 accepted operation, actor handoff, Store와 target 조건을 확인한다. | `PreflightRetireAsync(...)`가 일반 operation과 actor handoff의 zero transition을 확인하고 admission fence·owner generation을 commit한 뒤 handoff를 시작한다. | source·targeted/full unit 통과. process evidence는 남아 있다. |
| Host workload handoff | Unit별 restore·commit 후 source dispatch에서 분리하고, commit 뒤 실패한 unit의 authority는 target에 둔다. | `ZLinkFrameworkDrainExecutor`와 workload coordinator가 shell·actor·aggregate 순서, committed count와 partial restore/force-stop 경계를 구분한다. | unit 통과. multi-process failure E2E는 남아 있다. |
| Host deadline | 하나의 absolute deadline을 preflight, unit callback, target reservation과 cleanup에 적용한다. | `ZLinkFrameworkDrainExecutor`가 deadline을 workload에 전달하고 Spot scheduler의 pre/post-commit cleanup이 deadline token을 사용한다. | source·targeted/full unit 통과. deadline process E2E는 남아 있다. |
| Host process failure | Process 종료 뒤 다른 runtime이 published relocation을 자동으로 이어받지 않는다. | `ZLinkFrameworkRuntime.StartAsync(...)`는 published relocation startup recovery를 실행하지 않는다. 같은 process 안의 explicit recovery와 기존 canonical runtime test는 별도 범위다. | source 반영. process 종료 경계 E2E는 남아 있다. |
| Host commit 뒤 failure | Commit한 unit은 target에 유지하고 미commit source workload와 host `Serving` 상태만 복원한다. | partial commit은 `RestorePartialRelocation` 경로를 사용하고, source terminalization을 확인할 수 없으면 fail-closed teardown으로 끝낸다. | targeted/full unit 통과. process evidence는 남아 있다. |
| Host terminal 관찰 | 느린 observer도 terminal status를 잃지 않고 relocation·shutdown structured log를 기록한다. | `ZLinkFrameworkMaintenanceRuntime`은 unbounded status channel과 transient blocked publication을 사용하고, relocation·termination identifier와 mode/outcome/reason을 기록한다. | targeted/full unit 통과. 느린 observer process evidence는 남아 있다. |
| Cross-node Join prepare·commit | User Spot은 admission callback을 실행하고 Entry Spot은 callback 없이 membership을 commit한다. | Entry activation은 concrete `OnActorJoin` reflection/callback을 사용하지 않으며, actor handoff는 target import·authority commit·replay 순서를 유지한다. | targeted/full unit 통과. cross-process evidence는 남아 있다. |
| Join deadline | `Defer()` 시점의 absolute deadline을 admission, resolve, prepare, restore와 authority commit에 적용한다. | Actor remote join이 deadline을 Core transaction·reconciliation으로 전달하고 Spot/host relocation도 같은 deadline을 사용한다. | targeted/full unit 통과. process deadline evidence는 남아 있다. |
| Join lifecycle | `OnJoinedActor` 뒤 source leave는 target completion을 막지 않는 one-way notification이어야 한다. | source leave는 detached cleanup으로 실행되고 target completion·replay가 source leave 결과를 기다리지 않는다. | source·targeted/full unit 통과. cross-node process evidence는 남아 있다. |
| Bound Session Join | Join completion과 target dispatch가 먼저 진행되고 Session 위치 갱신 ACK가 Actor 처리를 막지 않는다. | session route commit은 target callback/replay 뒤에 실행되며 completion path와 분리된다. | targeted/full unit 통과. delayed ACK process evidence는 남아 있다. |
| Join process failure | Process restart 뒤 completion callback과 actor handoff를 다른 runtime이 자동 replay하지 않는다. | startup published-relocation recovery는 호출되지 않으며 same-process retry만 유지된다. | source 반영. restart process E2E는 남아 있다. |

### 3.2 이번 추가 implementation-level 재검토

다음 항목은 이전 표의 정상 경로 요약만으로는 드러나지 않아 production call path를 다시 따라가며
확인한 차이다. 각 항목은 기능의 존재 여부가 아니라 실패 시점, owner 변경 경계, callback 대상과
public contract를 비교한 결과다.

| 범위 | 확인한 구현 차이 | 판정 |
|---|---|---|
| Application HWM | `ZLinkInboundDispatchBudget`과 dispatch lease를 각 ingress의 admission owner에 연결했다. | STREAM과 managed RouteMesh의 native direct ingress 및 mailbox claim이 dispatch 전에 lease를 확보한다. RouteMesh HWM unit regression은 통과했고 mixed-topology process E2E는 남아 있다. |
| STREAM listener | `ConfigureSocket()`이 exact interface·registration·backend wrapper에 연결되고 actual endpoint를 `AdvertiseHost`와 결합한다. | Contract 76/76, package gate와 targeted/full unit 통과. port 0/wildcard process evidence는 남아 있다. |
| Spot terminal result | Spot scheduler와 actor relocation 결과가 commit knowledge, typed reason, aggregate terminal outcome을 보존한다. | targeted/full unit 통과. Spot multi-process evidence는 남아 있다. |
| Maintenance deadline | Host absolute deadline이 Spot unit과 cleanup token까지 전달된다. | targeted/full unit 통과. deadline process evidence는 남아 있다. |
| Actor Join admission | target callback과 reservation 전에 caller deadline을 확인하고 expired admission을 cleanup한다. | `ExpiredAdmission_DoesNotInvokeCallbackOrCreateReservation` 등 targeted/full unit 통과. process evidence는 남아 있다. |
| Entry Spot Join | Entry target은 admission callback 없이 Accepted가 되고 concrete method reflection을 실행하지 않는다. | targeted/full unit 통과. cross-node process evidence는 남아 있다. |
| Preflight gate | accepted operation과 actor handoff를 함께 기다린 뒤 fence를 publish한다. | targeted/full unit 통과. independent audit은 남아 있다. |
| Actor failure mapping | Capture·restore·Store·deadline과 commit 뒤 failure를 typed reason/commit phase로 host result에 전달한다. | targeted/full unit 통과. process failure evidence는 남아 있다. |
| Metrics | Spot retire scheduler와 standalone/remote actor relocation에 object kind와 terminal outcome metric을 연결한다. | source 반영. object-kind metric assertion과 process evidence는 남아 있다. |
| Public completion shape | `ZLinkActorJoinCompletion.Failed`는 `OperationId`와 `Kind`만 public으로 노출한다. | Contract 76/76과 package gate 통과. |

## 4. implementation gap inventory와 카드 상태

이 절은 발견 당시의 gap과 현재 닫힌 card를 함께 추적한다. `확인`, `test gap`, `contract 선행`과
`차단`은 열린 상태이며, `수정 완료`와 `충족`은 source·test·package evidence로 닫힌 상태다.
닫힌 card는 [현재 충족 판정](#6-현재-충족-판정)에도 요약하고, Phase A gate의 전체 분모에 포함한다.

### DN-IMP-001 — Application HWM의 host 전체 ingress 미적용

**현재 판정: unit-level runtime 수정 완료, process E2E 미완료.**
`ZLinkInboundDispatchBudget`과 dispatch lease는 RouteMesh·Spot·Actor·STREAM ingress에 연결되어
있다. STREAM은 pre-receive에서 admission하고, managed RouteMesh는 native direct application
record를 `OwnedMailbox`에 넣기 전에 admission하며 local mailbox record도 `TryClaim`에서
admission한다. pump은 이미 획득한 lease를 typed dispatch record로 transfer하고 다시 계산하지
않는다. RouteMesh HWM targeted test 1/1, 전체 UnitTests 1431/1431와 solution 1890/1890은
통과했지만, mixed-topology process E2E가 남아 전체 Phase A 완료로 판정하지 않는다.

[Framework API §2](../../framework/common/spec/06-framework-api.ko.md#2-root-등록)와
[Runtime 상태](../../framework/common/spec/24-runtime-monitoring.ko.md#2-application이-한-번에-읽는-상태)는
Framework가 받은 application payload를 host 전체에서 합산하도록 요구한다. RouteMesh, ClientServer,
fanout, Spot, Actor와 STREAM이 같은 `ApplicationHwmBytes`를 공유해야 한다.

`ZLinkInboundDispatchBudget`은 `ZLinkSpotNodeInitializer`와 `ZLinkStreamRuntimeManager`까지 전달된다.
`ZLinkManagedMeshNode`와 `ZLinkMeshDispatchPump`은 node-level budget과 lease transfer를 사용하고,
`ZLinkStreamNodeRuntime`은 framed packet을 queue에 admission하기 전에 같은 budget을 확인한다.
`InboundDispatchOptionsTests`, RouteMesh/STREAM/HWM targeted 128/128과 전체 UnitTests
1431/1431는 통과했으며, mixed-topology process evidence와 control-plane progress를 별도로
확인해야 한다.

**수정 범위**

1. Host가 만든 `ZLinkInboundDispatchBudget` 하나를 RouteMesh/Spot/Actor와 STREAM ingress에도 전달한다.
2. Complete message를 받은 뒤 queue에 넣기 전에 payload bytes를 한 번만 admission하고, handler가 성공, 실패 또는
   cancellation으로 끝나는 공통 terminal 경로에서 한 번 뺀다.
3. HWM에 도달하면 새 application receive만 중단한다. Completion, liveness와 relocation control은 계속
   처리한다.
4. Runtime status의 세 byte 값이 모든 ingress의 합계를 나타내도록 고친다.

### DN-IMP-002 — STREAM listener의 advertised endpoint 미확정

**현재 판정: source·contract·package 수정 완료, endpoint process E2E 미완료.** `ConfigureSocket()`,
actual bound endpoint와 `AdvertiseHost` 연결은 ContractTests 76/76, package gate와 targeted/full unit
test를 통과했다. port `0`·wildcard·restart generation process evidence는 남아 있다.

[Network listener identity §4](../../framework/common/spec/10-network-listener-identity.ko.md#4-port를-확정하는-방법)는
port `0`으로 bind한 뒤 실제 port를 읽고 `AdvertiseHost`와 결합하도록 요구한다. .NET exact interface도
`IZLinkStreamNodeBuilder.SetAdvertiseHost(...)`를 공개한다.

`ZLinkStreamNodeRegistration`이 socket configuration과 `AdvertiseHost`를 보유하고,
`ZLinkStreamRuntimeManager.InitializeStreamNodesAsync(...)`는 bind 뒤 backend의
`GetLastEndpoint()`를 읽어 advertised endpoint를 만든다. wildcard bind에서 override가 없으면
configuration error로 닫힌다. ContractTests 76/76, package gate와 targeted/full unit test가 이 public
projection과 registration path를 통과한다.

**수정 범위**

Bindings의 public `IStreamSocket.Options.LastEndpoint`가 실제 bound endpoint를 제공한다. 따라서 bindings
변경이나 reflection은 필요하지 않다.

1. `IZLinkBackendStreamSocket`과 `ZLinkBackendStreamSocketWrapper`를 이 public property에 연결한다.
2. 실제 bound endpoint와 listener override를 결합한다.
3. Wildcard `BindHost`에서 `AdvertiseHost`가 없으면 socket bind 전에 startup configuration error를 낸다.
4. STREAM endpoint를 MeshNode, ClientServer 또는 fanout descriptor에 기록하지 않는다.

### DN-IMP-003 — STREAM ingress의 유한한 message 상한을 위한 public projection 부재

**현재 판정: exact interface·source·package 수정 완료.** `IZLinkStreamNodeBuilder.ConfigureSocket()`을
exact interface, registration validator와 backend wrapper에 연결했다. ContractTests 76/76과 package gate가
통과했다. Listener별 actual socket behavior는 process E2E에서 추가 확인해야 한다.

[Framework API §2](../../framework/common/spec/06-framework-api.ko.md#2-root-등록)는 Auto 또는 양수
Application HWM에서 모든 application listener의 `MaxMessageSize`가 유한한 양수여야 한다고 정한다.
`IZLinkStreamNodeBuilder.ConfigureSocket()`이 이 설정을 public registration에 연결하고 validator가
STREAM listener도 같은 유한 상한 규칙으로 검사한다. Backend wrapper는 socket options를 적용한다.
Exact interface, ContractTests 76/76, package gate와 targeted/full unit test가 이 경계를 고정한다.

### DN-IMP-004 — Process 종료 뒤 다른 runtime의 host relocation 인계

**현재 판정: startup automatic takeover path source 수정 완료, process 종료 경계 E2E 미완료.**
`StartAsync(...)`는 published relocation startup recovery를 실행하지 않으며, 같은 process의 explicit retry
경로는 유지한다. target process 종료 뒤 unavailable 의미를 실제 두 process에서 확인하는 test가 남아 있다.

[Host Relocate §8.8](../../framework/common/spec/28-graceful-drain-handoff.ko.md#88-중간에-실패하면-어느-위치를-유지하는가)는
source나 target process가 종료되면 다른 runtime이 relocation을 이어받지 않도록 정한다. Owner 변경 뒤
target process가 종료되면 object를 unavailable 상태로 유지하며, 11.1.0은 자동 복구를 제공하지 않는다.

`ZLinkFrameworkRuntime.StartAsync(...)`는 published relocation을 startup에서 복구하지 않는다. Relocation
Store payload는 실행 중 handoff와 explicit same-process reconciliation에서만 사용하며, 새 process가
이전 owner를 대신해 takeover하거나 completion callback을 replay하는 진입점으로 사용하지 않는다. 기존
canonical runtime test 중 in-process recovery fixture는 이 process 경계와 별도로 분류하고, process 종료
뒤 unavailable 결과는 E2E card에서 확인한다.

**수정 범위**

1. Startup에서 published relocation을 자동으로 이어받는 경로를 11.1.0 host relocation에서 제거한다.
2. Commit 뒤 target process가 종료된 object는 source rollback이나 다른 target takeover 없이 unavailable로
   유지한다.
3. 같은 target process가 실행 중인 동안의 retry와 process restart 뒤 recovery를 구분한다.
4. Relocation Store의 durable payload는 실행 중 handoff와 검증에만 사용하고 새 runtime의 실행 권한으로
   해석하지 않는다.

### DN-IMP-005 — Host relocation preflight cancellation reason 손실

**현재 판정: source 수정 완료, deadline failure process evidence 미완료.** Preflight cancellation과 target
탐색 실패를 구분하는 path와 typed result mapping이 반영되었고 targeted/full unit test가 통과했다.

`ZLinkFrameworkMaintenanceRuntime`은 preflight callback의 typed result와 cancellation을 분리해
`Blocked/DeadlineExceeded`를 보존한다. `ZLinkFrameworkRuntime.PreflightRetireAsync(...)`는 target 탐색
실패를 `TargetUnavailable`로, owner 변경 전 단계의 deadline을 `DeadlineExceeded`로 전달한다. 관련
mapping과 accepted-operation gate는 targeted/full unit test에서 통과한다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은 target
탐색 자체가 deadline까지 실패한 경우만 `TargetUnavailable`로 분류한다. Framework가 callback을
cancellation하거나 owner 변경 전 작업이 deadline을 넘으면 `DeadlineExceeded`여야 한다.

Preflight 결과는 blocker와 cancellation 원인을 구분해 전달한다. Target 탐색 deadline과 다른 preflight
단계의 deadline을 같은 catch에서 합치지 않는다.

### DN-IMP-006 — Actor Join relocation의 commit 뒤 실행 순서와 recovery 범위 차이

**현재 판정: source 수정 완료, cross-process Join evidence 미완료.** Source leave는 detached cleanup으로
분리되고 Session route commit은 target callback/replay 뒤에 실행되며 startup recovery는 automatic replay를
수행하지 않는다. 같은 process targeted/full unit test는 통과했다.

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)는 cross-node Join의
정상 경로를 target admission, source seal과 capture, target restore, Location Store commit,
`OnJoinedActor`, source `OnLeaveActor`, target Join completion, 기존 작업과 temporary 작업 replay 순서로
정한다. 현재 production path는 source leave를 detached notification으로 실행하고, target completion과
queue replay를 source leave 결과와 분리한다. Bound Session route commit은 target callback/replay 뒤에
시작하며 ACK가 Actor dispatch barrier가 되지 않는다. Startup path는 process restart 뒤 completion cursor나
published root를 다른 runtime이 자동 replay하지 않는다. 같은 process의 retry와 cross-process failure
semantics는 process E2E에서 계속 확인한다.

**수정 범위**

1. `OnJoinedActor` 뒤 source leave notification을 보내되 그 결과를 target completion barrier로 사용하지
   않는다.
2. 저장된 기존 작업과 temporary 작업을 실제 queue로 옮겨 application dispatch를 연다.
3. Bound Session 위치 갱신은 completion 뒤 별도 retry 작업으로 시작하고 ACK를 Actor dispatch barrier로
   사용하지 않는다.
4. Process restart 뒤 completion callback과 cross-node Join을 자동 복구하는 durable cursor와 startup
   recovery 경로를 제거한다. 같은 process 안에서의 idempotent retry는 유지한다.

### DN-IMP-007 — 첫 commit 뒤 host relocation failure와 runtime 종료

**현재 판정: partial commit source·unit path 수정 완료, multi-process failure evidence 미완료.** Commit한
unit은 target에 유지하고 미commit source workload를 복원하는 path를 사용하며, source terminalization을
확인할 수 없으면 fail-closed 처리한다. targeted/full unit test가 통과했다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은 첫
owner 변경 뒤 failure가 발생하면 commit한 owner를 유지하고 다른 target을 선택하지 않도록 정한다. 아직
옮기지 않은 source workload만 다시 처리한 뒤 host는 `Serving`으로 돌아가며 caller는
`Blocked/RelocationFailed`를 받는다. 이미 commit한 object는 target에서 사용할 수 없더라도 source로
rollback하지 않는다.

`ZLinkFrameworkDrainExecutor.ExecuteWithProgressAsync(...)`는 committed unit과 미commit unit의 결과를
분리한다. Partial commit은 `RestorePartialRelocation` 경로에서 미commit source workload와 host
`Serving` 상태를 복원하고, source terminalization을 확인할 수 없는 경우에만 bounded fail-closed teardown을
사용한다. `DrainCoordinatorTests.Partial_commit_restores_serving_without_force_stop`와 관련 targeted/full
unit test가 이 경계를 통과한다.

**수정 범위**

1. Relocation의 commit 뒤 unit failure를 shutdown force reason과 분리하여 `DrainBlocked(RelocationFailed)`로
   전달한다.
2. Commit하지 않은 source unit만 admission과 dispatch를 복원한다. Commit한 authority와 membership은
   변경하지 않는다.
3. Descriptor를 `Serving`으로 되돌리고 application admission을 다시 열되, committed object의 source
   route는 다시 열지 않는다.
4. Runtime infrastructure와 Location owner lease는 유지한다. Descriptor rollback 확인에 실패한 경우에만
   spec의 bounded teardown 조건을 적용한다.

### DN-IMP-008 — Actor Join target commit reconciliation의 call deadline 미적용

**현재 판정: absolute deadline source·unit path 수정 완료, process deadline evidence 미완료.** Caller deadline을
Core transaction과 target reconciliation까지 전달하고, expired admission은 callback·reservation 전에
거부한다. targeted/full unit test가 통과했다.

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)는 Join call의
deadline을 필요한 relocation 전체에 적용한다. Deadline까지 위치 변경을 commit하지 못하면
`Failed/DeadlineExceeded`이며, 다음 call이 Store의 current authority를 확인하여 중단된 attempt를
정리하거나 이어간다.

`SubmitRoutedJoinActorCoreAsync(...)`가 계산한 absolute deadline은 Core transaction, target commit
reconciliation과 authority recheck에 전달된다. 각 request는 남은 시간으로 제한되고, commit 전 만료는
prepared state cleanup과 source rollback으로 닫으며 commit 여부가 불명확하면 authority를 확인할 때까지
source admission을 유지한다. Targeted/full unit test가 deadline과 admission cleanup 경계를 통과한다.

**수정 범위**

1. Source가 받은 monotonic absolute deadline을 target commit reconciliation까지 전달한다.
2. Deadline이 끝나면 authority를 다시 읽어 owner commit 여부를 확정한다. Commit 전이면 prepared root와
   target staging을 정리하고 source queue를 복원한 뒤 `DeadlineExceeded`를 반환한다.
3. Commit 결과를 알 수 없으면 source rollback을 추측하지 않는다. Current authority를 확인할 때까지
   source admission을 닫아 둔다.
4. Commit 뒤 callback과 completion retry는 DN-IMP-006의 같은-process 규칙을 따르며 caller cancellation로
   owner를 되돌리지 않는다.

### DN-IMP-009 — Host relocation terminal status와 structured log 보장

**현재 판정: observer queue·structured log source 수정 완료, slow-observer process evidence 미완료.**
Terminal status 보존, transient blocked publication과 표준 relocation/termination identifier를 반영했고
targeted/full unit test가 통과했다.

[Runtime monitoring §3](../../framework/common/spec/24-runtime-monitoring.ko.md#3-현재-상태-조회와-변화-관찰)는
중간 status를 합칠 수 있어도 가장 최근 `Sequence`와 relocation·shutdown terminal status를 생략하지
않도록 정한다. [Structured log §5](../../framework/common/spec/24-runtime-monitoring.ko.md#5-structured-log)는
host relocation에 `zlink.runtime.host.relocation_changed`, shutdown에
`zlink.runtime.host.termination_changed` identifier를 사용하도록 정한다.

`ZLinkFrameworkMaintenanceRuntime.ObserveAsync(...)`는 terminal status를 보존하는 unbounded channel을
사용한다. 일반 `Blocked`는 current snapshot에 저장하지 않지만 observer에는 transient terminal result로
게시한다. `ZLinkFrameworkMaintenanceRuntime`은 relocation에
`zlink.runtime.host.relocation_changed`, shutdown에 `zlink.runtime.host.termination_changed`를 사용하고
mode, targetApplicationVersion, state, outcome과 reason을 structured field로 기록한다. Unit test는
observer publication과 logger failure가 lifecycle result를 바꾸지 않는 경계를 확인하며, slow-observer
multi-process evidence는 E2E card에서 계속 추적한다.

**수정 범위**

1. Observer별 buffer에서 intermediate status만 합치고 terminal status는 제거하지 않는 queue 정책을
   구현한다.
2. 일반 `Blocked` 결과는 current `Status.RelocationResult`에 보존하지 않더라도 observer에 complete status로
   한 번 게시한다.
3. Host state, relocation result와 termination result 변화에 공통 identifier와 필요한 structured field를
   기록한다.
4. 느린 observer, observer cancellation과 logger provider failure가 lifecycle 결과를 바꾸지 않도록 한다.

### DN-IMP-010 — Spot relocation terminal reason과 commit 경계 손실

**현재 판정: typed terminal result·commit boundary source 수정 완료, Spot process evidence 미완료.** Spot
scheduler, actor relocation과 workload coordinator가 commit knowledge와 typed reason을 전달하며 targeted/full
unit test가 통과했다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
첫 owner 변경 뒤 실패하면 target owner를 유지하고 `Blocked/RelocationFailed`를 반환하도록 한다. 아직
commit하지 않은 source workload만 복원해야 하며, target owner를 source로 되돌리면 안 된다.

`ZLinkSpotRetireScheduler`, `ZLinkActorDrainCoordinator`와
`ZLinkStandaloneActorRelocationRuntime`은 unit result에 commit knowledge와 typed failure reason을 보존한다.
`ZLinkRelocationWorkloadCoordinator`는 aggregate terminal reason과 committed count를 host result로 전달하고,
`ZLinkFrameworkDrainExecutor`는 commit 전 rollback과 commit 뒤 partial restore를 분리한다. 관련
`DrainCoordinatorTests`와 actor relocation tests가 이 경계를 targeted/full suite에서 통과한다. Spot
multi-process failure와 object metric evidence는 아직 남아 있다.

**수정 범위**

1. Spot·Actor unit 결과에 terminal reason, commit 전후 단계와 실제 committed count를 함께 전달한다.
2. Aggregate의 terminal reason을 workload coordinator와 host result까지 보존한다.
3. Owner commit 뒤 failure는 source rollback이나 source route 재개 없이 `Blocked/RelocationFailed`로
   끝낸다. 미commit unit만 source queue와 admission을 복원한다.
4. Commit 경계를 확인할 수 없으면 Location Store authority를 재확인한 뒤에만 rollback 여부를 결정한다.

### DN-IMP-011 — Host relocation absolute deadline과 Spot unit·cleanup callback 전달

**현재 판정: host absolute deadline source 수정 완료, process deadline evidence 미완료.** Host deadline을 Spot
unit, target reservation, callback과 cleanup token에 전달하며 targeted/full unit test가 통과했다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
callback 취소, owner 변경 전 작업과 cleanup을 포함한 operation 전체가 deadline 안에 완료되어야 한다고
정한다. Deadline을 넘긴 단계는 원인에 맞게 `DeadlineExceeded` 또는 commit 뒤
`RelocationFailed`로 끝나야 한다.

`ZLinkSpotNodeCatalog.TryRelocateForRetireAsync(...)`는 host가 계산한 absolute deadline을 받아 각 Spot
phase와 callback에 전달한다. `ZLinkSpotRetireScheduler`의 pre-commit rollback과 post-commit completion,
`ZLinkStandaloneActorRelocationRuntime`의 source cleanup은 deadline-linked token으로 bounded 된다. Commit
전 만료는 `DeadlineExceeded`, commit 뒤 실패는 `RelocationFailed`로 typed result에 남긴다. Targeted/full
unit test가 이 source 경계를 통과하며, process deadline evidence는 E2E card에서 계속 추적한다.

**수정 범위**

1. `RelocateAsync`의 monotonic 또는 absolute deadline을 every unit, target reservation, restore,
   lifecycle callback과 source cleanup에 전달한다.
2. Unit을 시작하기 전에 남은 시간을 검사하고, 각 retry와 delayed cleanup도 같은 deadline token을 사용한다.
3. Owner commit 여부를 확인한 뒤 deadline failure를 `DeadlineExceeded`와 `RelocationFailed`로 구분한다.

### DN-IMP-012 — Actor Join target reservation의 `TargetUnavailable` mapping

**현재 판정: mapping source 수정 완료.** Requested Spot lookup의 `NotFound`와 target reservation
`Unavailable`을 분리했으며 targeted/full unit test가 통과했다.

[Spot Actor §4](../../framework/common/spec/15-spot-actor.ko.md#4-actor-join과-commit-순서)은 요청한 User
Spot ID 자체가 없으면 `NotFound`, 이동할 Entry Spot이나 호환 target node가 없거나 owner/membership
fence를 사용할 수 없으면 `Unavailable`을 반환하도록 정한다.

`ZLinkFrameworkRuntimeActors.AdmitRoutedActorJoinAsync(...)`와
`ZLinkCanonicalRelocationReservationOwner`는 `ZLinkRelocationCapacityReserveResult.TargetUnavailable`를
`ZLinkFrameworkErrorKind.Unavailable`로 매핑한다. Requested Spot lookup만 `NotFound`를 사용한다. Targeted/full
unit test가 local·remote Join mapping을 확인한다.

**수정 범위**

1. Requested Spot lookup 실패와 reservation target unavailable을 서로 다른 mapping으로 유지한다.
2. Local·remote Join과 retry reply가 모두 `Unavailable`을 사용하도록 공통 mapper를 둔다.

### DN-IMP-013 — `ZLinkActorJoinCompletion.Failed`의 public shape와 exact interface 불일치

**판정: 충족. Source·package·contract test를 exact interface에 맞췄다.**

.NET exact interface
[`06-actors.ko.md`](../../framework/common/spec/server/languages/dotnet/interfaces/06-actors.ko.md)는
`Failed(OperationId, Kind)`만 정의한다. 현재 source
`framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/IZLinkActorContext.cs`도 같은 두
인자만 public으로 노출한다.

`ZLinkDeferredActorJoin`은 retry advice를 public completion field로 복사하지 않는다. 재시도 여부는
오류 종류와 업무 상태, idempotency를 확인하는 caller/application policy가 결정한다. Sample payload와
common server guide도 retry hint를 public completion 계약으로 노출하지 않는다.

`DotNetExactInterfaceDeclarations_Match_Source_And_Package_Exports`는 14개 exact-interface 문서의
public declaration을 source와 대조하고, constructor는 source declaration과 일대일로 비교한다.
Compiled package export는 API snapshot으로 별도 확인하며, type owner uniqueness도 독립 assertion으로
유지한다. 이전 판정과 candidate review 이력은
[`ledger review log`](log/20260802-092859-ledger-review.ko.md#dn-imp-013-history)에 둔다.

### DN-IMP-014 — Object relocation metrics의 종류와 terminal outcome

**현재 판정: source 수정 완료, direct metric assertion과 process evidence 미완료.** Spot retire scheduler와
standalone/remote Actor relocation에 object kind와 terminal outcome metric path를 연결했다.

[Runtime metrics §5](../../framework/common/spec/25-runtime-metrics.ko.md#5-host-relocation과-shutdown)은
`zlink.relocation.started`, `completed`, `duration`, `bytes`에
`object_kind=actor|user_spot|instance_spot`와 `outcome=completed|aborted|failed|shutdown`을 요구한다.

`CreateRelocation(...)`은 remote/standalone Actor path와 Spot retire scheduler에 연결되어 object kind를
`UserSpot` 또는 `InstanceSpot`으로 기록한다. Terminal mapping은 Completed, Pending/Aborted, Failed와
Shutdown을 구분하고, post-commit failure는 `RelocationFailed`로 남긴다. Direct metric assertion과 실제
Spot/Instance process evidence는 아직 DN-REG-030과 E2E cards에서 추적한다.

**수정 범위**

1. Actor maintenance, User Spot aggregate, PerActor shell과 Instance Spot 각각에서 동일한 metric
   lifecycle을 시작하고 terminal outcome을 한 번만 기록한다.
2. Commit 전 abort, commit 뒤 failure와 shutdown cancellation을 metric outcome으로 분리한다.
3. Host-wide operation metric과 object relocation metric을 섞지 않고, spec에 없는 ID label을 추가하지
   않는다.

### DN-IMP-015 — Actor Join admission의 caller absolute deadline 미적용

**현재 판정: source·unit path 수정 완료, cross-node process evidence 미완료.** Caller absolute deadline을
admission callback과 reservation에 전달하고 expiry 전에 callback/reservation을 차단한다. targeted/full unit
test가 통과했다.

[Spot Actor §3](../../framework/common/spec/15-spot-actor.ko.md#3-entry-spot과-user-spot의-actor-membership)은
`Defer()` 시점에 absolute deadline을 고정하고 handler tail, admission, restore와 completion에 같은
deadline을 적용하도록 한다.

`ZLinkDeferredActorJoin`이 고정한 caller absolute deadline은
`ZLinkActorRemoteJoiner.SubmitRoutedJoinActorTransactionAsync(...)`와 target admission에 전달된다.
`ZLinkActorHandoffAdmissions.AdmitReservedAsync(...)`는 callback과 capacity reservation 전에 만료 여부를
확인하고, callback이 deadline 뒤에 끝나면 cleanup owner를 Store 수렴까지 유지한다. 관련
`ExpiredAdmission_DoesNotInvokeCallbackOrCreateReservation`와 cleanup tests가 targeted/full suite에서
통과한다.

**수정 범위**

1. `Defer()`에서 고정한 absolute deadline을 admission packet, target callback, reservation과 commit retry에
   전달한다.
2. Target은 callback과 capacity reservation 전에 deadline을 확인하고, 만료된 reservation은
   `DeadlineExceeded`로 cleanup한다.
3. Caller deadline 만료 뒤 source admission을 다시 열기 전에 current authority와 reservation 상태를
   확인한다.

### DN-IMP-016 — Entry Spot Join의 admission callback 실행 경로

**현재 판정: Entry Spot source·unit path 수정 완료, cross-node process evidence 미완료.** Entry target은
admission callback 없이 Accepted가 되고 concrete `OnActorJoin` reflection path를 실행하지 않는다. targeted/full
unit test가 통과했다.

[Spot Actor §4.1](../../framework/common/spec/15-spot-actor.ko.md#41-entry-spot과-user-spot의-callback-비교)과
[Spot model](../../framework/common/spec/11-spot-model.ko.md#3-공통점과-차이점)은 Entry Spot에
`OnActorJoin`이 없으며, User Spot target에서만 admission callback을 실행한다고 정한다. Entry Spot으로
복귀할 때는 admission 없이 membership을 commit하고 target `OnJoinedActor`, source `OnLeaveActor`만 실행한다.
같은 규칙은 cross-node Join과 host maintenance에서 모두 적용된다.

`ZLinkActorEntrySpotJoinCoordinator.JoinLocalEntrySpotAsync(...)`는 Entry membership을 callback 없이
commit한다. `ZLinkEntrySpotActivation`은 concrete `OnActorJoin` method를 admission descriptor로
resolve하지 않으며, `ZLinkSpotActivationDispatcher`의 no-handler rejection은 User Spot path에만
적용된다. `EntrySpotActorDispatchTests`와 관련 targeted/full unit test가 concrete method가 실행되지
않고 Accepted가 되는 경계를 확인한다.

**수정 범위**

1. Entry Spot descriptor와 target admission에서 `OnActorJoin` 경로를 제거한다.
2. Local·cross-node Entry Join은 capacity·restore·authority commit을 수행하되 admission callback 없이
   Accepted를 만들고, commit 뒤 Entry `OnJoinedActor`와 source `OnLeaveActor`만 실행한다.
3. Entry Spot concrete type에 우연히 같은 이름의 method가 있어도 public contract로 노출하거나 실행하지
   않는다.

### DN-IMP-017 — Relocate preflight의 일반 accepted operation 대기

**현재 판정: source·targeted/full unit path 수정 완료.** 일반 accepted operation과 actor handoff를 함께
기다리는 preflight fence가 반영되었다. 기존 Sol Medium review는 `CLEAN`이지만 새 독립 최종 audit은 남아
있다.

[Host Relocate §4](../../framework/common/spec/28-graceful-drain-handoff.ko.md#4-target을-선택하기-전에-확인하는-조건)는
target을 고르기 전에 Create, Join, Instance placement, session binding, inbound relocation과
infrastructure operation을 확인하고 먼저 끝낼 작업을 확정하도록 한다. 이 검사가 끝난 뒤에만 host
state·descriptor와 admission 경계를 바꾼다.

`ZLinkFrameworkRuntime.PreflightRetireAsync(...)`는 Spot preflight 뒤 일반 accepted operation과 actor
handoff의 zero transition을 모두 확인한다. Admission fence와 owner generation을 publish하기 전 snapshot
count/epoch를 다시 대조하므로 accepted Create/Join·session/inbound operation이 남은 상태에서 relocation
publication을 시작하지 않는다.

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
  `6/6 PASS`이며 최신 relocation·HWM filter는 `305/305 PASS`다.
- Sol Medium 최종 read-only review는 `Critical 0`, `High 0`, `Medium 0`인 `CLEAN`을 판정했다.
  formal exact-interface 문서와 public contract diff는 없다. 다음 A-G1 card로 진행한다.

### DN-IMP-018 — Actor maintenance relocation의 failure mapping과 구현 단계 정보

**현재 판정: typed failure mapping source·unit path 수정 완료, process failure evidence 미완료.** Capture·
restore·Store·deadline과 commit 뒤 failure를 commit phase와 typed reason으로 전달하며 targeted/full unit test가
통과했다.

[Host Relocate §10](../../framework/common/spec/28-graceful-drain-handoff.ko.md#10-relocate-완료와-실패)은
commit 전 Store failure를 `StoreUnavailable`, adapter·factory·restore incompatibility를
`StateIncompatible`, deadline을 `DeadlineExceeded`, commit 뒤 target runtime failure를
`RelocationFailed`로 구분한다.

`ZLinkStandaloneActorRelocationRuntime.RelocateSourceAsync(...)`와
`ZLinkActorDrainCoordinator.MoveActorAsync(...)`는 commit phase와 typed terminal reason을 result에 남긴다.
Capture/restore incompatibility, pre-commit Store failure, deadline과 post-commit runtime failure는 각각
`StateIncompatible`, `StoreUnavailable`, `DeadlineExceeded`, `RelocationFailed`로 구분된다. Targeted/full
unit test가 pre/post-commit mapping과 source rollback 경계를 통과하며 process failure evidence는 남아 있다.

**수정 범위**

1. Actor relocation result에 commit phase와 typed terminal reason을 포함시킨다.
2. Capture·factory·restore·Store·deadline failure를 owner commit 전후에 따라 정확히 매핑한다.
3. Target rejection과 retryable availability를 terminal failure와 구분하고, source rollback은 commit 전
   결과에만 허용한다.

### DN-IMP-019 — Location runtime status의 exact interface와 source public shape 불일치

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

### DN-IMP-020 — `ZLinkPageRequest` 첫 페이지 기본값과 exact interface 불일치

**판정: 충족. Source constructor default를 `100`으로 맞추고, runtime policy는 legacy `0` 입력을 `100`으로
정규화하며 음수와 상한 초과를 거부한다.**

같은 `.NET Location` exact interface는 `ZLinkPageRequest(PageSize = 100, ContinuationToken = null)`을
정의한다. `ZLinkPageRequestPolicy`를 runtime owner로 두어 Store와 operational query의 첫 페이지 의미를
한 곳에서 정규화한다.

Contract test와 `Page_Request_Policy_Normalizes_Default_And_Rejects_Invalid_Sizes` unit test가 default·범위
계약을 직접 확인한다.

Formal exact-interface 파일은 수정하지 않았다. `0`은 runtime compatibility input으로만 처리하고 public
constructor의 계약 기본값은 `100`으로 고정한다.

### DN-IMP-021 — Framework error retry advice의 exact-interface export 부재

**판정: 충족. `ZLinkRetryAdvice`와 `RetryAdvice`를 internal runtime policy로 제한하고 public exception은
`ErrorKind`만 export한다.**

[.NET Framework 오류 공개 인터페이스](../../framework/common/spec/server/languages/dotnet/interfaces/10-monitoring-errors.ko.md)는
application이 오류 종류와 업무 상태를 확인해 다음 operation을 결정하며 public exception에서 retry hint를
받지 않는다고 명시한다. Source와 package snapshot에서 retry advice export를 제거했고, runtime 내부 호출은
같은 assembly 또는 friend assembly에서만 사용한다.

Formal exact-interface 파일을 수정하지 않고 source, package snapshot, caller test를 public error contract에
맞췄다.

### DN-IMP-022 — `ZLinkConfigurationException` public constructor의 exact-interface 부재

**판정: 충족. Constructor를 internal로 제한하고 `Zlink.Framework.AspNetCore`에만 friend access를 부여했다.**

현재 `Zlink.Framework.Contracts.Errors.ZLinkConfigurationException`은 Framework와 ASP.NET Core runtime이
configuration failure를 만들 때 사용하는 public type이다. Application이 직접 생성하지 못하도록 public
constructor를 추가하지 않았으며, bidirectional declaration gate와 package export가 이를 확인한다.

### DN-IMP-023 — `ActorRef` constructor parameter name 불일치

**판정: 충족. Source를 positional record projection으로 맞춰 constructor와 generated `Deconstruct`의 parameter
name을 `ActorId`, `ObjectGeneration`, `MeshName`, `NodeRid`로 고정했다.**

`ActorRef`는 named argument를 사용할 수 있는 public constructor이므로 parameter name도 public signature의
일부다. Positional property의 custom `init` accessor에 기존 validation을 유지했으며 constructor·property
accessor·generated `Deconstruct` parameter name을 reflection과 API snapshot으로 검증한다.

## 5. 확인된 regression test와 inventory gap

### DN-TEST-001 — Markdown exact interface와 export의 직접 비교 부재

**판정: 충족.**

기존 public surface test는 reflection 결과와 별도 API snapshot만 비교했다. Exact interface 문서가 바뀌고
snapshot을 갱신하지 않거나, snapshot과 문서가 서로 다르게 바뀌어도 한쪽 차이를 직접 검출할 수 없었다.

A-G0 candidate는 다음 두 검증으로 exact interface와 compiled export를 양방향으로 고정했다.

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

### DN-TEST-007 — positional record projection의 exact declaration 분모 미확장

**판정: 충족.**

Exact interface는 `record struct Type(Parameter...)`처럼 property와 `Deconstruct`를 compiler projection으로
표현할 수 있다. Gate는 positional record parameter에서 public constructor, `get; init;` property와 generated
`Deconstruct(out ... ParameterName)`을 materialize하고, source reflection과 package API snapshot의 같은
projection을 대조한다.

Source extra를 무조건 제외하지 않으며, projection으로 설명할 수 없는 public member는 계속 실패한다. `ActorRef`
source는 validation을 유지하는 custom `init` accessor를 사용하고 positional constructor parameter name은
formal interface와 동일하게 유지한다.

### DN-TEST-008 — member signature referenced type FQN canonicalization의 불완전

**판정: 충족.**

Exact owner type은 고정 expected assembly·namespace mapping으로 확인하고, member type은 각 source/document의
Roslyn `SemanticModel`에서 `ITypeSymbol`을 얻어 assembly name과 fully-qualified type identity로 정규화한다.
따라서 parameter·return·property type이 같은 이름의 다른 namespace로 이동하면 양쪽 identity가 달라져 실패한다.

각 문서와 source project를 별도 compilation으로 binding하고 unresolved 또는 ambiguous type은 test failure로
처리한다. Record projection test도 같은 semantic resolver를 사용하므로 단순 token 이름 fallback이 없다.

### DN-TEST-002 — documentation regression의 exact interface 디렉토리 분모 미사용

**판정: 충족.**

`RegressionTests.DotNetExactInterfaceDocuments_Have_An_Explicit_Regression_Owner`가
`server/languages/dotnet/interfaces/`의 각 문서를 regression matrix의 실행 가능한 owner와 대조한다.
`ContractSurfaceCoverage.DotNetExactInterfaceTypes_Have_A_Single_DocumentOwner`는 package exported type이
정확히 하나의 문서 owner를 갖는지 확인한다. 현재 documentation regression `20/20`과 owner test `2/2`가
이 분모를 통과한다.

### DN-TEST-003 — 공통 E2E scenario inventory와 feature-map 불일치

**판정: 충족.**

`RegressionTests.CommonE2EConfigsHaveCompleteDotNetFeatureMapInventories`가 Config 1~14의 공통 E2E
문서 ID와 `.NET` feature-map ID를 중복·누락·unknown까지 비교한다. 현재 documentation regression에서
이 검사가 통과하고, `SM-G5A`·`SM-G5B`와 분리된 Config 3·7·10·11·12 ID도 각 feature-map에 존재한다.
실제 process selector와 evidence가 없는 항목은 inventory gap이 아니라 해당 Config의 E2E implementation
gap으로 유지한다.

### DN-TEST-004 — 기존 feature-map 일부 상태와 live source 불일치

**판정: 충족. Feature-map 상태를 live source와 현재 process evidence 수준에 맞춰 보정했다.**

다음 항목은 feature-map 설명과 production source가 일치하지 않는 확인 사례다.

| 보정한 기록 | live source와 현재 상태 |
|---|---|
| SubmitAdmission `SA-E2E-10`은 `source 구현·process 미검증`으로 기록한다. | `IZLinkClientServerChannelRoleBuilder`와 ClientServer runtime이 존재하고 contract/unit test가 통과한다. 실제 three-process evidence는 아직 없다. |
| PubSub `PS-D1`은 `source 구현·process 미검증`으로 기록한다. | `ZLinkAutomaticFanoutSubscriberRuntime`과 `ZLinkFanoutDiscovery`가 descriptor별 connection을 구현한다. Automatic actual-process evidence는 아직 없다. |

Source type의 존재만으로 `구현`으로 바꾸지 않고, scenario의 모든 관찰 조건을 process test가 직접 확인할
때만 `구현`으로 판정한다. `SA-E2E-10`의 process evidence는 DN-E2E-IMP-015가, `PS-D1`의 automatic
fanout evidence는 DN-E2E-IMP-006이 소유한다.

### DN-TEST-005 — 중앙 regression matrix의 실행 가능한 test 부재

**현재 판정: test gap. 일부 exact references와 fail-closed runner guard를 추가했지만, 전체 matrix 행의
실행 가능한 reference 분모는 아직 닫히지 않았다.**

`framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md`에는 `unit`,
`integration-single-process`처럼 계층만 적힌 행이 많다. 현재 참조 검사는 backtick으로 적힌 이름을
확인하며, 이번 갱신에서 Config 1~14 runner/inventory guard와 Config 12·14 fail-closed script를
명시했다. 구체적인 test 이름이 없는 기존 행은 여전히 검증 대상에서 빠진다.

각 행은 다음 중 하나를 가져야 한다.

- 현재 test tree에 존재하는 정확한 test method
- 공통 E2E와 feature-map에 모두 존재하는 scenario ID
- 실행 경로가 고정된 verification script

### DN-TEST-006 — relocation fixture delegate 계약의 재발 방지 확인

**판정: 이전 test wiring blocker는 현재 working tree에서 해소되었고, 재발 방지 항목으로 유지한다.**

`ZLinkFrameworkDrainExecutor`의 `DrainSpots` delegate는 relocation과 host shutdown의 absolute deadline을
구분하기 위해 `relocate`, `hostShutdown`, `absoluteDeadline`, `cancellationToken`을 받는다. 현재 fixture는
이 네 입력을 명시적으로 받고, 현재 targeted test에서 이 계약과
Shutdown·Relocate 경로가 함께 실행된다.

2026-08-02 현재 확인 결과는 다음과 같다.

- 이전 CS1593 compile blocker는 재현되지 않았다.
- targeted relocation·HWM·Entry Spot test는 305개가 모두 통과했다.
- source delegate를 바꾸면 fixture와 shutdown/relocate close-reason assertion을 같은 변경에서 갱신하고,
  이 targeted command를 다시 실행한다.

### 5.1 .NET E2E spec와 실제 구현의 추가 gap

다음 항목은 공통 E2E 문서의 scenario ID와 실제 `.NET` E2E의 role server, client, selector,
feature-map, aggregate runner를 대조한 결과다. 이름이나 source type이 있다는 사실만으로 통과시키지
않고, 공통 E2E가 요구하는 process 경계, client-visible 결과, role server evidence, terminal assertion과
실패 의미까지 확인한다. 아래 항목은 구현·E2E test를 이번 작업에서 수정했다는 뜻이 아니라, 후속 수정
목록을 고정한 것이다.

#### DN-E2E-IMP-001 — Config 14 Instance Spot process E2E 부재

**현재 판정: Config 14 process E2E 미완료.**

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

#### DN-E2E-IMP-002 — aggregate runner의 Config 12·Config 14 실행 누락

**현재 판정: Config 12 aggregate source는 현재 working tree에서 확장되었고, Config 14는 fail-closed 상태다. 두 Config의 현재 process evidence는 미완료.**

`framework/languages/dotnet/e2e/run_e2e_all.sh`의 `CONFIGS`에는 `ChannelEgressRouting`과
`InstanceSpot`이 포함되어 있고 `RegressionTests.EveryCommonE2EConfigHasAnExplicitAggregateRunnerEntry`가
Config 1~14의 feature-map·runner·aggregate entry를 검사한다. 현재 dirty 변경에서는 Config 12 runner가
`CH-E2E-04A/B/C`, `CH-E2E-07A/B/C`와 기존 누락 ID를 aggregate 목록에 넣었지만 `all`을 다시 실행하지
않았다. Config 14는 여전히 process fixture·role server·client가 없어 `run_e2e.sh all`이 exit 2로
fail-closed 된다.

**수정 목록**

1. 현재 Config 12 변경을 실제 process에서 실행해 selector별 terminal·role evidence를 수집하고 feature-map을 갱신한다. Config 14의 role process fixture도 구현한다.
2. 실행하지 못한 구성은 성공으로 세지 않고, config·scenario ID·누락 이유를 결과에 남긴다.
3. scenario ID와 role endpoint까지 검사하는 중앙 manifest gate를 추가한다.

#### DN-E2E-IMP-003 — 공통 scenario ID와 .NET selector·feature-map 분할 불일치

**판정: 확인. Inventory는 충족하고 process selector/evidence가 남아 있다.**

현재 documentation regression `20/20`은 Config 1~14의 공통 문서와 feature-map에 대해 duplicate,
missing, unknown ID를 검사한다. `SM-G5A`, `SM-G5B`, `PS-D7A`~`PS-E2C`, `MON-A4A`~`MON-D1B`,
`ST-E1B`·`ST-E1C`, `OBS-C9A`·`OBS-C9B`, `CH-E2E-04A`~`CH-E2E-07C`는 현재 각 feature-map에
존재한다. 남은 문제는 ID inventory가 아니라 exact selector, role server와 actual-process evidence다.

| Config | 현재 남은 process gap |
|---|---|
| 2 SpotService | 독립 selector source는 현재 working tree에 연결되었고 `SM-G5A`·`SM-G5B` feature-map은 여전히 미구현이다. actual-process evidence가 없다. |
| 3 PubSub | automatic fanout, liveness와 observer matrix의 process evidence |
| 7 RuntimeMonitoring | `MON-A4A/B`·`MON-D1A/B` split selector source는 현재 working tree에 연결되었다. role server와 actual-process evidence가 없다. |
| 10 SpotActorTransfer | `ST-E1B`·`ST-E1C`·`ST-F3A`의 exact selector와 process evidence |
| 11 ObservabilityOps | `OBS-C9A`·`OBS-C9B` split selector와 readiness runner source는 연결되었다. topology별 process evidence가 없다. |
| 12 ChannelEgressRouting | split selector와 aggregate 목록 source는 현재 working tree에 연결되었다. 현재 변경 기준 `all` 실행과 role-server evidence가 없다. |

**수정 목록**

1. 공통 문서와 feature-map의 split ID inventory는 현재 regression으로 고정되어 있으므로 다시 누락으로
   표시하지 않는다.
2. `Client/Program.cs`, scenario dispatch와 `run_e2e.sh`가 같은 ID를 선택하도록 연결하고, aggregate
   alias를 쓰는 경우 어떤 분할 ID를 포함하는지 명시한다. 현재 working tree에서 Config 2·4·7·8·11·12의
   일부 연결은 확인했지만 feature-map과 process 결과는 아직 갱신하지 않았다.
3. ID가 source-only, partial 또는 diagnostic-only이면 `구현`으로 표시하지 않고 필요한 process evidence를
   추가한다.

#### DN-E2E-IMP-004 — Config 1 RM-A7 실행 경로 및 RM-C10 계약 owner 부재

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

#### DN-E2E-IMP-005 — Config 2 SpotService scenario와 현재 public 의미 불완전

**현재 판정: `SM-G5A`·`SM-G5B` selector source는 현재 working tree에 추가되었지만, feature-map과 actual process evidence는 미완료.**

`SpotService/feature-map.ko.md`에는 `SM-A9`, `SM-A10`, `SM-A12`, `SM-A13`, `SM-B0A`, `SM-B10`,
`SM-B11`, `SM-G5A`, `SM-G5B`가 현재 `미구현`으로 기록되어 있고, `SM-C6`은 result-free one-way
publish 계약에 맞춘 `부분 구현`으로 기록되어 있다. 현재 `SpotService/Client/Program.cs`와 runner에는
`sm-g5a`·`sm-g5b` 경로가 있지만, 이 변경을 사용한 role server 실행과 actual-process evidence는 없다.

**수정 목록**

1. feature-map에 이미 있는 미구현 operation을 실제 role server endpoint와 독립 selector로 연결하고, success·reject·timeout·
   cancellation의 client result와 server terminal evidence를 함께 남긴다.
2. SM-C6은 제거된 publish result API를 되살리지 말고, 현재 one-way publish의 backpressure와 최종
   delivery 관찰 조건으로 시나리오를 다시 연결한다.
3. feature-map에 이미 분리된 `SM-G5A`·`SM-G5B`의 상태를 source와 맞춘 뒤, 각 selector의 role server
   evidence와 placement assertion을 실제 process에서 수집한다.

#### DN-E2E-IMP-006 — Config 3 automatic fanout·liveness·observer matrix의 process 검증 누락

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

#### DN-E2E-IMP-007 — Config 4 RC-B6 typed DTO process roundtrip 부재

**현재 판정: `RC-B6` client/server source와 selector는 현재 working tree에 추가되었지만, feature-map과 actual process evidence는 미완료.**

`RegistrationCodec/feature-map.ko.md`의 `RC-B6`은 별도 process에서 default JSON으로 typed DTO를 왕복하되
message-specific codec registration을 사용하지 않는 동작을 요구한다. 현재 map은 이를 `미구현`으로
기록한다. 현재 working tree에는 `JsonGolden` typed message, role-server endpoint·handler와 client
selector가 있으며 관련 Client·Server build는 통과했지만 actual process log는 없다.

**수정 목록**

1. 현재 RC-B6 source를 별도 process에서 실행하고 DTO roundtrip, unknown/non-JSON rejection과 default
   serializer 사용을 evidence로 확인한다.
2. 메시지별 codec 등록 함수나 호출부 우회는 추가하지 않는다.

#### DN-E2E-IMP-008 — Config 5 ResilienceLifecycle 후반 matrix의 runner 제외

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

#### DN-E2E-IMP-009 — Config 6 StoreFailure fault matrix의 부분 실행

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

#### DN-E2E-IMP-010 — Config 7 monitoring split과 placement 관찰 조건 불일치

**현재 판정: split selector source는 현재 working tree에 연결되었지만, role server와 actual process evidence는 미완료.**

공통 Config 7의 `MON-A4A/B`, `MON-D1A/B`는 feature-map에 존재하고 현재 client·runner에도 각 split ID가
연결되어 있다. 다만 fresh role server/process evidence가 없다. `MON-A6`의 placement
snapshot·reservation/commit/release·capacity reject process evidence도 없다. `MON-B1/B2`는 publish
monitoring 부재와 local/zero target 일부만 확인하며 blocked+normal target, rollback/no-retry와 message-flow
trace 조건을 확인하지 않는다.

**수정 목록**

1. 현재 연결된 split ID를 실제 role server 실행 경로에서 실행하고 fresh evidence를 기록한다.
2. placement role server가 snapshot, reservation, commit/release와 capacity reject를 실제로 관찰하게
   한다.
3. B1/B2에 blocked·normal target, rollback/no-retry와 message-flow trace를 포함한 terminal assertion을
   추가한다.

#### DN-E2E-IMP-011 — Config 8 execution-turn 후반 selector와 process evidence

**현재 판정: 다섯 ID의 source·selector·runner 경로는 현재 working tree에 추가되었고 build/regression은 통과했지만, feature-map과 actual process evidence는 미완료.**

`AutomaticTurnDispatch/Client/Program.cs`와 `run_e2e.sh`는 현재 `TD-D4`, `TD-D5`, `TD-D6`, `TD-E2A`,
`TD-F5A`를 인식하고, client·Play·Session build와 관련 regression이 통과했다. 그러나
`feature-map.ko.md`는 다섯 ID를 아직 `미구현`으로 기록하고 있으며, 새 변경을 사용한 process 실행 log가 없다.

**수정 목록**

1. feature-map 상태를 source와 맞춘 뒤 각 ID를 독립 selector로 실제 실행하고 process evidence를 남긴다.
2. PerActor async의 같은 actor 내부 blocking 범위, unsupported Yield의 pre-submit reject, same-gate
   self-await 검증, handler failure 뒤 deferred Join barrier 정리와 host shutdown 중 대기 종료를
   process evidence로 확인한다.

#### DN-E2E-IMP-012 — Config 10 relocation E2E 기대 의미와 공통 spec 충돌

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

#### DN-E2E-IMP-013 — Config 11 ObservabilityOps C9 split과 process evidence 부족

**현재 판정: `OBS-C9A/B` split selector와 readiness source는 현재 working tree에 연결되었지만, topology별 actual process evidence는 미완료.**

공통 Config 11은 `OBS-C9A`(Automatic topology)와 `OBS-C9B`(Manual topology)로 분리되어 있고
feature-map에도 두 ID가 존재한다. 현재 `Client/Program.cs`와 runner는 두 ID를 직접 선택하며 readiness
대기를 추가했다. OBS-A5와 OBS-C1~C8, C10~C12도 source implemented·process unverified이며, C9는
source implemented·process 미검증이다.

**수정 목록**

1. 현재 C9A/C9B 연결을 실제 role process에서 실행하고 aggregate 결과와 분리된 evidence를 기록한다.
2. readiness gate, concurrent shutdown과 각 observability output을 실제 role process에서 수집하고
   source-only 상태를 `구현`으로 올리지 않는다.

#### DN-E2E-IMP-014 — Config 12 ChannelEgressRouting split selector와 partial assertion 부족

**현재 판정: split selector와 aggregate source는 현재 working tree에 연결되었지만, current process assertion은 미완료.**

공통 Config 12는 `CH-E2E-04A/B/C`와 `CH-E2E-07A/B/C`를 요구하고 feature-map에도 split ID가 존재한다.
현재 client selector는 split ID를 직접 사용하고 runner의 aggregate 목록에도 이를 포함한다. Accepted drain/shutdown/new RID restart, protocol
unsolicited injection, known-but-not-ready `Unavailable`, STREAM, drain, timeout/cancel/disconnect,
generation/late reply와 topology count assertion도 부분 상태다. 중앙 runner에는 Config 12 entry가 있지만,
현재 변경 이후 `ChannelEgressRouting/run_e2e.sh all`을 다시 실행하지 않았으므로 process 통과를 판정할 수 없다.

**수정 목록**

1. 현재 연결된 split ID를 실제로 실행하고 aggregate alias가 결과에서 숨기지 않도록 process evidence를
   기록한다.
2. 각 partial row에 role server endpoint, client-visible result, route/ACK/generation/terminal evidence를
   추가한다. protocol unsolicited injection은 raw frame 우회가 아니라 허용된 test transport 경계에서
   검증한다.
3. 모든 selector와 evidence가 준비된 뒤 Config 12 `all`의 exit 0을 실제 실행으로 확인한다.

#### DN-E2E-IMP-015 — Config 13 SubmitAdmission gate matrix의 부분 구현

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

#### DN-E2E-IMP-016 — 일부 .NET E2E client의 role server 계약 우회

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

#### DN-E2E-IMP-017 — `all` 완료 분모의 partial·diagnostic-only 은폐 가능성

**현재 판정: 일부 guard 수정 완료, 중앙 manifest와 전체 process evidence는 미완료.**

여러 feature-map이 `부분`, `미구현`, `source 구현·process 미검증` 또는 `diagnostic_only`를 기록한다.
`SpotActorTransfer/Client/Program.cs`는 다수 selector를 `excludedFromAll` 또는 diagnostic-only로 분류하고,
현재 `ChannelEgressRouting`은 split selector와 aggregate 목록을 확장했으며 `InstanceSpot`은 여전히
evidence가 없어 exit 2를 반환한다. 현재 변경 이후 aggregate process 결과는 아직 없다.
추가한 regression은 Config 1~14의 feature-map·runner·aggregate entry와 inventory를 검사하지만, 각
scenario의 selector·scenario 파일·role endpoint·bounded terminal evidence까지 일대일로 검사하지 않는다.

**수정 목록**

1. 공통 E2E inventory를 source로 삼아 ID·feature-map·selector·scenario file/endpoint를 일대일로
   검사하는 중앙 manifest regression을 추가한다.
2. `부분`, `미구현`, `diagnostic_only`는 완료 분모에서 제외하되 성공으로 표시하지 않고 열린 gap으로
   출력한다.
3. `all`은 실행한 ID, 제외한 ID, 제외 사유와 terminal 결과를 모두 출력하고, 누락 ID가 있으면 실패한다.

## 6. 현재 충족 판정

마지막 runtime gate와 2026-08-03 현재 working tree를 함께 기준으로 삼되, source·contract·package·unit
evidence와 process evidence를 분리한다. 현재 E2E 변경은 build와 좁은 regression까지 확인했으며
actual process evidence로 승격하지 않았다. `구현`은 source와 적합한 unit/contract gate가 통과했다는
뜻이고, `process 대기`는 실제 role process 경계에서 확인할 조건이 남았다는 뜻이다.

| 범위 | 현재 증거 |
|---|---|
| Framework public contract와 package export | ContractTests `76/76`, package gate exit 0, API snapshot hash `399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a`가 통과했다. |
| RouteMesh·Spot·Actor·STREAM Application HWM | 하나의 `ZLinkInboundDispatchBudget`을 모든 application ingress에 연결하고 RouteMesh native admission·mailbox claim lease를 dispatch 전에 확보하도록 했다. RouteMesh/STREAM/HWM targeted `128/128`, 전체 UnitTests `1431/1431`, solution `1890/1890`이 통과했다. Mixed-topology process E2E와 control-plane progress evidence는 남아 있다. |
| STREAM configuration과 endpoint | `ConfigureSocket()`, actual endpoint와 advertise override가 source·contract·package에서 일치한다. port `0`·wildcard process evidence는 남아 있다. |
| Host preflight와 relocation commit boundary | accepted operation gate, absolute deadline, typed terminal result와 partial restore path가 반영되었고 targeted/full unit test가 통과했다. multi-process failure evidence와 독립 audit은 남아 있다. |
| Actor Join lifecycle | source leave, Session route commit, caller deadline, Entry Spot admission과 startup recovery 경계를 source와 unit test에 반영했다. cross-node process evidence는 남아 있다. |
| Maintenance observer와 structured log | terminal-preserving status stream, transient blocked observation과 표준 host identifier를 반영했고 targeted/full unit test가 통과했다. slow observer process evidence는 남아 있다. |
| ClientServer public surface와 runtime | Builder, discovery, local·remote selection, liveness와 monitoring 구현 및 contract/unit test가 존재한다. |
| Automatic classic fanout | 전용 publisher descriptor, automatic subscriber connection과 discovery runtime이 존재한다. automatic process coverage는 E2E gap으로 유지한다. |
| Framework error surface와 non-JSON content type | `ZLinkFrameworkErrorKind` contract, internal retry policy와 decode 전 content type 거부 test가 통과한다. |
| DN-IMP-019~023 exact-interface shape | Location status, page request, retry advice, configuration exception constructor와 `ActorRef` parameter name을 source·package·contract test로 대조했고 `충족`이다. |
| 현재 E2E 진행 | Config 2·4·7·8·11·12의 source·selector·runner 변경, 관련 build와 좁은 regression은 확인했다. | feature-map 갱신과 current process log가 없으므로 `process 대기`다. Config 14는 fail-closed다. |

## 7. 작업 순서

A-G0~A-G6의 각 항목은 2.2절의 card review gate를 독립적으로 통과해야 한다. 한 단계 안에 여러 gap이 있어도
Luna가 동시에 구현하지 않고 card 하나씩 완료한다. A-G1~A-G4에서 lifecycle, ownership, public contract 또는
module 경계를 바꾸기 전에는 Sol High 이상의 사전 review를 먼저 통과한다. A-G7의 마지막 audit은 이전
card를 검토하지 않은 새 Sol Max reviewer가 전체 범위를 read-only로 검사한다.

### A-G0 — audit 기준과 회귀 분모

1. 이 ledger의 candidate commit과 working tree manifest를 저장한다.
2. DN-TEST-001과 DN-TEST-002 구현을 완료해 exact interface 전체를 audit 분모로 고정한다.
3. DN-TEST-004에 해당하는 feature-map 전체를 source와 test로 다시 판정하고 상태를 보정한다.
4. DN-IMP-013의 `Failed` exact interface shape를 contract review에서 확정한다.
5. 발견한 차이는 이 ledger에 추가한 뒤 다음 구현 단계로 이동한다.

완료 조건은 exact interface 파일, public export와 regression evidence 사이에 소유자가 없는 항목이 0개인
상태다.

#### A-G0 evidence 요약

A-G0의 기준 commit, candidate test 결과와 review 이력은
[`ledger review log`](log/20260802-092859-ledger-review.ko.md#a-g0-historical-evidence)에 기록한다.
A-G0 candidate 결과는 Phase A의 과거 evidence이며, 현재 `HEAD`에서 재실행하지 않은 결과를 완료 증거로
사용하지 않는다.

### A-G1 — Host relocation과 Actor Join relocation의 계약 경계

현재 source와 unit path는 이 gate의 주요 lifecycle·deadline·commit boundary 변경을 포함한다. 최신
targeted `305/305`와 전체 UnitTests `1408/1408`이 통과했지만, cross-process failure, delayed ACK,
process restart와 slow observer 조건은 process E2E로 닫히지 않았다. 아래 목록은 현재 남은 검증 조건이다.

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

#### A-G1 사전 검토 evidence

DN-IMP-017, DN-IMP-010, DN-IMP-011과 DN-IMP-007의 설계 대안·Sol Medium finding·재검토 결과는
[`ledger review log`](log/20260802-092859-ledger-review.ko.md#a-g1-historical-design-review)에 기록한다.
현재 A-G1의 남은 조건은 scheduler-level direct metric assertion, cross-process failure/deadline evidence와
새 독립 review다. 기존 review의 `CLEAN` 결과와 최신 unit test 통과를 A-G1 전체 완료로 해석하지 않는다.

### A-G2 — STREAM configuration 계약

`ConfigureSocket()` exact interface, registration validation, backend wrapper와 package snapshot은 현재
구현되어 있다. 남은 조건은 port `0`, wildcard `AdvertiseHost`와 restart generation의 process evidence다.

1. DN-IMP-003의 두 대안을 검토하고 exact interface를 먼저 수정한다.
2. Contract test와 public API snapshot을 목표 interface에 맞춘다.
3. Production builder, registration validation과 backend wrapper를 구현한다.
4. Bindings public API가 부족하면 bindings 작업으로 분리하고 Framework에서 우회하지 않는다.

### A-G3 — RouteMesh·Spot·Actor ingress와 host Application HWM 연결

공통 host budget과 dispatch lease를 production ingress에 연결하고 managed RouteMesh native direct
record와 local mailbox claim의 admission owner를 dispatch 전으로 정렬했다. RouteMesh HWM unit
regression, targeted `128/128`, 전체 UnitTests `1431/1431`와 solution `1890/1890`이 통과했다.
이 card의 process gate는 아직 열려 있으며, mixed topology에서 pause·resume, duplicate/loss와
control-plane progress를 확인해야 한다.

1. RouteMesh node direct와 ChannelName receive가 host budget을 획득하도록 한다.
2. Spot과 Actor queue에 저장한 payload와 실행 중 payload를 같은 budget으로 계산한다.
3. Relocation temporary queue와 replay는 원본 payload를 두 번 계산하지 않도록 ownership 전환을 한 곳에서
   처리한다.
4. Completion, liveness와 relocation control 경로가 application pause에 막히지 않는지 검증한다.

### A-G4 — STREAM ingress와 host Application HWM 연결

STREAM session ingress와 Actor relay가 공통 Application HWM과 payload lease를 사용하도록 source를 연결했고
pre-receive pause/resume regression과 전체 UnitTests가 통과했다. 남은 조건은 disconnect·shutdown·handler
failure를 포함한 process evidence다.

1. Session packet 수신과 Actor relay가 같은 host budget을 사용하도록 한다.
2. Session close, reply token과 transport control은 application pause와 분리한다.
3. Handler failure, cancellation, disconnect와 shutdown에서 payload bytes를 정확히 한 번 반환한다.

### A-G5 — STREAM actual endpoint

DN-IMP-002 source·contract·package path는 구현되었다.

1. Port `0`, listener override, wildcard host와 restart generation을 unit 및 process test로 검증한다.
2. STREAM endpoint가 다른 descriptor 종류에 기록되지 않는 negative test를 추가한다.

### A-G6 — 공통 E2E와 feature-map 정렬

Config 1~14 feature-map inventory와 aggregate runner entry guard는 source regression으로 고정했다. 현재
Config 12·14 `all`은 누락 evidence에서 exit 2로 닫히며, 여러 Config에 process 미검증·미구현 행이 남아 있다.

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

### A-G7 — 최종 회귀와 package 검증

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
- [x] DN-IMP-003의 exact interface를 먼저 확정하고 source와 package를 맞췄다. ContractTests `76/76`과 package gate가 통과했다. Listener process evidence는 남아 있다.
- [ ] DN-IMP-004~018을 수정하고 DN-REG-014~034를 통과했다.
- [x] DN-IMP-019~023의 exact-interface declaration·owner, source·package export와 관련 regression을 모두 닫았다.
- [x] DN-TEST-001~004와 DN-TEST-007~008을 모두 닫았다.
- [x] DN-TEST-006의 이전 compile blocker를 확인하고 현재 targeted test 305/305 통과를 기록했다.
- [ ] DN-E2E-IMP-001~017을 수정하고 DN-REG-035~040을 통과했다.
- [ ] Config 1~14의 공통 scenario와 `.NET` feature-map·selector·aggregate runner 차이가 0개다.
- [ ] Contract, unit, provider, connector와 HTTP client regression이 모두 통과했다.
- [ ] Sample regression은 Phase B에서 별도로 실행하며 Phase A 완료를 대신하지 않는다.
- [x] 실제 NuGet package export가 source와 exact interface에 일치한다. `verify_packaged_contract.sh`가 exit 0으로 통과했다.
- [ ] Process E2E 전체에서 `부분`이나 `미구현`으로 남은 항목은 이 ledger의 열린 gap과 일대일로 연결된다.
- [ ] 모든 구현 card가 2.2절의 risk-based Codex review, finding 수정과 재검토 gate를 통과했다.
- [ ] 모든 POSD·DDD finding에 원칙, 책임 경계, 대안, 처리 결과와 Sol 재검토 판정이 기록되었다.
- [ ] A-G1~A-G4의 비자명한 설계 변경은 구현 전에 두 가지 이상 대안과 Sol High 이상 사전 review를 기록했다.
- [ ] Core·bindings bug를 Framework에서 우회한 코드가 없고, 모든 선행 수정에 하위 layer regression과
      Sol review 결과가 있다.
- [ ] Core·bindings 선행 수정의 version, local package와 Framework 참조를 갱신하고 새 package를 사용한
      contract·regression·process 검증을 기록했다.
- [ ] 마지막 독립 audit에서 기록하지 않은 `.NET` gap이 0개다.
- [ ] 마지막 독립 audit을 새 Sol Max reviewer가 수행했고 unresolved `Critical`·`High`·`Medium` finding이 0개다.

## 10. Phase B — common sample gap (Phase A 완료 후 시작)

Phase B는 Phase A의 `## 9. Phase A Spec 완료 판정 checklist`와 A-G7 독립 audit의 모든 항목이 통과된 뒤에만 시작한다.
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

이 sample 작업의 model과 review gate는 2.2절의 risk routing을 참고한다. 아래의 Luna Max와 review
profile은 시작점이며, 실제 선택은 sample candidate의 위험도와 진행 조건에 맞춰 조정한다. reviewer는
candidate를 수정하지 않는 독립 review를 수행한다.

| 역할 | Model과 reasoning의 참고 profile | 책임 | Source 수정 |
|---|---|---|---|
| Main implementer | Luna Max | Ledger card를 한 번에 하나씩 조사하고 regression을 먼저 고정한 뒤 구현·검증한다. | 허용 |
| Card reviewer | 2.2절에 따라 Terra Medium, Sol Medium 또는 Sol High | 공통 sample 문서, Framework spec, 실제 diff와 test·process evidence를 지정한 관점에서 검토한다. | 금지 |
| Final auditor | Sol Max | B-G7에서 이전 card review와 독립적으로 sample 7종과 artifact evidence 전체를 다시 검사한다. | 금지 |

Review 요청 전에 candidate의 위험을 2.2절 표로 분류한다. 실제 model과 reasoning level은 current
guide, candidate 위험도와 사용 가능한 reviewer를 보고 정한다. reviewer가 unavailable이면 해당 card의
review만 대기하고 독립적으로 가능한 작업을 계속한다. Main implementer와 reviewer는 같은 candidate를
동시에 수정하지 않는다.
Review 직전에 기준 commit 또는 working tree manifest, `git status --short`, 전체 diff, 실행 명령과
결과를 고정한다.

각 card는 다음 gate를 통과해야 한다.

1. Luna는 공통 sample 문서, 관련 Framework spec·exact interface, production owner와 기존 test를 먼저
   확인한다. Card 밖에서 발견한 gap은 별도 card나 선행 조건으로 기록한다.
2. Public message, lifecycle, ownership, state transition, package boundary 또는 지원 host 범위를 바꾸는
   결정은 서로 다른 대안 두 가지 이상을 비교하고 Sol High 이상의 사전 review를 받는다.
3. Luna는 실패 regression이나 현재 계약을 직접 검증하는 기존 test 근거를 먼저 고정한 뒤 source를
   수정하고 targeted regression과 필요한 process E2E를 실행한다.
4. 선정한 Codex reviewer는 Luna의 요약만 사용하지 않고 정식 문서, public contract, 전체 candidate와
   evidence를 직접 대조한다. `Critical`, `High`, `Medium` finding은 모두 blocking이다.
5. Luna가 blocking finding의 원인과 책임 경계를 수정하고 test를 다시 실행한 뒤 같은 tier 이상 Codex
   reviewer의 재검토를 받는다. `Low`도 수용·기각·후속 분리 가운데 하나와 근거를 기록한다.
6. 선정한 reviewer가 `CLEAN`으로 판정하고 필수 test와 process gate가 모두 통과한 뒤에만 다음 card로
   이동한다.

##### Sample POSD·DDD review 기준

Sample POSD·DDD finding은 Sol High 이상 reviewer가
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
동작까지 보장하려면 B-G6에서 별도 package-only clean consumer runner를 추가한다. 이 runner를 추가하지
않으면 완료 조건을 Framework package의 export·dependency 검증과 source sample process 검증으로
분리하여 기록하며, packaged Framework process E2E를 통과했다고 표현하지 않는다.

Sample 작업에서 Core 또는 bindings bug가 확인되면 sample이나 Framework에서 우회하지 않는다. Core
bug는 Core regression에, bindings bug는 해당 bindings regression에 실패를 먼저 재현하고 원인을 소유한
layer에서 수정한다. Raw frame 해석, reflection, private API, 상태 복제, retry helper 또는 sample 전용
adapter로 하위 layer 실패를 숨기는 변경은 완료로 인정하지 않는다.

하위 layer를 수정한 card는 다음 조건을 모두 충족한다.

1. 원인 layer, 최소 재현, public contract 근거와 regression 경로를 기록한다.
2. Luna Max 구현과 Sol High 이상의 read-only POSD·DDD review를 같은 candidate에 적용한다.
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
| Framework implementation audit | Phase A `DN-IMP-001`~`DN-IMP-023`이 source, targeted test와 reviewer evidence로 닫힘 |
| Framework process E2E | Phase A `DN-E2E-IMP-001`~`DN-E2E-IMP-017`에 실제 process 결과와 failure semantics가 기록됨 |
| Framework regression | Phase A의 contract·unit·package·E2E regression card, 특히 `DN-REG-035`~`DN-REG-040`이 통과함 |
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

B-G0~B-G7의 각 card는 2.2절의 Luna Max 구현과 risk-based Codex review gate를 독립적으로 통과한다. Core
또는 bindings 문제가 확인되면 2.3절의 하위 layer regression·수정·package gate를 먼저 닫는다.

#### B-G0 — 선행 Framework ledger 완료 조건

기존 `.NET Framework` ledger의 implementation·E2E·regression·package gate를 다시 실행한다. Framework
NuGet은 `verify_packaged_contract.sh`의 clean consumer로 public export와 dependency를 확인하고,
`Systems.Zlink`는 bindings package version·native hash·consumer 결과를 별도로 확인한다. 현재 sample
runner가 Framework `ProjectReference`를 사용한다는 사실도 manifest에 기록한다. 이 단계에서는 sample
source를 수정하지 않는다.

#### B-G1 — 공통 sample 계약과 모호성

현재 working tree의 common sample 문서 diff를 별도 manifest로 보존한다. `DS-IMP-001`, `DS-IMP-002`,
`DS-IMP-005`, `DS-IMP-008`처럼 public message 범위나 지원 OS를 바꾸는 항목은 공통 문서·guide·다른
언어 구현을 함께 읽고 다음을 기록한다.

- 확정된 public message와 internal-only type
- transport kind와 response completion의 의미
- field type, optionality, enum/named string 값
- 지원 runner와 browser path
- 계약 변경이 필요한 경우 관련 spec/guide owner와 review 결과

계약이 확정되지 않은 항목은 구현 card로 이동하지 않는다.

#### B-G2 — exact contract inventory와 실패 regression

공통 sample 7종의 정식 문서를 inventory의 단일 계약 owner로 유지한다. 우선 문서의 structured message
declaration과 table을 직접 읽어 `.NET` shared contract와 비교하고, 추출 결과는 기준 문서 path·hash를
가진 generated evidence로만 저장한다. Generated evidence를 사람이 독립적으로 수정하는 두 번째 계약
fixture로 사용하지 않는다.

Markdown 구조를 안정적으로 읽을 수 없다면 별도 test fixture를 바로 복사해 만들지 않는다. 대안은
공통 sample 아래에 machine-readable contract fixture를 새 단일 기준으로 두고 문서와 언어별 test가
함께 사용하도록 공통 sample 정책을 먼저 변경하는 것이다. 기본 선택은 정식 문서를 직접 읽는 방식이며,
fixture 승격은 공통 정책 변경 review를 통과한 경우에만 사용한다. Inventory의 한 행은 sample, message,
direction, transport kind, response, field shape, nullable, owner, 기준 위치와 evidence를 포함한다. 이
단계의 test가 먼저 실패해야 B-G3 이후 source 수정의 범위를 알 수 있다.

#### B-G3 — wire contract와 public sample path

`DS-IMP-001`~`DS-IMP-005`를 계약 결정에 따라 수정한다. shared message와 client/server handler를 함께
변경하고, serializer가 만드는 실제 payload를 assertion한다. `DS-IMP-003`의 in-process
`Command/Result`는 변경 대상에서 제외하고 public `StartOrderRes`만 정렬한다. 내부 workflow 이름이나
domain event 이름은 public wire contract와 분리한다. 새 public API, raw frame, reflection 또는 호출부
codec은 추가하지 않는다.

#### B-G4 — runtime method path와 ownership

`DS-IMP-006`을 우선 수정하고 각 sample의 relocation, state commit, cleanup, retry·deadline 경계를
실제 call path로 다시 검사한다. ZoneWorld처럼 internal method 직접 호출이 공통 message boundary를
우회하는 경우 Framework public handler와 sample application 책임을 분리한다. B-G3의 wire 수정이
runtime semantics에 영향을 주면 같은 card에서 process E2E를 다시 실행한다.

#### B-G5 — runner·guide·package 설명

`DS-IMP-007`과 `DS-IMP-008`을 지원 OS 결정 뒤 수정한다. Shell·PowerShell 목록, per-sample runner,
ZoneWorld browser configuration, dedicated Redis와 cleanup을 같은 template 규칙으로 맞춘다. Framework
contract version은 정식 governance, `Systems.Zlink` version은 `Directory.Packages.props`, Framework
NuGet artifact는 package verification evidence를 각각 기준으로 사용한다. Generated 문서는 생성 source를
수정한다.

#### B-G6 — 회귀 test와 실제 process evidence

B-G2에서 정식 문서로부터 읽은 inventory를 regression으로 고정하고, sample별 source runner를 실제로
실행한다. 각 실행은 build → dedicated Redis/resource → server start → readiness → client self-check →
evidence 수집 → cleanup 순서를 지켜야 한다. 한 sample의 로그가 남아 있다는 사실만으로 다른 sample의
완료를 추론하지 않는다.

이어 `Systems.Zlink` package version·native hash가 source runner의 실제 restore 결과와 일치하는지
확인하고 Framework NuGet은 `framework/languages/dotnet/scripts/verify_packaged_contract.sh`로 clean
consumer를 검증한다. Framework NuGet을 사용하는 package-only sample runner를 추가한 경우에만 packaged
Framework process E2E를 별도 결과로 기록한다.

#### B-G7 — 최종 독립 audit

이전 card를 검토하지 않은 새 Sol Max reviewer가 선행 Framework ledger와 이 ledger를 다시 읽는다.
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
- [ ] 모든 card가 2.2절의 risk-based Codex review·재검토 gate를 통과했다.
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
- [ ] 새 Sol Max reviewer의 독립 final audit에서 기록되지 않은 sample spec·구현 gap과 unresolved
      `Critical`·`High`·`Medium` finding이 0개다.

## 11. 통합 ledger 완료 판정

이 문서 전체는 Phase A Spec gap과 Phase B common sample gap을 순서대로 닫아야 완료된다.

- [ ] Phase A의 implementation·E2E·regression·package gap이 모두 닫혔다.
- [ ] Phase A의 A-G7 독립 audit이 `CLEAN`이고 unresolved `Critical`·`High`·`Medium` finding이 0건이다.
- [ ] Phase B의 sample 7종 inventory, wire/runtime gap, runner와 process evidence가 모두 닫혔다.
- [ ] Phase B의 실제 process E2E와 clean package consumer 결과가 sample card별로 기록되었다.
- [ ] 두 phase에서 기록되지 않은 `.NET` Framework·common sample gap이 0개다.
- [ ] 마지막 독립 audit이 Phase A와 Phase B를 함께 확인했고, 통합 ledger가 `CLEAN`이다.
