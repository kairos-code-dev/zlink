# Node.js 공통 sample 계약 차이 검토 및 수정 ledger

> 상태: 공통 sample과 Node 구현의 차이를 추적하는 수정 계획이다. 이 문서는 현재 sample
> 구현이나 공통 sample 계약을 완료로 판정하지 않는다.
>
> 기준: 2026-08-02 working tree. 공통 sample 문서, Node Framework, E2E와 CI에 진행 중인 변경이
> 있으므로 구현을 시작할 때 기준 commit과 변경 manifest를 다시 고정한다.
>
> 연계 문서: [`node-framework-spec-gap-ledger.ko.md`](node-framework-spec-gap-ledger.ko.md)의
> production contract와 package gap 가운데 각 sample card가 실제로 의존하는 항목만 선행 조건으로
> 연결한다. 관련 없는 Framework gap은 message·구조·test 정렬을 막지 않는다.

## 1. 목적과 완료 조건

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
- shell·PowerShell inventory, Chromium 경계, contract regression과 CI 강제 범위

다음 조건을 모두 만족해야 이 ledger를 완료로 표시할 수 있다.

1. 공통 sample 7종의 message·field·transport inventory와 Node shared contract가 한 행씩 대응한다.
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

이 문서 작성 단계에서는 Node sample, Framework production source와 test를 수정하지 않는다. 구현할
때는 각 card에 표시한 `ND-*` dependency와 `contract 선행` 항목만 먼저 닫는다.

## 2. 기준 문서와 조사 범위

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
| 연계 Framework ledger | `framework/doc/plan/node-framework-spec-gap-ledger/node-framework-spec-gap-ledger.ko.md` | 각 sample card가 실제로 의존하는 production contract와 E2E blocker |
| Audit 기준 log | [`log/2026-08-02-sample-gap-audit-baseline.ko.md`](log/2026-08-02-sample-gap-audit-baseline.ko.md) | commit, working-tree fingerprint, 명령, exit code와 test 결과 |

대상 sample과 핵심 판정 범위는 다음과 같다.

| Sample | 공통 핵심 흐름 | Node에서 직접 확인할 결과 |
|---|---|---|
| Bingo | Protobuf authentication, matching, room, reward, relocation과 cleanup | 공통 `.proto` subset, codec extension, Actor lifecycle와 Chromium result |
| TicTacToe | HTTP create, authentication, room join, turn, milestone, leave와 destroy | one-way leave, Logical Multicast, Entry Spot destroy와 manual topology |
| SupportChat | agent availability, conversation 생성·join, chat·typing, idle close와 reconnect | metadata routing, one-way typing, state sequence와 binding 교체 |
| DeliveryDispatch | 배송 생성, offer·decision, status, deadline과 reassignment | transport field 금지, timestamp wire shape, late decision과 evidence |
| ShoppingMall | order workflow, durable event, projection, idempotency와 compensation | 접수 응답, command/event 경계, replay와 failure sequence |
| GameQuest | session, gameplay event, projection, replay·reconcile와 dedupe | action inventory, typed payload, domain event와 Store adapter 경계 |
| ZoneWorld | movement, zone state, bot, border, relocation, fanout과 Ops | 공통 message boundary, application NodeId와 runtime RID 분리, browser evidence |

## 3. 선행 조건과 현재 검증 상태

### 3.1 Node Framework dependency 적용 기준

Sample이 Framework 구현 gap을 application code로 우회하면 안 된다. 다만 모든 Framework gap을 일괄
선행 조건으로 두지 않는다. 각 sample card가 호출하는 public API나 runtime 의미에 직접 영향을 주는
항목만 dependency로 연결한다.

