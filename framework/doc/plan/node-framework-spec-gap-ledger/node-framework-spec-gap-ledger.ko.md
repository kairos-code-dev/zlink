# Node.js Framework spec와 sample gap 통합 ledger

작성일: 2026-08-02

상태: Framework spec과 sample의 조사 결과 및 후속 작업을 순서대로 기록한 audit ledger. 이 문서는
구현 완료 판정이 아니다.

## 공통 실행 규칙 — 네 ledger 동시 진행

이 문서의 Node.js 작업은 C++, .NET, Java/Kotlin 작업과 동시에 진행한다. 현재 시스템 시각
`2026-08-02 09:56 KST (+09:00)` 기준 마감은 `2026-08-03 10:00 KST (+09:00)`이다. 마감 시점에
완료하지 못한 항목은 완료로 표시하지 않고, 현재 조건과 blocker를 기록한 뒤 다음 결정을 기다린다.

이 절에서 고정하는 것은 작업 간 경계, 하위 layer bug 처리, CPU·마감·log 위치처럼 지켜야 하는
조건이다. 구체적인 test 순서와 범위, review model·reasoning level, 도움 요청 시점, commit 단위와
push 시점은 진행 중 evidence와 dependency를 보고 workstream owner가 정한다. 처음 정한 방식이
맞지 않으면 작업을 멈추기보다 이유와 새 선택을 `log/`에 남기고 조정한다. 한 항목의 결정이
끝나지 않아도 독립적으로 진행할 수 있는 조사·재현·test 준비는 계속한다.

### 작업 경계와 CPU 제한

- 네 작업은 서로 독립된 workstream으로 진행한다. 이 Node.js workstream은 이 ledger와 Node.js
  Framework의 source, test, E2E, package와 그에 대응하는 진행 기록만 수정한다.
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
owner workstream에서만 만들며, 이 Node.js workstream의 commit에는 다른 언어 작업의 변경을 포함하지
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

이 ledger는 먼저 공통 Framework spec, Node exact interface, 공통 E2E spec과 현재 Node.js Framework의
public contract·production call path·process E2E 동작을 대조한다. Framework spec 작업의 완료 gate를
통과한 뒤에는 공통 sample 7종과 Node sample 구현을 같은 문서에서 대조하고 수정한다. 기능 이름이나
source 파일의 존재만으로 충족을 판정하지 않고, 실제 호출 순서와 role server evidence까지 확인하는
것을 목표로 한다.

Framework spec 단계의 완료 조건은 다음과 같다.

- 공통 spec과 Node exact interface의 public contract가 source export, package
  export와 일치한다.
- timeout, cancellation, callback, ownership, disposal, serialization, error
  mapping이 실제 production call path에서 목표 계약대로 동작한다.
- Config 1부터 Config 14까지의 모든 공통 scenario ID가 Node feature-map,
  selector, dispatch와 연결된다.
- aggregate all runner가 부분 구현, 전환 대상, diagnostic-only, source-only,
  N/A 항목을 성공으로 세지 않는다.
- E2E client는 공개 client API만 사용하고, Framework 호출과 내부 상태 확인은
  role server의 공개 endpoint 안에서 수행한다.
- client-visible result와 role server evidence를 같은 실행에서 확인하고,
  terminal reason, callback count, owner, generation, cleanup 같은 계약 결과를
  직접 assertion한다.
- package consumer, Node contract/API gate, unit/integration test, CI gate와
  관련 process E2E가 현재 working tree에서 성공한다.
- 각 implementation·E2E·test card가 구현 전 설계 review와 구현 후 독립 review를
  통과한다. 이 review는 POSD와 DDD 기준을 함께 적용하며, review 범위와 위험도에 맞는 Codex model과
  reasoning level을 사용한다.

이 문서에서 사용하는 판정 표기는 다음과 같다.

- ND-IMP-###: production implementation gap
- ND-E2E-IMP-###: Node E2E 구현·runner·process evidence gap
- ND-TEST-###: audit 또는 회귀 test 자체의 gap
- ND-REG-###: 새로 추가하거나 변경해야 할 회귀 test
- NS-IMP-###: Framework spec 완료 뒤 처리할 Node sample implementation gap
- NS-TEST-###: Sample audit 또는 process evidence test gap
- NS-REG-###: Sample에 새로 추가하거나 변경해야 할 회귀 test
- contract 선행: public contract 또는 exact interface를 먼저 확정해야 하는 항목
- 충족: 현재 source와 실행 결과가 해당 범위의 계약을 직접 증명한 항목

이 ledger가 소유하는 implementation gap은 공통 spec을 임의로 되살리지 않고 이
경로에서 관리한다. Node의 `addRouteMesh(meshName)`와 MeshNode role 경계도
ND-IMP-001의 public contract audit 범위에 포함한다. production source, test,
E2E runner, 공통 spec, feature-map의 변경은 해당 card의 근거와 review 결과를
ledger에 남긴 뒤에만 반영한다. 기존 working tree의 변경은 조사 대상으로만
취급하지 않고, 작업 대상이면 candidate manifest에 명시한다.

## 1.1 POSD·DDD 독립 review 운영

이 ledger의 review는 기능이 동작하는지만 확인하는 절차가 아니다. DDD로 상태와 책임의 의미를 먼저
정리한 뒤, [`software-design-principles.md`](../../../../doc/principal/software-design-principles.md)의
POSD 기준으로 그 구조가 호출자에게 전달하는 복잡성과 변경 증폭을 줄이는지 확인한다. 구현자 자신의
설명이나 좁은 test pass를 독립 review로 대체하지 않는다.

### Reviewer와 candidate 고정

| 역할 | 지정 | 책임 | 수정 권한 |
|---|---|---|---|
| Gap owner | 현재 작업자 | spec·source·test를 조사하고 design packet, regression과 구현 candidate를 준비한다. | 구현 phase에서 허용 |
| 독립 reviewer | Contract·architecture review에 적합한 Codex coding/review model | 전체 candidate와 근거를 read-only로 읽고 contract·POSD·DDD·test/evidence finding을 기록한다. | 금지 |
| Final reviewer | 독립 reviewer와 같거나 더 높은 수준의 Codex coding/review model | 모든 card와 전체 diff, fresh gate, process evidence를 다시 대조하고 최종 판정을 내린다. | 금지 |

Reviewer는 구현자의 요약만 읽지 않는다. 다음 입력을 같은 candidate 기준으로 직접 읽는다.

- 공통 Framework spec, Node exact interface, 공통 E2E와 public contract governance
- 해당 card의 production call path, package export, test·CI script와 전체 candidate diff
- 기준 commit 또는 immutable candidate SHA, `git status --short`, 변경 파일 manifest
- 실행한 명령, exit code, test result, process log, client result와 role server evidence
- 이전 round의 unresolved finding과 그 finding을 반영한 재검토 결과

Codex model과 reasoning level은 repository 크기가 아니라 해당 review의 판단 위험도에 따라 선택한다.

| Review 범위 | Codex model 기준 | 최소 reasoning level | 선택 기준 |
|---|---|---|---|
| Format, 링크, 표와 checklist의 기계 검사 | Balanced Codex coding model. 현재 예시는 `gpt-5.6-terra`다. | `medium` | 계약과 runtime 의미를 판정하지 않는 보조 검사다. |
| `R0 design` | Frontier Codex coding/review model. 현재 예시는 `gpt-5.6-sol`이다. | `high` | Public contract, 책임 경계, POSD·DDD 대안을 판단한다. |
| `R1 candidate` | Frontier Codex coding/review model | `high` | 전체 diff와 production call path에서 contract·runtime regression을 찾는다. |
| `R2 integration` | Frontier Codex coding/review model | `high` | Package·CI·process E2E evidence와 실패 의미를 함께 판정한다. |
| `R3 final` | Frontier Codex coding/review model | `xhigh` | 모든 card, 변경 manifest와 unresolved finding을 다시 대조해 완료를 판정한다. |

계약 근거가 충돌하거나 concurrency·lifecycle race처럼 재현과 책임 경계 판단이 어려우면 해당 round를
`max`로 올린다. 기계 검사에 사용한 balanced model은 R0~R3의 frontier model review를 대체하지 않는다.
표의 model 이름이 바뀌면 같은 역할을 제공하는 현재 Codex model을 선택하되, 낮은 역량의 model로
대체하지 않는다. 선택한 model ID, model 선택 근거, reasoning level과 level 선택 근거를 review log에
기록한다.

해당 범위에 적절한 Codex model을 바로 사용할 수 없으면 `Claude Fable` 또는 현재 guide가 정한
동등한 reviewer를 검토하고, reviewer가 모두 unavailable이면 해당 round만 `pending`으로 기록한다.
그동안 독립적으로 가능한 조사·test·manifest 준비는 계속한다. Review 중에는 구현자와 reviewer가
같은 working tree를 변경하지 않는다. 가능하면 candidate SHA를 별도 read-only worktree에 고정하고,
그렇지 않으면 review 시작 시점의 manifest와 diff를 결과에 포함한다.

### 진행 log 저장 규칙

이 작업의 진행 기록은 이 문서가 있는 디렉토리의 `log/` 아래에 저장한다.
review round, 구현 변경, 실행 명령과 exit code, Codex reviewer finding, 미해결 조건은
해당 log 파일에 기록하고, ledger에는 현재 판정과 log 링크만 남긴다. log 디렉토리가
없으면 먼저 만들며, repository의 다른 공통 log나 과거 snapshot을 현재 작업의 증거로
대체하지 않는다. 파일명은 `YYYY-MM-DD-주제.ko.md` 형식을 사용한다.

현재 진행 log: [`log/2026-08-02-posd-ddd-review.ko.md`](log/2026-08-02-posd-ddd-review.ko.md)

현재 runtime·unit phase log: [`log/2026-08-02-runtime-unit-completion.ko.md`](log/2026-08-02-runtime-unit-completion.ko.md)

### Review round와 통과 조건

각 card는 다음 round를 순서대로 거친다.

| Round | 시점 | 필수 산출물 | 통과 조건 |
|---|---|---|---|
| `R0 design` | source·test를 수정하기 전 | event storming, 책임·invariant 표, 두 가지 이상 설계 대안, POSD 위험 신호 목록 | 지정한 Codex reviewer의 최종 decision이 `DESIGN ACCEPTED` |
| `R1 candidate` | regression을 먼저 고정하고 구현 candidate를 만든 뒤 | 전체 diff, targeted test, public surface와 production call path 증거 | 지정한 Codex reviewer의 최종 decision이 `CLEAN` |
| `R2 integration` | package·CI·process E2E를 실행한 뒤 | fresh package, full gate, client result와 role server evidence | 좁은 test pass가 아닌 실제 contract·lifecycle·failure evidence까지 `CLEAN` |
| `R3 final` | 모든 card가 끝난 뒤 | 전체 ledger, 변경 manifest, fresh command 결과와 unresolved 목록 | 미해결 Critical/High/Medium finding이 0이고 최종 판정이 `CLEAN` |

`CLEAN`은 “빌드가 된다”는 뜻이 아니다. contract·POSD·DDD·test/evidence 범위에서 Critical, High,
Medium finding이 없고, Low finding은 수용·기각·후속 card 가운데 하나로 명시되어야 한다. reviewer가
찾지 못했다는 사실과 review를 실행하지 않았다는 사실을 구분한다.

### POSD review rubric

Codex reviewer는 각 candidate에서 다음 위험 신호를 먼저 열거하고, 해당 위험 신호가 왜 문제인지 근거를
기록한다.

1. **Deep module** — public interface보다 implementation이 제공하는 책임이 충분히 깊은가. API와
   runtime이 같은 일을 반복하는 shallow module은 없는가.
2. **Information hiding** — lifecycle, ownership, timeout, error mapping, routing, codec과 storage
   결정이 여러 module에 새어 나가지 않는가. public API가 내부 representation을 노출하지 않는가.
3. **Pass-through와 temporal decomposition** — 같은 인자를 전달만 하는 method·helper·adapter가
   생겼는가. 실행 순서대로 class를 나누어 하나의 invariant을 여러 곳이 나누어 소유하지 않는가.
4. **특수·범용 경계** — 특정 scenario 때문에 일반 Framework module에 option, branch, wrapper를
   추가하지 않았는가. special-purpose policy가 필요한 가장 높은 계층 또는 adapter에 남아 있는가.
5. **복잡성 아래로 이동** — 호출자가 사전 준비, 재시도, codec 변환, 내부 상태 해석을 해야 하는가.
   module이 기본값과 실패 처리를 내부에서 흡수할 수 있는데 caller에게 노출하지 않았는가.
6. **오류를 정의로 없애기** — caller가 다르게 처리할 수 없는 세부 오류를 여러 예외·status로
   노출하지 않는가. 의미 있는 경계에서 오류를 집약하는가.
7. **두 번 설계하기** — 첫 설계를 바로 구현하지 않고 interface 수준의 대안을 두 개 이상 비교했는가.
   선택 기준에 단순성, 일반성, 성능, caller 부담과 책임 경계가 포함되는가.
8. **Naming·comment** — 같은 개념에 같은 이름을 사용하고, 주석이 코드 반복이 아니라 계약과
   선택 이유를 설명하는가.

위험 신호가 있으면 finding에 `file:line`, 실제 호출 순서, 위반한 POSD 원칙, 영향, 대안 A·B,
선택한 대안과 재검증 test를 모두 기록한다. 단순한 취향이나 format 차이는 finding으로 만들지 않는다.

### DDD review rubric

Node Framework는 business domain만이 아니라 lifecycle, message, buffer, route, handle과 error를
다루는 system software다. 따라서 reviewer는 다음 event storming과 boundary 질문을 사용한다.

1. **Event storming** — 사용자가 관찰하거나 caller가 책임지는 state transition을 과거형 event로
   적는다. 예: `PeerConnected`, `MessageDispatched`, `ActorJoined`, `ReceiveTimedOut`,
   `ResourceClosed`. 각 event를 만든 command, 시작 actor와 failure event도 적는다.
2. **Entity·value object·aggregate** — identity와 lifecycle을 가진 connection·session·actor·spot,
   값으로 비교하는 endpoint·timeout·error detail, 함께 invariant을 지켜야 하는 route·relocation·
   buffer 단위를 구분한다. 어느 module이 aggregate invariant을 단독으로 보장하는지 명시한다.
3. **Bounded context와 언어** — runtime, transport, codec, storage, binding, application에서
   `timeout`, `cancellation`, `ownership`, `generation`, `error`가 같은 의미를 유지하는지 확인한다.
   같은 개념을 `RequestTimeout`, `DefaultTimeout`, `Deadline`처럼 서로 다른 규칙으로 사용하지 않는다.
4. **Application use case와 adapter** — 외부 request를 조정하는 use case와 domain rule을 분리한다.
   HTTP, stream, codec, Redis, Framework callback은 adapter·port 경계 밖에 두며 domain이 이를 직접
   import하지 않는지 확인한다.
5. **Lifecycle와 authority** — create, admission, commit, relocation, shutdown, recovery의 state
   owner와 authority가 한 경계에 있는가. 저장된 row나 callback이 실행 권한을 대신 결정하지 않는가.
6. **Command·event·failure** — command가 의도를 표현하고 event가 이미 발생한 사실을 표현하는가.
   timeout, cancellation, partial commit, retry와 cleanup이 domain invariant와 같은 경계에서
   해석되는가.
7. **DDD 이름의 shallow layer 방지** — Controller → ApplicationService → DomainService가 같은
   인자를 전달만 하지 않는가. mapper·port·adapter를 추가한 만큼 caller 복잡성이 실제로 줄었는가.

DDD finding에는 event, command, actor, aggregate 또는 boundary를 식별하고, 해당 개념이 source·spec·
test에서 같은 이름과 책임으로 표현되는지 적는다. DDD 용어를 붙이는 것만으로 구조를 정당화하지
않으며, POSD rubric으로 각 layer의 깊이를 다시 확인한다.

### Review report 형식

기계 검사는 다음 형식으로 기록한다. 이 결과에는 formal round의 decision을 부여하지 않으며,
`R0`~`R3` review를 대체하지 않는다.

```text
reviewer: Codex
model: <actual balanced or stronger model ID>
model basis: mechanical support
reasoning: medium | high | xhigh | max
reasoning basis: <format, link, table or checklist scope>
scope: <files and checks>
result: PASS | FAIL | BLOCKED
```

Formal round 결과는 ledger card 또는 연결된 review log에 다음 형식으로 남긴다.

