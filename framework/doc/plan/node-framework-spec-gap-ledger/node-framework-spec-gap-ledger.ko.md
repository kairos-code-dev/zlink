# Node.js Framework spec gap ledger

작성일: 2026-08-02

상태: 조사 결과와 후속 작업을 기록한 audit ledger. 이 문서는 구현 완료 판정이 아니다.

## 1. 목적과 완료 조건

이 ledger는 공통 Framework spec, Node exact interface, 공통 E2E spec과 현재
Node.js Framework의 public contract·production call path·process E2E 동작을 한
기준으로 대조한 결과를 기록한다. 기능 이름이나 source 파일의 존재만으로
충족을 판정하지 않고, 실제 호출 순서와 role server evidence까지 확인하는 것을
목표로 한다.

완료 조건은 다음과 같다.

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
  통과한다. 이 review는 POSD와 DDD 기준을 함께 적용하며, 지정 reviewer는
  `Sol Medium`(`gpt-5.6-sol`, reasoning `medium`)이다.

이 문서에서 사용하는 판정 표기는 다음과 같다.

- ND-IMP-###: production implementation gap
- ND-E2E-IMP-###: Node E2E 구현·runner·process evidence gap
- ND-TEST-###: audit 또는 회귀 test 자체의 gap
- ND-REG-###: 새로 추가하거나 변경해야 할 회귀 test
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
| 독립 reviewer | `Sol Medium` — `gpt-5.6-sol`, reasoning `medium` | 전체 candidate와 근거를 read-only로 읽고 contract·POSD·DDD·test/evidence finding을 기록한다. | 금지 |
| Final reviewer | 동일한 `Sol Medium` 설정 | 모든 card와 전체 diff, fresh gate, process evidence를 다시 대조하고 최종 판정을 내린다. | 금지 |

Reviewer는 구현자의 요약만 읽지 않는다. 다음 입력을 같은 candidate 기준으로 직접 읽는다.

- 공통 Framework spec, Node exact interface, 공통 E2E와 public contract governance
- 해당 card의 production call path, package export, test·CI script와 전체 candidate diff
- 기준 commit 또는 immutable candidate SHA, `git status --short`, 변경 파일 manifest
- 실행한 명령, exit code, test result, process log, client result와 role server evidence
- 이전 round의 unresolved finding과 그 finding을 반영한 재검토 결과

`Sol Medium`을 사용할 수 없으면 다른 model이나 reasoning level로 대체하지 않는다. 해당 round를
`blocked`로 기록하고 사용 가능해진 뒤 같은 immutable candidate에서 review를 다시 시작한다. Review 중에는
구현자와 reviewer가 같은 working tree를 변경하지 않는다. 가능하면 candidate SHA를 별도 read-only
worktree에 고정하고, 그렇지 않으면 review 시작 시점의 manifest와 diff를 결과에 포함한다.

### 진행 log 저장 규칙

이 작업의 진행 기록은 이 문서가 있는 디렉토리의 `log/` 아래에 저장한다.
review round, 구현 변경, 실행 명령과 exit code, Sol Medium finding, 미해결 조건은
해당 log 파일에 기록하고, ledger에는 현재 판정과 log 링크만 남긴다. log 디렉토리가
없으면 먼저 만들며, repository의 다른 공통 log나 과거 snapshot을 현재 작업의 증거로
대체하지 않는다. 파일명은 `YYYY-MM-DD-주제.ko.md` 형식을 사용한다.

현재 진행 log: [`log/2026-08-02-posd-ddd-review.ko.md`](log/2026-08-02-posd-ddd-review.ko.md)

### Review round와 통과 조건

각 card는 다음 round를 순서대로 거친다.

| Round | 시점 | 필수 산출물 | 통과 조건 |
|---|---|---|---|
| `R0 design` | source·test를 수정하기 전 | event storming, 책임·invariant 표, 두 가지 이상 설계 대안, POSD 위험 신호 목록 | contract 경계와 선택한 설계가 근거를 갖고, Sol Medium이 `DESIGN ACCEPTED` 또는 finding을 반환 |
| `R1 candidate` | regression을 먼저 고정하고 구현 candidate를 만든 뒤 | 전체 diff, targeted test, public surface와 production call path 증거 | Sol Medium이 전체 candidate를 읽고 `CLEAN` 또는 수정 finding을 반환 |
| `R2 integration` | package·CI·process E2E를 실행한 뒤 | fresh package, full gate, client result와 role server evidence | 좁은 test pass가 아닌 실제 contract·lifecycle·failure evidence까지 `CLEAN` |
| `R3 final` | 모든 card가 끝난 뒤 | 전체 ledger, 변경 manifest, fresh command 결과와 unresolved 목록 | 미해결 Critical/High/Medium finding이 0이고 최종 판정이 `CLEAN` |