| Dependency 범위 | 영향을 받는 작업 | 적용 기준 |
|---|---|
| Typed codec와 dispatch | `NS-IMP-003`, `NS-IMP-007`의 실제 wire 검증 | 관련 `ND-IMP-*`가 typed payload를 production path에서 처리하지 못할 때만 blocked로 둔다. |
| Actor·Spot와 session binding | `NS-IMP-002`, `NS-IMP-005`, `NS-IMP-006`, `NS-IMP-007`의 process 검증 | 현재 exact interface로 sample 계약을 표현할 수 없을 때 해당 card만 blocked로 둔다. |
| Package 정렬 | `NS-IMP-009`의 version 설명과 G6 process 실행 | `ND-IMP-004`가 닫혀 fresh package를 만들기 전에는 package·process 완료만 보류한다. |
| Framework process E2E | Sample이 사용하는 public call path의 runtime 증명 | 관련 scenario가 실패할 때 해당 sample process evidence만 보류한다. |
| 독립적인 sample 작업 | message declaration, logical structure, inventory와 문서 regression | 관련 Framework gap과 무관하면 즉시 진행한다. |

현재 연계 ledger와 production source가 동시에 수정되고 있다. 예를 들어 ledger에는
`configureInboundDispatch()`와 unknown content type 처리가 미충족으로 기록되어 있지만 현재 working
tree source에는 관련 변경이 존재한다. 따라서 source 존재만으로 card를 닫지 않고 exact interface,
runtime test, packaged consumer와 process evidence를 다시 실행해 선행 ledger 상태를 갱신해야 한다.

### 3.2 Audit 기준과 이번 조사에서 실행한 검증

이번 판정의 `HEAD`, working-tree fingerprint와 상세 실행 결과는
[`2026-08-02 sample gap audit 기준 log`](log/2026-08-02-sample-gap-audit-baseline.ko.md)에 고정했다.
Fingerprint가 달라지면 아래 결과를 현재 evidence로 사용하지 않고 다시 실행한다.

```text
cd framework/languages/node
npm run build
node --test test/contract/sample*.test.js
```

| 실행 | 결과 | 판정 |
|---|---|---|
| `npm run build` | 통과 | TypeScript와 browser bundle이 compile됨. sample 계약 충족 증거는 아님. |
| `sample*.test.js` | 실패, 83개 중 81개 통과 | DeliveryDispatch의 오래된 RouteMesh 이름 assertion과 Bingo 실제 runner의 peer readiness timeout이 실패했다. |
| 통합 sample runner | suite 내부 Bingo 실행 실패 | Bingo replacement peer의 `ConnectionReady` evidence를 기다리다 timeout이 발생했다. 일곱 sample 전체 통과 evidence는 없다. |

DeliveryDispatch test 실패는 현재 source가 공통 topology를 위반한다는 증거가 아니다. Source는
`courierMeshName`과 `customerMeshName`을 사용하고 공통 문서도 두 RouteMesh를 구분한다. 이 test는
현재 계약으로 고친 뒤, public Actor API와 private Spot handle 금지라는 본래 assertion을 유지해야 한다.
Bingo 실패는 static gate가 아니라 실제 process runner 실패다. Readiness 조건이나 peer 연결 원인을
확인하고 같은 fingerprint 또는 새로 기록한 candidate에서 다시 통과하기 전에는 Bingo를 완료로 표시하지
않는다.

### 3.3 현재 inventory와 문서 차이

| ID | 현재 근거 | 초기 판정 |
|---|---|---|
| `NS-IMP-008` | Bingo, TicTacToe, SupportChat과 ZoneWorld는 `Shared/Configuration` 대신 client/server별 설정을 두며 sample마다 logical tree가 다르다. | 구조 parity gap |
| `NS-IMP-009` | `samples/README.ko.md`는 Framework 10.0.0을 설명하지만 root가 참조하는 bindings archive는 11.1.0이다. | documentation/package gap |
| `NS-IMP-009` | shell과 PowerShell 통합 runner는 일곱 sample을 모두 포함한다. Bingo client는 README가 요구하는 `PASS Bingo.Ts`를 출력하지 않고, TicTacToe는 공통 `tictactoe=completed` marker를 출력하지 않는다. | runner marker gap |
| `NS-TEST-001` | 기존 sample test는 구조·특정 symbol을 넓게 검사하지만 공통 문서의 전체 message·field·transport inventory를 직접 비교하지 않는다. | test gap |

## 4. gap 판정 규칙

