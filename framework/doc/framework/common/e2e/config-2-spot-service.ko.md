<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Location messaging](config-1-location-messaging.ko.md) | [다음: Pub/Sub](config-3-pubsub.ko.md)
<!-- framework-adapter-nav:end -->

# Config 2 — Spot 기반 서비스 배포

> 현재 .NET framework E2E는 `framework/languages/dotnet/e2e/SpotService` 아래에서 이
> 시나리오를 구현한다. 이 문서는 언어별 구현이 유지해야 할 검증 의도, 서버 역할, marker
> 기준을 설명한다.

상태를 소유하는 서비스 구성을 시작한다. entry Spot이 user Spot으로 라우팅하고, 해당 Spot에 actor와
bound session이 연결되는 배포다. 이 구성을 한 번 시작한 뒤 spot messaging과 session push가 실제
사용자 흐름에서 동작하는지 확인한다. 구조는 정본 샘플 Bingo·TicTacToe를 따르되, 검증에 필요한 흐름으로만
좁혔다.

## 1. 목적과 범위

- 다룬다: ChannelName↔Spot, Spot↔Spot messaging(send/request/publish), Logical Multicast,
  entry/user Spot 생성과 상태, actor join과 remote actor, bound session push, stream session,
  MeshNode direct·Spot direct 혼재 트래픽의 에러 계약과 소유권 독립, MeshNode scale-out 때 기존 owner
  유지와 신규 배치.
- 여기서 다루지 않는 것(다른 config로): codec 변주, channel provider의 scale·failover(Config 1), resilience(Config 5), location store 장애/복구(Config 6), 기존 actor owner의 명시적 relocation(Config 10), `Retire` handoff(Config 11).

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록하고, peer descriptor와 Spot authority를 framework lifecycle이 자동 갱신한다. |
| relocation store | 1 | 공식 Redis relocation store extension이 사용하는 공유 Redis instance. Actor가 다른 MeshNode의 Spot으로 join하는 시나리오가 immutable state·journal payload를 저장하도록 각 play host가 `AddRelocationStore(new ZLinkRedisRelocationStore(...))`로 별도 등록한다. |
| play(actor) 노드 | 2 (`play-a`, `play-b`) | Object role을 `Server`로 고정하고 Entry Spot, User Spot factory와 Actor factory를 등록한다. User Spot factory는 explicit `Disabled` relocation policy를 사용한다. Actor factory는 explicit `Snapshot` policy와 Actor type에 맞는 relocation adapter를 사용하며, 두 노드 모두 같은 stable type capability와 cross-node join에 필요한 Actor 전체, Spot 전체와 User Spot stable-type population capacity를 게시한다. MeshNode의 단일 ROUTER endpoint에 handler와 timer를 등록하고 `/evidence`·`/health`를 제공한다. |
| session(gateway) 노드 | 2 (`session-a`, `session-b`) | Object role을 `Client`로 고정하고 Location Store를 등록한다. Stream session을 호스팅하고 인자가 없는 `EnableActorDispatch()`로 global Actor dispatch capability를 활성화한다. 각자 stream endpoint를 사용하며 업무 로직은 play 노드가 처리한다. |
| consumer | 시나리오별 | ChannelName client + stream client. entry spot은 location store 기반으로 resolve(자기도 같은 store를 등록). |

이 배포의 핵심은 **연결과 로직을 나눠 둔 것**이다. client는 session(gateway) 노드에 stream으로
연결되지만 actor는 play 노드에 존재한다(session 노드는 STREAM/auth/relay 전용이라 actor를 직접
호스팅하지 않는다). Session handler는 stream header metadata의 `actor-id`를 읽고 current dispatch context와
payload를 선택한 bound Actor의 relay call에 함께
전달한다. Relay를 호출하면 request reply capability는 runtime으로 이전되고 Actor typed reply가 original
STREAM correlation을 한 번 완료한다. One-way packet에는 reply capability가 없으므로 relay 결과는 admission만
나타낸다. Actor가 내보내는 push도 session을 거쳐 client로 relay된다.

ActorId와 Spot ID는 물리 MeshNode RID와 독립된 global object identity다. Application은 domain key에서 같은
global identity를 일관되게 만들 수 있지만, physical owner를 계산하거나 고정하지 않는다. Framework가 object
role, stable type capability, placement weight와 capacity를 사용해 최초 owner를 선택하고 Location Store의 current
authority로 이후 호출을 라우팅한다. Automatic topology의 MeshNode RID는 framework가 lifecycle마다 발급하므로
restart 전후에 같은 RID나 endpoint를 요구하지 않는다.

handler 동작(공유):

- entry spot은 `JoinReq`로 user spot/actor를 만들고, user spot은 `StateReq(op)`로 상태를 바꾼다.
- **session이 끊겨도 actor membership은 자동으로 바뀌지 않는다.** Framework가 current
  binding snapshot 전체에 disconnect를 자동 all-settled 통지하며 application의 session
  callback은 Actor 목록을 순회하지 않는다.
- user spot은 명시적 `Close`로만 닫힌다(joined actor가 남아 있으면 거부). close 시
  언어별 `OnClosing` callback이 실행된다.
- 미등록 spot route/actor packet은 dispatch error로 처리되고 message-flow error evidence
  (`surface`=`spot_route`/`spot_actor`, `reason`=`no_handler`, `action`=`reply_error`/`drop`)에 남는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → spot 노드 → session 노드 순으로 시작하고 client
시나리오를 순차 실행한다. stream 시나리오는 consumer가 stream client로 접속한다. 실행이 끝나면
전용 prefix의 key를 정리하거나 disposable Redis instance를 버린다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 `key_transitions` 이상으로 켜 `corr=`로 디버깅한다.

## 4. 시나리오

### Track A — Spot 생성·상태·routing

#### SM-A1 entry spot 생성과 request

우선순위: `P0`

**검증 질문:** entry spot에 join을 보내면, user spot이 새로 만들어지고 그 id가 reply로 돌아오는가.

- 절차: consumer가 역할 server의 app endpoint로 요청하고 server가 startup evidence에 기록한 Entry Spot
  RID로 `RequestToSpot(JoinReq)`를 보낸다.
- 검증: entry Spot이 user Spot을 생성하고 reply에 Spot ID가 포함된다. Spot evidence에 생성 기록이
  남고, Spot manager `Find(SpotId)`가 Ready `SpotRef`의 owner RID·MeshName·ObjectGeneration을 반환한다.
- 세부 동작: Entry Spot dispatch + Spot 생성 + Spot authority 등록.

#### SM-A2 user spot request와 state mutation

우선순위: `P0`

**검증 질문:** 한 spot에 상태 변경을 연달아 보내도, 순서대로 누적되고 동시 요청에도 상태가 깨지지 않는가.

- 절차: 생성된 user spot에 `StateReq(op)`를 연속으로 보낸다.
- 검증: 각 reply가 누적 상태를 정확히 반영한다(순서 보존). 동시 요청에도 상태가 깨지지 않는다.
- 세부 동작: spot 단위 직렬 처리 + 상태 일관성.

#### SM-A3 global Spot ID route resolve

우선순위: `P1`

**검증 질문:** 특정 spot id로 보낸 request가 정확히 그 spot이 있는 노드에서만 처리되는가.

- 절차: 특정 spot id를 가진 user spot으로 직접 라우팅되는 request를 보낸다.
- 검증: 해당 spot이 있는 노드에서만 처리(다른 노드 evidence엔 없음).
- 세부 동작: spot route resolve의 정확성.

#### SM-A4 global Spot ID와 current owner resolve

우선순위: `P0`

**검증 질문:** 같은 domain key에서 얻은 global Spot ID 호출이 physical owner를 계산하지 않고도 항상 current
owner에 도달하는가.

- 절차: 앱이 entity key(예: order id, player id)에 대응하는 global Spot ID를 일관되게 사용해 request를 보낸다.
  Target MeshNode RID, endpoint와 owner generation은 호출 인자로 전달하지 않는다.
- 검증: 같은 key의 호출은 같은 logical Spot identity를 사용하고 Location Store가 가리킨 current owner에 도달한다.
  Scale-out만으로 owner는 바뀌지 않지만 `Retire`·relocation으로 owner가 바뀐 후에는 같은 RID가 새 owner를
  resolve한다. Application이 node owner 규칙을 계산하지 않는다.
- 세부 동작: global Spot identity와 physical owner selection의 분리. Scale-out 뒤 기존 owner 유지와 신규 대상
  배치는 Track G의 SM-G2에서 다룬다.

#### SM-A5 Stage wrapper

우선순위: `P2`

**검증 질문:** Spot에 Stage wrapper(playhouse Stage 류)를 추가해도 Spot messaging·timer·lifecycle이
같은 의미로 동작하는가.

- 절차: Stage wrapper(playhouse Stage 류)를 추가한 Spot에 request와 timer command를 보낸다.
- 검증: Stage wrapper를 통해도 spot 메시징·timer·lifecycle이 같은 의미로 동작한다.
- 세부 동작: SPOT 위 Stage wrapper 계층.