`CLEAN`은 “빌드가 된다”는 뜻이 아니다. contract·POSD·DDD·test/evidence 범위에서 Critical, High,
Medium finding이 없고, Low finding은 수용·기각·후속 card 가운데 하나로 명시되어야 한다. reviewer가
찾지 못했다는 사실과 review를 실행하지 않았다는 사실을 구분한다.

### POSD review rubric

Sol Medium은 각 candidate에서 다음 위험 신호를 먼저 열거하고, 해당 위험 신호가 왜 문제인지 근거를
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

각 round 결과는 ledger card 또는 연결된 review log에 다음 형식으로 남긴다.

```text
reviewer: Sol Medium
model: gpt-5.6-sol
reasoning: medium
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
fresh test 결과를 고정하고 같은 Sol Medium 설정으로 해당 round를 다시 수행한다. `deferred`는 완료가
아니며, owner와 후속 card ID가 있어야 한다.

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
| node --test --test-force-exit test/contract/contract-surface.test.js | 통과, 33/33 | 선언과 일부 runtime export 및 package root 검증은 충족. immutable API snapshot과 exact member negative check는 없음. |
| node --test --test-force-exit test/contract/backend-public-api-only.test.js | 통과, 5/5 | backend public API-only 검증은 충족. 모든 E2E client의 import 경계를 증명하지 않음. |
| npm run verify:m5-foundation | 통과, 5/5 | M5 foundation 회귀 범위는 충족. |
| npm run verify:m6a-runtime | 통과, 25/25 | topology, admission, reconnect/liveness와 HWM의 좁은 runtime 범위는 충족. |
| npm run verify:m6b-runtime | 통과, 43/43 | SpotWide, PerActor, yield, actor order, exactly-once와 instance recovery의 좁은 범위는 충족. |
| npm run verify:m6c-runtime | 통과, 79/79 | authority, deadline/cancellation, cleanup, relocation, restart recovery와 replay의 좁은 범위는 충족. |
| Node E2E runner 전체 bash -n | 통과 | shell syntax만 검증. scenario dispatch와 process evidence는 검증하지 않음. |
| npm test를 300초 bounded 실행 | 실패 후 timeout, channel-client.test.js의 route raw SPOT request case에서 중단 | full test green이 아니다. channel-client test의 실패 출력은 exit code 1이고 outer command는 exit 124. |
| npm run verify:coverage를 180초 bounded 실행 | timeout, 같은 channel-client case 이후 진행 없음 | coverage도 완료 결과가 아니다. |
| npm run verify:ci | 실패 | build/typecheck/lint와 다수 test 이후 documentation-regression.test.js가 framework/doc/framework/common/spec/30-implementation-gap.ko.md ENOENT로 실패. |
| ./scripts/verify_packaged_contract.sh | 실패 | server consumer install이 @zlink-systems/zlink@11.0.2를 registry에서 찾지 못해 404. |
| npm ls @zlink-systems/zlink --all | invalid, exit 1 | root는 local 11.1.0 archive를 가리키지만 workspace와 lock은 11.0.2를 요구. |
| e2e/ToActorMessaging/run_e2e.sh TA-A1 | 실패, exit 2 | server/caller/session/client build가 현재 Node public API와 맞지 않음. |
| e2e/SubmitAdmission/run_e2e.sh SA-E2E-14 | 실패, exit 2 | binding package 11.0.2와 native artifact가 불완전하여 process를 시작하지 못함. |

현재 실패는 모두 같은 원인으로 묶지 않는다.

- timeout 또는 hang: npm test와 verify:coverage의 channel-client 경로.
- tree/environment blocker: documentation-regression이 요구하는 공통
  30-implementation-gap 문서가 현재 tree에 없음.
- package blocker: root, workspace, lock의 zlink version이 일치하지 않고
  11.0.2 consumer install이 registry에 의존함.
- production/API drift: ToActorMessaging의 stale builder, actor context,
  lifecycle signature와 HTTP client method.
- native artifact blocker: SubmitAdmission이 후보 native artifact를
  incomplete로 판정.
- historical evidence: feature-map의 과거 pass와 과거 log. 현재 process
  실행 결과가 아니므로 완료 판정에 사용하지 않음.

## 4. 현재 충족 판정

| 항목 | 판정 | 직접 확인한 evidence와 한계 |
|---|---|---|
| package root export의 기본 경계 | 충족 | contract-surface.test.js가 framework, nestjs, locations, stream connector의 root export를 확인. package consumer install은 version mismatch로 별도 실패. |
| 일부 backend public API-only 경계 | 충족 | backend-public-api-only.test.js 5/5. 모든 E2E Client tree와 실제 process call path까지 포함하지 않음. |
| 좁은 runtime lifecycle·authority·deadline·cleanup·replay 범위 | 충족 | M5/M6 152개 test와 actor handoff focused test가 통과. 공통 전체 contract와 process evidence를 대체하지 않음. |
| browser transport smoke | 충족 | Chromium 1/1. Node server public API parity와 전체 stream connector E2E는 별도. |
| E2E client의 reflection/raw-frame 사용 여부 | 충족(정적 scan 범위) | 조사한 Client 경로에서 reflection과 raw-frame 호출은 찾지 못함. 다만 server Framework package 직접 import가 발견되어 client-only architecture 전체는 미충족. |
| Nest exact builder contract | 미충족 | configureInboundDispatch가 runtime과 declaration에 없고 exact interface 밖의 setter가 공개됨. ND-IMP-001 참조. |
| common error model | 미충족 | public enum 41개 detailed kind가 exact Node target 13개와 다름. ND-IMP-002 참조. |
| unknown content type 처리 | 미충족 | 알 수 없는 content type이 Buffer로 handler에 전달됨. ND-IMP-003 참조. |
| package consumer parity | 미충족 | 11.1.0과 11.0.2가 혼재하고 packaged install이 실패. ND-IMP-004 참조. |
| Config 1-14 aggregate E2E | 미충족 | aggregate가 12 config만 호출하고 exit 0만 집계함. ND-E2E-IMP-001, ND-E2E-IMP-002 참조. |

## 5. ND-IMP-* production implementation gap

### ND-IMP-001 — Nest exact builder의 inbound dispatch 계약과 public member 불일치

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/server/languages/node/interfaces/07-nestjs-host.ko.md:169-186, framework/doc/framework/common/spec/00-public-contract-governance.ko.md:8-21
- Node source·test 경로: framework/languages/node/packages/nestjs/src/contracts.ts:192-210, framework/languages/node/packages/nestjs/src/options-builder.ts:101-132, framework/languages/node/packages/framework/src/contracts/Configuration/Builders.ts:32-55, framework/languages/node/packages/framework/src/contracts/Configuration/RegistrationBuilders.ts:128-140 및 169-176, framework/languages/node/test/contract/contract-surface.test.js
- 실제 동작: Nest builder declaration과 build 결과에는 options, codecs, configureDispatch, store, version, maintenance, compression, locations, network, channel, stream, build가 있다. exact interface가 요구하는 configureInboundDispatch는 없다. 반대로 exact interface에 없는 setActorTransferTimeout과 setMessageFollowDuration이 public builder에 있다.
- 기대 동작: Node exact interface의 builder member를 public contract로 사용하고, configureInboundDispatch를 같은 계약으로 제공한다. exact interface에 없는 setter는 public surface에 남길지 먼저 governance 절차로 확정해야 하며, 현재 source만으로 이를 정식 계약으로 간주하지 않는다.
- gap 판정 근거: build 후 runtime probe에서 typeof builder.configureInboundDispatch가 undefined였다. contract-surface.test.js는 이름 존재와 일부 export만 검사하여 이 member 누락과 extra member를 닫지 못한다.
- 구체적인 수정 목록: (1) exact interface와 현재 setter의 계약 여부를 contract 선행 항목으로 확정한다. (2) 목표 계약에 맞춰 Nest builder declaration·implementation·generated dist를 정렬한다. (3) configureInboundDispatch가 worker/admission/preflight 순서를 보존하는지 production call path에서 확인한다. (4) package export와 consumer compile을 다시 검증한다.
- 필요한 회귀 test: ND-REG-001. configureInboundDispatch의 declaration 및 runtime 존재, exact parameter/return type, exact interface 밖 setter의 처리, build 후 package root export를 함께 검사한다.
- 선행 조건과 작업 순서: contract 선행 → ND-IMP-002 error contract 확인 → Nest source 수정 → ND-REG-001 → ToActor/SpotService process build 순서로 진행한다.
- 구현 완료 evidence: exact interface member 비교가 missing/extra 없이 통과하고, configureInboundDispatch를 사용한 real builder path가 admission/preflight 순서를 보이는 test와 process E2E를 통과한다. 과거 source type 존재만으로 완료하지 않는다.

### ND-IMP-002 — common error model과 Node public error surface 불일치

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/32-framework-error-model.ko.md:19-33 및 66-78, framework/doc/framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md:59-77
- Node source·test 경로: framework/languages/node/packages/framework/src/contracts/Errors/ZLinkFrameworkException.ts:1-16 및 18-104, framework/languages/node/test/contract/contract-surface.test.js:536-599, framework/languages/node/packages/framework/src/runtime/
- 실제 동작: Node public enum은 ActorRouteNotFound, PayloadDecodeFailed, RequestProtocolError, DeadlineExceeded 등 41개의 detailed string kind와 0부터 40까지의 numeric mapping을 export한다. exception은 kind 외에 code와 isRetriable를 public field로 노출하고 cause와 retryable 인자를 받는다. 현재 contract test도 이 41개 table을 정답으로 검사한다.
- 기대 동작: Node exact interface가 고정한 ZLinkFrameworkErrorKind 13개 common kind를 numeric 0부터 12까지 제공하고, exact exception surface와 terminal error mapping을 따른다. wire/payload/reply type failure는 ProtocolError로, cancellation/deadline은 정의된 cancellation/deadline 의미로 완료되어야 한다.
- gap 판정 근거: exact interface의 target table과 source export table이 다르며, narrow contract test가 오히려 old detailed table을 authority로 삼는다. 따라서 test green은 common error parity 증거가 아니다.
- 구체적인 수정 목록: (1) exact Node error interface를 목표로 확정한다. (2) 내부 detailed failure를 유지할 필요가 있으면 public common kind와 내부 진단 정보를 분리한다. (3) send/request/handler/HTTP response의 terminal mapping을 ProtocolError, cancellation, deadline 계약에 맞춘다. (4) code, retryability, cause, serialization 노출이 exact interface와 맞는지 함께 정리한다.
- 필요한 회귀 test: ND-REG-002. 13개 enum 이름과 값, exception public member, wire/payload/reply failure, cancellation과 deadline의 terminal mapping을 exact table로 검사한다.
- 선행 조건과 작업 순서: contract 선행 → error mapping inventory → production exception/dispatch 수정 → ND-REG-002 → E2E evidence gate 순서로 진행한다.
- 구현 완료 evidence: exact interface와 runtime export가 동일하고, 각 failure class가 client-visible terminal error와 role server evidence에서 동일한 kind/reason으로 직접 확인된다. 41개 source enum table의 존재는 증거가 아니다.

### ND-IMP-003 — 알 수 없는 content type이 ProtocolError가 아니라 Buffer로 전달됨

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/06-framework-api.ko.md:411-419, framework/doc/framework/common/spec/32-framework-error-model.ko.md:66-78
- Node source·test 경로: framework/languages/node/packages/framework/src/runtime/channels/channel-envelope.ts:223-247, framework/languages/node/packages/framework/src/runtime/channels/channel-dispatch-pipeline.ts:209-227, framework/languages/node/packages/framework/src/runtime/spots/spot-route-packet-dispatch.ts:120-168, framework/languages/node/packages/framework/src/runtime/spots/index.ts:869-895 및 916-940, framework/languages/node/test/contract/channel-envelope-error.test.js
- 실제 동작: registered serializer와 binary, JSON 처리는 분기하지만 unknown content type은 envelope payload를 Buffer로 반환한다. 실제 channel dispatch와 spot dispatch는 그 결과를 handler에 전달한다. 예외가 발생한 경우에만 PayloadDecodeFailed mapping을 수행한다.
- 기대 동작: 등록되지 않은 non-JSON content type은 JSON으로 추측하지 않고 ProtocolError로 완료되어야 한다. handler가 Buffer를 정상 payload로 처리하거나 side effect를 발생시키면 안 된다. send와 request의 terminal mapping도 같은 error contract를 따라야 한다.
- gap 판정 근거: build 후 decodeChannelPayload에 application/x-audit-unknown과 01 02 03을 넣었을 때 Buffer:010203이 반환되었다. malformed JSON test는 decode exception만 검사하므로 unknown content type 경로를 닫지 못한다.
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
   error 처리와 변경 증폭을 기록한 뒤 선택한다. `Sol Medium`이 `DESIGN ACCEPTED`를
   반환하기 전에는 source나 test를 수정하지 않는다.
5. **regression-first 구현**: `R0`가 통과한 card만 실패를 재현하는 regression을
   먼저 고정한다. 그 뒤 production contract, codec call path, package 재현성,
   E2E selector·endpoint를 책임 owner가 있는 계층에서 수정한다. 새 helper,
   reflection, raw frame, private workaround로 하위 계층의 gap을 숨기지 않는다.
6. **`R1 candidate`**: targeted test와 public surface·production call path evidence를
   실행하고 candidate SHA 또는 immutable manifest를 고정한다. `Sol Medium`은 전체
   diff를 읽어 contract·POSD·DDD·test/evidence finding을 반환한다. `NOT CLEAN`이면
   다음 card로 가지 않고 finding을 수정한 새 candidate에서 같은 round를 반복한다.
7. **E2E inventory와 per-config process**: ND-E2E-IMP-001에 따라 Config 1-14의
   374개 exact ID와 map·selector를 맞춘다. ND-E2E-IMP-003과 ND-E2E-IMP-004를 처리해
   role endpoint, client allowlist, evidence schema를 정렬한다. Config 12·14처럼
   public contract가 필요한 항목은 `contract 선행`으로 유지한다.
8. **package와 aggregate gate**: ND-IMP-004를 해결해 local package version,
   workspace, lock, native artifact와 clean consumer를 일치시킨다. 이어
   ND-E2E-IMP-002를 처리해 Config 1-14를 aggregate에 포함하고 partial,
   diagnostic-only, source-only, N/A와 child exit 0만으로 PASS가 되지 않게 한다.
9. **`R2 integration`**: full npm test, coverage, verify:ci, packaged contract와
   관련 process E2E를 제한 시간 만료 없이 실행한다. client result, role server
   evidence, terminal reason, callback count, owner, generation과 cleanup을 한 실행에서
   확인한다. 좁은 M5/M6 pass나 source compile은 이 round의 대체 증거가 아니다.
10. **`R3 final`과 완료 판정**: Sol Medium이 전체 ledger, 변경 manifest, fresh gate,
    process evidence와 unresolved finding을 독립적으로 다시 읽는다. current run
    result만 사용하고 historical log, source-only, timeout, hang, skip은 완료 evidence에서
    제외한다. 미해결 Critical/High/Medium finding이 0이고 Low disposition이 기록된 뒤에만
    checklist를 갱신한다.

## 8. 기존 회귀 test의 유지·변경·추가 목록

### 8.1 유지할 기존 검증

다음 검증은 현재 범위의 직접 증거로 유지한다. 다만 표시한 한계를 함께
기록해야 한다.

| 기존 검증 | 현재 결과 | 유지 목적과 한계 |
|---|---|---|
| test/contract/contract-surface.test.js | 33/33 | package root와 일부 선언/export 경계를 유지. exact member negative check와 immutable snapshot으로 확장 필요. |
| test/contract/backend-public-api-only.test.js | 5/5 | backend public API 사용 경계 유지. E2E Client 전체 import scan을 대체하지 않음. |
| verify:m5-foundation | 5/5 | foundation admission과 topology 회귀 유지. |
| verify:m6a-runtime | 25/25 | HWM, reconnect/liveness와 admission 회귀 유지. |
| verify:m6b-runtime | 43/43 | routing, yield, actor order와 exactly-once 회귀 유지. |
| verify:m6c-runtime | 79/79 | authority, deadline/cancellation, relocation, recovery, replay 회귀 유지. |
| test:browser | Chromium 1/1 | browser transport smoke 유지. |
| E2E runner bash -n | 통과 | shell syntax만 유지. 실행 evidence로 승격하지 않음. |

### 8.2 ND-TEST-* audit 또는 gate gap

#### ND-TEST-001 — public API snapshot과 exact negative comparison 부재

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/spec/server/languages/node/interfaces/01-foundation-configuration.ko.md 및 07-nestjs-host.ko.md, framework/doc/framework/common/spec/00-public-contract-governance.ko.md:75-88
- Node test/source 경로: framework/languages/node/test/contract/contract-surface.test.js:31-61 및 319-346 및 536-599, framework/languages/node/packages/*/dist/
- 실제 동작: checked-in Node public API snapshot 파일을 찾지 못했다. contract-surface test는 exact interface 문서를 동적으로 읽어 누락 이름과 일부 runtime export를 검사하고, error enum은 현재 source의 41개 detailed table을 정답으로 검사한다. Nest builder의 exact member와 extra public member는 닫지 않는다.
- 기대 동작: release contract를 재현할 수 있는 exact Node API snapshot 또는 동등한 구조화된 manifest가 있어야 하며, 이름·signature·optional/default·error·ownership·export의 missing과 extra를 모두 비교해야 한다.
- gap 판정 근거: dynamic presence check는 source와 문서가 동시에 잘못 갱신되는 drift를 잡지 못하고, old error table을 common target과 비교하지 않는다.
- 구체적인 수정 목록: (1) exact interface에서 machine-readable contract를 생성하거나 checked-in snapshot을 만든다. (2) missing뿐 아니라 extra member와 member signature를 비교한다. (3) error enum과 exception surface를 common target과 비교한다. (4) package root export와 clean consumer API를 snapshot gate에 연결한다.
- 필요한 회귀 test: ND-REG-001, ND-REG-002. snapshot diff에서 missing·extra·signature drift가 실패해야 한다.
- 선행 조건과 작업 순서: contract 선행 → snapshot format 결정 → snapshot 생성 → test gate 연결 → package consumer 검증 순서로 진행한다.
- 구현 완료 evidence: clean checkout에서 snapshot test가 exact interface, source declaration, runtime export, package export를 모두 비교하고 drift를 재현 가능하게 보고한다.

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

#### ND-TEST-003 — full regression과 CI gate가 전체 계약·E2E 범위를 실행하지 않음

- 공통 spec 또는 E2E 문서 경로: framework/doc/framework/common/e2e/ 전체 14개 Config, framework/doc/framework/common/spec/의 Node contract
- Node script/CI 경로: framework/languages/node/package.json:6-30, framework/languages/node/scripts/run_node_framework_ci_gate.js:4-21, framework/languages/node/scripts/run_node_runtime_gate.js:37-44, .github/workflows/framework-node.yml:7-19 및 185-192
- 실제 동작: verify:ci는 channel-client.test.js, http-client.test.js, message-packet-name.test.js, stream-connector.test.js, stream-session-runtime.test.js, test/browser/stream-connector-chromium.test.js를 hard-coded skip한다. workflow는 common spec/e2e path를 filter에 포함하지 않고 aggregate Node process E2E도 호출하지 않는다. npm test와 verify:coverage는 channel-client route case에서 bounded timeout이 발생했다.
- 기대 동작: skip은 명시적 blocked/diagnostic 결과로 보고되고 전체 성공 수에 포함되지 않아야 한다. CI path filter는 contract와 E2E inventory 변화를 감지해야 하며, Node aggregate process E2E와 package consumer gate가 CI에 연결되어야 한다.
- gap 판정 근거: current verify:ci의 skip list와 workflow command를 직접 확인했고, full test와 coverage가 완료되지 않았다. narrow M5/M6 pass는 이 gate의 대체 증거가 아니다.
- 구체적인 수정 목록: (1) skip된 test의 owner와 종료 조건을 기록한다. (2) skip을 PASS에서 분리한 결과 형식을 만든다. (3) common spec/e2e와 feature-map path를 workflow filter에 추가한다. (4) aggregate E2E와 packaged contract를 CI job에 연결한다. (5) channel-client hang/late route cleanup을 bounded regression으로 재현하고 원인을 수정한다.
- 필요한 회귀 test: ND-REG-008. full test, coverage, skip report, documentation fixture, aggregate E2E의 timeout·hang·skip을 성공으로 숨기지 않는지 검사한다.
- 선행 조건과 작업 순서: ND-IMP-*와 ND-E2E-IMP-* 해결 → skip policy 확정 → channel-client regression → CI workflow 연결 → full gate 순서로 진행한다.
- 구현 완료 evidence: npm test, verify:coverage, verify:ci, packaged contract, aggregate process E2E가 현재 tree에서 제한 시간 만료·skip·historical output 없이 종료하고, CI report가 전체 범위와 미실행 항목을 명시한다.

### 8.3 추가 또는 변경할 ND-REG-* regression ID

아래 ID는 이번 audit에서 새로 제안한 회귀 test 목록이다. 실제 test 파일은
수정하지 않았다.

| ID | 유지·변경·추가 내용 | 관련 gap | 완료 evidence |
|---|---|---|---|
| ND-REG-001 | Nest exact builder member, parameter/return type, missing configureInboundDispatch와 extra member를 declaration·runtime·package에서 비교 | ND-IMP-001, ND-TEST-001 | exact interface와 source/runtime/package가 same set으로 통과 |
| ND-REG-002 | 13개 common error kind와 numeric value, exception surface, ProtocolError/cancellation/deadline mapping을 비교 | ND-IMP-002, ND-TEST-001 | client-visible terminal error와 role server evidence의 kind/reason 일치 |
| ND-REG-003 | unknown content type send/request의 ProtocolError, handler count 0, callback exactly-once, response body/status를 검사 | ND-IMP-003 | real dispatch path에서 no Buffer fallback이 확인됨 |
| ND-REG-004 | root/workspace/lock/archive/native artifact version consistency와 clean consumer install을 검사 | ND-IMP-004 | npm ls clean, consumer build/run, native load 성공 |
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
- [ ] `R0 design`을 `Sol Medium`(`gpt-5.6-sol`, reasoning `medium`)이 독립적으로
  검토하고 `DESIGN ACCEPTED` 또는 처리할 finding을 남겼다.
- [ ] `R1 candidate`에서 전체 candidate diff, production call path, targeted test와
  public/package surface를 같은 reviewer가 read-only로 확인했다.
- [ ] `R2 integration`에서 package·CI·process E2E와 client·role server evidence를
  검토했다. source compile이나 좁은 test pass를 대체 증거로 사용하지 않았다.
- [ ] `R3 final`에서 전체 변경 manifest와 ledger를 다시 읽었고, 미해결
  Critical/High/Medium contract·POSD·DDD finding이 0이다.
- [ ] Low finding은 `accepted`, `rejected-with-reason`, `fixed`, `deferred` 중 하나로
  처리되었고, `deferred` 항목에는 owner와 후속 card ID가 있다.
- [ ] 각 round 결과에 reviewer, model, reasoning, candidate, scope, decision,
  finding, evidence와 재실행 조건이 기록되어 있다.
- [ ] `Sol Medium`을 사용할 수 없는 round를 다른 model로 대체하지 않고 `blocked`로
  기록했다.

### Verification

- [ ] npm test와 verify:coverage가 timeout 또는 hang 없이 종료한다.
- [ ] verify:ci의 skip list와 documentation fixture가 현재 tree와 일치한다.
- [ ] common spec/e2e와 feature-map 변경이 CI path filter에 포함된다.
- [ ] packaged contract, 관련 process E2E, aggregate runner가 current run에서
  성공한다.
- [ ] 완료 보고에서 current run, historical log, timeout/hang/skip,
  environment blocker를 서로 구분한다.

현재는 위 checklist를 완료로 표시하지 않는다. POSD·DDD review round도 아직 실행하지
않았으며, ND-IMP-001부터
ND-E2E-IMP-005와 ND-TEST-001부터 ND-TEST-003은 unresolved 상태이며,
ND-REG-001부터 ND-REG-008은 후속 회귀 test 작업 입력이다.