| 상태 | 의미 | 다음 행동 |
|---|---|---|
| `확인` | 공통 계약과 Node source 또는 process path의 차이가 재현됐다. | 실패 regression을 먼저 고정하고 책임 owner에서 수정한다. |
| `contract 선행` | 공통 문서의 public/internal 범위나 transport 의미가 모호하다. | 구현을 바꾸지 않고 공통 문서와 관련 spec을 먼저 review한다. |
| `test gap` | 구현이 맞을 수 있지만 현재 test가 해당 계약을 직접 판정하지 않는다. | exact inventory, serialized wire 또는 process evidence를 추가한다. |
| `documentation gap` | source와 runner의 기준을 문서가 현재 값으로 설명하지 않는다. | source owner와 version 기준을 확인한 뒤 문서를 갱신한다. |
| `blocked` | 선행 Framework public contract, package 또는 runtime 의미가 완료되지 않았다. | sample에서 우회하지 않고 선행 ledger가 닫힐 때까지 유지한다. |
| `충족` | source, wire, process evidence와 regression이 같은 계약을 증명한다. | 근거 명령과 artifact를 기록한다. |

Build 성공, source type 존재와 정적 문자열 test만으로 `충족`을 부여하지 않는다. Handler invocation,
state commit, failure, cleanup과 client-visible 결과까지 같은 실행에서 확인한다.

### 4.1 Card 책임과 현재 상태

`Gap owner`는 현재 sample 정렬 작업자다. `Common sample owner`가 필요한 항목은 공통 계약 review를
마칠 때까지 구현하지 않는다. `ND-*` dependency는 해당 runtime 기능을 실제로 검증할 때만 적용한다.

| ID | 분류 | Owner | Dependency | 현재 상태 | 완료 evidence |
|---|---|---|---|---|---|
| `NS-IMP-001` | Sample wire·Framework transport | Gap owner | 없음 | 확인 | `NS-REG-002`~`004`, client process |
| `NS-IMP-002` | Framework routing 경계·Sample wire | Gap owner | Actor/session runtime 관련 `ND-*` | 확인 | `NS-REG-005`, DeliveryDispatch process |
| `NS-IMP-003` | Sample wire·codec 책임 | Gap owner + Common sample owner | Typed codec 관련 `ND-*` | Contract review 필요 | `NS-REG-006`, GameQuest process |
| `NS-IMP-004` | Sample 업무 계약 | Gap owner + Common sample owner | 없음 | Contract review 필요 | `NS-REG-007`, ShoppingMall process |
| `NS-IMP-005` | Sample 업무 계약·Actor lifecycle input | Gap owner | Actor lifecycle 관련 `ND-*` | 확인 | `NS-REG-004`, `008`, SupportChat process |
| `NS-IMP-006` | Sample 실패 결과·Actor join completion | Gap owner | Actor join 관련 `ND-*` | 확인 | `NS-REG-003`과 client-visible failure test |
| `NS-IMP-007` | Sample wire·Framework routing 경계 | Gap owner | Spot/Actor·typed codec 관련 `ND-*` | 확인 | `NS-REG-009`, ZoneWorld process |
| `NS-IMP-008` | 언어별 logical structure | Gap owner | 없음 | 확인 | `NS-REG-011`, 구조 mapping |
| `NS-IMP-009` | 문서·runner·package | Gap owner | G6에는 `ND-IMP-004` 필요 | 확인 | `NS-REG-013`, `016`, runner result |
| `NS-TEST-001` | Test와 process evidence | Gap owner | 없음 | 확인 | `NS-REG-001`~`017`, CI result |

## 5. 구현 수준에서 확인된 gap

### NS-IMP-001 — one-way message 이름과 handler 의미가 공통 계약과 다름

**현재 판정: `확인`.**

공통 TicTacToe는 `LeaveGameMsg`, SupportChat은 `SetTypingMsg`를 one-way send로 선언한다. 현재 Node
shared contract, packet name, handler와 client는 각각 `LeaveGameReq`, `SetTypingReq`를 사용한다.
실제 handler type은 send이므로 transport는 one-way지만 wire 이름이 request를 나타낸다.

수정할 때 shared type, packet name, decorator, client submission, log와 regression을 한 번에 바꾼다.
이름만 `Msg`로 바꾸고 request/reply를 추가하거나, 호환 wrapper를 두 이름으로 유지하지 않는다.

### NS-IMP-002 — DeliveryDispatch가 application message에 route·Attempt를 노출하고 timestamp 계약이 다름