#### SM-A6 spot lifecycle (initialize·close)

우선순위: `P1`

**검증 질문:** Spot을 만들 때 initialize lifecycle, 닫을 때 `OnClosing`이 정확히 한 번씩 실행되며,
current Actor membership이 있으면 상태를 바꾸지 않고 close를 거부하는가.

- 절차: User Spot을 생성해 initialize 시점을 evidence에 남긴다. Actor가 join한 상태에서 exact
  `SpotRef`로 `Close`를 먼저 호출하고, 그 결과를 확인한 뒤 Actor를 leave 또는 destroy하고 다시 닫는다.
- 검증: 첫 close는 `false`를 반환하며 Spot admission·authority·Actor membership과
  `OnClosing` count는 바뀌지 않는다. Framework가 Actor를 숨게 이동하거나 파괴하지 않는다. 명시적
  leave 또는 destroy 뒤 close만 성공하고 `OnClosing(ExplicitClose)`가 직렬화된 close 경로에서 한 번 실행된다.
- 세부 동작: spot 생성·종료 lifecycle 콜백.

#### SM-A7 spot 타입 불일치 (SpotTypeMismatch)

우선순위: `P1`

**검증 질문:** 이미 만든 spotId를 다른 spot 타입으로 다시 `GetOrCreate`하면, `SpotTypeMismatch`로 명확히 거부되는가.

- 절차: stable type A와 RID로 User Spot `GetOrCreate`를 완료한 뒤, 같은 RID를 stable type B의
  `GetOrCreate`로 다시 요청한다.
- 검증: 두 번째 요청은 `SpotTypeMismatch` public error로 실패한다(`ZLinkFrameworkException`). 처음 만든 spot과 그 상태는 영향받지 않는다.
- 세부 동작: 같은 rid 재사용 시 타입 일치 강제.

#### SM-A8 worker offload (`Context.RunCpuWorker`)

우선순위: `P2`

**검증 질문:** 무거운 CPU 작업을 `RunCpuWorker`로 spot의 직렬 스레드 밖에서 실행해도, 결과는 spot context로 안전하게 전달되어 반영되는가.

- 절차: spot handler가 무거운 CPU 작업을 `Context.RunCpuWorker(...)`로 offload하고 `.Yield(...)`로 기다린 뒤, 결과를 받아 spot 상태에 반영한다. 같은 시간대에 그 spot/노드로 다른 request도 보낸다.
- 검증: worker가 spot 직렬 스레드 밖(bounded worker pool)에서 실행된다. `.Yield(...)`가 turn을 반납하므로 그동안 같은 spot의 다른 처리가 진행되고, worker 결과는 spot 직렬 컨텍스트로 돌아와 상태에 안전히 반영된다(경합 없음).
- 세부 동작: **worker 축(어느 스레드에서 실행되는가)과 turn 축(같은 spot이 진행하는가)은 별개다** ([04 §1.2](../../spec/04-async-execution-policy.ko.md)). `.Async(...)`로 기다리면 같은 offload라도 turn은 유지된다 — 그 대비는 [config-8 TD-C4](config-8-execution-turn.ko.md)가 소유한다.
- 세부 동작: spot 직렬성 유지 + 무거운 작업 offload.

#### SM-A9 Store-backed User Spot publication barrier

우선순위: `P0`

**검증 질문:** Store-backed User Spot create가 final generation을 확보한 뒤에도 factory·initialize가 끝나기
전에는 remote caller에게 existing Spot으로 공개되지 않는가.

- 절차: User Spot `Creating` CAS 뒤 factory·initialize 사이의 internal test barrier를 닫고 다른 process에서
  manager `Find`와 direct request를 시도한다. Barrier를 열어 `Ready` CAS까지 완료한 뒤 다시 호출한다.
  별도 반복에서는 factory·initialize를 실패시키고 fenced delete 응답을 한 번 손실시킨다.
- 검증: `Ready` 전 `Find`는 `SpotRef`를 반환하지 않고 handler count는 0이다. 성공 뒤에는 NewObject CAS가
  발급한 같은 final Spot generation으로 resolve·request가 성공한다. 실패는 partial Ready row·scope·timer를
  남기지 않고 exact read로 delete 결과에 수렴하며 다음 caller만 더 높은 object·authority owner generation으로
  새 create를 시작한다.
- 세부 동작: Store-backed User Spot의 `Creating`·factory·initialize·`Ready` publication barrier.

#### SM-A10 Entry Spot ID 발급과 lifecycle

우선순위: `P0`

**검증 질문:** Object Server가 Entry Spot ID를 MeshNode RID와 독립적으로 발급하고 descriptor의 exact
mapping을 lifecycle 전체에서 사용하는가.

- 절차: Diagnostic prefix를 `play`로 지정해 Object Server를 시작하고 MeshNode descriptor와 Entry Spot
  location을 읽는다. 같은 lifecycle에서 descriptor를 여러 번 갱신한 뒤 같은 endpoint로 replacement
  lifecycle을 시작한다. 별도 반복에서는 Location Store test double이 첫 Entry Spot ID identity claim에
  active conflict를 반환한다.
- 검증: Entry Spot ID는
  `^play-entry-[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$`와 일치하고
  MeshNode RID도 같은 UUID v4 bit·lowercase canonical 표현을 사용하되 두 UUID는 독립적이다.
  Full MeshNode RID에 marker나 suffix를 이어 붙인 값이 아니다. 같은 lifecycle에서는 RID가 유지되고
  replacement에서는 MeshNode RID와 Entry Spot ID가 모두 바뀐다. Active conflict는 기존 record를 변경하지
  않고 첫 claim에서 `SpotIdConflict`로 startup을 실패하며 Ready descriptor나 Entry Spot authority를
  남기지 않는다. 두 번째 UUID 생성과 두 번째 claim은 0건이다.
- 검증: Descriptor의 Entry Spot ID와 lifecycle generation은 실제 Ready Entry Spot location과 정확히
  일치한다. Actor create, `JoinEntrySpot`과 relocation evidence는 이 mapping을 사용하며 RID 문자열 parse로
  Entry Spot을 계산하지 않는다.
- 검증: `NewClaim`은 descriptor publication과 같은 mutation에서 Entry Spot global identity를 원자적으로
  claim한다. Descriptor의 immutable digest에는 Entry Spot ID가 포함되며, 같은 lifecycle에서 이를 바꾸는
  update는 side effect 없이 거부된다.
- 검증: 정상 종료의 descriptor remove와 owner-loss cleanup의 `RemoveAllByOwner`는 해당 lifecycle이 claim한
  Entry Spot identity만 함께 제거한다. 이전 lifecycle의 지연된 cleanup은 replacement lifecycle의 descriptor,
  Entry Spot authority와 identity claim을 제거하지 못한다.
- 세부 동작: Entry Spot ID format, active collision 즉시 실패, lifecycle stability, descriptor mapping,
  immutable digest와 lifecycle-fenced cleanup.

#### SM-A11 Entry Spot 예약 ID 형식 거부

우선순위: `P0`

**검증 질문:** Caller가 Framework 전용 Entry Spot ID 형식을 User·Instance Spot ID로 지정하면 side
effect 전에 거부되는가.

- 절차: `play-entry-f67e5507-21c6-4a15-bfd1-4a240bfab371`을 User Spot `GetOrCreate` Spot ID로 사용하고, 별도
  요청에서는 같은 Spot ID로 Instance Spot direct intent를 제출한다. Location Store reservation, target 선택과
  factory 실행 횟수를 기록한다.
- 검증: 두 요청 모두 `InvalidConfiguration`으로 끝난다. Location Store reservation, target 선택과 factory
  실행 횟수는 모두 0이며 authority, `Reserved` typed capacity bundle과 creation payload를 남기지 않는다.
- 세부 동작: Framework-issued Entry Spot namespace의 pre-admission validation.

#### SM-A12 User Spot automatic ID active collision

우선순위: `P0`

**검증 질문:** Framework가 User Spot `Create`에 발급한 UUID v4 Spot ID가 active authority와 충돌할 때 문제를
새 identity retry로 숨기지 않는가.

- 절차: UUID generator를 고정하고 User Spot `Create`의 첫 global authority reservation에 같은 Spot ID의 active
  authority conflict를 반환한다. UUID 생성, Location Store reservation과 factory 호출 횟수를 기록한다.
- 검증: 호출은 기존 authority를 바꾸지 않고 `SpotIdConflict`로 즉시 끝난다. UUID 생성과 reservation은
  각각 1건이고 factory 호출은 0건이며 두 번째 UUID, reservation과 creation payload는 없다.
- 세부 동작: Framework-issued logical Spot ID의 UUID v4와 active collision 즉시 실패.

#### SM-A13 SpotId string boundary와 legacy binary 거부

우선순위: `P0`

**검증 질문:** SpotId가 transport RID와 분리된 UTF-8 exact string으로 모든 public·wire·Store 경계에서
같게 처리되는가.