```text
reviewer: Codex
model: <actual model ID>
model basis: <contract, architecture, integration or final-audit reason>
reasoning: high | xhigh | max
reasoning basis: <round scope, contract risk, concurrency or final-audit reason>
round: R0 design | R1 candidate | R2 integration | R3 final
candidate: <commit SHA or immutable working-tree manifest>
scope: <card IDs and files>
decision: DESIGN ACCEPTED | CLEAN | NOT CLEAN | BLOCKED

finding: <ID>
severity: Critical | High | Medium | Low
category: contract | POSD | DDD | test/evidence | leftover
file:line: <path>:<line>
evidence: <observed behavior or call path>
violated rule: <spec, governance, POSD or DDD rule>
impact: <caller, state owner, lifecycle, compatibility or maintenance impact>
alternatives: <at least two design alternatives when non-trivial>
recommendation: <selected correction and owner>
verification: <test, process evidence and rerun condition>
status: open | accepted | rejected-with-reason | fixed | deferred
```

`NOT CLEAN`인 round는 다음 round로 진행하지 않는다. 구현자가 finding을 수정한 뒤 새 candidate와
fresh test 결과를 고정하고 같은 model·reasoning 설정 또는 더 높은 level로 해당 round를 다시 수행한다.
`deferred`는 완료가 아니며, owner와 후속 card ID가 있어야 한다.

## 2. 조사 범위와 authoritative source

### 2.1 기준 문서와 구현 경계

| 구분 | authoritative source 또는 조사 대상 | 사용 방법 |
|---|---|---|
| 공통 Framework 계약 | framework/doc/framework/common/spec/ | 공통 lifecycle, error, codec, HTTP, routing, stream, actor 계약의 목표 |
| Node exact interface | framework/doc/framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md부터 08-location-maintenance.ko.md | Node public 이름, 인자, 반환값, optional 값과 builder surface의 목표 |
| Node HTTP client exact interface | framework/doc/framework/common/spec/http-client/languages/node/node-http-client.ko.md | Node HTTP client의 공개 계약 확인 |
| Node stream connector exact interface | framework/doc/framework/common/spec/stream-connector/languages/typescript/03-stream-connector.ko.md | Node가 사용하는 TypeScript stream connector 계약 확인 |
| 공통 E2E 계약 | framework/doc/framework/common/e2e/ | process 분리, role server endpoint, client 제약, evidence와 완료 조건의 목표 |
| Node production source | framework/languages/node/packages/ | public declaration, runtime dispatch, error, codec, lifecycle call path 확인 |
| Node package surface | framework/languages/node/package.json, framework/languages/node/packages/*/package.json, package-lock.json | version pin, package export, consumer install 확인 |
| Node E2E | framework/languages/node/e2e/ | feature-map, scenario dispatch, process role와 실제 endpoint 확인 |
| Node test와 CI | framework/languages/node/test/, framework/languages/node/scripts/, .github/workflows/framework-node.yml | audit gate, skip list, test script, path filter, aggregate 실행 확인 |
| 기존 log와 snapshot | framework/doc/plan/log/framework-public-contract-gap-implementation/ | 과거 상태의 참고 자료로만 사용. 현재 성공 증거로 승격하지 않음 |

공통 E2E 문서는 검증해야 할 누락과 process 구조를 정하는 입력이다. 공통
E2E 문서나 다른 언어의 구현만으로 Node public API를 추가하는 근거로 사용하지
않는다. public contract는 공통 spec과 Node exact interface를 먼저 따른다.

공통 contract governance는
framework/doc/framework/common/spec/00-public-contract-governance.ko.md의
public contract 범위, ownership, completion과 변경 순서를 적용한다. 특히
timeout, cancellation, error, callback, ownership과 completion도 public
contract의 일부로 취급한다.

### 2.2 Config 전체 범위와 scenario ID 대조

공통 E2E 문서에서 확인한 scenario heading은 모두 374개이다. 아래 표의
누락은 공통 문서에 있는 exact ID가 Node feature-map 또는 Node selector에
없거나, alias로만 대체된 경우를 포함한다. 상태가 implemented로 표시되어도
현재 process 실행이 없으면 완료로 판정하지 않는다.

| Config | 공통 E2E 문서 | 공통 ID 수 | Node feature-map/runner 상태와 exact 누락 |
|---|---|---:|---|
| 1 Location messaging | framework/doc/framework/common/e2e/config-1-location-messaging.ko.md | 17 | RegistryMessaging feature-map은 16개. RM-A7 누락. RM-C9는 전환 대상 표시. |
| 2 Spot service | framework/doc/framework/common/e2e/config-2-spot-service.ko.md | 66 | SpotService map은 55개. SM-A9, SM-A10, SM-A11, SM-A12, SM-A13, SM-B0, SM-B0A, SM-B10, SM-B11, SM-G5A, SM-G5B 누락. SM-B6 재검증 필요, SM-C5, SM-C6, SM-D10, SM-D14 전환 필요 표시. |
| 3 PubSub | framework/doc/framework/common/e2e/config-3-pubsub.ko.md | 24 | PubSub map은 PS-D7, PS-E2 alias를 사용한다. exact ID PS-D7A, PS-D7B, PS-E2A, PS-E2B, PS-E2C가 누락. map의 PS-A2는 전환 대상, D/E 계열은 미구현 또는 전환 상태. |
| 4 Registration codec | framework/doc/framework/common/e2e/config-4-registration-codec.ko.md | 12 | RegistrationCodec map은 A1-A6, B1-B5만 보유. RC-B6 누락. |
| 5 Resilience lifecycle | framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md | 39 | ResilienceLifecycle map은 A-D 계열 20개만 보유. RL-E1부터 RL-E5, RL-F1부터 RL-F14가 모두 누락. |
| 6 Store failure recovery | framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md | 28 | DiscoveryRegistryHa map과 runner는 10개만 보유. SF-B3, SF-C3, SF-C4, SF-C5, SF-F1부터 SF-F11, SF-G1부터 SF-G3 누락. |
| 7 Monitoring | framework/doc/framework/common/e2e/config-7-monitoring.ko.md | 12 | RuntimeMonitoring map에 A1, A2, A3, A5, B1, B2, C1만 존재. MON-A4A, MON-A4B, MON-A6, MON-D1A, MON-D1B 누락. A/D 일부는 전환 대상 표시. |
| 8 Execution turn | framework/doc/framework/common/e2e/config-8-execution-turn.ko.md | 32 | AutomaticTurnDispatch map은 27개지만 TD-D4, TD-D5, TD-D6, TD-E2A, TD-F5A가 exact ID로 누락. runner의 실제 selector는 TD-F1, TD-F2, TD-F3 중심이고 source-gate가 포함됨. |
| 9 To-Actor messaging | framework/doc/framework/common/e2e/config-9-to-actor-messaging.ko.md | 7 | ToActorMessaging map은 TA-A1부터 TA-A4, TA-B1부터 TA-B3를 모두 표시. 현재 TA-A1 process build가 public API drift로 실패하므로 map 상태만으로 충족하지 않음. |
| 10 Spot actor relocation | framework/doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md | 43 | SpotActorTransfer map에는 exact ID가 있으나 ST-B1, ST-E1A는 partial, ST-B3는 전환, ST-F3A, ST-G1부터 ST-G6, ST-H4A, ST-H4B는 미구현. runner는 ST-F3/ST-H4 alias와 subset만 선택하여 split ID가 직접 dispatch되지 않음. |
| 11 Observability ops | framework/doc/framework/common/e2e/config-11-observability-ops.ko.md | 22 | ObservabilityOps map은 OBS-A1-A4, OBS-B1-B4, OBS-C1-C11을 보유. OBS-A5, OBS-C9A, OBS-C9B, OBS-C12 누락. OBS-C9 alias는 C9A/C9B를 대체하지 못함. all은 C1-C5까지만 호출. |
| 12 Channel egress routing | framework/doc/framework/common/e2e/config-12-channel-egress-routing.ko.md | 16 | Node feature-map과 runner 디렉터리가 없음. CH-E2E-01, CH-E2E-02, CH-E2E-03, CH-E2E-06, CH-E2E-07A, CH-E2E-07B, CH-E2E-07C, CH-E2E-11, CH-E2E-04A, CH-E2E-04B, CH-E2E-04C, CH-E2E-05, CH-E2E-10, CH-E2E-12, CH-E2E-08, CH-E2E-09 전체 누락. |
| 13 Submit admission | framework/doc/framework/common/e2e/config-13-submit-admission.ko.md | 20 | SubmitAdmission map은 20개 exact ID를 보유하지만 구현 상태는 SA-E2E-14만 implemented, SA-E2E-01, SA-E2E-05, SA-E2E-08, SA-E2E-09, SA-E2E-20은 partial, 나머지는 unimplemented. all은 partial 항목까지 선택. |
| 14 Instance spot | framework/doc/framework/common/e2e/config-14-instance-spot.ko.md | 36 | Node feature-map과 runner 디렉터리가 없음. IS-E2E-01부터 IS-E2E-36 전체 누락. |

Node aggregate의 기본 목록은
framework/languages/node/e2e/run_e2e_all.sh:7-20의 12개 config이다. 이
목록에 Config 12와 Config 14가 없다. 따라서 공통 inventory 전체를 실행하는
aggregate가 아니다.

## 3. 현재 검증 결과

아래 결과는 2026-08-02 현재 working tree에서 이 audit 중 새로 실행한
결과이다. 과거 log, feature-map의 historical pass, source type 존재는 별도
증거로 취급했다.

| 실행 | 현재 결과 | 해석 |
|---|---|---|
| framework/languages/node에서 npm run build | 통과 | production TypeScript compile은 통과하지만 exact member parity나 process E2E를 증명하지 않음. |
| npm run test:browser | 통과, Chromium 1/1 | browser transport의 좁은 검증은 충족. 전체 stream connector 계약의 증거는 아님. |
| node --test --test-force-exit test/contract/contract-surface.test.js | 초기 audit 33/33 | 수정 전에는 선언과 일부 runtime export만 확인했고 snapshot·exact negative comparison이 없었다. 후속 결과는 38/38이다. |
| node --test --test-force-exit test/contract/backend-public-api-only.test.js | 통과, 5/5 | backend public API-only 검증은 충족. 모든 E2E client의 import 경계를 증명하지 않음. |
| npm run verify:m5-foundation | 통과, 5/5 | M5 foundation 회귀 범위는 충족. |
| npm run verify:m6a-runtime | 통과, 26/26 | topology, endpoint-only upgrade, admission, reconnect/liveness와 HWM의 좁은 runtime 범위는 충족. |
| npm run verify:m6b-runtime | 통과, 43/43 | SpotWide, PerActor, yield, actor order, exactly-once와 instance recovery의 좁은 범위는 충족. |
| npm run verify:m6c-runtime | 통과, 79/79 | authority, deadline/cancellation, cleanup, relocation, restart recovery와 replay의 좁은 범위는 충족. |
| Node E2E runner 전체 bash -n | 통과 | shell syntax만 검증. scenario dispatch와 process evidence는 검증하지 않음. |
| npm test bounded 실행 | 미완료, deferred `e2e-scenario-header-gate.test.js`에서 중단 | common E2E inventory 374개와 Node scenario 207개의 차이를 보고했다. full package gate green으로 표시하지 않는다. |
| npm run verify:coverage | 후속 범위 | E2E·sample과 함께 coverage full gate를 다시 실행한다. 이번 unit gate 58/58의 대체 결과가 아니다. |
| npm run verify:ci | 실패 | build/typecheck/lint·Chromium·비-E2E test를 통과한 뒤 E2E scenario header gate가 common scenario 171개 누락을 보고. |
| ./scripts/verify_packaged_contract.sh | 통과 | `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs`. |
| npm ls @zlink-systems/zlink --all | clean, exit 0 | root와 모든 workspace가 local `@zlink-systems/zlink@11.1.0`을 resolve. |
| e2e/ToActorMessaging/run_e2e.sh TA-A1 | 실패, exit 2 | server/caller/session/client build가 현재 Node public API와 맞지 않음. |
| e2e/SubmitAdmission/run_e2e.sh SA-E2E-14 | 실패, exit 2 | binding package 11.0.2와 native artifact가 불완전하여 process를 시작하지 못함. |

현재 실패는 모두 같은 원인으로 묶지 않는다.

- full package gate blocker: deferred static E2E header inventory gate.
- documentation-regression path는 현재 Node gap ledger를 authoritative source로 읽고 17/17 통과한다.
- package version blocker는 해소됐다. root, workspace, lock과 local archive가 11.1.0으로 정렬됐고
  clean packaged consumer 검증이 통과했다. process E2E는 별도 evidence로 남아 있다.
- production/API drift: ToActorMessaging의 stale builder, actor context,
  lifecycle signature와 HTTP client method.
- native artifact blocker: SubmitAdmission이 후보 native artifact를
  incomplete로 판정.
- historical evidence: feature-map의 과거 pass와 과거 log. 현재 process
  실행 결과가 아니므로 완료 판정에 사용하지 않음.

## 4. 현재 충족 판정

| 항목 | 판정 | 직접 확인한 evidence와 한계 |
|---|---|---|
| package root export의 기본 경계 | 충족 | contract-surface.test.js가 framework, nestjs, locations, stream connector의 root export를 확인하고, packaged consumer도 11.1.0 정책으로 통과했다. |
| 일부 backend public API-only 경계 | 충족 | backend-public-api-only.test.js 5/5. 모든 E2E Client tree와 실제 process call path까지 포함하지 않음. |
| 좁은 runtime lifecycle·authority·deadline·cleanup·replay 범위 | 충족 | M5/M6 153개 test와 actor handoff focused test가 통과. 공통 전체 contract와 process evidence를 대체하지 않음. |
| browser transport smoke | 충족 | Chromium 1/1. Node server public API parity와 전체 stream connector E2E는 별도. |
| E2E client의 reflection/raw-frame 사용 여부 | 충족(정적 scan 범위) | 조사한 Client 경로에서 reflection과 raw-frame 호출은 찾지 못함. 다만 server Framework package 직접 import가 발견되어 client-only architecture 전체는 미충족. |
| Nest exact builder contract | 비-E2E 충족, process E2E 후속 | `configureInboundDispatch` declaration·runtime·Nest contract test와 checked-in exact member snapshot이 통과했다. process evidence만 ND-IMP-001 후속 조건이다. |
| common error model | runtime 충족, process E2E 후속 | public 13-kind enum, 내부 mapping과 ProtocolError terminal mapping이 unit/runtime test를 통과했다. client-visible process evidence는 ND-IMP-002 후속 조건이다. |
| unknown content type 처리 | runtime 충족, process E2E 후속 | unknown non-JSON payload가 handler에 Buffer로 전달되지 않고 public ProtocolError로 종료된다. ND-IMP-003의 process evidence는 후속 범위다. |
| package consumer parity | 비-E2E 충족 | `npm ls`가 11.1.0으로 clean하고 packaged contract가 통과했다. process E2E와 native role-server evidence는 제외 범위다. |
| Config 1-14 aggregate E2E | 미충족 | aggregate가 12 config만 호출하고 exit 0만 집계함. ND-E2E-IMP-001, ND-E2E-IMP-002 참조. |

### 4.1 2026-08-02 runtime·unit phase 현재 판정

이번 phase의 범위는 production runtime gap과 Node production unit·contract test이다. E2E process와
sample 구현·실행은 후속 범위로 유지한다. ND-IMP-001의 Nest `configureInboundDispatch`, ND-IMP-002의
공통 public error mapping, ND-IMP-003의 unknown content type 처리는 현재 source와 회귀 test에서
runtime 기준을 충족했다. 후속 fresh run에서 ND-IMP-004의 비-E2E package parity도 충족했지만,
process E2E·sample gap은 아직 완료로 표시하지 않는다.

M5/M6 runtime 회귀는 153/153이다. 초기 production unit·contract 범위는 58/58이었고, 후속
비-E2E inventory는 59개 파일 1001/1001이다. 전체 Node test inventory는
123개이며, browser·integration, E2E 또는 sample 소스를 읽는 test, native child process test를 후속
범위로 제외했다. 정확한 제외 기준과 명령 결과는
[`log/2026-08-02-runtime-unit-completion.ko.md`](log/2026-08-02-runtime-unit-completion.ko.md)에 기록한다.

2026-08-02 후속 Poller 정렬 candidate에서는 STREAM, Channel·Route ROUTER, ClientServer control
DEALER, Fanout SUB와 Framework 내부 raw RouteMesh ROUTER·DEALER의 수신 직전에 `PollIn` readiness를
확인하도록 연결했다. STREAM은 public `recv`와 reusable `Received`를 사용하며 packet callback ingress를
사용하지 않는다. 해당 구현과 회귀 결과는 위 phase log에 기록했으며, Codex read-only review에서
중복 monitor 등록과 stale buffered state를 추가로 수정하고 targeted regression을 다시 통과했다.
다만 full `npm test`는 현재 dirty worktree의 actor-manager location takeover failure에서 중단되고,
E2E process와 sample은 후속 범위이므로 repository-wide phase 완료로 표시하지 않는다.

### 4.2 2026-08-02 비-E2E 반영 후 현재 판정

사용자 요청에 따라 E2E scenario source·runner, sample과 process evidence를 이번 반영에서 제외하고
Node production runtime, public contract, package와 unit·contract gate를 갱신했다. 다음 항목은
비-E2E 범위에서 현재 evidence를 확보했다.

| 항목 | 현재 판정 | fresh evidence |
|---|---|---|
| ND-IMP-001 Nest builder public member | 비-E2E 충족 | runtime member exact-set test와 Nest module suite 통과 |
| ND-IMP-002 common error surface | 비-E2E 충족 | public 13-kind/error mapping contract와 runtime suite 통과 |
| ND-IMP-003 unknown content type | 비-E2E 충족 | handler 호출 전 `ProtocolError`와 empty reply 회귀 통과 |
| ND-IMP-004 package pin·consumer surface | 비-E2E 충족 | `npm ls` clean at 11.1.0, packaged contract PASS |
| ND-TEST-001 public snapshot·negative comparison | 비-E2E 충족 | `node-public-contract.json`, exact Nest member test와 contract-surface 38/38 통과; 전체 E2E/process surface는 후속 |
| ND-TEST-002 documentation regression | 충족 | live Node ledger path 기준 17/17 PASS |
| ND-TEST-003 CI path·non-E2E gate | 비-E2E 충족 | common guide path filter 추가, build/typecheck/lint와 59 contract files 1001/1001 PASS |

runtime queue와 worker pool의 POSD·DDD 검토, 선택한 대안과 재검증 결과는
[`log/2026-08-02-posd-ddd-review.ko.md`](log/2026-08-02-posd-ddd-review.ko.md)에 기록했다. E2E inventory
171개 누락과 Config 12/14 process blocker는 이번 범위에서 그대로 unresolved로 남긴다.

## 5. ND-IMP-* production implementation gap

### ND-IMP-001 — Nest exact builder의 inbound dispatch 계약과 public member 불일치 (runtime 완료)

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/server/languages/node/interfaces/07-nestjs-host.ko.md:169-186, framework/doc/framework/common/spec/00-public-contract-governance.ko.md:8-21
- Node source·test 경로: framework/languages/node/packages/nestjs/src/contracts.ts:192-210, framework/languages/node/packages/nestjs/src/options-builder.ts:101-132, framework/languages/node/packages/framework/src/contracts/Configuration/Builders.ts:32-55, framework/languages/node/packages/framework/src/contracts/Configuration/RegistrationBuilders.ts:128-140 및 169-176, framework/languages/node/test/contract/contract-surface.test.js
- 현재 동작: Nest builder declaration과 runtime build 결과에 `configureInboundDispatch`가 포함되고, 해당 options가 inbound dispatch 설정 경로에 전달된다. checked-in snapshot과 runtime member 비교에서 exact interface 밖의 내부 helper가 노출되지 않는다.
- 기대 동작: Node exact interface의 builder member를 public contract로 사용하고, `configureInboundDispatch`를 같은 계약으로 제공한다. 내부 codec·중복 이름 helper는 public builder 표면에 노출하지 않는다.
- 기존 gap 판정 근거: 수정 전 build 결과에서 `configureInboundDispatch`가 없었고 내부 helper가 runtime prototype에 남아 있었다. 현재는 `contract-surface.test.js`와 `nestjs-module.test.js`가 declaration·runtime member와 실제 builder path를 확인한다. process E2E 증거는 후속 범위다.
- 구체적인 수정 목록: (1) exact interface snapshot과 builder declaration·implementation을 정렬한다. (2) 내부 helper를 module-local로 숨긴다. (3) `configureInboundDispatch`가 worker/admission/preflight 순서를 보존하는지 production call path에서 확인한다. (4) package export와 consumer compile을 검증한다. 비-E2E 항목은 완료했고 process evidence만 후속이다.
- 필요한 회귀 test: ND-REG-001. configureInboundDispatch의 declaration 및 runtime 존재, exact parameter/return type, exact interface 밖 setter의 처리, build 후 package root export를 함께 검사한다.
- 선행 조건과 작업 순서: contract 선행 → ND-IMP-002 error contract 확인 → Nest source 수정 → ND-REG-001 → ToActor/SpotService process build 순서로 진행한다.
- 구현 완료 evidence: exact interface member 비교가 missing/extra 없이 통과하고, configureInboundDispatch를 사용한 real builder path가 admission/preflight 순서를 보이는 test와 process E2E를 통과한다. 과거 source type 존재만으로 완료하지 않는다.

### ND-IMP-002 — common error model과 Node public error surface 불일치 (runtime 완료)

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/32-framework-error-model.ko.md:19-33 및 66-78, framework/doc/framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md:59-77
- Node source·test 경로: framework/languages/node/packages/framework/src/contracts/Errors/ZLinkFrameworkException.ts:1-16 및 18-104, framework/languages/node/test/contract/contract-surface.test.js:536-599, framework/languages/node/packages/framework/src/runtime/
- 현재 동작: Node public `ZLinkFrameworkErrorKind`는 common 13개 kind를 0부터 12까지 제공한다. 내부 detailed failure는 `framework-errors-internal.ts`에서 public kind로 매핑하고, `ZLinkFrameworkException`은 public `kind`와 `cause` 계약을 사용한다.
- 기대 동작: Node exact interface가 고정한 ZLinkFrameworkErrorKind 13개 common kind를 numeric 0부터 12까지 제공하고, exact exception surface와 terminal error mapping을 따른다. wire/payload/reply type failure는 ProtocolError로, cancellation/deadline은 정의된 cancellation/deadline 의미로 완료되어야 한다.
- 기존 gap 판정 근거: 수정 전 source가 detailed error table을 public surface로 노출했다. 현재는 `contract-surface.test.js`, `channel-envelope-error.test.js`와 M6C error mapping test가 public enum·exception·terminal mapping을 확인한다. process E2E의 client-visible evidence는 후속 범위다.
- 구체적인 수정 목록: (1) exact Node error interface를 목표로 확정한다. (2) 내부 detailed failure를 유지할 필요가 있으면 public common kind와 내부 진단 정보를 분리한다. (3) send/request/handler/HTTP response의 terminal mapping을 ProtocolError, cancellation, deadline 계약에 맞춘다. (4) code, retryability, cause, serialization 노출이 exact interface와 맞는지 함께 정리한다.
- 필요한 회귀 test: ND-REG-002. 13개 enum 이름과 값, exception public member, wire/payload/reply failure, cancellation과 deadline의 terminal mapping을 exact table로 검사한다.
- 선행 조건과 작업 순서: contract 선행 → error mapping inventory → production exception/dispatch 수정 → ND-REG-002 → E2E evidence gate 순서로 진행한다.
- 구현 완료 evidence: exact interface와 runtime export가 동일하고, 각 failure class가 client-visible terminal error와 role server evidence에서 동일한 kind/reason으로 직접 확인된다. 41개 source enum table의 존재는 증거가 아니다.

### ND-IMP-003 — 알 수 없는 content type이 ProtocolError가 아니라 Buffer로 전달됨 (runtime 완료)

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/06-framework-api.ko.md:411-419, framework/doc/framework/common/spec/32-framework-error-model.ko.md:66-78
- Node source·test 경로: framework/languages/node/packages/framework/src/runtime/channels/channel-envelope.ts:223-247, framework/languages/node/packages/framework/src/runtime/channels/channel-dispatch-pipeline.ts:209-227, framework/languages/node/packages/framework/src/runtime/spots/spot-route-packet-dispatch.ts:120-168, framework/languages/node/packages/framework/src/runtime/spots/index.ts:869-895 및 916-940, framework/languages/node/test/contract/channel-envelope-error.test.js
- 현재 동작: registered serializer와 binary·JSON 처리는 기존 경로를 사용하고, 등록되지 않은 non-JSON content type은 public `ProtocolError`로 변환된다. channel dispatch와 spot dispatch는 handler 호출 전에 terminal failure를 완료한다.
- 기대 동작: 등록되지 않은 non-JSON content type은 JSON으로 추측하지 않고 ProtocolError로 완료되어야 한다. handler가 Buffer를 정상 payload로 처리하거나 side effect를 발생시키면 안 된다. send와 request의 terminal mapping도 같은 error contract를 따라야 한다.
- 기존 gap 판정 근거: 수정 전 decoder가 unknown content type을 Buffer로 반환했다. 현재는 `channel-envelope-error.test.js`와 production dispatch 경로를 포함한 unit·runtime test가 `ProtocolError`와 no-handler path를 확인한다.
- 구체적인 수정 목록: (1) unknown content type fallback을 ProtocolError 결과로 바꾼다. (2) channel, spot route, request, send의 실제 call path에서 handler invocation 전에 실패가 완료되는지 확인한다. (3) callback exactly-once와 no-handler side effect를 보장한다. (4) HTTP 또는 role server evidence에 노출되는 status/body/error code mapping을 common error spec과 맞춘다.
- 필요한 회귀 test: ND-REG-003. unknown content type의 send/request에 대해 ProtocolError, handler count 0, callback count 1, terminal reason과 response body를 직접 검사하고 binary/JSON 정상 경로를 회귀한다.
- 선행 조건과 작업 순서: ND-IMP-002 error kind 확정 → decoder와 dispatch pipeline 수정 → ND-REG-003 → channel-client bounded test와 process E2E 순서로 진행한다.
- 구현 완료 evidence: unknown content type을 넣은 real channel/spot request가 ProtocolError로 종료되고 handler evidence가 0이며 callback exactly-once가 확인된다. decode helper의 반환 type만 검사하는 test는 충분하지 않다.

### ND-IMP-004 — package version pin과 packaged consumer surface 불일치

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/00-public-contract-governance.ko.md, scripts/local-package/README.ko.md:19-27, framework/doc/framework/common/e2e/config-13-submit-admission.ko.md
- Node source·package·test 경로: framework/languages/node/package.json:57, framework/languages/node/packages/framework/package.json:16, framework/languages/node/packages/framework-locations-redis/package.json:15, framework/languages/node/package-lock.json:26 및 1245-1246, framework/languages/node/packages/*/package.json:7-15, framework/languages/node/scripts/verify_packaged_contract.sh
- 실제 동작: root package는 local @zlink-systems/zlink 11.1.0 archive를 가리키지만 workspace package와 lock은 11.0.2를 요구한다. node_modules는 11.0.2를 invalid로 표시한다. packaged consumer install은 @zlink-systems/zlink@11.0.2 registry lookup 404로 실패하고, SubmitAdmission은 11.0.2 native artifact incomplete로 중단된다.
- 기대 동작: Node framework의 중앙 version pin, workspace dependency, lockfile, local package archive와 consumer verification이 같은 명시적 버전 정책을 따른다. local package 정책상 bindings source를 직접 참조하지 않고, 선택한 package version이 실제 consumer와 E2E process에 전달되어야 한다.
- gap 판정 근거: npm ls가 invalid를 반환했고 verify_packaged_contract.sh가 실제 consumer install을 완료하지 못했다. package root export test가 통과해도 package dependency graph와 native artifact가 맞지 않으므로 배포 계약은 충족되지 않는다.
- 구체적인 수정 목록: (1) framework가 사용할 zlink version을 중앙 지점에서 확정한다. (2) workspace package.json과 package-lock의 dependency와 resolved archive를 동일 정책으로 갱신한다. (3) local package 생성·배포와 stale native cache를 점검한다. (4) package root export, clean consumer install, Redis/process E2E를 같은 version으로 재실행한다.
- 필요한 회귀 test: ND-REG-004. package.json, workspace manifests, lockfile, archive metadata의 version consistency와 실제 clean consumer install, native artifact load를 검사한다.
- 선행 조건과 작업 순서: local-package 정책 확인 → 중앙 version 확정 → package/lock 재현성 회복 → ND-REG-004 → SubmitAdmission와 aggregate E2E 순서로 진행한다.
- 구현 완료 evidence: npm ls가 invalid 없이 종료하고, registry에 의존하지 않는 지정 local package consumer가 설치·build·실행되며, process E2E가 같은 version의 native artifact로 role server를 시작한다.