**현재 판정: `확인`.**

Node `BindCourierReq/Res`와 `BindCourierSessionReq/Res`는 `sessionRoute`를 노출한다. 공통 계약은 session
binding을 Framework가 관리하며 application response에는 courierId만 남기도록 요구한다.
`OfferDeliveryNotify`와 `CourierDecisionMsg`에도 공통 client-facing 계약에 없는 `attempt`가 있고,
status message는 `occurredAt: string`을 사용하지만 공통 계약은 `occurredAtUnixMs: int64`다.

Attempt는 Dispatch application state에 유지하고, 늦은 decision 판정에 필요한 correlation은 handler가
소유한 offer state에서 해결한다. Session route나 ActorRef를 새 DTO로 옮기는 방식은 허용하지 않는다.
Timestamp는 실제 JSON wire number와 ordering assertion을 함께 고정한다.

### NS-IMP-003 — GameQuest의 action inventory와 payload codec 책임이 공통 계약과 다름

**현재 판정: `확인`과 `contract 선행`.**

Node에는 공통 문서에 없는 `CompleteMissionReq/Res`, `UnlockFeatureReq/Res`, projection 삭제·재생성과
deactivate message가 있다. `JoinSessionRes`에는 공통 `playerId`가 없고, 공통 문서가 선언한
`ClosePlayerQuestMsg`도 없다. `GameplayMsg.payload`와 stored event payload는 `number[]`이며 shared
contract와 Domain에서 `TextEncoder`, `TextDecoder`, `JSON.stringify`, `JSON.parse`로 직접 변환한다.

먼저 client-facing action, maintenance-only command와 Store record를 분리한다. 공통 계약에 없는 action을
유지하려면 공통 sample 계약 변경 review가 선행되어야 하며, 다른 언어에 있다는 사실만으로 승인하지
않는다. Gameplay payload는 Framework typed JSON codec이 object를 처리하게 하고, application handler와
Domain에서 encode/decode helper를 제거한다. Domain event의 `*Event` 이름과 transport message를 별도
inventory로 관리한다.

### NS-IMP-004 — ShoppingMall 접수 응답과 workflow command가 공통 message 계약과 다름

**현재 판정: `확인`과 `contract 선행`.**

공통 `StartOrderRes`는 `orderId`와 `state: OrderState`를 반환하지만 Node는 `orderId`와 `status: string`을
반환한다. 공통 workflow request의 `sourceCommandId`가 Node request에 없으며 Node는
`PrepareInventoryReservedReq`, `PrepareInventoryEffectReq`, `VerifyExpectedVersionFenceReq`를 별도로
전송한다. 반대로 공통 `ReserveInventory`, `ReleaseInventory`, `AuthorizePayment` message는 shared
contract에 없다.

Client 접수 response는 공통 shape로 맞춘다. 내부 command는 process·Spot boundary를 넘는 application
message인지 Domain 내부 command인지 먼저 분류하고, public/shared message이면 공통 계약과 같은 이름과
완료 의미를 사용한다. Version fence와 effect interruption은 runner evidence 또는 internal test로 남길
수 있지만 공통 workflow를 대체하는 public 계약으로 승격하지 않는다.

### NS-IMP-005 — SupportChat conversation 생성 계약과 one-way typing 경계가 다름

**현재 판정: `확인`.**

공통 계약은 `ConversationCreateReq/Res`에 생성 시각과 전체 `ConversationState`를 고정한다. Node는
Spot create payload를 `ConversationCreateRequest`라는 Infrastructure local type으로 선언하고,
`OpenConversationApiRes`에서는 `conversationId`와 status만 반환한다. `SupportUserActorCreateReq`도 공통
message inventory 밖에 있다. `JoinConversationFailedNotify`에는 공통 문서에 없는 `isRetriable` field가
추가되어 있다.

Spot create payload가 application contract이면 `Shared/Contracts`의 공통 message로 이동하고 전체 state
완료 의미를 맞춘다. Actor create payload가 Framework lifecycle input이라면 internal-only로 표시하되
모든 Node role이 같은 declaration을 사용해야 한다. `isRetriable`은 공통 error 계약과 중복되는지 먼저
review하고, source에 있다는 이유로 client contract에 남기지 않는다. `SetTypingReq` 수정은
`NS-IMP-001`이 소유한다.

