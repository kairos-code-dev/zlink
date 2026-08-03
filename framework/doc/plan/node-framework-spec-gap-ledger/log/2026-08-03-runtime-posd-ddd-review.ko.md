# Node runtime card POSD·DDD 수시 검토 — 2026-08-03

이 log는 문서 마지막에 한 번 수행하는 review가 아니라, runtime gap을 수정하는 각 시점에
DDD의 상태 owner와 POSD의 복잡성 위험을 다시 확인한 기록이다. source·unit 변경은 기존 dirty
worktree를 보존한 상태에서 진행했다.

## 공통 DDD 판정

| card | 핵심 event·command | state owner | 유지한 invariant |
|---|---|---|---|
| ND-IMP-009 | `SpotRequestSubmitted` 전에 `SameSpotAwaitRejected`; `SpotSendAdmitted`는 계속 FIFO에 들어감 | `ZLinkSpotSerialExecutor`와 Spot outbound | 현재 Spot claim을 가진 handler는 같은 Spot awaited request를 transport에 제출할 수 없음 |
| ND-IMP-010 | handler dispatch failure가 STREAM error reply로 변환됨 | `ZLinkStreamSessionRuntime`의 wire reply boundary | public Framework error kind는 wire JSON `code` 문자열로만 노출됨 |
| ND-IMP-011 | Ready authority commit 뒤 `SpotRouteRemembered`, authority delete 뒤 `SpotRouteForgotten` | User Spot creation coordinator와 owning MeshNode route cache | route fence가 publication보다 먼저 cache에 있고, 삭제된 authority의 fence는 재사용되지 않음 |

각 card에서 application client가 route fence, raw frame, 내부 authority record를 직접 해석하지
않도록 했다. client-visible 결과는 public API와 process endpoint를 통해서만 관찰한다.

## ND-IMP-009 — 같은 claim awaited request

### 위험 신호

- `requestToSpot`마다 caller가 현재 Spot인지 판정하면 route·execution 정책이 호출부로 새어 나간다.
- transport에서 cycle을 감지하면 이미 request admission과 timeout이 시작되어 같은 claim invariant가
  늦게 실패한다.
- `AsyncLocalStorage`로 ambient Spot을 추적하면 execution owner와 lifecycle 경계가 분리되어
  continuation에서 의미가 달라질 수 있다.

### 검토한 대안과 선택

1. transport layer에서 source/target route를 비교한다. 모든 transport에 적용되지만 execution claim의
   owner를 transport가 알아야 하므로 정보가 새고, submit 전 실패를 보장하기 어렵다.
2. serial executor가 owning Spot ID를 보유하고 outbound request가 transport 시작 전에 검사한다.
   claim과 source identity가 같은 경계에 있고 one-way send는 별도 admission으로 유지할 수 있으므로
   이 대안을 선택했다.

`sourceSpotId`는 public user API가 아니라 내부 executor metadata다. request만 검사하고 self-send는
기존 FIFO 경로를 유지해 계약 범위를 넓히지 않았다.

### 검증

- `test/contract/entry-spot-serial-dispatch.test.js`: 25/25
- `TD-D6`: process PASS. `session-a-flow.log`, `play-a-flow.log`에서 self-cycle rejection과
  self-send FIFO evidence를 확인했다.

## ND-IMP-010 — STREAM error code 문자열

### 위험 신호

- numeric internal enum을 그대로 wire payload에 넣으면 언어별 public error name 계약이 깨진다.
- codec 계층에서 Framework exception을 해석하면 codec이 Framework error table을 소유하게 된다.

### 검토한 대안과 선택

1. stream codec이 숫자 enum을 이름으로 변환한다. 여러 runtime이 같은 mapping을 복제하므로
   information hiding이 깨진다.
2. `ZLinkStreamSessionRuntime`의 dispatch-error reply boundary에서 public enum name을 JSON
   `code`로 만들고 codec은 opaque payload만 운반한다. error ownership과 stream wire boundary가
   일치하므로 이 대안을 선택했다.

### 검증

- `test/contract/stream-session-runtime.test.js`: 51/51
- `NotFound`가 `{code: "NotFound", message: ...}`로 인코딩되는 회귀 assertion을 통과했다.

## ND-IMP-011 — User Spot Ready route fence lifecycle

### 위험 신호

- caller가 Ready 이후 route를 직접 기억하면 authority·route cache 책임이 application으로
  이동한다.
- publication 뒤에 route를 기억하면 첫 inbound request가 exact fence 없이 target에 도착할 수 있다.
- close에서 route를 잊지 않으면 삭제된 authority의 StoreVersion fence가 stale route로 남는다.

### 검토한 대안과 선택

1. MeshNode가 authority store를 주기적으로 재조회해 route를 복구한다. 첫 request latency와
   reconciliation 의존성이 생기고 commit-publication 순서를 직접 보장하지 못한다.
2. coordinator가 durable Ready commit과 authority delete의 경계에서 owning MeshNode에 route
   sink를 호출한다. route cache는 MeshNode가 소유하고 coordinator는 fence 값과 전이 순서만
   전달하므로 책임을 분리할 수 있다. 이 대안을 선택했다.