#### 2026-08-02 비-E2E 갱신

위의 11.0.2 mismatch는 이전 audit snapshot이다. 현재 `framework/languages/node/package.json`,
workspace manifests와 lockfile은 11.1.0으로 정렬되었고, `npm ls @zlink-systems/zlink --all`가
clean으로 끝난다. `scripts/verify_packaged_contract.sh`도 7개 package, browser ESM과 server
CommonJS consumer를 통과했다. process E2E의 native role-server evidence는 이 범위에 포함하지 않았다.

## 6. ND-E2E-IMP-* Node E2E implementation gap

공통 E2E의 완료 조건은 framework/doc/framework/common/e2e/config-1-location-messaging.ko.md:438-452,
config-7-monitoring.ko.md:47-55, config-10-spot-actor-relocation.ko.md:30-45,
config-12-channel-egress-routing.ko.md:7-14, config-13-submit-admission.ko.md:395-413,
config-14-instance-spot.ko.md:28-43을 기준으로 삼았다. client result와 role
server evidence를 모두 확인해야 하며, log·source type·unit test만으로
scenario를 완료할 수 없다.

### ND-E2E-IMP-001 — common inventory와 Node feature-map의 exact ID coverage 불일치

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/config-1-location-messaging.ko.md부터 config-14-instance-spot.ko.md 전체, 세부 누락은 2.2절 표에 기록
- Node source·feature-map·runner 경로: framework/languages/node/e2e/RegistryMessaging/feature-map.ko.md, SpotService/feature-map.ko.md, PubSub/feature-map.ko.md, RegistrationCodec/feature-map.ko.md, ResilienceLifecycle/feature-map.ko.md, DiscoveryRegistryHa/feature-map.ko.md, RuntimeMonitoring/feature-map.ko.md, AutomaticTurnDispatch/feature-map.ko.md, ToActorMessaging/feature-map.ko.md, SpotActorTransfer/feature-map.ko.md, ObservabilityOps/feature-map.ko.md, SubmitAdmission/feature-map.ko.md
- 실제 동작: 공통 374개 ID와 비교하면 Config 1-8, 11의 exact ID 누락과 alias가 있고, Config 12와 Config 14는 Node map 자체가 없다. Config 9와 Config 10은 map에 ID가 있어도 현재 실행 상태가 partial, transition, unimplemented이거나 stale build로 막혀 있다.
- 기대 동작: common inventory의 exact ID가 Node map에 동일한 문자열로 존재하고, 각 항목에 implemented, partial, diagnostic_only, source_only, blocked 같은 실행 상태와 evidence 요구가 명시되어야 한다. alias는 exact scenario의 대체 ID로 계산하지 않는다.
- gap 판정 근거: live parser로 common heading 374개를 수집하고 Node map의 exact token을 대조했다. map에 없는 ID와 alias가 표에 재현되며, map의 historical log는 현재 실행 결과가 아니다.
- 구체적인 수정 목록: (1) 14개 Config의 common inventory를 machine-readable exact ID로 고정한다. (2) Node map에 누락 ID와 상태를 추가하되, public API가 필요한 항목은 contract 선행으로 분리한다. (3) alias를 exact ID로 바꾸거나 명시적 mapping과 별도 결과를 둔다. (4) source-only와 historical log를 pass 상태에서 분리한다.
- 필요한 회귀 test: ND-REG-005. common E2E inventory와 Node feature-map exact set, alias, 상태 enum, config ownership을 비교한다.
- 선행 조건과 작업 순서: 공통 inventory 고정 → ND-IMP-* contract/package blocker 정리 → feature-map 정렬 → per-config selector 정렬 → aggregate gate 순서로 진행한다.
- 구현 완료 evidence: 374개 common ID가 Node map에 exact match하고, 각 ID에 현재 실행 결과와 evidence type이 있으며, 누락·alias·historical-only 항목이 0개로 보고된다.