### NS-IMP-006 — TicTacToe leave 이름과 실패 notify inventory가 다름

**현재 판정: `확인`과 `test gap`.**

Node의 create, join, state와 `PlayerWinMilestoneEvent`는 공통 흐름과 대체로 대응한다. 그러나 one-way
leave는 `LeaveGameReq`를 사용하며 공통 `JoinGameFailedNotify`가 shared contract에 없다. Actor join
rejection이 어떤 terminal error 또는 notify로 client에 전달되는지 process assertion이 필요하다.

Leave 수정은 `NS-IMP-001`에서 수행한다. Join 실패는 공통 `JoinGameFailedNotify`를 실제 push로 구현할지,
Framework request error로 끝낼지 공통 문서의 현재 계약을 기준으로 고정하고 client self-check를 추가한다.
Entry Spot의 destroy 순서와 duplicate destroy no-op은 기존 lifecycle test를 유지하면서 실제 runner
evidence로 확인한다.

### NS-IMP-007 — ZoneWorld가 공통 movement·push message boundary와 wire shape를 사용하지 않음

**현재 판정: `확인`.**

Node `JoinWorldRes`, `EnterWorldRes`, `EnterZoneRes`는 공통 client/application 계약에 없는 `nodeId`를
포함하고 `ZoneChangedNotify`는 `nodeId`와 `transferred`를 노출한다. NodeId는 Ops application identity로
사용할 수 있지만 game client가 owner 위치나 relocation 결과를 받는 field가 되어서는 안 된다.

같은 zone 이동은 공통 `UpdatePositionMsg` 대신 Infrastructure local `UpdateZonePositionMsg`를 사용한다.
Bot tick은 `BotTickReq/Res`, actor push는 payload가 `unknown`인 `DeliverZoneNotification` wrapper로
처리하며 공통 `BotTickMsg`, `DeliverZoneStateMsg`, `DeliverWorldAnnounceMsg`와 다르다.

Shared contract에 공통 message를 선언하고 typed handler가 해당 message를 직접 처리하게 한다.
`unknown` payload와 constructor 이름 switch를 제거하고, application NodeId와 Framework RID를 분리한다.
Relocation 성공 여부는 runner evidence에서 확인하며 browser wire field로 보내지 않는다.

### NS-IMP-008 — sample별 logical implementation structure가 공통 구조와 다름

**현재 판정: `확인`.**

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

### NS-IMP-009 — Node sample README, completion marker와 package 기준이 현재 상태와 다름

**현재 판정: `documentation gap`과 `test gap`.**

Node sample README는 10.0.0 public API를 사용한다고 설명하지만 현재 root dependency는 local 11.1.0
archive를 가리킨다. 정확한 package version은 선행 `ND-IMP-*`에서 확정한 뒤 문서를 갱신한다.

Shell과 PowerShell 통합 runner는 일곱 sample을 모두 포함하지만 marker 정책이 일관되지 않다. Bingo는
`bingo=completed`만 출력하고 README의 `PASS Bingo.Ts`가 없으며, TicTacToe는 `PASS TicTacToe.Ts`만
출력하고 공통 `tictactoe=completed`가 없다. Sample별 phase marker는 실제 runner evidence로만 사용하고,
공통 completion marker와 `PASS <Sample>`의 역할을 구분한다.

### NS-TEST-001 — 공통 sample 전체를 덮는 exact inventory와 process evidence gate가 없음

**현재 판정: `test gap`.**

현재 sample regression은 파일 존재, 일부 symbol, topology와 lifecycle을 넓게 검사하지만 공통 문서의
모든 message·field·transport kind·state flow를 Node shared contract와 직접 비교하지 않는다. 일부 test는
DeliveryDispatch의 `SampleNames.routeMesh`처럼 이전 구조를 정답으로 고정한다.

Machine-readable inventory 또는 동일한 정보를 가진 contract fixture를 만들고, static contract와 실제
serialized payload를 함께 검사한다. Static test는 process smoke를 대체하지 않으며, runner가 client
self-check와 server evidence를 모두 확인하는지도 별도 gate로 검증한다.