- 절차: UTF-8 encoded 크기가 각각 1, 255와 256 bytes인 Spot ID를 `GetOrCreate`와 direct call에
  제출한다. `Room`과 `room`, NFC `é`와 NFD `é`를 각각 별도 Spot ID로 생성한다. 마지막으로 legacy
  wire의 Spot field에 유효하지 않은 UTF-8 binary를 넣어 target admission에 제출한다.
- 검증: 1-byte와 255-byte ID는 exact value로 생성·조회·호출되고 256-byte ID는 Store·factory side
  effect 전에 `InvalidConfiguration`으로 거부된다. 대소문자 쌍과 Unicode 표현 쌍은 normalization 없이
  서로 다른 네 authority key를 만든다. Invalid UTF-8 frame은 application queue와 Location Store에
  도달하지 않고 protocol failure로 끝나며 binary 값을 base64나 replacement character로 바꾸지 않는다.
- 검증: 같은 Spot ID의 User·Instance·Entry kind 또는 stable type 충돌은 namespace를 분리하지 않고
  `SpotTypeMismatch` 또는 identity conflict로 끝난다. `MeshName`을 바꿔도 새 identity namespace가
  만들어지지 않는다.
- 세부 동작: UTF-8 1..255-byte bound, case-sensitive exact equality, no normalization, global namespace와
  v11 binary Spot RID clean break.

### Track B — actor join과 lifecycle

actor join은 actor가 어느 노드의 mailbox에서 실행되느냐에 따라 local과 remote로 나뉜다. 두
경우를 모두 본다.

#### SM-B0 explicit type global create와 existing-only find

우선순위: `P0`

**검증 질문:** Actor create·GetOrCreate가 global authority와 factory 하나로 수렴하고 manager `Find`가
Missing Actor를 생성하지 않는가.

- 절차: `play-a`와 `play-b`에 같은 stable Actor type factory를 등록한다. 먼저 manager `Find`로 Missing
  ID를 조회한 뒤 `play-a`만 eligible하도록 placement weight를 설정하고 explicit stable type·Actor ID의
  `Create`와 duplicate `GetOrCreate(...).InMesh(...)`를 실행한다. 이어서 manager `Find`로 같은 ID를
  조회한다.
- 검증: Missing `Find`는 empty이며 두 factory를 실행하지 않는다. Create와 GetOrCreate는
  `play-a`의 factory 하나와 같은 Actor generation으로 수렴하고 `play-b` factory count는 0이다.
  `Find`는 생성된 current `ActorRef`만 반환한다. Public create call은 target node·endpoint·owner token을
  받지 않고 eligible Object Server를 선택한다.
- 세부 동작: explicit stable type global lifecycle과 existing-only manager `Find`.

#### SM-B0A Actor creation 승인·거절과 concurrent terminal result

우선순위: `P0`

**검증 질문:** Entry Spot creation callback이 Actor별로 직렬화되고 서로 다른 operation은 자신의 request와
reply를 유지하며, 거절된 Actor와 capacity가 공개되지 않는가.

- 절차: 서로 다른 process의 caller가 같은 Actor ID와 stable type이지만 서로 다른 request와 `OperationId`로
  `GetOrCreate`를 동시에 호출한다. 첫 callback은 typed reply와 함께 거절한다. 두 번째 operation은 Creating
  정리를 기다린 뒤 새 reservation으로 자신의 request를 callback에 전달하고 승인된다. 두 번째 operation의
  동일한 `OperationId`를 다시 전달하고, Ready Actor에 새로운 `OperationId`로 `GetOrCreate`도 반복한다.
- 검증: 같은 Actor의 factory와 creation callback은 동시에 실행되지 않는다. 첫 operation은 자신의
  `Rejected` reply를 받고 두 번째 operation은 첫 reply를 공유하지 않은 채 `Created`와 자신의 reply를 받는다.
  동일한 `OperationId` 재전달은 callback을 다시 실행하지 않고 같은 semantic terminal result를 반환한다.
  Command reply는 재전달 request의 current correlation과 reply route로 새로 encode하며,
  Ready Actor에 대한 새 operation은 callback 없이 `Existing`을 반환한다. `Created`만 `ActorRef`, initial Entry
  membership, Ready authority와 active capacity를 가진다. `Rejected`는 `Find`와 Actor messaging에서
  조회되지 않고 Ready authority·active capacity·destroy callback을 남기지 않으며 pending capacity가 0으로
  돌아온다. 거절 뒤 새 call은 새 reservation ID로 실행된다. Ready 반복은 `Existing`을 반환하고 factory와
  creation callback을 호출하지 않는다. 최초 생성에서는 Entry Spot의 `OnActorJoin`과 `OnJoinedActor`가 모두
  0건이다.
- 세부 동작: Actor별 reservation 직렬화, operation-scoped terminal replay, atomic reject cleanup과 Entry creation lifecycle.

#### SM-B1 local actor join

우선순위: `P0`

**검증 질문:** 같은 MeshNode의 Entry Spot에 있는 Actor가 local User Spot으로 join하면 membership callback과
Actor queue가 그 owner node에서만 실행되는가.

- 절차: `play-a`만 해당 stable Actor type의 eligible capacity를 갖게 한 뒤 global Actor manager가 explicit
  Actor ID·stable type으로 create한다. Factory 실행과 initial Entry membership의 `Ready` evidence를 확인한 뒤,
  `play-a`에 만든 User Spot으로 Actor가 `JoinSpot(globalSpotId)`을 호출한다.
- 검증: factory와 `Ready` publication은 `play-a`에서 한 번 관찰된다. Join evidence 순서는 target
  `OnActorJoin` → Location Store membership CAS commit → target `OnJoinedActor` → source `OnLeaveActor`이며,
  세 callback과 후속 Actor request가 `play-a`에서만 실행된다. Same-node join은 relocation adapter와 Relocation
  Store를 사용하지 않는다.
- 세부 동작: local actor join + local mailbox 실행 + exact membership lifecycle.

#### SM-B2 remote actor join

우선순위: `P0`

**검증 질문:** source Entry Spot의 Actor가 다른 MeshNode의 User Spot으로 join하면 state를 relocation하고 후속
호출이 target Actor queue에 도달하는가.

- 절차: Actor placement capacity는 `play-a`만 허용해 Actor를 initial Entry Spot에 먼저 생성하고, User Spot
  placement capacity는 `play-b`만 허용해 target Spot을 만든다. `play-a`의 Actor가 target global Spot ID로
  `JoinSpot`을 호출한다. Caller는 target node RID·endpoint·owner token을 전달하지 않는다.
- 검증: target `OnActorJoin` accept 뒤 source adapter `Capture`, Relocation Store immutable root 저장, target factory와
  adapter `Restore`·journal staging, Location Store owner·membership CAS commit, target `OnJoinedActor`, source
  `OnLeaveActor`, accepted message·journal replay, durable cleanup, `Completed` CAS, route ACK·steady normalization
  순서가 evidence에 남는다.
  Target Ready와 caller success는 이 순서가 끝난 뒤에만 공개된다. 그 뒤 global Actor ID request는 `play-b`의
  Actor queue에서 처리되고 `play-a`의 old Actor queue는 실행하지 않는다.
- 세부 동작: explicit Snapshot policy를 사용하는 cross-node Actor join과 mailbox 이동.

#### SM-B3 요청 message 객체 충실도

우선순위: `P0`

**검증 질문:** 여러 필드·중첩 객체·배열이 든 메시지를 보냈을 때, 받은 쪽에서 한 글자도 빠지거나 변형되지 않고 그대로 도착하는가.

- 절차: actor join과 후속 actor request를 **여러 필드·중첩 객체·컬렉션**을 가진 message 객체로 보낸다(예: `JoinReq{ actorId, displayName, level, attributes:{...}, tags:[...] }`).
- 검증: actor/handler가 받은 객체의 모든 필드가 보낸 값과 정확히 일치한다 — 필드 누락·타입 손상·null 치환·컬렉션 순서 변형이 없다. reply도 핵심 필드를 그대로 반영하고, evidence에 수신 필드가 기록된다.
- 세부 동작: 요청 payload 객체 round-trip 충실도.

#### SM-B4 remote actor request

우선순위: `P1`

**검증 질문:** 다른 노드에 있는 actor로 request를 보내도, 노드 경계를 넘어 처리되고 reply가 돌아오는가.

- 절차: `play-a`의 consumer가 `play-b`에 있는 actor로 request를 보낸다.
- 검증: 노드 경계를 넘어 대상 actor에서 처리되고 reply가 돌아온다.
- 세부 동작: cross-node actor routing.

#### SM-B5 actor 미등록 request

우선순위: `P0`

**검증 질문:** handler 없는 actor packet을 보내면 error로 명확히 실패하고, 그 이유가 observer에 남는가.

- 절차: handler 없는 actor packet 이름으로 request를 보낸다.
- 검증: error reply로 끝나고 message-flow error evidence(`surface`=`spot_actor`, `reason`=`no_handler`, `action`=`reply_error`)가 남는다.
- 세부 동작: actor negative path + 관측(enum 필드).