현재 구현의 route callback은 host 조립 경계에서만 연결된다. application sample에는 새 helper나
raw route API를 추가하지 않았다. `remember -> publish`와 `delete -> forget` 순서는 unit에서
직접 assertion한다.

### 검증

- `npm run verify:m6c-runtime`: 79/79
- `TD-D6`: process PASS
- message trace/file log: `framework/languages/node/e2e/AutomaticTurnDispatch/log/20260803-102712-710905/`
  의 `session-a-flow.log`, `play-a-flow.log`, `play-a.evidence.log`, `session-a.evidence.log`

## 후속 수시 점검

sample과 나머지 E2E card를 구현할 때도 같은 형식으로 상태 owner, invariant, POSD red flag와 두
대안을 먼저 기록한다. 새로운 public API, caller-side codec, raw-frame 우회, scenario 전용 runtime
branch가 필요해지는 경우에는 구현을 중단하고 contract 선행 card로 되돌린다.

## 2026-08-03 sample card 수시 검토

### SMP-ND-015 — Bingo replacement readiness evidence

현재 실패는 replacement process가 종료된 뒤에도 runner가
`bingo-room-peer ConnectionReady remote=...`라는 예전 application log marker를 기다리는
것이다. 현재 source의 `RoomRouterReadinessHandler`는 public `ZLinkRouteMeshRuntime.observe()`로
`bingo-room-status`를 기록하며, 예전 peer-event monitoring surface는 현재 registration composer의
계약에 포함되지 않는다. 따라서 stale marker를 server 내부에 다시 추가하는 것은 현재 계약을
복원하는 작업이 아니라 이전 구현을 재도입하는 작업이다.

위험 신호는 runner가 제거된 runtime event를 직접 요구하는 것, transport endpoint를 sample
server의 임의 log와 중복 기록하는 것, replacement 시점 이전의 readiness line을 재사용해
검증하는 것이다.

검토한 대안은 다음과 같다.

1. 예전 `monitoring.socket`과 peer event handler를 복원한다. 현재 source·composer·public
   runtime 경계와 맞지 않고, sample이 stale internal monitoring shape에 의존하게 되므로
   선택하지 않는다.
2. runner가 현재 공개 Location Store의 SpotMesh peer row와 `ZLinkRouteMeshRuntime` readiness
   경계를 사용해 replacement peer의 endpoint가 등록되고 retired endpoint가 제거되는 전이를
   확인한다. readiness 전이의 owner를 location/runtime 계층에 두고 runner는 관찰 결과만
   검증하므로 이 대안을 선택한다.

검증 기준은 `replacement present=true`, `old present=false`, drain terminal, replacement 이후
surviving peer의 readiness이며, temporary stderr debug나 fixed routing ID를 추가하지 않는다.

### SMP-ND-016 — TicTacToe deferred join 뒤 첫 request

TicTacToe isolated run의 trace는 `JoinGameReq`와 deferred join의 actor relay까지 기록하지만
첫 `PlaceMarkReq`는 stream server에 도착한 event가 없다. 브라우저 timeout은 결과이고, 현재
evidence는 client-side request sequencing 또는 deferred join completion 경계를 추가 확인해야
한다는 뜻이다. sample에 delay·retry를 넣어 해결하는 것은 원인과 순서를 숨기는 우회다.

검토할 대안은 다음과 같다.

1. sample client가 join 뒤 sleep/retry를 수행한다. timing에 의존하고 public actor/session
   membership invariant를 호출자에게 떠넘기므로 선택하지 않는다.
2. message trace의 `JoinGameReq`, deferred join commit, bound-session response, 첫
   `PlaceMarkReq`를 같은 correlation 흐름으로 대조하고, 실제 ordering invariant가 깨졌다면
   deferred-join runtime owner에서 queue/admission을 수정한다. sample handler의 public 호출
   표면을 유지할 수 있으므로 이 대안을 선택한다.

현재 evidence:

- Bingo isolated run: `/tmp/zlink-bingo.ts-BHmsJC/logs/` 및 `runDir=/tmp/zlink-bingo.ts-BHmsJC`
- TicTacToe isolated run: `/tmp/zlink-tictactoe.ts-0Tl5t4/logs/flow/`
- runtime 변경 후 targeted contract gates: 268/268

수정 뒤에는 sample process 결과와 trace/file log를 함께 다시 기록한다.

## 2026-08-03 codec 경계 수시 검토

### SMP-ND-017 — selective application serializer와 JSON fallback

선택적인 application serializer가 하나만 등록된 경우에도 Framework 내부 payload의 송신은 JSON으로
남겨야 하지만, 같은 registry를 사용하는 STREAM 수신은 해당 non-JSON payload를 복원해야 한다. 기존
수정은 실제 payload가 없는 default 선택까지 거부해 Bingo `AuthenticateReq`를 JSON text로 전달했고,
handler에서 `accessToken=undefined`가 관찰됐다.

검토한 대안은 다음과 같다.