## 6. Sample별 구현 path 검토 matrix

| Sample | 우선 읽을 Node path | 구현 단계에서 확인할 계약 | 완료 evidence |
|---|---|---|---|
| Bingo | `Shared/Contracts/bingo_messages.proto`, `protobuf-codec.ts`, `Server/Play`, `Client`, `Runner` | 공통 Protobuf subset, extension 등록, transfer/control type 범위, room·Actor lifecycle | Chromium response·push, reward, relocation와 Entry Spot destroy evidence |
| TicTacToe | `Shared/Contracts/messages.ts`, `Server/Api`, `Server/Play`, `Client`, `Runner` | one-way leave, join failure, turn, milestone publish와 manual topology | HTTP·STREAM payload, milestone, leave·destroy와 Redis cleanup |
| SupportChat | `Shared/Contracts/messages.ts`, `Server/Support`, `Server/Session`, `Client`, `Runner` | conversation create, metadata, typing, MessageSeq, idle close와 reconnect | assignment·chat·typing·close ordering과 binding 교체 evidence |
| DeliveryDispatch | `Shared/Contracts/messages.ts`, `Server/DispatchCenter`, `Tracking`, `CourierSession`, `Session`, `Client` | route field 금지, Attempt owner, timestamp, deadline·late decision | 정상·reassign 상태 sequence와 server offer/status evidence |
| ShoppingMall | `Shared/Contracts/messages.ts`, `Server/CommerceApi`, `OrderWorkflow`, `Shared/Store`, `Client` | StartOrder response, sourceCommandId, event fold, compensation와 projection rebuild | idempotency·failure·resume result와 durable event/projection evidence |
| GameQuest | `Shared/Contracts/messages.ts`, `Server/GameApi`, `QuestMission`, `Shared/Store`, `Client` | action set, object payload, dedupe, replay·reconcile, domain/store separation | EventId·progress notify·projection 결과와 duplicate append 0 evidence |
| ZoneWorld | `Shared/contracts.ts`, `Server/Gateway`, `ZoneNode`, `Ops`, `Client`, shared browser | typed movement/push, NodeId/RID, relocation, border, fanout와 Message Follow | headless·Chromium result, same-zone/border state, Ops와 cleanup evidence |

## 7. 수정 순서와 card gate

### G0 — 기준 snapshot과 선행 Framework ledger 재검증

현재 dirty working tree의 Node Framework, sample 문서와 CI 변경을 manifest로 고정한다. 선행 ledger의
status를 현재 source, exact interface, package와 test로 다시 계산한다. 이 단계에서는 sample을 수정하지
않는다.

### G1 — 공통 sample 계약과 internal-only 범위 확정

`NS-IMP-003`부터 `NS-IMP-007`까지의 extra/missing message를 `client-facing`, `server application`,
`Framework lifecycle input`, `persistence record`, `test/evidence-only`로 분류한다. `contract 선행` 항목은
공통 문서 review가 끝나기 전까지 구현 card로 이동하지 않는다.

### G2 — exact sample inventory와 실패 regression 고정

Sample, message, direction, transport kind, response, field, optionality, codec, owner와 evidence를 가진
inventory를 만든다. Node shared contract, packet name과 decorator를 비교해 `NS-REG-*`이 먼저 실패하게
한다. 기존 test가 현재 공통 계약과 충돌하면 본래 보장을 약화하지 않고 기준만 갱신한다.

### G3 — wire contract와 codec 책임 정렬

`NS-IMP-001`부터 `NS-IMP-007`의 shared contract, handler와 client를 함께 수정한다. GameQuest의 수동
payload encode/decode와 ZoneWorld의 `unknown` notification wrapper를 제거해 typed codec 경로로
연결한다. Bingo는 Protobuf를 유지하며 JSON sample과 같은 declaration으로 바꾸지 않는다.

### G4 — logical structure와 state owner 정렬

`NS-IMP-008`의 role mapping을 기준으로 source를 `Client`, `Shared`, `Server/<Role>`과 필요한
Domain/Application/Infrastructure 책임에 대응시킨다. A/B process 이름, runner probe와 evidence tool은
업무 role과 분리한다. Node decorator scan을 유지하고 handler 목록을 module code에 반복하지 않는다.