### ND-E2E-IMP-002 — aggregate all runner의 Config 범위와 완료 판정 부족

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/ 전체 14개 Config, 특히 config-1-location-messaging.ko.md:438-452와 config-12-channel-egress-routing.ko.md, config-14-instance-spot.ko.md
- Node runner 경로: framework/languages/node/e2e/run_e2e_all.sh:7-20 및 56-141
- 실제 동작: DEFAULT_CONFIGS는 DiscoveryRegistryHa, RegistrationCodec, RegistryMessaging, PubSub, SpotService, RuntimeMonitoring, ResilienceLifecycle, AutomaticTurnDispatch, ObservabilityOps, ToActorMessaging, SpotActorTransfer, SubmitAdmission 12개만 포함한다. child runner가 exit 0이면 PASS로 집계하고, common 374개 ID, feature-map 상태, client result, role server evidence, callback/owner/generation/cleanup assertion을 해석하지 않는다.
- 기대 동작: all은 Config 1-14를 포함하고 exact inventory 전체를 dispatch해야 한다. partial, unimplemented, diagnostic_only, source_only, N/A, timeout과 skip을 성공으로 합산하지 않고, 각 scenario의 client result와 role server evidence가 모두 있는 경우에만 PASS여야 한다.
- gap 판정 근거: Config 12와 Config 14가 aggregate 목록과 Node directory 모두에 없고, runner 출력의 PASS 조건이 child exit code 하나뿐이다. 따라서 현재 all 성공이 common E2E 완료를 의미하지 않는다.
- 구체적인 수정 목록: (1) Config 12와 Config 14의 Node feature-map 및 process runner 설계를 contract 선행으로 등록한다. (2) aggregate를 common inventory 기반으로 구동한다. (3) child result를 status와 evidence schema로 수집한다. (4) partial, diagnostic-only, source-only, N/A를 실패 또는 미완료로 분리한다. (5) callback count, terminal reason, owner, generation, cleanup evidence를 결과에 포함한다.
- 필요한 회귀 test: ND-REG-006. aggregate가 14개 Config와 374개 ID를 모두 선택하고, 의도적으로 partial/N/A/source-only child를 넣었을 때 PASS 수에 포함하지 않는지 검사한다.
- 선행 조건과 작업 순서: ND-E2E-IMP-001 → per-config exact selector → role evidence schema → aggregate 변경 → ND-REG-006 → 실제 process 실행 순서로 진행한다.
- 구현 완료 evidence: aggregate report에 Config 1-14와 374개 exact ID가 모두 나타나고, 각 ID의 process exit, client result, role server evidence, terminal/callback/ownership fields가 확인된다. child exit 0만으로 PASS가 되지 않아야 한다.

### ND-E2E-IMP-003 — scenario selector와 partial/source-only 결과의 all dispatch 불일치

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/config-3-pubsub.ko.md, config-8-execution-turn.ko.md, config-10-spot-actor-relocation.ko.md, config-11-observability-ops.ko.md, config-13-submit-admission.ko.md
- Node runner·feature-map 경로: framework/languages/node/e2e/PubSub/run_e2e.sh 및 feature-map.ko.md, AutomaticTurnDispatch/run_e2e.sh:19-43 및 177-189, SpotActorTransfer/run_e2e.sh:10-15 및 322-328, ObservabilityOps/run_e2e.sh:12-29, SubmitAdmission/run_e2e.sh:22-55 및 128-169
- 실제 동작: PubSub runner는 PS-D7A/B와 PS-E2A/B/C 대신 PS-D7와 PS-E2 alias를 사용한다. AutomaticTurnDispatch는 full에서 일부 scenario와 source-gate를 실행하며 TD-C5 source-gate result=passed를 E2E 결과처럼 출력할 수 있다. SpotActorTransfer는 ST-F3와 ST-H4 alias 및 subset을 사용하여 ST-F3A와 ST-H4A/B를 직접 선택하지 않는다. ObservabilityOps all은 C5까지만 호출한다. SubmitAdmission all은 partial인 SA-E2E-01, 05, 08, 09, 20을 implemented 결과와 함께 선택한다.
- 기대 동작: selector 문자열 하나가 common exact ID 하나의 process dispatch로 연결되어야 한다. source-gate, historical log, partial 구현, Kotlin-only N/A는 client-visible 및 role server evidence가 없으면 PASS가 될 수 없다.
- gap 판정 근거: runner case와 all 배열을 source에서 직접 확인했고, feature-map 상태와 common ID를 대조했다. alias·subset·source-only 경로가 현재 aggregate의 성공 수에 섞여 있다.
- 구체적인 수정 목록: (1) 모든 split ID에 exact case를 만든다. (2) alias는 제거하거나 별도 alias로 표시하되 exact ID 결과를 생성한다. (3) source-gate를 diagnostic evidence로만 분류한다. (4) partial/transition/unimplemented/N/A 상태가 process PASS를 반환하지 않도록 result schema와 exit policy를 정한다. (5) role server evidence와 client result를 selector별로 저장한다.
- 필요한 회귀 test: ND-REG-005와 ND-REG-006. map/selector exact 비교와 all status aggregation을 함께 검사한다.
- 선행 조건과 작업 순서: ND-E2E-IMP-001 → 각 runner selector 정렬 → status/evidence schema → aggregate gate 순서로 진행한다.
- 구현 완료 evidence: common exact ID를 입력한 selector가 정확히 한 process procedure를 선택하고, 실제 endpoint 호출과 양쪽 evidence가 있는 경우만 PASS로 보고된다. source-gate 출력만 있는 경우는 PASS가 아니다.

### ND-E2E-IMP-004 — E2E client가 server Framework package를 직접 import함

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/config-1-location-messaging.ko.md:7-16 및 33-43, config-7-monitoring.ko.md:7-13 및 25-45, config-10-spot-actor-relocation.ko.md:30-45
- Node E2E client 경로: framework/languages/node/e2e/ObservabilityOps/Client/Scenarios/obs-c4-forced-session-drain-scenario.ts:7, ObservabilityOps/Client/Support/observability-support.ts:6, ObservabilityOps/Client/Scenarios/obs-c5-rollout-scenario.ts:15, DiscoveryRegistryHa/Client/Scenarios/SfC2GracefulShutdownScenario.ts:6, DiscoveryRegistryHa/Client/Scenarios/SfA1BaselineScenario.ts:5, RegistryMessaging/Client/Support/dynamic-cluster-launcher.ts:7, RegistryMessaging/Client/Scenarios/rm-c8-payload-round-trip-scenario.ts:2, RegistryMessaging/Client/Scenarios/rm-b2-scale-in-scenario.ts:7, RegistryMessaging/Client/Scenarios/rm-a1-discovery-request-scenario.ts:2, RegistryMessaging/Client/Scenarios/rm-c5-missing-packet-scenario.ts:2, RuntimeMonitoring/Client/Scenarios/mon-a4-availability-transition-scenario.ts:13
- 실제 동작: 위 Client subtree에서 @zlink-systems/framework를 직접 import하는 source가 발견되었다. ToActorMessaging/peer-fault.js:8에도 Framework require가 있으므로 scenario support와 runner support를 구분해 조사해야 한다. 조사한 Client 경로에서는 reflection과 raw-frame 호출은 찾지 못했지만, server Framework package 직접 의존만으로 client-only architecture를 증명할 수 없다.
- 기대 동작: E2E client는 공개 HTTP client 또는 stream connector를 사용하여 role server의 public business/evidence/control endpoint만 호출해야 한다. Framework messaging API, Location Store, private record, internal runtime state는 role server process 안에 있어야 한다.
- gap 판정 근거: Config 1, 7, 10의 공통 E2E는 client가 Framework API나 내부 상태를 직접 다루지 않도록 명시한다. 정적 import scan에서 server package import가 확인되었다.
- 구체적인 수정 목록: (1) Client에서 Framework import를 제거하고 role server application endpoint로 이동한다. (2) client가 필요한 결과는 공개 HTTP client 또는 stream connector response로 전달한다. (3) runner support는 process orchestration 코드와 client business code를 분리한다. (4) reflection, private/internal API, raw frame scan을 CI gate로 고정한다.
- 필요한 회귀 test: ND-REG-007. Client import allowlist, server/client process boundary, client result와 role server evidence 동시 확인, terminal reason·callback count·owner·generation·cleanup assertion을 검사한다.
- 선행 조건과 작업 순서: scenario별 role endpoint와 evidence schema 확정 → client import 정리 → ND-REG-007 정적 gate → process E2E 순서로 진행한다.
- 구현 완료 evidence: Client compilation이 허용된 public client package만 참조하고, 실제 process에서 Client는 role server public endpoint만 호출한다. client result와 role server evidence가 같은 scenario에서 확인되고 Framework 내부 record를 읽지 않아야 한다.

### ND-E2E-IMP-005 — 현재 public API와 process E2E 구현의 call path가 연결되지 않음

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/config-9-to-actor-messaging.ko.md, config-10-spot-actor-relocation.ko.md, config-13-submit-admission.ko.md:395-413
- Node E2E·production 경로: framework/languages/node/e2e/ToActorMessaging/Server/Actor, Caller, Session, Client, framework/languages/node/e2e/ToActorMessaging/run_e2e.sh:170-220, framework/languages/node/e2e/SubmitAdmission/run_e2e.sh, framework/languages/node/packages/nestjs/src/contracts.ts, framework/languages/node/packages/http-client/src/
- 실제 동작: ToActorMessaging server build에서 ZLinkActorJoinRequest, ZLinkSpotActorSendContext, ZLinkSpotActorRequestContext export가 없고 factory와 Entry Spot lifecycle signature가 현재 API와 다르다. ZLinkNestMeshNodeBuilder.channelName이 없고, 인자 수가 3개인 stale call이 남아 있으며, ZLinkActorGetOrCreateCall을 ActorRef처럼 사용하는 오류도 발생한다. Client에는 ZLinkHttpRequestBuilder.submitRaw가 없다. SubmitAdmission은 binding package 11.0.2 native artifact incomplete로 process를 시작하지 못한다.
- 기대 동작: public Node API를 사용하는 role server와 public HTTP/stream client가 실제 endpoint를 호출하고, process 종료·recovery·takeover·replay와 terminal evidence를 common scenario 계약대로 확인해야 한다.
- gap 판정 근거: TA-A1 process runner를 실제로 실행했을 때 build 단계에서 exit 2가 발생했고, SA-E2E-14도 native artifact 검사에서 exit 2가 되었다. 따라서 feature-map의 implemented 표시는 현재 process evidence가 아니다.
- 구체적인 수정 목록: (1) ND-IMP-001부터 ND-IMP-004를 먼저 정리한다. (2) E2E server/caller/session/client를 현재 exact public API에 맞춘다. (3) stale internal type과 private workaround를 추가하지 않고 public contract가 부족하면 contract 선행으로 분리한다. (4) local package와 native artifact를 재생성·검증한다. (5) process별 role evidence와 client assertion을 추가한다.
- 필요한 회귀 test: ND-REG-007과 ND-REG-008. real process call path, bounded timeout, build failure, hang, skip, role evidence 누락을 PASS로 계산하지 않는지 검사한다.
- 선행 조건과 작업 순서: production contract/package → E2E compile → client architecture → process startup/readiness → failure/recovery/cleanup assertion → aggregate 순서로 진행한다.
- 구현 완료 evidence: TA-A1과 SA-E2E-14를 포함한 관련 process runner가 현재 working tree에서 exit 0이고, 각 role endpoint 호출, client-visible result, terminal reason, callback count, owner, generation, cleanup evidence가 출력·assert된다. source compile만 통과한 경우는 부족하다.

## 7. 작업 순서와 review gate

gap은 다음 순서로 처리한다. 앞 단계가 확정되지 않으면 뒤 단계의 green 결과를
완료로 기록하지 않는다. 각 단계의 `R0`와 `R1`은 해당 card를 구현하는 동안
반드시 수행하고, `R2`와 `R3`는 여러 card를 묶은 뒤에도 다시 수행한다.

1. **기준 고정**: 현재 working tree, 기준 commit, package version, 공통 spec·exact
   interface·E2E inventory와 dirty 변경을 manifest로 고정한다. historical log와
   feature-map의 과거 pass는 현재 결과로 승격하지 않는다.
2. **contract 선행**: Node exact interface의 Nest builder와 error model을 common
   spec, exact Node 문서, governance로 고정한다. 공통 E2E나 다른 언어의 surface를
   public API 추가 근거로 사용하지 않는다.
3. **DDD design packet**: 수정할 card마다 event storming으로 event, command,
   actor와 failure를 적고, entity·value object·aggregate, bounded context,
   lifecycle authority, port·adapter 경계를 표로 만든다. state transition과
   invariant의 단일 owner를 지정한다.
4. **POSD 대안 설계와 `R0 design`**: 첫 구현 전에 interface 수준의 대안 두 가지
   이상을 비교한다. 각 대안의 caller 부담, 정보 은닉, module 깊이, 일반성, 성능,
   error 처리와 변경 증폭을 기록한 뒤 선택한다. 1.1절 정책에 따라 frontier Codex model을 `high`
   이상으로 사용하며, 어려운 계약 충돌이나 concurrency 판단은 `max`로 올린다. Reviewer가
   `DESIGN ACCEPTED`를 반환하기 전에는 source나 test를 수정하지 않는다.
5. **regression-first 구현**: `R0`가 통과한 card만 실패를 재현하는 regression을
   먼저 고정한다. 그 뒤 production contract, codec call path, package 재현성,
   E2E selector·endpoint를 책임 owner가 있는 계층에서 수정한다. 새 helper,
   reflection, raw frame, private workaround로 하위 계층의 gap을 숨기지 않는다.
6. **`R1 candidate`**: targeted test와 public surface·production call path evidence를
   실행하고 candidate SHA 또는 immutable manifest를 고정한다. 1.1절 정책에 따라 frontier Codex model을
   `high` 이상으로 사용해 전체 diff의 contract·POSD·DDD·test/evidence finding을 확인한다. 어려운 계약
   충돌이나 concurrency 판단은 `max`로 올린다. `NOT CLEAN`이면
   다음 card로 가지 않고 finding을 수정한 새 candidate에서 같은 round를 반복한다.
7. **E2E inventory와 per-config process**: ND-E2E-IMP-001에 따라 Config 1-14의
   374개 exact ID와 map·selector를 맞춘다. ND-E2E-IMP-003과 ND-E2E-IMP-004를 처리해
   role endpoint, client allowlist, evidence schema를 정렬한다. Config 12·14처럼
   public contract가 필요한 항목은 `contract 선행`으로 유지한다.
8. **package와 aggregate gate**: ND-IMP-004를 해결해 local package version,
   workspace, lock, native artifact와 clean consumer를 일치시킨다. 이어
   ND-E2E-IMP-002를 처리해 Config 1-14를 aggregate에 포함하고 partial,
   diagnostic-only, source-only, N/A와 child exit 0만으로 PASS가 되지 않게 한다.
9. **`R2 integration`**: 1.1절 정책에 따라 frontier Codex model을 `high` 이상으로 사용한다. Full npm
   test, coverage, verify:ci, packaged contract와
   관련 process E2E를 제한 시간 만료 없이 실행한다. client result, role server
   evidence, terminal reason, callback count, owner, generation과 cleanup을 한 실행에서
   확인한다. 좁은 M5/M6 pass나 source compile은 이 round의 대체 증거가 아니다.
10. **`R3 final`과 완료 판정**: 1.1절 정책에 따라 frontier Codex model을 `xhigh` 이상으로 사용해 전체
    ledger, 변경 manifest, fresh gate, process evidence와 unresolved finding을 독립적으로 다시 읽는다.
    어려운 계약 충돌이나 concurrency 판단은 `max`로 올린다. Current run
    result만 사용하고 historical log, source-only, timeout, hang, skip은 완료 evidence에서
    제외한다. 미해결 Critical/High/Medium finding이 0이고 Low disposition이 기록된 뒤에만
    checklist를 갱신한다.

## 8. 기존 회귀 test의 유지·변경·추가 목록

### 8.1 유지할 기존 검증

다음 검증은 현재 범위의 직접 증거로 유지한다. 다만 표시한 한계를 함께
기록해야 한다.