1. sample에서 Framework 내부 요청마다 JSON 전용 경로를 직접 지정한다. sample과 runtime 경계를
   오염시키고 다른 sample에 같은 codec workaround를 반복하게 되므로 선택하지 않는다.
2. runtime이 송신은 실제 타입으로 선택하고, 수신 default decode에서는 단일 selective serializer를
   사용하되 JSON bytes는 먼저 JSON으로 복원한다. 선택 기준을 runtime 내부에 두고 public codec
   surface를 늘리지 않으므로 이 대안을 선택했다.

검증 기준은 selective serializer의 framework payload JSON fallback, application wire decode, Bingo
STREAM authentication을 각각 unit과 process/file-log로 확인하는 것이다.

## 2026-08-03 stable type projection 수시 검토

### SMP-ND-018 — explicit Spot stable type의 class-name alias

`addSpotFactory('game.room', RoomSpot, ...)`처럼 public stable type를 명시해도 descriptor projection이
`spotFactories`의 구현 class 이름을 별도 capability로 추가하고 있었다. 이 경우 placement resolver는
`game.room`과 `RoomSpot`을 서로 다른 User Spot type로 인식한다. application이 요청한 stable type과
실제 factory registration이 달라질 수 있으므로, location routing과 object creation의 source of
truth가 분리되는 위험 신호다.

검토한 대안은 다음과 같다.

1. sample이 구현 class 이름을 stable type로 사용하도록 바꾼다. 기존 public contract의 stable type를
   호출자에게 다시 노출하고, 여러 언어에서 class 이름에 의존하게 하므로 선택하지 않는다.
2. runtime descriptor projection이 explicit registration의 implementation을 확인하면 class-name
   legacy alias를 만들지 않도록 한다. 명시 registration은 그대로 유지하고, registration이 없는
   legacy factory만 class-name fallback을 사용하므로 compatibility와 stable type 단일성을 함께
   보장할 수 있어 이 대안을 선택했다.

검증:

- `backend-contract.test.js`: descriptor regression을 포함한 38/38
- Bingo 최신 process trace에서 `bingo.room` capability만 사용하도록 재검증 예정

## 2026-08-03 terminal completion race 정리

### ND-IMP-012 — Instance request terminal completion과 authority route 변경

이번 aggregate sample 실행에서 GameQuest의 마지막 Alice `SyncQuestProgressReq`가
`Instance Spot request failed with result 105 and errno 17`로 종료했다. message trace는 request가
`flow-api-b.log`에 도착하고 mission handler가 앞선 요청을 처리한 뒤 terminal completion 경계에서
실패한 흐름을 보인다. 같은 경로는 standalone GameQuest 실행에서 통과하기도 하므로, 현재 상태는
결정적 계약 위반으로 단정하지 않고 owner close/reassignment와 Instance terminal completion이
겹치는 process race 후보로 기록한다.

위험 신호는 두 가지다. 첫째, `ServiceStatefulRuntime`이 authority route가 terminal completion
중 변경된 경우를 generic internal failure로 변환하면 lifecycle 상태 변화가 public 결과에서 사라진다.
둘째, sample이 `105`를 무시하거나 sleep·retry를 추가하면 runtime ownership과 route fence 문제를
호출자에게 전가한다. 이는 POSD의 정보 은닉과 오류를 정의로 없애라 원칙에 어긋나며, DDD의
authority/stateful runtime 경계를 흐린다.

검토한 대안은 다음과 같다.

1. GameQuest에 delay, retry 또는 `105` 무시를 추가한다. process를 우연히 통과시킬 수 있지만
   sample이 runtime lifecycle 정책을 소유하게 되고 동일한 우회가 다른 sample에 반복되므로
   선택하지 않는다.
2. activation authority의 fence 검증과 stateful runtime의 terminal completion 경계에서 route
   변경을 명시적인 stale/lifecycle 결과로 분류하고, owner close·reassignment 회귀를 추가한다.
   address transport는 resolver 재시도만 담당하고 sample public surface는 유지할 수 있으므로
   이 대안을 선택했다. 단, 사용자가 작업 중단을 요청했으므로 구현과 회귀 추가는 다음 작업으로
   보류한다.

DDD 책임 경계는 다음과 같이 유지한다.

- activation authority: owner, generation과 route fence의 commit·release 순서를 관리한다.
- stateful runtime: Instance admission과 terminal completion의 결과를 결정한다.
- Spot manager: application materialization과 lifecycle activation을 수행한다.
- address transport: stale route resolver를 무효화하고 제한된 재시도를 수행한다.
- GameQuest sample: domain command와 client-visible assertion만 수행한다.

현재 evidence는 `npm run build` PASS, M6B 48/48, M6C 79/79, standalone GameQuest PASS다.
반면 `npm run verify:samples` aggregate는 GameQuest에서 `105/17`로 실패했으며, trace run은
`/tmp/zlink-gamequest.ts-4dGp9W`, 추가 재현 run은 `/tmp/zlink-gamequest.ts-gBTMw7`이다.
temporary `ZLINK_TRACE_*`와 sample 전용 gameplay trace는 commit 전에 제거했고, 이 항목은
full sample·E2E 완료로 표시하지 않는다.