### G5 — runner, README와 package 설명 정렬

`NS-IMP-009`를 선행 package version 결정에 맞춰 수정한다. Shell·PowerShell inventory, completion
marker, Chromium 실행, readiness, 실행별 Redis와 cleanup을 같은 정책으로 맞춘다.

### G6 — 실제 process evidence와 CI 연결

일곱 runner를 fresh package로 실행한다. 각 sample에서 response·push assertion, state owner evidence,
failure와 cleanup을 수집한다. Full sample gate를 CI 필수 단계에 연결하고 common sample 문서나 Node
sample 변경이 workflow를 실행하도록 path filter를 확인한다.

### G7 — 최종 독립 audit

공통 sample, Node exact interface, production package, sample source, test, runner와 artifact를 다시
대조한다. `확인`, `contract 선행`, `test gap`, `blocked`가 하나라도 남으면 완료로 표시하지 않는다.

## 8. 유지할 test와 추가할 regression

기존 test는 범위를 줄이지 않는다. Lifecycle, generated routing ID, browser bundle, decimal, scale-out,
Entry Spot, domain과 ZoneWorld gate를 exact inventory test로 대체하지 않고 함께 유지한다.

| ID | 추가·변경할 regression | 직접 판정할 내용 |
|---|---|---|
| `NS-REG-001` | `CommonSampleContractInventoryMatchesNodeSharedContracts` | 일곱 sample의 message, field, optionality, enum과 codec 일치 |
| `NS-REG-002` | `CommonSampleTransportKindsMatchNodeHandlers` | Req/Res, Msg, Notify와 Event가 request/send/push/publish handler와 일치 |
| `NS-REG-003` | `TicTacToeLeaveUsesOneWayMessage` | `LeaveGameMsg`와 Entry Spot destroy, duplicate no-op |
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

## 9. 실행과 evidence 수집 계획

1. 기준 commit, dirty manifest, Node/npm version, package archive와 native artifact hash를 기록한다.
2. 공통 inventory와 Node shared contract 비교에서 의도한 실패만 남는지 확인한다.
3. Node Framework와 일곱 sample을 build하고, 영향받은 contract/domain test를 실행한다.
4. `run_samples.sh`와 `run_samples.ps1`의 inventory를 같은 fixture와 비교한다.
5. 각 sample runner를 실행별 Redis·config·log 디렉터리로 실행한다.
6. Client가 response, push, 순서와 금지 결과를 직접 assertion했는지 확인한다.
7. Server evidence에서 state owner, idempotency, relocation, deadline, cleanup 결과를 확인한다.
8. Bingo·TicTacToe·SupportChat·DeliveryDispatch·GameQuest의 browser client는 실제 Chromium에서 검증한다.
   ZoneWorld는 headless scenario와 shared Chromium client를 모두 실행한다.
9. 성공과 실패 뒤 process, browser, Redis와 temporary resource가 정리됐는지 확인한다.
10. Full Node regression, package consumer, sample gate와 CI workflow 결과를 ledger card에 기록한다.

고정 sleep, 이전 log, source symbol과 completion 문자열만으로 readiness나 성공을 판정하지 않는다.
실패한 sample을 제외한 나머지 결과만으로 전체 완료를 표시하지 않는다.

## 10. 완료 checklist

- [ ] 선행 Node Framework ledger의 production, package, E2E와 regression card가 현재 source 기준으로 닫혔다.
- [ ] 공통 sample 문서와 기존 dirty change의 기준 manifest를 보존했다.
- [ ] 일곱 sample의 message·field·transport·codec inventory가 작성됐다.
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
- [ ] Shell·PowerShell runner가 같은 inventory, marker, resource와 cleanup 정책을 사용한다.
- [ ] `NS-REG-001`~`NS-REG-017` 중 적용 대상이 통과하고 제외 항목에는 근거가 있다.
- [ ] 일곱 실제 process smoke에서 client self-check와 server evidence가 모두 확인됐다.
- [ ] Full Node regression, package consumer와 CI sample gate가 fresh package로 통과했다.
- [ ] 최종 독립 audit에서 미기록 sample spec gap이 0개다.