#### SM-B6 actor leave vs disconnect callback

우선순위: `P0`

**검증 질문:** 명시적 leave는 `OnLeaveActorAsync`, 비정상 disconnect 통지는 `OnDisconnectActorAsync` — 두 경로가 각각 맞는 콜백을 actor당 한 번씩만 부르는가.

- 절차: join한 Actor에 대해 (a) application이 명시적으로 `leaveActor(...)`를 호출하거나,
  (b) Actor를 bind한 STREAM connection을 비정상 종료한다. (b)의 session disconnect callback은
  Actor 목록을 순회하거나 `NotifyDisconnectedAsync(...)`를 다시 호출하지 않는다.
- 검증: (a)는 Actor 소유 노드의 Spot에서 `OnLeaveActorAsync`만 실행한다. (b)는 disconnect 시점의
  current binding snapshot에 있는 Actor마다 `OnDisconnectActorAsync`를 exact binding identity 기준
  최대 한 번 자동 실행한다. 두 경우 모두 Actor destroy를 뜻하지 않으며, (b)는 Spot membership을
  변경하지 않는다. 한 Actor callback이 실패해도 다른 Actor callback과 session cleanup은 계속된다.
- 세부 동작: 명시적 leave와 physical disconnect automatic all-settled notification의 구분.

#### SM-B7 actor handler 실행 순서

우선순위: `P1`

**검증 질문:** factory·Ready publication과 membership callback이 끝난 뒤 packet admission이 열리고, 같은
Actor의 packet 순서가 보존되는가.

- 절차: Factory와 membership callback을 기록하는 Actor를 생성하고 Entry Spot에서 User Spot으로 join한 직후
  여러 packet을 연속으로 보낸다.
- 검증: factory → initial `Ready` publication 뒤 join의 target `OnActorJoin` → Location Store membership CAS
  commit → target `OnJoinedActor` → source `OnLeaveActor`가 완료되고 나서 packet dispatch가 시작된다. 같은
  Actor의 packet은 Actor queue에서 직렬 순서로 처리되며 evidence에 전체 순서가 남는다.
- 세부 동작: Actor publication·membership lifecycle과 packet admission 순서.

#### SM-B8 ActorRef를 사용하는 exact actor destroy

우선순위: `P1`

**검증 질문:** current `ActorRef`로 exact incarnation을 파괴하면 해당 Actor만 정리되고, ID가 같은 다른
generation에는 영향을 주지 않는가.

- 절차: Actor를 User Spot에 join한 뒤 `JoinEntrySpot`을 완료하고 manager `Find`로 current `ActorRef`를 얻는다.
  Manager의 exact destroy에 이 ref를 전달한다. 같은 ref로 한 번 더 호출한 뒤 같은 Actor ID를 새 generation으로
  다시 만들고 이전 ref로 destroy를 시도한다.
- 검증: 첫 destroy만 current mailbox·binding·authority를 정리하고 `true`를 반환한다. 같은 incarnation이
  없을 때는 `false`, 새 generation이 존재할 때 이전 ref는 `ActorGenerationStale`로 끝난다. Destroy 자체는
  membership 이동이 아니므로 완료된 leave 이후 `OnLeaveActor`를 다시 호출하지 않으며 새 incarnation은
  유지된다.
  `JoinEntrySpot`은 target Entry Spot의 `OnActorJoin` 없이 membership을 commit한 뒤 target
  `OnJoinedActor`, source User Spot `OnLeaveActor` 순서로 완료된다.
- 세부 동작: exact `ActorRef` fence를 사용하는 actor destroy와 idempotency.

#### SM-B9 Spot join admission accept와 reject

우선순위: `P1`

**검증 질문:** 이미 존재하는 Actor의 join proposal을 target `OnActorJoin`이 심사하고, reject하면 source
owner와 membership을 그대로 유지하는가.

- accept 절차: Actor를 source Entry Spot에 먼저 생성하고 factory·Ready evidence를 확인한다. Accept하도록
  설정한 local·remote User Spot에 각각 join한다.
- accept 검증: Local 반복은 target `OnActorJoin` → membership CAS commit → target `OnJoinedActor` → source
  `OnLeaveActor` 순서로 완료되고 caller는 accept reply를 받는다. Remote 반복은 callback만 local 순서에
  대입하지 않고 SM-B2의 Snapshot relocation·durable cleanup·Ready evidence를 그대로 사용한다.
- reject 절차: 별도 Actor를 source Entry Spot에 먼저 생성하고 current ActorRef·owner·membership generation을
  기록한다. Reject하도록 설정한 local·remote User Spot에 각각 join한다.
- reject 검증: target `OnActorJoin`만 실행되고 caller는 timeout이 아닌 분류된 reject reply를 받는다. Source
  owner·Actor state·Entry membership과 generation은 바뀌지 않는다. Source `OnLeaveActor`, target
  `OnJoinedActor`, adapter `Capture`·`Restore`, Relocation Store root와 target Actor factory evidence는 모두 없다.
- 세부 동작: 생성과 join admission을 분리한 accept·reject 및 pre-commit source 보존.

#### SM-B10 Object role과 Location Store startup 경계

우선순위: `P0`

**검증 질문:** Object role 또는 Actor dispatch를 구성하면서 Location Store를 빠뜨리면 socket bind 전에
실패하고, role `None` manual topology는 Node·Channel 기능만 제공하는가.

- startup 오류 절차: Location Store 없이 Object role `Client` 또는 `Server`를 선택한 host와, Actor dispatch를
  활성화했지만 Location Store를 등록하지 않은 stream host를 각각 시작한다.
- startup 오류 검증: 두 host 모두 listener socket bind와 handler admission 전에 configuration error로 실패한다.
  Runtime-local Actor manager, hidden authority, factory 실행과 synthetic owner generation은 생성되지 않는다.
- manual 절차: 별도 host는 object role을 선택하지 않은 `None` 상태에서 fixed RID와 explicit peer endpoint를
  사용하는 manual MeshNode·Channel만 구성한다. Node direct와 Channel send/request를 실행하고 Actor manager,
  Spot manager, factory와 Actor dispatch capability가 노출되지 않는지 확인한다.
- manual 검증: Node direct와 Channel operation은 성공하지만 Actor·Spot create, find, message와 factory는 사용할
  수 없다. Actor dispatch를 추가로 활성화하면 role `None` 조건 때문에 startup configuration error가 된다.
- 세부 동작: Store-backed object runtime과 role `None` manual Node·Channel topology의 명확한 경계.

#### SM-B11 Store-backed Actor publication barrier

우선순위: `P0`

**검증 질문:** Store-backed Actor create가 final `ActorRef`를 먼저 확보하더라도 factory와 initial Entry
membership이 끝나기 전에는 remote caller에게 existing Actor로 공개되지 않는가.

- 절차: Actor `Creating` CAS 뒤 factory·initial membership 사이의 internal test barrier를 닫는다. 다른
  process에서 manager `Find`와 direct Actor request를 시도한 뒤 barrier를 열어 initialize와 `Ready` CAS를
  완료한다. 별도 반복에서는 factory와 initial membership을 각각 실패시키고 fenced delete 응답도 한 번
  손실시킨다.
- 검증: Barrier 전 `Find`는 existing Actor를 반환하지 않고 remote handler count는 0이다. Barrier 뒤에는
  NewObject CAS가 발급한 같은 final Actor generation으로 resolve·request가 성공한다. 실패한 반복은 partial
  Ready row·membership·runtime object를 남기지 않고 exact read로 delete 결과에 수렴하며, 다음 caller만 더 높은
  object·authority owner generation으로 새 create를 시작한다.
- 세부 동작: Store-backed Actor의 `Creating`·factory·membership·`Ready` publication barrier.

### Track C — messaging 방향

여기서는 메시지가 흐르는 방향(channel→spot, spot→channel, spot→spot)별로, 한 시나리오 안에서
send·request·publish verb와 timeout·미등록 negative를 모두 본다(같은 점검 매트릭스를 방향만 바꿔
적용).

#### SM-C1 channel → spot messaging

우선순위: `P0`

**검증 질문:** 외부 channel에서 spot으로 들어오는 방향에서 request·send·publish·timeout·미등록 처리가 모두 올바르게 동작하는가.

- request: ChannelName client가 spot으로 request → 정확한 reply, spot evidence에 기록.
- send: one-way send → reply 없이 spot evidence에 command 기록.
- publish: channel이 publish → 구독한 spot이 수신(미구독 spot은 미수신).
- timeout: 느린 spot handler에 짧은 timeout → client timeout 예외, 이후 같은 연결의 정상 messaging 비오염.
- 미등록: handler 없는 spot packet → request는 error reply, send는 drop. message-flow error evidence(`surface`=`spot_route`, `reason`=`no_handler`, `action`=`reply_error`/`drop`)가 남는다.
- 세부 동작: 외부 channel에서 spot으로 들어오는 방향 전체 verb + negative.

#### SM-C2 spot → channel messaging

우선순위: `P0`