| 기존 검증 | 현재 결과 | 유지 목적과 한계 |
|---|---|---|
| test/contract/contract-surface.test.js | 38/38 | checked-in snapshot, exact member negative comparison과 package/runtime export 경계를 유지한다. |
| test/contract/backend-public-api-only.test.js | 5/5 | backend public API 사용 경계 유지. E2E Client 전체 import scan을 대체하지 않음. |
| verify:m5-foundation | 5/5 | foundation admission과 topology 회귀 유지. |
| verify:m6a-runtime | 26/26 | HWM, endpoint-only upgrade, reconnect/liveness와 admission 회귀 유지. |
| verify:m6b-runtime | 43/43 | routing, yield, actor order와 exactly-once 회귀 유지. |
| verify:m6c-runtime | 79/79 | authority, deadline/cancellation, relocation, recovery, replay 회귀 유지. |
| test:browser | Chromium 1/1 | browser transport smoke 유지. |
| E2E runner bash -n | 통과 | shell syntax만 유지. 실행 evidence로 승격하지 않음. |

### 8.2 ND-TEST-* audit 또는 gate gap

#### ND-TEST-001 — public API snapshot과 exact negative comparison 부재 (수정 전 audit)

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md 및 07-nestjs-host.ko.md, framework/doc/framework/common/spec/00-public-contract-governance.ko.md:75-88
- Node test/source 경로: framework/languages/node/test/contract/contract-surface.test.js:31-61 및 319-346 및 536-599, framework/languages/node/packages/*/dist/
- 수정 전 실제 동작: checked-in Node public API snapshot 파일을 찾지 못했다. contract-surface test는 exact interface 문서를 동적으로 읽어 누락 이름과 일부 runtime export를 검사하고, public error enum은 현재 13개 common kind를 검사했다. 내부 detailed failure mapping과 Nest builder extra public member를 한 번에 비교하는 snapshot은 없었다.
- 기대 동작: release contract를 재현할 수 있는 exact Node API snapshot 또는 동등한 구조화된 manifest가 있어야 하며, 이름·signature·optional/default·error·ownership·export의 missing과 extra를 모두 비교해야 한다.
- gap 판정 근거: dynamic presence check는 source와 문서가 동시에 잘못 갱신되는 drift를 잡지 못하고, public·internal error mapping과 extra member의 snapshot drift를 닫지 않는다.
- 구체적인 수정 목록: (1) exact interface에서 machine-readable contract를 생성하거나 checked-in snapshot을 만든다. (2) missing뿐 아니라 extra member와 member signature를 비교한다. (3) error enum과 exception surface를 common target과 비교한다. (4) package root export와 clean consumer API를 snapshot gate에 연결한다.
- 필요한 회귀 test: ND-REG-001, ND-REG-002. snapshot diff에서 missing·extra·signature drift가 실패해야 한다.
- 선행 조건과 작업 순서: contract 선행 → snapshot format 결정 → snapshot 생성 → test gate 연결 → package consumer 검증 순서로 진행한다.
- 구현 완료 evidence: clean checkout에서 snapshot test가 exact interface, source declaration, runtime export, package export를 모두 비교하고 drift를 재현 가능하게 보고한다.

#### 2026-08-02 비-E2E 갱신

초기 snapshot 부재 기록 이후 `test/contract/contract-surface.test.js`에 checked-in
`node-public-contract.json` fixture와 Nest builder exact runtime member 비교를 추가했다. 현재
contract surface와 Nest module suite는 121/121 통과한다. 전체 common E2E/process 결과까지 하나의
snapshot으로 묶는 작업은 E2E 제외 조건에 따라 후속으로 남긴다.

#### ND-TEST-002 — documentation-regression이 요구하는 공통 문서가 현재 tree에 없음

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/ 및 framework/doc/framework/common/spec/30-implementation-gap.ko.md로 해석되는 documentation regression fixture
- Node test/CI 경로: framework/languages/node/test/contract/documentation-regression.test.js, framework/languages/node/scripts/run_node_runtime_gate.js, framework/languages/node/scripts/run_node_framework_ci_gate.js
- 실제 동작: verify:ci 실행 중 documentation-regression.test.js가 framework/doc/framework/common/spec/30-implementation-gap.ko.md를 ENOENT로 열지 못해 실패했다. 현재 작업 범위에는 공통 spec을 새로 만들거나 수정할 권한이 없으므로 이 문서는 생성하지 않았다.
- 기대 동작: test가 요구하는 authoritative 문서 경로와 실제 spec tree가 일치해야 한다. fixture가 historical path를 요구한다면 test/CI의 소유자가 현재 경로로 정리해야 하며, 이를 Node 구현 gap의 완료로 숨기지 않아야 한다.
- gap 판정 근거: current working tree의 verify:ci failure가 missing file을 직접 보고했다. 이는 Node production source의 동작 gap과 별도의 tree/CI blocker이다.
- 구체적인 수정 목록: (1) spec owner가 30-implementation-gap 문서의 존재와 위치를 확정한다. (2) 필요하면 documentation-regression fixture를 authoritative path로 갱신한다. (3) Node audit에서는 이 blocker가 해결되기 전 verify:ci를 green으로 기록하지 않는다.
- 필요한 회귀 test: ND-REG-008. documentation regression fixture가 missing path 없이 실행되고, missing document를 skip 또는 pass로 숨기지 않는지 검사한다.
- 선행 조건과 작업 순서: common spec owner의 path 결정 → fixture/CI 정렬 → verify:ci 재실행 순서로 진행한다.
- 구현 완료 evidence: current tree에서 documentation-regression이 ENOENT 없이 통과하고, 문서 path 변경 이력이 authoritative source와 연결된다.

#### 2026-08-02 비-E2E 갱신

documentation-regression fixture는 현재 Node gap ledger 경로를 읽도록 정렬되었고 17/17 통과한다.
따라서 이 항목의 ENOENT blocker는 해소되었다. `verify:ci` 전체는 이후 E2E scenario header gate에서
실패하므로 CI 전체 green으로 확대 해석하지 않는다.

#### ND-TEST-003 — full regression과 CI gate가 전체 계약·E2E 범위를 실행하지 않음

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/ 전체 14개 Config, framework/doc/framework/common/spec/의 Node contract
- Node script/CI 경로: framework/languages/node/package.json:6-30, framework/languages/node/scripts/run_node_framework_ci_gate.js:4-21, framework/languages/node/scripts/run_node_runtime_gate.js:37-44, .github/workflows/framework-node.yml:7-19 및 185-192
- 실제 동작: verify:ci는 channel-client.test.js, http-client.test.js, message-packet-name.test.js, stream-connector.test.js, stream-session-runtime.test.js, test/browser/stream-connector-chromium.test.js를 hard-coded skip한다. workflow는 common spec/e2e path를 filter에 포함하지 않고 aggregate Node process E2E도 호출하지 않는다. 현재 npm test는 deferred static E2E scenario-header gate에서 중단하며 coverage full gate는 이번 phase에서 실행하지 않았다.
- 기대 동작: skip은 명시적 blocked/diagnostic 결과로 보고되고 전체 성공 수에 포함되지 않아야 한다. CI path filter는 contract와 E2E inventory 변화를 감지해야 하며, Node aggregate process E2E와 package consumer gate가 CI에 연결되어야 한다.
- gap 판정 근거: current verify:ci의 skip list와 workflow command를 직접 확인했고, full test와 coverage가 완료되지 않았다. narrow M5/M6 pass는 이 gate의 대체 증거가 아니다.
- 구체적인 수정 목록: (1) skip된 test의 owner와 종료 조건을 기록한다. (2) skip을 PASS에서 분리한 결과 형식을 만든다. (3) common spec/e2e와 feature-map path를 workflow filter에 추가한다. (4) aggregate E2E와 packaged contract를 CI job에 연결한다. (5) deferred E2E inventory gate와 coverage를 bounded full gate로 연결한다.
- 필요한 회귀 test: ND-REG-008. full test, coverage, skip report, documentation fixture, aggregate E2E의 timeout·hang·skip을 성공으로 숨기지 않는지 검사한다.
- 선행 조건과 작업 순서: ND-IMP-*와 ND-E2E-IMP-* 해결 → skip policy 확정 → channel-client regression → CI workflow 연결 → full gate 순서로 진행한다.
- 구현 완료 evidence: npm test, verify:coverage, verify:ci, packaged contract, aggregate process E2E가 현재 tree에서 제한 시간 만료·skip·historical output 없이 종료하고, CI report가 전체 범위와 미실행 항목을 명시한다.

#### 2026-08-02 비-E2E 갱신

Node workflow push·pull request filter에 common guide path를 추가했고, build·typecheck·lint와
E2E·sample·native integration을 제외한 contract inventory 59개 파일(1001/1001)을 통과했다.
전체 `verify:ci`는 사용자 요청으로 제외한 E2E scenario inventory 171개 누락에서 exit 1이므로
ND-TEST-003의 E2E/aggregate 부분은 unresolved로 유지한다.

### 8.3 추가 또는 변경할 ND-REG-* regression ID

아래 ID는 이번 audit에서 제안했고, 비-E2E 범위의 기존 contract/runtime test에 반영한 회귀
검증 목록이다. E2E/process evidence가 필요한 부분은 별도 후속 범위로 남긴다.

| ID | 유지·변경·추가 내용 | 관련 gap | 완료 evidence |
|---|---|---|---|
| ND-REG-001 | Nest exact builder member, parameter/return type, missing configureInboundDispatch와 extra member를 declaration·runtime·package에서 비교 | ND-IMP-001, ND-TEST-001 | exact interface와 source/runtime/package same-set 검증 통과; process evidence는 E2E 후속 |
| ND-REG-002 | 13개 common error kind와 numeric value, exception surface, ProtocolError/cancellation/deadline mapping을 비교 | ND-IMP-002, ND-TEST-001 | public enum·exception·terminal mapping unit/runtime 검증 통과; client process evidence는 E2E 후속 |
| ND-REG-003 | unknown content type send/request의 ProtocolError, handler count 0, callback exactly-once, response body/status를 검사 | ND-IMP-003 | production dispatch unit path에서 no Buffer fallback과 no-handler 경로 확인; process evidence는 E2E 후속 |
| ND-REG-004 | root/workspace/lock/archive/native artifact version consistency와 clean consumer install을 검사 | ND-IMP-004 | `npm ls` clean 및 packaged consumer PASS; native role-server evidence는 E2E 후속 |
| ND-REG-005 | common 374 ID와 feature-map·selector exact set, alias, status를 비교 | ND-E2E-IMP-001, ND-E2E-IMP-003 | 누락·alias·중복·소유권 불일치 0 |
| ND-REG-006 | aggregate all의 Config 1-14 범위와 partial/diagnostic/source-only/N/A 제외, client·role evidence gate를 검사 | ND-E2E-IMP-002, ND-E2E-IMP-003 | 374개 ID 결과가 status와 evidence를 갖고 PASS가 정확히 계산됨 |
| ND-REG-007 | Client public package allowlist, role endpoint call, client result와 role evidence, terminal/callback/owner/generation/cleanup assertion을 검사 | ND-E2E-IMP-004, ND-E2E-IMP-005 | 실제 process에서 client가 Framework 내부 API를 호출하지 않음 |
| ND-REG-008 | documentation fixture, bounded full test, coverage, skip/timeout/hang, aggregate child exit policy를 검사 | ND-TEST-002, ND-TEST-003, ND-E2E-IMP-005 | ENOENT·timeout·skip·partial을 성공으로 숨기지 않고 CI가 종료 |

## 9. 완료 판정 checklist

### Contract

- [ ] common Framework spec과 Node exact interface의 target version을 기록했다.
- [ ] Nest builder의 missing 및 extra public member가 exact contract와 일치한다.
- [ ] parameter, return type, optional/default, timeout, cancellation, callback,
  ownership, disposal이 declaration과 runtime call path에서 일치한다.
- [ ] 13개 common error kind와 ProtocolError, cancellation, deadline mapping이
  client-visible 결과와 role server evidence에서 일치한다.
- [ ] unknown content type이 Buffer fallback 없이 ProtocolError로 종료되고
  handler와 callback exactly-once 계약이 확인된다.
- [ ] package export, version pin, lockfile, local archive, native artifact와
  clean consumer가 같은 정책을 사용한다.

### Runtime

- [ ] lifecycle state transition과 admission/preflight 순서가 실제 호출 순서로
  확인된다.
- [ ] owner/authority 경계, queue ownership, replay order, deadline 전파,
  cancellation reason, retry/rollback/cleanup 범위가 직접 assertion된다.
- [ ] concurrent shutdown, in-flight operation, process 종료 뒤 recovery,
  takeover, replay와 terminal mapping이 확인된다.
- [ ] HWM, stream, routing, actor join, relocation의 각 Node call path가
  좁은 unit pass와 구분되어 process evidence로 확인된다.

### E2E

- [ ] Config 1부터 Config 14까지의 common 374개 exact ID가 Node map과
  selector에 존재한다.
- [ ] Config 12와 Config 14가 aggregate 범위에 포함된다.
- [ ] alias, partial, unimplemented, transition, diagnostic_only, source_only,
  N/A가 PASS로 집계되지 않는다.
- [ ] client selector와 scenario dispatch가 실제 role server endpoint를 호출한다.
- [ ] Client는 공개 HTTP client 또는 stream connector만 사용하고 Framework
  내부 API, store, private record, raw frame을 직접 호출하지 않는다.
- [ ] client-visible result와 role server evidence가 모두 확인된다.
- [ ] terminal reason, callback count, owner, generation, cleanup을 직접
  assertion한다.
- [ ] historical log, source type 존재, unit test pass만으로 E2E 완료를
  표시하지 않는다.

### POSD·DDD review

- [ ] 모든 non-trivial card에 event, command, actor, failure, aggregate,
  invariant와 state owner를 적은 DDD design packet이 있다.
- [ ] 모든 non-trivial card가 첫 구현 전에 interface 수준의 대안 두 가지 이상을
  비교하고 선택 이유를 기록했다.
- [ ] `R0 design`을 frontier Codex model과 `high` 이상 reasoning으로 독립적으로
  검토했고 최종 decision이 `DESIGN ACCEPTED`다.
- [ ] `R1 candidate`에서 전체 candidate diff, production call path, targeted test와
  public/package surface를 frontier Codex model과 `high` 이상 reasoning으로 read-only 확인했고 최종
  decision이 `CLEAN`이다.
- [ ] `R2 integration`에서 frontier Codex model과 `high` 이상 reasoning으로 package·CI·process E2E와
  client·role server evidence를 검토했고 최종 decision이 `CLEAN`이다. Source compile이나 좁은 test
  pass를 대체 증거로 사용하지 않았다.
- [ ] `R3 final`에서 frontier Codex model과 `xhigh` 이상 reasoning으로 전체 변경 manifest와 ledger를
  다시 읽었고, 미해결
  Critical/High/Medium contract·POSD·DDD finding이 0이며 최종 decision이 `CLEAN`이다.
- [ ] 어려운 계약 충돌이나 concurrency·lifecycle race가 포함된 round는 reasoning을 `max`로 올렸다.
- [ ] Low finding은 `accepted`, `rejected-with-reason`, `fixed`, `deferred` 중 하나로
  처리되었고, `deferred` 항목에는 owner와 후속 card ID가 있다.
- [ ] 각 round 결과에 reviewer, model, reasoning, candidate, scope, decision,
  finding, evidence와 재실행 조건이 기록되어 있다.
- [ ] 각 round에서 current guide와 candidate 위험도에 맞는 Codex model 또는 대체 reviewer를
  선택했고, 실제 model ID와 model·reasoning 선택 근거를 기록했다.
- [ ] 필요한 reviewer를 바로 사용할 수 없었던 경우 `pending` 또는 `blocked` 사유와 그동안
  독립적으로 진행한 작업을 기록했으며, review 전에는 완료로 표시하지 않았다.

### Verification

- [ ] npm test와 verify:coverage가 timeout 또는 hang 없이 종료한다.
- [ ] verify:ci의 skip list와 documentation fixture가 현재 tree와 일치한다.
- [ ] common spec/e2e와 feature-map 변경이 CI path filter에 포함된다.
- [ ] packaged contract, 관련 process E2E, aggregate runner가 current run에서
  성공한다.
- [ ] 완료 보고에서 current run, historical log, timeout/hang/skip,
  environment blocker를 서로 구분한다.

전체 checklist는 E2E·sample·process evidence가 남아 있어 완료로 표시하지 않는다. 다만
비-E2E 반영 후 ND-IMP-001~004와 ND-TEST-002, ND-TEST-003의 비-E2E 조건은 fresh evidence로
확인했고, ND-TEST-001은 checked-in snapshot과 exact member 비교를 확보했다. ND-E2E-IMP-001~005,
ND-TEST-003의 E2E/aggregate 조건과 ND-REG-005~008은 사용자가 제외한 후속 범위로 유지한다.
ND-REG-001~004는 현재 contract, error, content-type과 package gate의 회귀 입력으로 반영했다.

## 10. Framework spec 완료 gate와 sample 착수 조건

Framework spec 작업과 sample 작업은 한 문서에서 관리하지만 동시에 진행하지 않는다. 먼저 1~9절의
`ND-IMP-*`, `ND-E2E-IMP-*`, `ND-TEST-*`와 `ND-REG-*`를 완료한다. 다음 조건을 모두 만족한
candidate에서만 sample 단계를 시작한다.

- 9절 Contract, Runtime, E2E, POSD·DDD review와 Verification checklist가 모두 완료됐다.
- `ND-IMP-001`~`004`, `ND-E2E-IMP-001`~`005`, `ND-TEST-001`~`003`이 완료 evidence와
  함께 닫혔다.
- `ND-REG-001`~`008`이 통과하고 full test, coverage, CI, package consumer와 aggregate process E2E가
  같은 candidate에서 성공했다.
- R3 final review의 미해결 Critical·High·Medium finding이 0개이고 최종 decision이 `CLEAN`이다.
- 기준 commit 또는 immutable candidate SHA, 전체 변경 manifest와 실행 artifact가 기록됐다.

조건이 하나라도 남아 있으면 sample 단계의 상태는 `Spec gate 대기`다. Sample 문서, source, test와
runner를 수정하지 않고 Framework spec 단계의 미완료 항목을 먼저 처리한다. Gate 통과 뒤 sample
regression에서 새로운 Framework contract 또는 runtime gap이 발견되면 sample에서 우회하지 않고 새
`ND-*` 작업으로 돌아간다.

## 11. 공통 sample 계약 차이와 수정 작업

이 절은 10절 gate를 통과한 뒤 실행한다. 현재 기록된 차이는 조사 결과이며, 아직 sample 구현 착수를
승인한 상태가 아니다.

### 11.1 목적과 완료 조건

이 문서는 [Framework 공통 sample](../../framework/common/sample/README.ko.md)과
`framework/languages/node/samples/`의 Node.js 구현을 대조해 차이를 기록하고, 그 차이를 공통 계약에
맞게 수정하는 순서를 정한다. 과거 실행 기록은 현재 source와 test로 다시 확인하며 완료 evidence로
대체하지 않는다.

검토 범위는 다음과 같다.

- message 이름, field 이름과 타입, optional·null·array·enum 의미
- request/reply, one-way send, notify와 publish의 호출 방식과 완료 의미
- Bingo의 Protobuf schema와 나머지 sample의 typed JSON wire 구조
- topology, Actor·Spot state owner, session binding, relocation과 Message Follow 경계
- `Client`, `Shared`, `Server`와 `Domain`, `Application`, `Infrastructure` 책임 배치
- Node의 decorator와 module scan을 사용하는 handler 자동 등록
- runner의 build, 실행별 resource, readiness, client self-check, server evidence와 cleanup
- shell·PowerShell 실행 목록, Chromium 경계, contract regression과 CI 강제 범위

다음 조건을 모두 만족해야 이 ledger를 완료로 표시할 수 있다.

1. 공통 sample 7종의 message·field·transport 목록과 Node shared contract가 한 행씩 대응한다.
2. 공통 문서에 없는 Node message는 `internal-only`, `test/evidence-only`, `계약 후보` 가운데 하나로
   분류한다. 다른 언어 구현만을 근거로 공통 계약에 추가하지 않는다.
3. wire와 runtime gap은 책임을 소유한 shared contract, handler, application 또는 Framework에서
   수정한다. client나 sample 호출부에 raw frame, private API, 수동 JSON codec 또는 우회 helper를
   추가하지 않는다.
4. 모든 sample이 공통 `Client → Shared → Server` logical structure와 역할별 책임을 유지한다.
5. Node에서는 decorator·metadata scan으로 handler를 자동 등록한다. C++의 compile-time 명시 등록
   방식을 Node sample에 도입하지 않는다.
6. 일곱 runner가 client-visible response·push와 server-side state·ownership evidence를 같은 실행에서
   확인하고, 성공과 실패 모두에서 자신이 만든 resource를 정리한다.
7. sample contract regression, Node Framework regression, package consumer 검증과 실제 process smoke가
   모두 현재 package로 통과한다.
8. 마지막 독립 review에서 기록되지 않은 Node sample spec gap이 0개다.

Sample 구현은 10절의 Framework spec 완료 gate를 통과한 뒤 시작한다. Gate를 통과하기 전에는
`NS-IMP-*`, `NS-TEST-*`와 `NS-REG-*`를 설계·구현 완료로 진행하지 않는다. Gate 통과 뒤에는 각
작업 항목의 `contract 선행` 조건을 먼저 닫는다.

### 11.2 기준 문서와 조사 범위

공통 sample 문서는 workflow, topology, application message와 self-check를 정의한다. Framework 기능의
public contract는 공통 Framework spec과 Node exact interface가 소유한다. Sample 문서나 다른 언어
구현은 새 Framework public API를 추가하는 근거가 아니다.

| 구분 | 기준 위치 | 이번 비교에서 확인할 내용 |
|---|---|---|
| 작성 원칙 | `doc/principal/documentation/sample-writing-guide.ko.md` | sample 구조, message declaration, sequence, contract review와 완료 판정 |
| 공통 sample index | `framework/doc/framework/common/sample/README.ko.md` | codec, topology, naming, handler 등록, runner와 공통 금지사항 |
| 공통 sample 계약 | `bingo`, `tictactoe`, `supportchat`, `deliverydispatch`, `event/shoppingmall`, `event/gamequest`, `zoneworld` | 역할, message, state, 흐름, self-check와 완료 evidence |
| Framework 공통 spec | `framework/doc/framework/common/spec/` | routing, Actor·Spot, session, relocation, failure와 error의 목표 계약 |
| Node exact interface | `framework/doc/framework/common/spec/server/languages/node/interfaces/` | Node public type, builder, handler, async와 lifecycle 표현 |
| Node sample source | `framework/languages/node/samples/` | shared contract, server/client path, structure와 runner |
| Node sample regression | `framework/languages/node/test/contract/sample*.test.js`와 관련 domain test | static 구조, handler, lifecycle, runner와 wire gate |
| Node package·CI | `framework/languages/node/package.json`, `scripts/`, `.github/workflows/framework-node.yml` | package version, full gate, sample 실행과 path filter |
| Framework spec 단계 | 이 문서 1~10절 | Sample 착수 전에 완료해야 하는 production contract, package, E2E와 regression gate |
| Audit 기준 log | [`log/2026-08-02-sample-gap-audit-baseline.ko.md`](log/2026-08-02-sample-gap-audit-baseline.ko.md) | commit, working-tree fingerprint, 명령, exit code와 test 결과 |

대상 sample과 핵심 판정 범위는 다음과 같다.

| Sample | 공통 핵심 흐름 | Node에서 직접 확인할 결과 |
|---|---|---|
| Bingo | Protobuf authentication, matching, room, reward, relocation과 cleanup | 공통 `.proto` subset, codec extension, Actor lifecycle와 Chromium result |
| TicTacToe | HTTP create, authentication, room join, turn, milestone, leave와 destroy | one-way leave, Logical Multicast, Entry Spot destroy와 manual topology |
| SupportChat | agent availability, conversation 생성·join, chat·typing, idle close와 reconnect | metadata routing, one-way typing, state sequence와 binding 교체 |
| DeliveryDispatch | 배송 생성, offer·decision, status, deadline과 reassignment | transport field 금지, timestamp wire shape, late decision과 evidence |
| ShoppingMall | order workflow, durable event, projection, idempotency와 compensation | 접수 응답, command/event 경계, replay와 failure sequence |
| GameQuest | session, gameplay event, projection, replay·reconcile와 dedupe | action 목록, typed payload, domain event와 Store adapter 경계 |
| ZoneWorld | movement, zone state, bot, border, relocation, fanout과 Ops | 공통 message boundary, application NodeId와 runtime RID 분리, browser evidence |

### 11.3 선행 조건과 현재 검증 상태

#### 11.3.1 Framework spec 완료 gate 적용 기준

Sample에서 Framework 구현 gap을 application code로 우회하면 안 된다. 10절 gate가 완료되기 전에는
message declaration, logical structure, regression과 문서 변경을 포함한 sample 수정 작업을 시작하지
않는다. 모든 `NS-*` 작업의 현재 실행 상태는 `Spec gate 대기`다.

10절 gate를 통과한 뒤 regression이 새로운 Framework 실패를 재현하면 sample에서 우회하지 않는다.
새 `ND-*` 작업으로 분리하고 Framework 단계로 돌아가 해당 gap을 닫은 뒤 sample 작업을 재개한다.

#### 11.3.2 Audit 기준과 현재 판정

최초 조사에 사용한 `HEAD`, sample 입력 fingerprint, 실행 명령과 상세 결과는
[`2026-08-02 sample gap audit 기준 log`](log/2026-08-02-sample-gap-audit-baseline.ko.md)에 고정했다.
이 log는 최초 gap을 설명하는 historical baseline이며 현재 candidate의 완료 evidence가 아니다.

현재 판정은 sample contract suite 실패, 통합 sample runner 미통과와 Sample 계약 충족 미판정이다.
10절 gate를 통과한 candidate에서는 Node Framework production source, dependency lock, package와 native
artifact hash까지 포함한 immutable manifest를 새로 기록하고 build, contract test와 실제 process
runner를 모두 다시 실행한다. Baseline fingerprint가 같더라도 candidate `HEAD` 또는 manifest가 다르면
과거 결과를 재사용하지 않는다.

#### 11.3.3 현재 계약 목록과 구현 차이

| ID | 현재 근거 | 초기 판정 |
|---|---|---|
| `NS-IMP-001` | TicTacToe와 SupportChat의 one-way handler가 각각 `LeaveGameReq`, `SetTypingReq` wire 이름을 사용한다. | Sample wire gap |
| `NS-IMP-002` | DeliveryDispatch message에 session route·Attempt가 노출되고 timestamp field와 type이 다르다. | Framework 경계·Sample wire gap |
| `NS-IMP-003` | GameQuest action 목록, `JoinSessionRes`, close message와 gameplay payload가 공통 계약과 다르다. | Sample wire·codec gap |
| `NS-IMP-004` | ShoppingMall response, workflow command와 durable event 경계가 공통 계약과 다르다. | Sample 업무 계약 gap |
| `NS-IMP-005` | SupportChat conversation create·response와 typing message가 공통 계약과 다르다. | Sample wire·lifecycle input gap |
| `NS-IMP-006` | TicTacToe rejected join completion이 client-visible terminal failure로 전달되지 않는다. | Sample 실패 결과 gap |
| `NS-IMP-007` | ZoneWorld game client message가 NodeId·relocation 결과를 노출하고 내부 message가 공통 typed boundary와 다르다. | Framework routing 경계·Sample wire gap |
| `NS-IMP-008` | Bingo, TicTacToe, SupportChat과 ZoneWorld는 `Shared/Configuration` 대신 client/server별 설정을 두며 sample마다 logical tree가 다르다. | 구조 parity gap |
| `NS-IMP-009` | `samples/README.ko.md`의 Framework version이 root의 bindings archive와 다르다. Shell·PowerShell runner는 일곱 sample을 포함하지만 Bingo와 TicTacToe의 완료 marker가 공통 문서와 다르다. | documentation·package·runner marker gap |
| `NS-TEST-001` | 기존 sample test는 구조·특정 symbol을 넓게 검사하지만 공통 문서의 전체 message·field·transport 목록을 직접 비교하지 않는다. | test gap |

### 11.4 차이 판정 기준

| 상태 | 의미 | 다음 행동 |
|---|---|---|
| `확인` | 공통 계약과 Node source 또는 process path의 차이가 재현됐다. | 실패 regression을 먼저 고정하고 책임 owner에서 수정한다. |
| `contract 선행` | 공통 문서의 public/internal 범위나 transport 의미가 모호하다. | 구현을 바꾸지 않고 공통 문서와 관련 spec을 먼저 review한다. |
| `test gap` | 구현이 맞을 수 있지만 현재 test가 해당 계약을 직접 판정하지 않는다. | 정확한 계약 목록, serialized wire 또는 process evidence를 추가한다. |
| `documentation gap` | source와 runner의 기준을 문서가 현재 값으로 설명하지 않는다. | source owner와 version 기준을 확인한 뒤 문서를 갱신한다. |
| `blocked` | 선행 Framework public contract, package 또는 runtime 의미가 완료되지 않았다. | sample에서 우회하지 않고 10절 Framework spec gate가 닫힐 때까지 유지한다. |
| `충족` | source, wire, process evidence와 regression이 같은 계약을 증명한다. | 근거 명령과 artifact를 기록한다. |

Build 성공, source type 존재와 정적 문자열 test만으로 `충족`을 부여하지 않는다. Handler invocation,
state commit, failure, cleanup과 client-visible 결과까지 같은 실행에서 확인한다.

#### 11.4.1 작업 항목의 책임과 현재 상태

작업 담당자는 현재 sample 정렬 작업자다. 공통 sample 계약 담당자가 필요한 항목은 contract review를
마친 뒤 구현한다. 아래 작업은 모두 10절 Framework spec 완료 gate를 공통 선행 조건으로 가지며, gate
통과 전 상태는 `Spec gate 대기`다.

| ID | 분류 | 담당 | 추가 선행 조건 | 현재 실행 상태 | 완료 evidence |
|---|---|---|---|---|---|
| `NS-IMP-001` | Sample wire·Framework transport | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-002`~`004`, client process |
| `NS-IMP-002` | Framework routing 경계·Sample wire | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-005`, DeliveryDispatch process |
| `NS-IMP-003` | Sample wire·codec 책임 | 작업 담당자 + 공통 sample 계약 담당자 | Contract review | Spec gate 대기 | `NS-REG-006`, GameQuest process |
| `NS-IMP-004` | Sample 업무 계약 | 작업 담당자 + 공통 sample 계약 담당자 | Contract review | Spec gate 대기 | `NS-REG-007`, ShoppingMall process |
| `NS-IMP-005` | Sample 업무 계약·Actor lifecycle input | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-004`, `008`, SupportChat process |
| `NS-IMP-006` | Sample 실패 결과·Actor join completion | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-003`과 client-visible failure test |
| `NS-IMP-007` | Sample wire·Framework routing 경계 | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-009`, ZoneWorld process |
| `NS-IMP-008` | 언어별 logical structure | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-011`, 구조 mapping |
| `NS-IMP-009` | 문서·runner·package | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-013`, `016`, runner result |
| `NS-TEST-001` | Test와 process evidence | 작업 담당자 | 없음 | Spec gate 대기 | `NS-REG-001`~`017`, CI result |

### 11.5 구현에서 확인된 차이

#### NS-IMP-001 — one-way message 이름과 handler 의미가 공통 계약과 다름

**현재 판정: `확인`.**

- 분류: `LeaveGameMsg`와 `SetTypingMsg` 이름은 sample wire 계약이고, response 없이 끝나는 send 의미는
  Framework public contract다.
- 계약 근거: [TicTacToe §6.2](../../framework/common/sample/tictactoe/README.ko.md#62-room-request와-publish-event),
  [SupportChat §6.2](../../framework/common/sample/supportchat/README.ko.md#62-conversation-request와-one-way-send),
  [상호작용 모델 §2](../../framework/common/spec/03-interaction-model.ko.md#2-공통-모델),
  [Node Spot interface](../../framework/common/spec/server/languages/node/interfaces/04-spots.ko.md)
- 현재 source: [TicTacToe messages.ts](../../../../framework/languages/node/samples/TicTacToe.Ts/Shared/Contracts/messages.ts):137,
  [SupportChat messages.ts](../../../../framework/languages/node/samples/SupportChat.Ts/Shared/Contracts/messages.ts):54
- 검증: type, packet name과 send handler를 source에서 대조했다. 실제 wire name과 response 부재는
  `NS-REG-002`~`004`와 process 실행으로 다시 확인한다.

공통 TicTacToe는 `LeaveGameMsg`, SupportChat은 `SetTypingMsg`를 one-way send로 선언한다. 현재 Node
shared contract, packet name, handler와 client는 각각 `LeaveGameReq`, `SetTypingReq`를 사용한다.
실제 handler type은 send이므로 transport는 one-way지만 wire 이름이 request를 나타낸다.

수정할 때 shared type, packet name, decorator, client submission, log와 regression을 한 번에 바꾼다.
이름만 `Msg`로 바꾸고 request/reply를 추가하거나, 호환 wrapper를 두 이름으로 유지하지 않는다.

#### NS-IMP-002 — DeliveryDispatch가 application message에 route·Attempt를 노출하고 timestamp 계약이 다름

**현재 판정: `확인`.**

- 분류: session route와 owner 위치 은닉은 Framework public contract이고, Attempt와 timestamp field는
  DeliveryDispatch sample wire 계약이다.
- 계약 근거: [DeliveryDispatch §6](../../framework/common/sample/deliverydispatch/README.ko.md#6-message-계약),
  [상호작용 모델 §8](../../framework/common/spec/03-interaction-model.ko.md#8-stream-session),
  [Session–Actor dispatch](../../framework/common/spec/20-session-actor-dispatch.ko.md),
  [Node Actor와 session binding interface](../../framework/common/spec/server/languages/node/interfaces/05-actors.ko.md)
- 현재 source: [DeliveryDispatch messages.ts](../../../../framework/languages/node/samples/DeliveryDispatch.Ts/Shared/Contracts/messages.ts):18
- 검증: `sessionRoute`, `attempt`, `occurredAt: string`을 source에서 확인했다. Runtime binding과 JSON
  number wire는 `NS-REG-005`와 DeliveryDispatch process 실행으로 확인한다.

Node `BindCourierReq/Res`와 `BindCourierSessionReq/Res`는 `sessionRoute`를 노출한다. 공통 계약은 session
binding을 Framework가 관리하며 application response에는 courierId만 남기도록 요구한다.
`OfferDeliveryNotify`와 `CourierDecisionMsg`에도 공통 client-facing 계약에 없는 `attempt`가 있고,
status message는 `occurredAt: string`을 사용하지만 공통 계약은 `occurredAtUnixMs: int64`다.

Attempt는 Dispatch application state에 유지하고, 늦은 decision 판정에 필요한 correlation은 handler가
소유한 offer state에서 해결한다. Session route나 ActorRef를 새 DTO로 옮기는 방식은 허용하지 않는다.
Timestamp는 실제 JSON wire number와 ordering assertion을 함께 고정한다.

#### NS-IMP-003 — GameQuest의 action 목록과 payload codec 책임이 공통 계약과 다름

**현재 판정: `확인`과 `contract 선행`.**

- 분류: `GameplayMsg`와 action 목록은 sample wire·업무 계약이다. Message마다 codec을 처리하지 않는
  책임은 Framework public contract다. 추가 action의 유지 여부는 계약 후보라서 공통 sample review가
  필요하다.
- 계약 근거: [GameQuest §6](../../framework/common/sample/event/gamequest.ko.md#6-message-계약),
  [Node codec 책임](../../framework/common/spec/server/languages/node/interfaces/03-location-observability.ko.md),
  [Node Spot interface](../../framework/common/spec/server/languages/node/interfaces/04-spots.ko.md)
- 현재 source: [GameQuest messages.ts](../../../../framework/languages/node/samples/GameQuest.Ts/Shared/Contracts/messages.ts):74,
  [quest-domain.ts](../../../../framework/languages/node/samples/GameQuest.Ts/Server/QuestMission/Domain/quest-domain.ts):240,
  [quest-progress-store.ts](../../../../framework/languages/node/samples/GameQuest.Ts/Server/Shared/Store/quest-progress-store.ts):270
- 검증: `payload: number[]`, `TextEncoder`, `TextDecoder`와 수동 JSON 변환을 source에서 확인했다.
  `NS-REG-006`과 GameQuest process 실행 전에는 wire 수정 완료로 판정하지 않는다.

Node에는 공통 문서에 없는 `CompleteMissionReq/Res`, `UnlockFeatureReq/Res`, projection 삭제·재생성과
deactivate message가 있다. `JoinSessionRes`에는 공통 `playerId`가 없고, 공통 문서가 선언한
`ClosePlayerQuestMsg`도 없다. `GameplayMsg.payload`와 stored event payload는 `number[]`이며 shared
contract와 Domain에서 `TextEncoder`, `TextDecoder`, `JSON.stringify`, `JSON.parse`로 직접 변환한다.

먼저 client-facing action, maintenance-only command와 Store record를 분리한다. 공통 계약에 없는 action을
유지하려면 공통 sample 계약 변경 review가 선행되어야 하며, 다른 언어에 있다는 사실만으로 승인하지
않는다. Gameplay payload는 Framework typed JSON codec이 object를 처리하게 하고, application handler와
Domain에서 encode/decode helper를 제거한다. Domain event의 `*Event` 이름과 transport message를 별도
목록으로 관리한다.

#### NS-IMP-004 — ShoppingMall 접수 응답과 workflow command가 공통 message 계약과 다름

**현재 판정: `확인`과 `contract 선행`.**

- 분류: response, workflow command와 durable event는 ShoppingMall sample 업무 계약이다. 이 card는 새
  Framework public API를 요구하지 않는다. Extra command를 shared contract로 유지하려면 공통 sample
  review가 필요하다.
- 계약 근거: [ShoppingMall §6](../../framework/common/sample/event/shoppingmall.ko.md#6-message-계약),
  [Node Spot interface](../../framework/common/spec/server/languages/node/interfaces/04-spots.ko.md)
- 현재 source: [ShoppingMall messages.ts](../../../../framework/languages/node/samples/ShoppingMall.Ts/Shared/Contracts/messages.ts):27,
  [order-store.ts](../../../../framework/languages/node/samples/ShoppingMall.Ts/Server/Shared/Store/order-store.ts):105
- 검증: `StartOrderRes`, workflow request와 prepare/fence message 목록을 source에서 대조했다.
  `NS-REG-007`과 ShoppingMall process 실행으로 idempotency·compensation 결과를 확인한다.

공통 `StartOrderRes`는 `orderId`와 `state: OrderState`를 반환하지만 Node는 `orderId`와 `status: string`을
반환한다. 공통 workflow request의 `sourceCommandId`가 Node request에 없으며 Node는
`PrepareInventoryReservedReq`, `PrepareInventoryEffectReq`, `VerifyExpectedVersionFenceReq`를 별도로
전송한다. 반대로 공통 `ReserveInventory`, `ReleaseInventory`, `AuthorizePayment` message는 shared
contract에 없다.

Client 접수 response는 공통 shape로 맞춘다. 내부 command는 process·Spot boundary를 넘는 application
message인지 Domain 내부 command인지 먼저 분류하고, public/shared message이면 공통 계약과 같은 이름과
완료 의미를 사용한다. Version fence와 effect interruption은 runner evidence 또는 internal test로 남길
수 있지만 공통 workflow를 대체하는 public 계약으로 승격하지 않는다.

#### NS-IMP-005 — SupportChat conversation 생성 계약과 one-way typing 경계가 다름

**현재 판정: `확인`.**

- 분류: conversation response와 typing message는 SupportChat sample wire 계약이다. Actor·Spot creation
  payload 전달 방식은 Framework lifecycle contract이고 payload 자체는 sample application data다.
- 계약 근거: [SupportChat §6](../../framework/common/sample/supportchat/README.ko.md#6-message-계약),
  [Actor model §6.4](../../framework/common/spec/14-actor-model.ko.md#64-creation-request와-factory-실행),
  [Node Actor interface](../../framework/common/spec/server/languages/node/interfaces/05-actors.ko.md)
- 현재 source: [SupportChat messages.ts](../../../../framework/languages/node/samples/SupportChat.Ts/Shared/Contracts/messages.ts):27,
  [conversation-create-request.ts](../../../../framework/languages/node/samples/SupportChat.Ts/Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation-create-request.ts):1
- 검증: response field, local creation type와 `isRetriable`을 source에서 확인했다. `NS-REG-004`,
  `NS-REG-008`과 SupportChat process 실행으로 최종 wire를 확인한다.

공통 계약은 `ConversationCreateReq/Res`에 생성 시각과 전체 `ConversationState`를 고정한다. Node는
Spot create payload를 `ConversationCreateRequest`라는 Infrastructure local type으로 선언하고,
`OpenConversationApiRes`에서는 `conversationId`와 status만 반환한다. `SupportUserActorCreateReq`도 공통
message 목록 밖에 있다. `JoinConversationFailedNotify`에는 공통 문서에 없는 `isRetriable` field가
추가되어 있다.

Spot create payload가 application contract이면 `Shared/Contracts`의 공통 message로 이동하고 전체 state
완료 의미를 맞춘다. Actor create payload가 Framework lifecycle input이라면 internal-only로 표시하되
모든 Node role이 같은 declaration을 사용해야 한다. `isRetriable`은 공통 error 계약과 중복되는지 먼저
review하고, source에 있다는 이유로 client contract에 남기지 않는다. `SetTypingReq` 수정은
`NS-IMP-001`이 소유한다.

#### NS-IMP-006 — TicTacToe leave 이름과 client-visible join 실패 결과가 다름

**현재 판정: `확인`과 `test gap`.**

- 분류: leave 이름과 join terminal result는 TicTacToe sample 계약이다. Deferred join completion의 상태와
  optional reply는 Framework public contract다.
- 계약 근거: [TicTacToe §6.2와 §7.1](../../framework/common/sample/tictactoe/README.ko.md#62-room-request와-publish-event),
  [Node Actor interface](../../framework/common/spec/server/languages/node/interfaces/05-actors.ko.md)
- 현재 source: [tic-tac-toe-game-spot.ts](../../../../framework/languages/node/samples/TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe-game-spot.ts):74,
  [play-actor.ts](../../../../framework/languages/node/samples/TicTacToe.Ts/Server/Play/Infrastructure/ZLink/Actors/play-actor.ts):66
- 검증: Spot은 rejected reply를 만들지만 `PlayActor.onJoinCompleted()`가 rejected completion을 client에
  전달하지 않는 것을 확인했다. 공통 계약은 notify와 typed error response를 모두 허용하므로 특정
  notify type의 부재만으로 수정 방향을 정하지 않는다.

Node의 create, join, state와 `PlayerWinMilestoneEvent`는 공통 흐름과 대체로 대응한다. 그러나 one-way
leave는 `LeaveGameReq`를 사용한다. Actor join rejection reply는 만들어지지만 현재 Actor completion
callback이 rejected 결과를 반환하므로 client는 terminal error나 notify를 받지 못한다.

Leave 수정은 `NS-IMP-001`에서 수행한다. Join 실패는 `JoinGameFailedNotify` push 또는 typed error response
가운데 현재 public API로 정확히 전달할 수 있는 방식을 선택하고 client self-check를 추가한다.
Entry Spot의 destroy 순서와 duplicate destroy no-op은 기존 lifecycle test를 유지하면서 실제 runner
evidence로 확인한다.

#### NS-IMP-007 — ZoneWorld가 공통 movement·push message boundary와 wire shape를 사용하지 않음

**현재 판정: `확인`.**

- 분류: game·내부 routing message의 이름과 field는 ZoneWorld sample 계약이다. NodeId와 RID 분리,
  global ID routing과 typed Spot·Actor call은 Framework public contract다.
- 계약 근거: [ZoneWorld §6](../../framework/common/sample/zoneworld/README.ko.md#6-message-계약),
  [상호작용 모델 §2](../../framework/common/spec/03-interaction-model.ko.md#2-공통-모델),
  [Node Channel interface](../../framework/common/spec/server/languages/node/interfaces/02-channel-messaging.ko.md),
  [Node Spot interface](../../framework/common/spec/server/languages/node/interfaces/04-spots.ko.md)
- 현재 source: [ZoneWorld contracts.ts](../../../../framework/languages/node/samples/ZoneWorld/Shared/contracts.ts):14,
  [player-actor.ts](../../../../framework/languages/node/samples/ZoneWorld/Server/ZoneNode/Infrastructure/ZLink/Actors/player-actor.ts):31,
  [zone-runtime-handlers.ts](../../../../framework/languages/node/samples/ZoneWorld/Server/ZoneNode/Infrastructure/ZLink/Handlers/zone-runtime-handlers.ts):61
- 검증: client field, `BotTickReq/Res`, `UpdateZonePositionMsg`, `unknown` wrapper와 constructor-name switch를
  source에서 확인했다. `NS-REG-009`와 ZoneWorld process 실행으로 wire와 relocation 결과를 확인한다.

Node `JoinWorldRes`, `EnterWorldRes`, `EnterZoneRes`는 공통 client/application 계약에 없는 `nodeId`를
포함하고 `ZoneChangedNotify`는 `nodeId`와 `transferred`를 노출한다. NodeId는 Ops application identity로
사용할 수 있지만 game client가 owner 위치나 relocation 결과를 받는 field가 되어서는 안 된다.

같은 zone 이동은 공통 `UpdatePositionMsg` 대신 Infrastructure local `UpdateZonePositionMsg`를 사용한다.
Bot tick은 `BotTickReq/Res`, actor push는 payload가 `unknown`인 `DeliverZoneNotification` wrapper로
처리하며 공통 `BotTickMsg`, `DeliverZoneStateMsg`, `DeliverWorldAnnounceMsg`와 다르다.

Shared contract에 공통 message를 선언하고 typed handler가 해당 message를 직접 처리하게 한다.
`unknown` payload와 constructor 이름 switch를 제거하고, application NodeId와 Framework RID를 분리한다.
Relocation 성공 여부는 runner evidence에서 확인하며 browser wire field로 보내지 않는다.

#### NS-IMP-008 — sample별 logical module 구조가 공통 구조와 다름

**현재 판정: `확인`.**

- 분류: 언어별 구현이 유지할 logical module 배치는 sample 작성 guide의 parity contract다. Framework
  public API나 runtime 내부 구조를 새로 정의하지 않는다.
- 계약 근거: [Sample 작성 guide §5.9](../../../../doc/principal/documentation/sample-writing-guide.ko.md#59-구현-구조),
  [언어별 표현 기준](../../framework/common/sample/languages/README.ko.md)
- 현재 source: [Node samples](../../../../framework/languages/node/samples/)
- 검증: 일곱 sample의 directory와 logical role을 비교했다. G1에서 공통 역할별 mapping을 고정하고
  `NS-REG-011`로 누락과 불필요한 업무 layer를 확인한다.

일곱 sample 모두 최상위 `Client`, `Shared`, `Server`는 있지만 내부 구조가 공통 문서와 일관되지 않다.
Bingo, TicTacToe와 SupportChat은 configuration을 Client와 Server에 나눠 두고, ZoneWorld는
`Shared/Configuration`이 없다. DeliveryDispatch에는 공통 역할과 별도로 `DispatchApi`,
`DispatchCenter`, `Courier`, `Probe`가 병렬로 있고 역할별 Domain/Application/Infrastructure 경계를
찾기 어렵다. GameQuest와 ShoppingMall에는 runtime instance 이름인 `ApiA/ApiB`, `MissionA/MissionB`,
`WorkflowA/WorkflowB`가 logical role module과 함께 배치되어 있다.

공통 문서의 역할을 기준으로 module mapping 표를 먼저 작성한다. Process entrypoint는 역할 module의
Program에 두고 A/B instance는 runner configuration으로 표현한다. Shared configuration과 wire contract를
한 위치에서 찾을 수 있게 한다. Directory를 기계적으로 늘리지 않으며, 한 파일에 여러 type을 두더라도
Domain/Application/Infrastructure 책임과 의존 방향은 유지한다.

#### NS-IMP-009 — Node sample README, completion marker와 package 기준이 현재 상태와 다름

**현재 판정: `documentation gap`과 `test gap`.**

- 분류: version·실행 방법은 Node sample 문서 책임이고, completion marker와 cleanup은 공통 sample과
  Node runner 계약이다. Package version 확정만 `ND-IMP-004`에 의존한다.
- 계약 근거: [Node sample README](../../../../framework/languages/node/samples/README.ko.md),
  [공통 sample runner 정책](../../framework/common/sample/README.ko.md),
  [Bingo 완료 기준](../../framework/common/sample/bingo/README.ko.md#11-완료-기준),
  [TicTacToe runner](../../framework/common/sample/tictactoe/README.ko.md#10-runner와-smoke-실행)
- 현재 source: [Node package.json](../../../../framework/languages/node/package.json):57,
  [Node sample README](../../../../framework/languages/node/samples/README.ko.md):3,
  [run_samples.sh](../../../../framework/languages/node/samples/run_samples.sh),
  [run_samples.ps1](../../../../framework/languages/node/samples/run_samples.ps1)
- 검증: version pin, 두 runner의 실행 목록과 client marker를 source에서 확인했다. `NS-REG-013`,
  `NS-REG-016`과 실제 runner exit/result로 완료를 판정한다.

Node sample README는 10.0.0 public API를 사용한다고 설명하지만 현재 root dependency는 local 11.1.0
archive를 가리킨다. 정확한 package version은 선행 `ND-IMP-*`에서 확정한 뒤 문서를 갱신한다.

Shell과 PowerShell 통합 runner는 일곱 sample을 모두 포함하지만 marker 정책이 일관되지 않다. Bingo는
`bingo=completed`만 출력하고 README의 `PASS Bingo.Ts`가 없으며, TicTacToe는 `PASS TicTacToe.Ts`만
출력하고 공통 `tictactoe=completed`가 없다. Sample별 phase marker는 실제 runner evidence로만 사용하고,
공통 completion marker와 `PASS <Sample>`의 역할을 구분한다.

#### NS-TEST-001 — 공통 sample 전체의 계약과 process 결과를 직접 확인하는 test가 없음

**현재 판정: `test gap`.**

- 분류: 공통 sample wire와 flow를 Node 구현에 대응시키는 contract regression 및 process evidence gap이다.
- 계약 근거: [Sample 작성 guide §8](../../../../doc/principal/documentation/sample-writing-guide.ko.md#8-spec-기능-대조와-최종-review),
  [공통 sample 완료 정책](../../framework/common/sample/README.ko.md)
- 현재 source: [Node sample contract tests](../../../../framework/languages/node/test/contract/),
  [2026-08-02 audit log](log/2026-08-02-sample-gap-audit-baseline.ko.md)
- 검증: 기준 audit에서 sample contract suite와 aggregate process smoke가 통과하지 않았다. 자세한
  실행 결과와 실패 원인은 [sample gap audit 기준 log](log/2026-08-02-sample-gap-audit-baseline.ko.md)가
  소유한다.

현재 sample regression은 파일 존재, 일부 symbol, topology와 lifecycle을 넓게 검사하지만 공통 문서의
모든 message·field·transport kind·state flow를 Node shared contract와 직접 비교하지 않는다. 일부 test는
DeliveryDispatch의 `SampleNames.routeMesh`처럼 이전 구조를 정답으로 고정한다.

Machine-readable 계약 목록 또는 동일한 정보를 가진 contract fixture를 만들고, static contract와 실제
serialized payload를 함께 검사한다. Static test는 process smoke를 대체하지 않으며, runner가 client
self-check와 server evidence를 모두 확인하는지도 별도 gate로 검증한다.

### 11.6 Sample별 구현 위치와 검토 항목

| Sample | 우선 읽을 Node path | 구현 단계에서 확인할 계약 | 완료 evidence |
|---|---|---|---|
| Bingo | `Shared/Contracts/bingo_messages.proto`, `protobuf-codec.ts`, `Server/Play`, `Client`, `Runner` | 공통 Protobuf subset, extension 등록, transfer/control type 범위, room·Actor lifecycle | Chromium response·push, reward, relocation와 Entry Spot destroy evidence |
| TicTacToe | `Shared/Contracts/messages.ts`, `Server/Api`, `Server/Play`, `Client`, `Runner` | one-way leave, join failure, turn, milestone publish와 manual topology | HTTP·STREAM payload, milestone, leave·destroy와 Redis cleanup |
| SupportChat | `Shared/Contracts/messages.ts`, `Server/Support`, `Server/Session`, `Client`, `Runner` | conversation create, metadata, typing, MessageSeq, idle close와 reconnect | assignment·chat·typing·close ordering과 binding 교체 evidence |
| DeliveryDispatch | `Shared/Contracts/messages.ts`, `Server/DispatchCenter`, `Tracking`, `CourierSession`, `Session`, `Client` | route field 금지, Attempt owner, timestamp, deadline·late decision | 정상·reassign 상태 sequence와 server offer/status evidence |
| ShoppingMall | `Shared/Contracts/messages.ts`, `Server/CommerceApi`, `OrderWorkflow`, `Shared/Store`, `Client` | StartOrder response, sourceCommandId, event fold, compensation와 projection rebuild | idempotency·failure·resume result와 durable event/projection evidence |
| GameQuest | `Shared/Contracts/messages.ts`, `Server/GameApi`, `QuestMission`, `Shared/Store`, `Client` | action set, object payload, dedupe, replay·reconcile, domain/store separation | EventId·progress notify·projection 결과와 duplicate append 0 evidence |
| ZoneWorld | `Shared/contracts.ts`, `Server/Gateway`, `ZoneNode`, `Ops`, `Client`, shared browser | typed movement/push, NodeId/RID, relocation, border, fanout와 Message Follow | headless·Chromium result, same-zone/border state, Ops와 cleanup evidence |

### 11.7 수정 순서와 단계별 통과 조건

#### G0 — Framework spec 완료 evidence와 sample 기준 상태 재검증

10절 Framework spec 완료 evidence와 현재 candidate SHA, 전체 변경 manifest를 고정하고 sample audit
입력 fingerprint를 다시 계산한다. Fingerprint는 sample 계약 비교 입력의 변경만 감지한다. Candidate
SHA 또는 전체 manifest가 이전 실행과 다르면 fingerprint 값과 관계없이 build, contract test와 실제
process runner를 모두 다시 실행한다. 이 단계에서는 sample을 수정하지 않는다.

#### G1 — 공통 sample 계약과 `R0 design`

`NS-IMP-003`부터 `NS-IMP-007`까지의 extra/missing message를 `client-facing`, `server application`,
`Framework lifecycle input`, `persistence record`, `test/evidence-only`로 분류한다. `contract 선행` 항목은
공통 문서 review가 끝나기 전까지 구현 작업으로 이동하지 않는다. 각 `NS-*` card의 design packet은
frontier Codex model과 `high` 이상 reasoning으로 `R0 design`을 통과해야 한다. 어려운 계약 충돌이나
concurrency·lifecycle race는 `max`로 올린다.

#### G2 — 정확한 sample 계약 목록과 실패 regression 고정

Sample, message, direction, transport kind, response, field, optionality, codec, owner와 evidence를 가진
계약 목록을 만든다. Node shared contract, packet name과 decorator를 비교해 `NS-REG-*`이 먼저 실패하게
한다. 기존 test가 현재 공통 계약과 충돌하면 본래 보장을 약화하지 않고 기준만 갱신한다.

#### G3 — wire contract와 codec 책임 정렬

`NS-IMP-001`부터 `NS-IMP-007`의 shared contract, handler와 client를 함께 수정한다. GameQuest의 수동
payload encode/decode와 ZoneWorld의 `unknown` notification wrapper를 제거해 typed codec 경로로
연결한다. Bingo는 Protobuf를 유지하며 JSON sample과 같은 declaration으로 바꾸지 않는다.

#### G4 — logical structure와 state owner 정렬

`NS-IMP-008`의 role mapping을 기준으로 source를 `Client`, `Shared`, `Server/<Role>`과 필요한
Domain/Application/Infrastructure 책임에 대응시킨다. A/B process 이름, runner probe와 evidence tool은
업무 role과 분리한다. Node decorator scan을 유지하고 handler 목록을 module code에 반복하지 않는다.

#### G5 — runner, README와 package 설명 정렬

`NS-IMP-009`를 선행 package version 결정에 맞춰 수정한다. Shell·PowerShell 실행 목록, completion
marker, Chromium 실행, readiness, 실행별 Redis와 cleanup을 같은 정책으로 맞춘다. G3~G5에서 만든 각
candidate는 frontier Codex model과 `high` 이상 reasoning으로 `R1 candidate`를 통과해야 한다.

#### G6 — 실제 process evidence와 CI 연결

일곱 runner를 fresh package로 실행한다. 각 sample에서 response·push assertion, state owner evidence,
failure와 cleanup을 수집한다. Full sample gate를 CI 필수 단계에 연결하고 common sample 문서나 Node
sample 변경이 workflow를 실행하도록 path filter를 확인한다. Frontier Codex model과 `high` 이상
reasoning으로 `R2 integration`을 수행하며, 기계 검사 결과로 이 review를 대체하지 않는다.

#### G7 — 최종 독립 audit

공통 sample, Node exact interface, production package, sample source, test, runner와 artifact를 다시
대조한다. Frontier Codex model과 `xhigh` 이상 reasoning으로 `R3 final`을 수행한다. 어려운 계약 충돌이나
concurrency·lifecycle race는 `max`로 올린다. `확인`, `contract 선행`, `test gap`, `blocked`가 하나라도
남거나 최종 decision이 `CLEAN`이 아니면 완료로 표시하지 않는다.

### 11.8 유지할 test와 추가할 regression

기존 test는 범위를 줄이지 않는다. Lifecycle, generated routing ID, browser bundle, decimal, scale-out,
Entry Spot, domain과 ZoneWorld gate를 정확한 계약 목록 test로 대체하지 않고 함께 유지한다.

| ID | 추가·변경할 regression | 직접 판정할 내용 |
|---|---|---|
| `NS-REG-001` | `CommonSampleContractInventoryMatchesNodeSharedContracts` | 일곱 sample의 message, field, optionality, enum과 codec 일치 |
| `NS-REG-002` | `CommonSampleTransportKindsMatchNodeHandlers` | Req/Res, Msg, Notify와 Event가 request/send/push/publish handler와 일치 |
| `NS-REG-003` | `TicTacToeLeaveAndJoinFailureMatchContract` | `LeaveGameMsg`, Entry Spot destroy, duplicate no-op와 client-visible join terminal failure |
| `NS-REG-004` | `SupportChatTypingUsesOneWayMessage` | `SetTypingMsg`, metadata, handler response 없음과 peer push |
| `NS-REG-005` | `DeliveryDispatchMessagesHideFrameworkRouting` | sessionRoute·ActorRef·NodeRid 금지, Attempt owner와 timestamp number |
| `NS-REG-006` | `GameQuestUsesTypedGameplayPayload` | object payload, 호출부 encode/decode 금지, dedupe와 replay·reconcile |
| `NS-REG-007` | `ShoppingMallWorkflowMatchesCommonContract` | StartOrder state, sourceCommandId, command/event와 compensation 순서 |
| `NS-REG-008` | `SupportChatConversationCreateMatchesCommonContract` | create payload·response state와 extra field 범위 |
| `NS-REG-009` | `ZoneWorldUsesTypedMovementAndPushMessages` | UpdatePosition, BotTick, zone/world delivery와 NodeId/RID 분리 |
| `NS-REG-010` | `BingoProtobufCommonSubsetIsExact` | 공통 field/tag/optional/repeated/reserved와 extra type 분류 |
| `NS-REG-011` | `NodeSamplesFollowCommonLogicalStructure` | 역할 mapping과 Domain/Application/Infrastructure 의존 방향 |
| `NS-REG-012` | `NodeSamplesUseDecoratorScanForHandlers` | 자동 scan 유지, manual handler list와 빠진 decorator 금지 |
| `NS-REG-013` | `NodeSampleRunnersUseCanonicalInventoryAndMarkers` | 양 host의 일곱 sample, completion·PASS marker와 실패 exit |
| `NS-REG-014` | `NodeSampleRunnersUseIsolatedResourcesAndCleanup` | 실행별 Redis/config/log/process와 성공·실패 cleanup |
| `NS-REG-015` | `NodeSampleCompletionRequiresClientAndServerEvidence` | payload·ordering assertion과 state owner evidence가 모두 있어야 PASS |
| `NS-REG-016` | `NodeSampleDocumentationMatchesPackageAndRunner` | README version, 실행 명령, Chromium 경계와 current package 일치 |
| `NS-REG-017` | `NodeSamplesDoNotOwnFrameworkCodecOrRoutes` | raw frame, message별 codec, private API, route identity와 manual JSON 변환 금지 |

`NS-REG-001`~`017`은 모두 sample 완료에 필요한 regression이다. Contract 변경으로 어떤 항목이 더는
필요하지 않다면 해당 ID의 계약 근거와 대체 검증을 이 표에서 수정하고, 독립 review 승인을 받은 뒤에만
제외할 수 있다. 근거만 기록한 임의 제외는 완료로 인정하지 않는다.

### 11.9 실행 및 evidence 수집 계획

1. 기준 commit, dirty manifest, Node/npm version, package archive와 native artifact hash를 기록한다.
2. 공통 계약 목록과 Node shared contract 비교에서 의도한 실패만 남는지 확인한다.
3. Node Framework와 일곱 sample을 build하고, 영향받은 contract/domain test를 실행한다.
4. `run_samples.sh`와 `run_samples.ps1`의 실행 목록을 같은 fixture와 비교한다.
5. 각 sample runner를 실행별 Redis·config·log 디렉터리로 실행한다.
6. Client가 response, push, 순서와 금지 결과를 직접 assertion했는지 확인한다.
7. Server evidence에서 state owner, idempotency, relocation, deadline, cleanup 결과를 확인한다.
8. Bingo·TicTacToe·SupportChat·DeliveryDispatch·GameQuest의 browser client는 실제 Chromium에서 검증한다.
   ZoneWorld는 headless scenario와 shared Chromium client를 모두 실행한다.
9. 성공과 실패 뒤 process, browser, Redis와 temporary resource가 정리됐는지 확인한다.
10. Full Node regression, package consumer, sample gate와 CI workflow 결과를 해당 작업 항목에 기록한다.

고정 sleep, 이전 log, source symbol과 completion 문자열만으로 readiness나 성공을 판정하지 않는다.
실패한 sample을 제외한 나머지 결과만으로 전체 완료를 표시하지 않는다.

### 11.10 완료 checklist

- [ ] 10절 Framework spec 완료 gate가 통과했고 그 evidence가 현재 `HEAD`와 일치한다.
- [ ] 공통 sample 문서와 기존 dirty change의 기준 manifest를 보존했다.
- [ ] 일곱 sample의 message·field·transport·codec 목록이 작성됐다.
- [ ] `contract 선행`과 internal-only message 범위가 review로 확정됐다.
- [ ] `NS-IMP-001`~`NS-IMP-009`, `NS-TEST-001`의 owner와 상태가 닫혔다.
- [ ] TicTacToe leave와 SupportChat typing이 이름과 실행 모두 one-way send다.
- [ ] DeliveryDispatch message에 session route, ActorRef와 owner NodeRid가 없고 timestamp wire가 일치한다.
- [ ] GameQuest가 typed object payload를 사용하고 application·Domain에서 codec을 직접 처리하지 않는다.
- [ ] ShoppingMall의 접수 response, workflow command와 durable event가 공통 계약과 일치한다.
- [ ] SupportChat conversation create와 ZoneWorld movement·push message가 공통 경계를 사용한다.
- [ ] Bingo는 공통 Protobuf schema를 유지하고 extra type의 범위가 명시됐다.
- [ ] 일곱 sample의 logical role과 책임을 같은 위치에서 찾을 수 있다.
- [ ] Node handler는 decorator·metadata scan으로 자동 등록되고 수동 목록을 반복하지 않는다.
- [ ] Shell·PowerShell runner가 같은 실행 목록, marker, resource와 cleanup 정책을 사용한다.
- [ ] `NS-REG-001`~`NS-REG-017`이 모두 통과했다. 제외가 필요하면 11.8절의 계약 변경·대체 검증·독립
  review 조건을 먼저 충족했다.
- [ ] 일곱 실제 process smoke에서 client self-check와 server evidence가 모두 확인됐다.
- [ ] Full Node regression, package consumer와 CI sample gate가 fresh package로 통과했다.
- [ ] Sample `R0`~`R2`는 frontier Codex model과 `high` 이상 reasoning, `R3 final`은 frontier Codex
  model과 `xhigh` 이상 reasoning으로 수행했다.
- [ ] 어려운 계약 충돌이나 concurrency·lifecycle race가 포함된 sample round는 `max`로 올렸다.
- [ ] 각 sample review log에 실제 model ID, model·reasoning 선택 근거, candidate와 decision이 기록됐다.
- [ ] 필요한 Codex model이나 reasoning level을 사용할 수 없을 때 낮은 model·level로 대체하지 않고
  `blocked`로 기록했다.
- [ ] 최종 독립 audit에서 미기록 sample spec gap이 0개다.