**검증 질문:** Spot이 ChannelName으로 내보내는 send/request와 Logical Multicast가 제대로 동작하는가.

- request: spot handler가 처리 중 외부 channel로 request → reply를 받아 처리에 반영.
- send: spot → channel one-way → 대상 ChannelName handler evidence에 기록.
- Logical Multicast: Spot이 target ChannelName과 topic으로 publish하면 target channel의 remote node마다 한 번 도달하고, 각 node의 구독 Spot만 수신한다.
- timeout: 느린 channel handler에 짧은 timeout → spot 쪽 timeout 처리, spot 상태 비오염.
- 미등록: 대상 channel에 handler 없음 → request는 error reply, send는 drop + observer marker.
- 세부 동작: Spot에서 ChannelName으로 나가는 send/request + Logical Multicast + negative.

#### SM-C3 spot → spot messaging

우선순위: `P1`

**검증 질문:** spot 사이에서 직접 주고받는 request·send·publish가 양쪽 evidence와 일치하게 동작하는가.

- request: 한 user spot이 다른 user spot으로 request → reply, 양쪽 evidence 일치.
- send: spot → spot one-way → 대상 spot evidence 기록.
- publish: spot 이벤트를 다른 spot이 구독 수신.
- timeout: 느린 대상 spot에 timeout → 소스 spot 정상 유지.
- 미등록: 대상 spot에 handler 없음 → error/drop + observer marker.
- 세부 동작: spot 간 직접 messaging 전체 verb + negative.

#### SM-C4 local Spot이 없는 MeshNode의 Logical Multicast

우선순위: `P1`

**검증 질문:** local Spot을 하나도 호스팅하지 않는 MeshNode도 ChannelName을 대상으로 Logical Multicast를 제출할 수 있고, 그 channel의 구독 Spot이 받는가.

- 절차: local Spot이 없는 MeshNode가 target ChannelName과 topic으로 Logical Multicast를 제출한다.
  전송에는 같은 MeshNode ROUTER 연결을 사용한다.
- 검증: target channel에 참여한 원격 node마다 routed message가 한 번 도달하고, 각 수신 node에서는 topic이 일치하는 local Spot만 이벤트를 받는다. 미구독 Spot은 받지 않는다.
- 세부 동작: Spot을 호스팅하지 않는 origin MeshNode의 Logical Multicast. SM-C2와 달리 publisher가 Spot context가 아닌 node client다.

#### SM-C5 Logical Multicast의 node 간 도달 (제출 성공 ≠ 수신)

우선순위: `P0`

**검증 질문:** 한 node의 Spot이 제출한 Logical Multicast를 **다른 node**의 구독 Spot이 실제로 받는가. origin의 제출 성공만으로 통과시키지 않는다.

- 절차: `play-a`의 Spot이 target ChannelName과 topic으로 Logical Multicast를 제출한다. Remote subscriber
  node는 weight `1`, `10000`, `0`을 각각 사용하고 positive node에는 topic 구독 Spot을 하나씩 둔다.
- 검증: 성공 기준은 **수신 측 evidence**다. Weight `1`과 `10000`인 두 remote node에는 routed message가
  정확히 한 번씩 도달하고 각 구독 Spot이 한 번 받는다. Weight 크기가 전송 횟수를 늘리지 않으며 weight
  `0` node는 새 remote target에서 제외된다. 발행 측의 publish 성공 로그는 보조 증거일 뿐 단독으로는
  통과가 아니다. 연결 미성립이면 발행이 성공으로 보여도 시나리오는 실패해야 한다.
- 세부 동작: origin node가 target channel의 remote node마다 routed message를 한 번 제출하고, 수신 node가 topic과 일치하는 local Spot에 message ref를 공유한다.

#### SM-C6 Logical Multicast ROUTER backpressure

우선순위: `P0`

- 절차: 수락 가능한 remote peer와 ROUTER 송신 HWM에 도달한 remote peer를 함께 둔 상태에서 public
  asynchronous publish를 한 번 실행한다.
- 검증: Runtime은 확정한 snapshot의 target마다 bounded admission을 최대 한 번 시도하고 막힌 target을
  backpressure detail에 기록한다. 별도 blocking publish나 동기 `TrySubmit` 계열은 사용하지 않는다. 앞에서
  수락된 peer의 전달은 뒤 target의 실패 때문에 취소되지 않으며, publish detail의 remote
  snapshot·admitted·dropped 수가 실제 수신
  evidence와 일치해야 한다.
- 별도 회귀: local matching Spot queue 하나를 가득 채우고 다른 local target은 수락 가능하게 두어,
  가득 찬 target만 drop 수에 기록되고 다른 target은 수신하는지 확인한다.

### Track D — session bind·relay·push와 stream

stream session은 actor에 bind되어 양방향으로 메시지를 relay한다. 여기서는 bind 위치(local/remote),
actor가 존재하는 Spot 종류(entry/user), 한 session에 bind된 actor 수(단일/다중)를 나눠서 본다.

#### SM-D1 actor session bind & relay — local

우선순위: `P0`

**검증 질문:** gateway에 연결된 client가 같은 play 노드의 actor에 bind했을 때, 양방향 relay(client→actor, actor→client push)가 동작하는가.

- 절차: `play-a`만 eligible하도록 placement weight·capacity를 설정해 global Actor ID로 Actor를 만들고 current
  `ActorRef`를 얻는다. Consumer가 `session-a` gateway에 stream으로 접속·auth한 뒤 이 exact ref를 session bind에
  한 번 제출한다. 이어서 client → actor request(`actor-id` metadata 포함)를 보내고 actor → client push를
  트리거한다. Caller는 `play-a`의 RID나 endpoint를 bind 입력으로 전달하지 않는다.
- 검증: bind 성립 후 client packet이 `header.Metadata.Get("actor-id")`로 선택한 bound Actor에 current dispatch
  context와 함께 relay되어 처리된다. Client request의 Actor typed reply는 original STREAM correlation을 한 번
  완료하고, Actor push는 같은 session으로 relay되어 client가 받는다. Bind하지 않은 client는 받지 않는다.
- 세부 동작: exact `ActorRef` bind와 같은-process owner의 Actor relay.

#### SM-D2 actor session bind & relay — remote

우선순위: `P0`

**검증 질문:** bind 대상 actor가 gateway와 다른 원격 play 노드에 있어도, 노드 경계를 넘는 양방향 relay가 동작하는가.

- 절차: `play-b`만 eligible하도록 placement weight·capacity를 설정해 global Actor ID로 Actor를 만들고 current
  `ActorRef`를 얻는다. Consumer가 `session-a` gateway에 연결한 뒤 이 exact ref를 session bind에 한 번 제출하고
  같은 양방향 relay를 수행한다. Caller는 target MeshName·RID·endpoint를 bind 입력으로 전달하지 않는다.
- 검증: client packet이 gateway → 원격 play 노드로 노드 경계를 넘어 actor에 relay되고, 원격 actor의 push가 gateway를 거쳐 같은 session으로 돌아온다.
- 세부 동작: gateway↔원격 play 노드 cross-node relay.

#### SM-D3 entry spot vs user spot actor bind

우선순위: `P1`

**검증 질문:** bind 대상 actor가 entry spot이나 user spot에 있을 때 bind·relay·push가 같은 의미로 동작하는가.

- 절차: bind 대상 actor가 (1) entry spot에 있는 경우와 (2) user spot에 있는 경우 각각 session을 bind한다.
- 검증: 두 경우 모두 bind·양방향 relay가 정상이며, push·dispatch가 actor가 존재하는 Spot 종류와 무관하게 같은 의미로 동작한다.
- 세부 동작: spot 종류별 bind 동등성.

#### SM-D4 한 stream에 여러 actor bind

우선순위: `P0`

**검증 질문:** 한 stream에 actor를 여럿 bind했을 때, `actor-id`로 지정한 actor에게만 정확히 가고(오배달 없이), id 없이 보내면 실패하는가.

- 절차: 한 stream session에 여러 Actor(예: `actor-x`, `actor-y`)를 bind한다. Stream header metadata
  `actor-id`에 대상 Actor ID를 실어 보내고, session handler가 current dispatch context와 payload를 선택한
  bound Actor의 relay call에 전달한다. 각 Actor가 push를 낸다.
- 검증: 각 packet이 `actor-id`로 지정한 Actor로만 relay되고 교차 오배달이 없다. Request relay는 해당
  Actor reply로 original STREAM correlation을 한 번 완료하고, 각 Actor push는 같은 session으로 relay되어
  client가 Actor별로 구분해 받는다. **Relay 대상 선택은 application 책임이다.** Framework는 `actor-id`
  metadata를 자동 해석하지 않으며, session이 대상을 찾지 못하면 application이 relay를 호출하지 않고
  current dispatch에 실패를 반환한다([31 §3](../../spec/server/31-session-actor-dispatch.ko.md#3-inbound-dispatch와-reply)).
- 세부 동작: 다중 Actor bind, explicit target 선택과 dispatch-context relay.

#### SM-D4A rebind와 stale binding 격리

우선순위: `P0`

**검증 질문:** 같은 Actor를 Session A에서 Session B로 rebind한 뒤 Session A의 이전 binding token으로
도착한 relay와 늦은 disconnect가 Session B의 current binding에 영향을 주지 않는가.

- 절차: Actor X를 Session A에 bind한 뒤 같은 `ActorRef`를 Session B에 명시적으로 rebind한다. Session A의
  이전 binding token과 저장 route로 relay를 제출하고, Session A의 이전 connection lifecycle에서 늦게
  도착한 disconnect도 제출한다. 각 Session에는 Actor X 외의 Actor도 함께 bind해 다중 Actor binding을
  유지한다.
- 검증: stale relay는 Actor X의 Session B binding으로 전달되지 않고 typed stale 결과로 끝난다. Session A의
  늦은 disconnect는 Session B binding을 해제하거나 Actor X의 disconnect callback을 다시 실행하지 않는다.
  Session A와 B에 함께 bind된 다른 Actor의 binding도 유지된다. Actor X의 `ObjectGeneration`과 Spot
  membership은 바뀌지 않는다.
- 세부 동작: cross-session explicit rebind, exact binding identity와 stale lifecycle event 격리.

#### SM-D4B 저장 route relay와 stale mapping

우선순위: `P0`

**검증 질문:** bind 뒤 relay·disconnect가 Location Store를 다시 읽지 않고 저장 route만 사용하며,
stale route도 숨은 lookup이나 retry 없이 한 번의 forwarding 또는 typed stale 결과로 끝나는가.

- 절차: exact `ActorRef` bind 직후 Location Store read counter를 기록하고 이후 read를 차단한다.
  Valid stored route로 request, push와 disconnect를 수행한다. 이어 stale route에 대해 active committed
  forwarding mapping이 있는 경우와 mapping이 만료되거나 없는 경우를 각각 실행한다.
- 검증: bind 이후 두 경우 모두 Location Store read counter 증가는 `0`이다. Valid route는 owner lease와
  local admission deadline 안에서 성공한다. Active mapping은 같은 exact identity를 최대 한 번
  forwarding하고, expired/missing mapping은 typed stale 결과로 끝난다. Runtime은 fresh `ActorRef`를
  lookup하거나 Store 장애를 이유로 retry·deadline 연장을 하지 않는다.
- 세부 동작: stored route no-Store dispatch와 single-forward stale mapping.

#### SM-D5 physical session disconnect automatic fan-out

우선순위: `P0`

**검증 질문:** 연결이 끊기면 Framework가 current binding 전체에 통지하면서 Actor와 membership은 유지하는가.

- 절차: local·remote Actor를 여러 개 bind하고 한 Actor callback에는 실패를 주입한 뒤 STREAM을 비정상
  종료한다. Application의 session disconnect callback은 Actor 목록을 순회하지 않는다. Automatic 통지와
  별도 `NotifyDisconnectedAsync` 논리 통지를 동시에 실행하는 race도 반복한다.
- 검증: disconnect 시점의 current binding 전체가 저장 route로 통지를 받고 각 current Spot callback은 exact
  binding identity마다 최대 한 번 실행된다. 한 Actor 실패 뒤에도 다른 Actor 통지와 Session cleanup이
  완료된다. Actor ObjectGeneration과 Spot membership은 유지된다. Location Store read를 차단해도 owner
  lease와 local admission deadline 안의 저장 route 통지는 성공하며, expired route의 deadline은 Store
  장애 때문에 연장되지 않는다.
- 세부 동작: automatic all-settled fan-out, binding identity dedupe와 no-Store route.

#### SM-D5A application logical disconnect

우선순위: `P0`

- 절차: physical connection을 유지한 채 선택한 bound Actor 하나에 public `NotifyDisconnectedAsync` 대응
  operation을 호출한다.
- 검증: 선택 Actor의 current Spot callback 완료 뒤 terminal이 완료되고 다른 bound Actor callback은
  실행되지 않는다. Actor, membership과 connection은 유지된다.
- 세부 동작: 명시적 logical notification과 physical disconnect의 구분.

#### SM-D6 bound session push 타깃팅

우선순위: `P0`

**검증 질문:** actor 상태가 바뀌면 그 변화가 bind한 session에게만 push되고, bind 안 한 consumer는 못 받는가.

- 절차: consumer가 session에 bind한 뒤, 다른 경로로 그 actor 상태를 바꾼다.
- 검증: 상태 변경이 bound session으로만 push되어 해당 consumer가 수신한다. bind 안 한 consumer는 받지 않는다.
- 세부 동작: bound session push 타깃팅.

#### SM-D7 stream session auth와 packet dispatch

우선순위: `P0`

**검증 질문:** stream 연결과 auth가 성공해야 messaging이 동작하는가(auth 실패는 명확한 오류인가).

- 절차: consumer가 stream client로 접속·auth하고 packet을 주고받는다.
- 검증: auth 성공 후 request/notify가 정상 dispatch된다. auth 실패는 공개 오류.
- 세부 동작: stream session 수명 + dispatch.

#### SM-D8 stream reconnect

우선순위: `P1`

**검증 질문:** stream 연결을 끊었다가 다시 연결하면, 연결이 끊어진 시점의 pending은 자동 재전송 없이
실패하고 재접속 뒤 auth·rebind를 수행했을 때 messaging이 정상적으로 재개되는가.

- 절차: stream 연결을 끊었다가 재접속한다. 끊김 시점의 pending request 결과와 재접속 후 동작을 본다.
- 검증: 끊김 시 pending request는 `Disconnected`로 실패하고 자동 재전송되지 않는다. 재접속은 새 session이므로 app이 다시 auth·rebind하고, 필요한 상태는 replay/snapshot packet으로 복구한다. rebind 후 messaging이 정상 재개된다.
- 세부 동작: 재접속 = 재auth·rebind + app replay (자동 재전송 없음).

#### SM-D9 inbound observer

우선순위: `P1`

**검증 질문:** stream inbound observer가 들어오는 메시지의 종류·이름·seq를 관측 evidence로 남기는가.

- 절차: stream inbound observer를 등록하고 메시지를 받는다.
- 검증: observer가 inbound 종류·이름·seq를 관측 evidence로 남긴다.
- 세부 동작: inbound 관측.

#### SM-D10 stream backpressure

우선순위: `P1`

**검증 질문:** 처리 속도보다 빠르게 메시지를 입력해도 정해진 흐름 제어대로 동작하고, session 상태가
손상되거나 다른 session에 영향을 주지 않는가.

- 절차: stream client가 처리 속도보다 빠르게 메시지를 주고받아 backpressure를 유발한다.
- 검증: 정해진 흐름 제어 규칙(버퍼/대기/drop)대로 동작하고, session 상태가 유지되며 다른 session에 영향이 없다.
- 세부 동작: stream 흐름 제어.

#### SM-D11 stream request와 channel request 혼합

우선순위: `P1`

**검증 질문:** 같은 consumer가 stream과 channel을 함께 사용해도, 두 경로가 서로 간섭하지 않고 각 reply를 원래 경로로 반환하는가.

- 절차: 같은 consumer가 stream session request와 일반 channel request를 섞어 보낸다.
- 검증: 두 경로가 서로 간섭 없이 각자 정상 동작하고, reply가 올바른 경로로 돌아온다.
- 세부 동작: stream·channel 혼합 경로 격리.

#### SM-D12 session 재접속 이전성 (다른 연결 서버로)

우선순위: `P0`

**검증 질문:** client가 다른 gateway로 갈아타 재접속해도, play 노드의 actor 상태는 그대로 유지되고 rebind 후 messaging이 이어지는가.

- 절차: client가 연결 서버 `session-a`에 연결해 play 노드 actor와 messaging한 뒤 연결을 끊고
  **다른 연결 서버 `session-b`**로 재접속한다.
- 검증: 로직(play 노드)의 actor 상태는 연결 서버와 무관하게 유지된다. client는 `session-b`에서 다시 auth하고 같은 actor id로 rebind한 뒤 snapshot + 이후 event로 상태를 복구한다(자동 이전·재전송 아님). rebind 후 messaging이 정상 재개된다.
- 세부 동작: 연결/로직 분리 — 다른 gateway로 재auth·rebind 이전성.

#### SM-D13 stream heartbeat

우선순위: `P1`

**검증 질문:** heartbeat가 정상이면 session 연결이 유지되고, heartbeat가 중단되면 `Disconnected`로
감지된 뒤 current binding 전체에 자동 통지되는가.

- 절차: stream 연결을 유지하며 heartbeat 주기를 지나친다. 한쪽이 heartbeat를 멈춘다.
- 검증: 정상 heartbeat 동안 session 연결이 유지된다. heartbeat 중단은 connector에서
  `Disconnected` 상태나 오류로 감지되며 server stream session의 `OnDisconnectedAsync`가 실행된다.
  Framework는 disconnect 시점의 current binding snapshot을 고정하고 각 exact binding identity에
  `OnDisconnectActorAsync`를 최대 한 번 all-settled 방식으로 자동 실행한다. Session handler는 Actor
  목록을 순회하거나 `NotifyDisconnectedAsync`를 다시 호출하지 않는다.
- 세부 동작: heartbeat 기반 liveness와 physical disconnect automatic notification.

#### SM-D14 stream TLS

우선순위: `P2`

**검증 질문:** TLS 위에서도 bind·relay·push가 평문과 똑같이 동작하고, 잘못된 인증서는 거부되는가.

- 절차: stream 연결을 TLS로 수립해 auth·messaging을 수행한다.
- 검증: TLS 위에서 bind·relay·push가 평문과 같은 의미로 동작한다. 잘못된 인증서는 연결 거부.
- 세부 동작: TLS stream 전송.

#### SM-D15 cross-role 다단 push 사슬

우선순위: `P0`

**검증 질문:** 다른 role이 시작한 상태 변화가 actor send를 거쳐 bound session push로 client stream까지 끝까지 도달하는가.

- 절차: client와 무관한 별도 role(예: tracking)이 channel request를 받아 `SendToActor`로 대상 actor에 상태 변경 메시지를 보내고, 그 actor의 핸들러가 bound session으로 notify를 push한다. client는 stream에서 그 notify를 기다린다.
- 검증: client가 notify를 실제 수신한다 — 이것만이 성공 기준. 중간 각 hop(channel 수신, actor send 도달, push 발신)의 flow trace가 남아 단절 시 지점을 특정할 수 있다. actor send의 대상 핸들러가 미등록이면 silent drop이 아니라 관측 가능한 실패가 남는다.
- 세부 동작: role 경계 2회 이상을 넘는 push 사슬의 end-to-end 도달 + hop별 관측성. (발신 role들의 로그는 전부 정상인데 client만 timeout인 결함 — 중간 hop의 핸들러 미등록 — 이 실제로 있었다.)

### Track E — negatives와 timer

#### SM-E1 spot route 미등록 request

우선순위: `P0`

**검증 질문:** handler 없는 spot route packet은 error로 실패하고, 그 이유가 observer에 남는가.

- 절차: handler 없는 spot route packet으로 request를 보낸다.
- 검증: error reply + message-flow error evidence(`surface`=`spot_route`, `reason`=`no_handler`, `action`=`reply_error`).
- 세부 동작: spot route negative path(enum 필드).

#### SM-E2 spot timer

우선순위: `P1`

**검증 질문:** spot이 등록한 timer가 정해진 주기대로 발화하고, 그 효과(상태 변화·push)가 관측되는가.

- 절차: spot이 timer를 건다.
- 검증: timer가 한 번/주기대로 발화하고 그 효과(상태 변화·push)가 관측된다.
- 세부 동작: spot timer 발화.

#### SM-E3 idle timer 기반 명시적 close

우선순위: `P1`

**검증 질문:** 일정 시간 활동이 없으면 timer handler가 spot을 `CloseAsync`로 닫고, 활동 중이거나 actor가 남아 있으면 안 닫는가.

- 절차: user spot이 `AddTimer<THandler>`로 주기 timer를 돌리며 마지막 활동 시각을 기록한다. idle 임계를 넘으면 timer handler가 (joined actor가 모두 떠난 뒤) `CloseAsync`를 호출한다. 활동이 오면 마지막 활동 시각을 갱신한다.
- 검증: idle 초과 시 spot이 `CloseAsync`로 닫히고 `OnClosingAsync` 콜백이 evidence에 기록된다. joined actor가 남아 있으면 close가 거부된다(먼저 actor leave 필요). 활동 중엔 닫히지 않는다.
- 세부 동작: 애플리케이션 idle timer + 명시적 `CloseAsync` + `OnClosingAsync` (자동 actor callback 아님).

#### SM-E4 spot timer overrun 정책

우선순위: `P1`

**검증 질문:** timer handler가 주기보다 느려 tick이 밀릴 때, 설정한 `OverrunPolicy`(SkipLateTicks / CatchUpBounded / DelayNextTick)대로 동작하는가.

- 절차: 주기보다 처리가 느린 timer handler를 각 `ZLinkTimerOverrunPolicy`로 설정해 실행한다.
- 검증: 정책별로 관측이 일치한다 — `SkipLateTicks`는 밀린 tick을 건너뛰고, `CatchUpBounded`는 정해진 한도까지만 따라잡으며, `DelayNextTick`은 다음 tick을 미룬다. evidence의 발화 패턴이 정책과 맞는다.
- 세부 동작: timer overrun 정책별 tick 처리.

### Track F — MeshNode ROUTER에서 Channel·Spot routed path 공존

ChannelName handler, RID direct handler와 Spot direct handler가 같은 MeshNode ROUTER를 사용할 때 namespace,
대상 선택과 lifecycle이 서로 섞이지 않는지 검증한다. 별도 중계 계층이나 Spot 전용 socket은 사용하지
않는다. 외부 코드는 `SendToSpot`·`RequestToSpot`에 global Spot ID만 전달하며 Framework가
Location Store에서 current owner·generation·Mesh를 resolve한다. `SpotRef`는 exact close와 관측용이지
messaging target이 아니다.

#### SM-F1 route client → local target Spot

- 절차: consumer가 같은 node가 소유한 global Spot ID로 `RequestToSpot`·`SendToSpot`을 제출한다.
- 검증: request는 target Spot의 reply 하나로 완료되고 send는 target Spot evidence에만 기록된다.

#### SM-F2 ToSpot 다른 MeshNode owner 호출

우선순위: `P0`

공통 구현 요구: 모든 framework 언어에서 필수다.

- 절차: source와 target MeshNode를 서로 다른 process로 시작하고 target에 User Spot을 만든다.
  Source 역할 server가 MeshName, owner RID, endpoint와 `SpotRef`를 전달하지 않고 global Spot ID만으로
  `RequestToSpot`·`SendToSpot`을 제출한다.
- 검증: Location Store가 가리킨 remote owner의 target Spot handler만 각각 한 번 실행한다. Request는
  remote reply로 terminal-once 완료되고 send는 source outbound admission으로 완료된다. Evidence의
  ObjectGeneration과 owner generation이 current authority와 일치하며 source가 다른 MeshNode를 relay로
  사용하거나 Spot 전용 socket을 만들면 실패다.

#### SM-F3 ChannelName·RID direct·Spot direct namespace 분리

- 절차: 같은 MeshNode에 같은 packet name을 사용하는 ChannelName request, RID direct request와 Spot direct
  request를 섞어 제출한다.
- 검증: 각 메시지는 channel handler, node handler, target Spot handler에 정확히 한 번 도달하고 각각의
  reply context가 유지된다.

#### SM-F4 target Spot 없음과 stale generation

- 절차: 존재하지 않는 Spot ID로 existing-only request와 send를 제출한다. User Spot을 닫고
  같은 RID로 새 incarnation을 만든 뒤 이전 `SpotRef`로 exact `Close`를 시도한다.
- 검증: Missing request와 send는 target-not-found 계약으로 완료되고 Instance cold activation을 시작하지
  않는다. 이전 `SpotRef` close는 stale-generation으로 끝나며 새 incarnation을 닫지 않는다.
  정상 ChannelName과 RID direct messaging은 영향을 받지 않는다.

#### SM-F5 Spot lifecycle과 MeshNode lifecycle 분리

- 절차: ChannelName request와 Spot direct request를 처리한 뒤 target user Spot을 닫고 ChannelName request를
  다시 제출한다.
- 검증: 닫힌 Spot 경로만 실패하고 MeshNode peer와 ChannelName handler는 ready 상태를 유지한다.

#### SM-F6 같은 MeshName의 다중 MeshNode에서 원격 Spot·Actor 도달

- 절차: 같은 MeshName에 속한 source와 target MeshNode를 서로 다른 프로세스로 시작한다. 두 노드는 Object
  Server role, 같은 stable Actor type factory, explicit Snapshot policy·adapter, Relocation Store와 충분한
  typed population capacity를 §2대로 구성한다. Source Entry Spot에 Actor를 먼저 생성하고 target User Spot을
  만든다. Source Spot은 global Spot ID로 target Spot에 request와 send를 제출하고, Actor는
  `JoinSpot(globalSpotId)`로 target User Spot에 join한다. 별도 Spot 전용 ROUTER나 PUB/SUB socket은 구성하지
  않는다.
- 검증: 시작 순서와 무관하게 두 MeshNode가 ready가 된 뒤 request reply, send evidence와 Actor join
  evidence가 target Spot에만 기록된다. Cross-node join은 adapter capture·restore와 Relocation Store root를
  사용하고 target `OnActorJoin` → target staging·restore → authority commit → target `OnJoinedActor` → source
  `OnLeaveActor` → accepted message·journal replay → durable cleanup → `Completed` → route ACK·steady
  normalization → Ready 순서를 지킨다. 호출자는 owner RID, endpoint, generation 또는 내부 route frame을
  조립하지 않는다.

### Track G — MeshNode 증설, 장애와 복구

여기서는 MeshNode가 추가되거나 spot/actor/session 노드가 비정상 종료되고, 동시 트래픽이 경합할 때
stateful 경로가 어떻게 동작하는지 본다. Config 5(resilience)는 channel provider를 다루므로,
spot 배포(play/session 노드)를 쓰는 시나리오는 Config 2에 둔다. 프로세스를
비정상 종료시키는 시나리오는 harness의 `kill`/`stop`/`restart` 연산을 전제한다(없으면 "미구현(하네스
대기)").

#### SM-G1 play 노드 crash와 복구

우선순위: `P0`

**검증 질문:** actor·bound session이 설정된 play 노드가 비정상 종료되면 그 노드의 상태만 영향을
받고, 같은 역할의 replacement와 다른 노드에서의 application 복구가 각각 명시적인 재join·rebind로
정상화되는가.

- 절차: 먼저 `play-a`에 actor join + session bind 상태를 만들고 `play-b`에도 독립 기준 actor와
  session을 준비한 뒤 `play-a`를 `SIGKILL`한다. crash 직전 처리 중인 actor request와 crash 뒤 같은 global
  Actor ID로 보낸 request의 실패를 각각 관찰한다. 다음 두 복구 경로를 **모두** 실행한다.
  1. old MeshNode descriptor·Actor authority projection이 owner lease 만료로 성공 조회에서 제외될 때까지 기다린다. 그 뒤
     같은 `play-a` 역할을 automatic topology로 restart하고 새 lifecycle의 MeshNode RID·endpoint와 owner
     generation이 게시되는지 확인한다. 이전 RID나 endpoint 재사용을 요구하지 않는다. Replacement의 Actor
     type capability와 recovery readiness가 게시될 때까지 bounded wait한다.
     Application은 global Actor ID의 manager `GetOrCreate(...).InMesh(...)`를 실행해 current `ActorRef`를
     얻고 session에 rebind한 뒤 application snapshot/replay를 적용한다.
  2. 다시 같은 장애를 만든 뒤 old Actor authority projection이 성공 조회에서 제외될 때까지 기다리고,
     `play-b`만 eligible하도록 placement weight·capacity를 고정한 뒤 같은 global Actor ID의 manager
     `GetOrCreate(...).InMesh(...)`를 실행한다. 반환된 current `ActorRef`를 session에 rebind한 뒤
     application snapshot/replay를 적용한다.
- 검증: crash 직전 처리 중인 request는 연결 단절이 먼저 확정되면 retriable `RouteNotConnected`,
  handler 완료 여부를 caller가 확정할 수 없으면 설정한 request timeout 안의 timeout으로 끝난다.
  owner lease 만료로 old Actor authority projection이 성공 조회에서 제외된 뒤, Actor를 다시 만들기 전에
  global Actor ID request는 `ActorRouteNotFound`로 끝난다. Actor를 같은 ID와 새 generation으로 다시 만든 뒤
  같은 global Actor ID의 fresh request는 성공한다. 이전 `ActorRef`는 session rebind 같은 exact-ref lifecycle
  operation에서 `ActorLocationStale`로 거부되며 messaging target으로 사용하지 않는다. 각 실패는 설정한
  timeout 안에 종료되고 어느 단계에서도 old handler가
  실행되지 않아야 한다. stream 연결 종료는 framework request 오류와 섞지 않고
  connector/session의 `Disconnected`로 별도 확인한다. `play-b`의 기준 actor·session은 영향받지 않는다.
  두 복구 경로 모두 current reference 확정·rebind 뒤 messaging이 정상화되고 필요한 상태는 application snapshot/replay로
  복원된다. 자동 상태 이전이나 실패 request의 자동 재전송은 단언하지 않는다.
- 세부 동작: stateful 노드 crash 격리 + 새 automatic MeshNode identity를 사용한 replacement 복구 + global Actor manager와
  explicit application state를 사용한 복구.

#### SM-G2 MeshNode scale-out과 신규 배치

우선순위: `P1`

**검증 질문:** MeshNode를 추가해도 기존 owner는 유지되고, 새 Actor·User Spot create만
eligible placement target을 선택하는가.

- 절차: `play-a`만 eligible한 상태에서 기존 Actor와 User Spot을 만들고 owner evidence를
  기록한다 → `play-b`를 추가하고 peer·type capability·capacity·placement weight 반영을 bounded
  wait로 확인한다 → 기존 대상에 다시 request한다 → `play-a`의 placement weight를 0으로
  바꾼 뒤 새 global ID로 Actor `Create`·`GetOrCreate`와 User Spot `Create`·`GetOrCreate`를 public
  fluent call로 실행한다.
- 검증: node 추가 전 만든 대상의 owner는 `play-a`로 유지되고 후속 request도 같은 owner가 처리한다.
  새 Actor와 Spot은 positive placement weight와 capacity를 가진 `play-b`에 배치된다. 두 node의
  대상이 동시에 정상 처리되고 node 추가만으로 기존 owner가 이동·자동 재분배되었다고
  단언하지 않는다.
- 세부 동작: MeshNode 증설 발견 + 기존 owner 유지 + 신규 global object remote placement.
  기존 owner를 바꾸는 동작은 Config 10의 Actor relocation 또는 Config 11의 `Retire` handoff에서
  검증한다.

#### SM-G3 동시 join/leave 경합

우선순위: `P1`

**검증 질문:** 같은 spot/actor에 다수 client가 동시에 join·leave·request를 보내도, membership과 상태가 일관되고 lifecycle callback이 중복·누락 없이 실행되는가.

- 절차: 같은 entry spot/actor 대상으로 다수 client가 동시에 join, leave, request를 섞어 보낸다.
- 검증: 경합 전에 필요한 Actor factory·Ready publication을 완료한다. 경합 중에는 accept된 membership마다
  target `OnActorJoin`, target `OnJoinedActor`, source `OnLeaveActor`가 commit 경계에 맞춰 정해진 횟수만
  실행되고 reject된 proposal은 `OnActorJoin` 뒤 source를 유지한다. Callback에는 중복·누락이 없고 같은
  Spot의 처리는 직렬 순서를 보존한다.
- 세부 동작: 동시 lifecycle 경합 일관성.

#### SM-G4 다수 bound session push 부하

우선순위: `P2`

**검증 질문:** 많은 session이 bind된 상태에서 동시에 push가 쏟아져도, 각 push가 제 session으로만 가고 누락·오배달 없이 격리되는가.

- 절차: 다수 actor에 다수 session을 bind한 뒤, 동시에 actor push를 대량으로 트리거한다.
- 검증: 각 push가 해당 bound session으로만 relay되고(교차 오배달 없음), 부하 중에도 session 간 격리가 유지된다. (전량 무손실은 공개 계약이 아니므로 단언하지 않는다 — 오배달 없음과 격리 유지에 초점.)
- 세부 동작: 대규모 bound session push 타깃팅(오배달 없음·격리).

#### SM-G5 node-wide placement weight 범위와 비율

우선순위: `P0`

**검증 질문:** Object placement가 Channel·ClientServer와 같은 signed `0..10000` weight 계약을 사용하고
capacity를 먼저 검사하는가.

- 절차: 동일 capability와 충분한 typed capacity를 가진 두 Object Server의 placement weight를 `100`,
  `300`으로 두고 많은 Actor·User Spot을 생성한다. Startup과 runtime update에서 `0`, 기본값 `100`,
  `10000`, `-1`, `10001`을 각각 적용한다. 별도 반복에서는 weight가 큰 node의 capacity만 소진한다.
  마지막 반복에서는 첫 target snapshot을 고정한 직후 선택된 node의 해당 typed capacity를 다른
  reservation으로 소진해 Store reserve를 실패시키고, authority가 계속 Missing인 상태에서 다른 eligible
  node를 선택할 수 있게 둔다.
- 검증: 장기 신규 placement 비율은 `1:3`에 수렴하되 기존 object owner는 바뀌지 않는다. `0`, `100`,
  `10000`은 허용하고 `-1`, `10001`은 descriptor revision과 placement mutation 전에 configuration error다.
  Weight `0`은 새 placement·relocation target에서 제외하며 이미 reservation을 얻은 operation은 유지한다.
  Capacity가 소진된 high-weight node는 weight 계산 전에 제외되고 eligible low-weight node가 선택된다.
  Snapshot 뒤 capacity race로 첫 reserve가 실패한 operation도 같은 deadline 안에서 실패한 candidate
  lifecycle을 제외하고 두 번째 node에 한 번만 reservation을 만들며 factory를 중복 실행하지 않는다.
  합계는 최소 64-bit 정수로 계산해 overflow하지 않는다.
- 세부 동작: node-wide placement weight validation, capacity-first selection과 runtime revision ordering.

## 5. 완료 기준

- Track A~G의 `P0` 시나리오가 모두 통과한다.
- public contract만 직접 호출하고 `ensure`로 단언한다(raw transport frame 조립·내부 helper 금지).
- 실패 시 store 연결 상태와 spot/session/consumer 로그·evidence로 원인 레이어를 분리한다.
