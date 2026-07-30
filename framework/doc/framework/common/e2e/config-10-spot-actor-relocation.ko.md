<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: To-actor messaging](config-9-to-actor-messaging.ko.md) | [다음: 관측·운영 배포](config-11-observability-ops.ko.md)
<!-- framework-adapter-nav:end -->

# Config 10 — Spot actor join/relocation 배포

actor가 Entry Spot과 user Spot 사이를 이동하거나, 다른 MeshNode의 user Spot으로 relocation될 때
[Spot Actor Join / Relocation 공통 스펙](../spec/15-spot-actor.ko.md)을 실제 배포 형태에서 만족하는지
검증한다. 이 config는 단순 Spot request 성공 여부가 아니라 admission, application join·leave,
infrastructure relocation, authority commit, bound session route와 failure cleanup이 계약한 순서로
관찰되는지 본다.

이 문서는 e2e 시나리오 정의만 둔다. 언어별 구현은 public framework API와 역할 server endpoint로
작성해야 한다. 현재 언어별 구현이 공통 스펙의 목표 공개 계약을 아직 제공하지 못하면 skip으로 완료
처리하지 않고 feature-map에 public contract parity gap으로 남긴다. 내부 helper, raw frame 조작,
테스트 전용 adapter로 이 config를 통과시키면 안 된다.

이 config의 actor 이동은 공개 relocation 계약을 호출해 처리 주체를 명시적으로 바꾸는 동작이다.
MeshNode를 추가하는 scale-out만으로 기존 owner가 자동 변경되는 동작은 계약하지 않는다. MeshNode
증설과 신규 배치는 Config 2 SM-G2에서 검증하고, 운영자가 host `Relocate`로 기존 actor를
다른 node로 인계하는 동작은 Config 11의 maintenance handoff 시나리오에서 검증한다.

## 1. 목적과 범위

- 다룬다: 같은 node join 순서, remote actor relocation 정상 경로, relocation state 복원, 빈 state relocation, admission/commit
  분리, source node down 전후 동작, authority commit 시점, moving 중 actor packet admission 차단,
  `RecreateOnRelocation`·`PreserveStateWith` adapter 경계와 callback 실패, bound session 이전, **seal 전에 수락한 Actor journal의
  순서 보존·Ready 전 replay, Message Follow와 route 제거, request reply correlation과 timeout**.
- 여기서 다루지 않는다: 일반 spot messaging 전체(Config 2), 비동기 handler 완료 후 mailbox 재개(Config 8),
  actor id 기반 no-bind send/request(Config 9), location store 자체 장애(Config 6).
- 계약 근거: `OnActorJoin`은 admission만 담당하고 actor instance를 받지 않는다. admission accept 뒤
  durable Actor authority row는 CAS commit 시점부터 target owner를 가리킨다. Remote relocation의 target
  factory·`Restore`·journal staging은 commit 전에 끝난다. Infrastructure relocation은 application의
  join·leave callback을 호출하지 않는다. Commit 뒤 journal replay, durable source cleanup, `Completed`,
  route ACK와 steady normalization을 모두 끝낸 뒤에만 target Actor admission을 연다. 같은 node join에는
  이 relocation phase와 Relocation Store를 적용하지 않는다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. Actor·Spot authority row와 owner lease는 framework lifecycle이 관리한다. |
| relocation store | 1 | 공식 Redis relocation store extension. Location Store와 같은 deployment를 사용할 수 있지만 별도 prefix와 `AddRelocationStore(...)` 등록을 사용한다. |
| actor 노드 | 2 (`actor-a`, `actor-b`), multi-hop 반복은 3 (`actor-c` 추가) | Object Server role의 Entry Spot + User Spot + Actor mailbox host. 모든 node에 같은 stable Actor·User Spot type, positive placement capacity와 explicit relocation policy를 등록한다. `PreserveStateWith` type은 kind별 adapter를 등록하고 `RecreateOnRelocation` type은 adapter 없이 factory만 등록한다. Lifecycle callback과 adapter는 order marker를 남긴다. |
| session gateway | 2 (`session-a`, `session-b`) | Object Client role과 Location Store를 등록하고 stream session, exact `ActorRef` bind와 actor push를 관찰한다. Remote relocation 뒤 bound session push가 target actor로 이어지는지 검증한다. |
| relocation controller | 1 | Object Client role과 Location Store를 등록한 실제 사용자 요청 역할 server. HTTP endpoint 안에서 actor 생성, join, global Actor ID packet send와 failure injection을 public framework API로 실행한다. |
| consumer | 시나리오별 | HTTP client wrapper로 relocation controller endpoint를 호출하고, 필요한 경우 stream connector로 session gateway에 연결해 push와 bind 상태를 관찰한다. |

actor 노드는 아래 evidence를 공통으로 남긴다.

- actor ID, actor type, actor generation 또는 ref snapshot, source/target Spot ID, source/target node RID.
- callback order marker: `admission`, `source_sealed`, `capture`, `target_factory`, `restore`, `journal_staged`,
  `prepared`, `authority_committed`, `journal_replayed`, `source_cleanup`, `completed`,
  `route_ack`, `steady_normalized`, `ready`, `admission_open`, `success_reply`.
- accepted-work marker: `journal_accepted`(seal 전에 수락), `journal_staged`(target queue 준비),
  `journal_replayed`(commit 뒤 replay), `moving_rejected`(seal 뒤 `Unavailable`),
  `message_follow_relay`(commit 뒤 이전 route message 전달),
  `message_follow_route_removed`(duration 경과 뒤 route 제거),
  `message_follow_expired`(route 제거 뒤 이전 route message 거부).
  각 actor packet은 arrival sequence index를 함께 남겨 target 처리 순서와 대조할 수 있어야 한다.
  `message_follow_*` runtime marker는 진단 자료일 뿐이다. Process 완료 판정은 public terminal과
  application handler의 exactly-once·zero-count evidence를 사용한다.
- `OnActorJoin` 입력 snapshot. actor id 외에 actor instance나 route metadata가 전달되었거나 저장되면 실패 evidence로 남긴다.
- relocation state marker. state를 담는 actor type은 source actor state version과 target actor 복원 state가 같은지 확인할 수 있어야 한다. 빈 state actor type은 target `OnJoinedActor` 이후 별도 조회 marker를 남긴다.
- actor packet handler marker. moving 중 source와 target 양쪽 handler가 동시에 처리하지 않았는지 대조한다.
- bound session snapshot. relocation 전후 push 대상 session gateway와 client connector를 비교한다.
- workload marker. relocation unit별 application state, queue, accepted journal, timer와 framing의 실제 encoded
  byte 수, permit 대기·seal·authority commit·admission 개방 시각, host operation의 시작·terminal 시각을
  monotonic clock으로 기록한다. Process별 CPU time, peak RSS와 Relocation Store read/write byte 수도 함께
  기록하되 correctness 판정은 public authority와 역할 server evidence를 사용한다.
- service marker. relocation 대상과 무관한 control Actor·Spot의 request latency와 성공 수, one-way 수락·처리
  sequence, relocation 대상의 operation ID·reply correlation·handler 실행 수를 1초 구간으로 기록한다.

relocation controller는 시나리오 실행 전용 driver가 아니다. consumer는 실제 app endpoint를 호출하고,
그 endpoint 내부에서 framework의 public actor/spot API가 실행되어야 한다. 장애 주입은 endpoint가
application 상태를 바꾸거나 `run_e2e.sh`가 역할 process를 중단하는 방식으로만 수행한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → actor 노드 → session gateway → relocation controller
순으로 시작한 뒤 client 시나리오를 순차 실행한다. 각 시나리오는 독립 actor id와 spot id를 사용해야
한다. 시나리오가 process 중단이나 route 차단을 사용하면 follow-up 검증 뒤 반드시 원래 topology를
복구하거나 다음 시나리오가 새 prefix를 쓰게 한다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 지원하는 언어는 최소 `key_transitions`로 켠다. 실패 시에는
actor id, relocation id, source/target node rid, callback order marker, authority snapshot, bound
session snapshot을 함께 남긴다.

## 4. 시나리오

### Track A — local join

#### ST-A1 local join accept 순서

우선순위: `P0`

**검증 질문:** 같은 node에서 actor가 Entry Spot에서 user Spot으로 join할 때 admission, leave, location
commit, joined, success reply가 정해진 순서로 관찰되는가.

- 절차: consumer가 relocation controller endpoint를 호출해 `actor-local-ok`를 만들고 같은 node의 user
  Spot으로 `JoinSpot`을 실행한다. controller는 join reply를 받은 뒤 같은 global Actor ID로 actor packet을 보내
  committed target membership에서 처리되는지 확인한다.
- 검증: evidence order는 `admission -> authority_committed -> leave -> joined -> success_reply`다.
  `OnActorJoin` evidence에는 actor id와 request만 있고 actor instance snapshot이나 route metadata가 없어야 한다.
  success reply 전에 authority row가 committed target User Spot을 가리켜야 한다. join 이후 packet은 target
  Actor handler에서만 처리되고 Spot lifecycle callback을 경유하지 않는다.
- 검증: 이 local join에는 `capture`, `target_factory`, `restore`, `journal_staged`, relocation `completed`,
  Message Follow marker가 없어야 한다. Relocation Store를 읽거나 쓰지 않는다.
- 세부 동작: 같은 node join 완료 조건과 actor instance 미노출 admission.

#### ST-A2 local join reject side effect 없음

우선순위: `P0`

**검증 질문:** target admission이 reject하면 source membership, source location, handler dispatch가
그대로 유지되는가.

- 절차: `actor-local-reject`를 Entry Spot에 만든 뒤 target User Spot의 join handler가 typed rejection reply와
  함께 거부하도록 request를 보낸다. 이후 같은 global Actor ID로 actor packet을 보낸다.
- 검증: Public join call은 exception이 아니라 `Rejected` result와 typed reply를 반환한다. Wire terminal은
  `ok`, Framework failure code는 `none`이며 target admission·policy failure의 `rejected` terminal과 섞이지
  않는다. Source `OnLeaveActor`, target `OnJoinedActor`, target authority commit evidence가 없어야 한다.
  Actor authority는 source Entry Spot을 유지한다. Actor packet은 source membership의 Actor handler에서
  처리되고 target membership이 생긴 것처럼 처리되지 않는다.
- 세부 동작: reject 시 side effect 없음.

#### ST-A3 target joined 전 packet dispatch 차단

우선순위: `P0`

**검증 질문:** target `OnJoinedActor`가 지연되는 동안 actor packet이 source와 target 양쪽에서 동시에
처리되지 않는가.

- 절차: target `OnJoinedActor`가 bounded latch를 기다리도록 설정한 뒤 local join을 시작한다. Local membership
  control barrier가 닫힌 동안 같은 actor로 packet을 보낸다. latch를 해제한 뒤 follow-up packet을 보낸다.
- 검증: membership 변경 구간에는 source와 target Actor handler가 같은 actor packet을 동시에 처리한 evidence가
  없어야 한다. target `OnJoinedActor` 완료 전 target Actor handler 성공 evidence도 없어야 한다. latch 해제
  뒤 follow-up packet은 target Actor handler에서 처리된다. Relocation Store, factory·adapter, accepted journal,
  `Completed`와 Message Follow route를 사용한 evidence가 있으면 실패다.
- 세부 동작: relocation sequence를 사용하지 않는 local membership barrier.

### Track B — remote relocation 정상 경로

#### ST-B1 remote relocation 성공 순서와 state 복원

우선순위: `P0`

**검증 질문:** actor가 `actor-a`에서 `actor-b`의 user Spot으로 이동할 때 relocation state가 복원되고
callback 순서가 공통 스펙과 일치하는가.

- 절차: `actor-remote-ok`를 `actor-a`에 만들고 mutable state를 설정한다. controller가 `actor-b`의
  user Spot으로 join을 실행한다. Source Actor relocation adapter의 `Capture`는 application 형식의
  opaque bytes를 반환하고 target factory가 staging Actor를 만든 뒤 같은 adapter의 `Restore`가 bytes를
  적용한다. Seal 전에 수락했지만 처리되지 않은 Actor packet 하나를 함께 준비해 accepted journal의 staging과
  replay도 관찰한다.
- 검증: evidence order는 `admission -> source_sealed -> capture -> target_factory -> restore -> journal_staged ->
  prepared -> authority_committed -> joined -> journal_replayed -> leave -> source_cleanup -> completed ->
  route_ack -> steady_normalized -> ready -> admission_open -> success_reply`다. `Restore`와 journal staging은 authority commit 전에
  완료되어야 한다. Target `OnActorJoin` admission callback은 Actor ID와 join request만 받고, commit 뒤
  `OnJoinedActor`는 target factory가 만든 concrete Actor instance를 받는다. Target joined callback과 journal
  replay 뒤 source `OnLeaveActor`를 durable cleanup의 일부로 실행한다. 두 callback의 인자를 서로 바꾸거나
  admission callback에 mutable Actor를 노출하면 실패다. Accepted journal replay는 commit 뒤 정해진 단계에서
  실행하지만, durable source cleanup, `Completed`, route ACK와 steady normalization 전에 새 target application
  admission으로 들어온 handler를 실행하거나 join 성공 reply를 반환하면 실패다. Join 완료
  뒤 authority snapshot으로 current generation을 확인하고 global Actor ID로 state 조회 request를 보내 source
  state version이 복원되었는지 확인한다. source와
  target Actor ID와 object generation은 유지되고 owner generation과 owner snapshot만 target owner 값으로
  바뀐다.
- 세부 동작: remote admission/commit 분리 + relocation state 복원.

#### ST-B2 commit 뒤 source 종료와 cleanup recovery

우선순위: `P0`

**검증 질문:** authority commit 뒤 source cleanup이 끝나기 전에 source가 사라져도 target recovery가 cleanup과
Ready barrier를 끝낸 뒤 join을 완료하는가.

- 절차: remote relocation 정상 경로를 실행하되 authority commit과 callback·journal replay 뒤, source가
  `source_cleanup` marker를 남기기 전에 대기하도록 설정한다. 이 시점에 join success reply와 새 target
  application admission handler가 모두 0건임을 확인한 뒤 `run_e2e.sh`가 `actor-a` process에 `SIGKILL`을 보낸다.
  `Relocate`나 `Shutdown`으로 cleanup을
  완료하는 경로를 섞지 않는다. Recovery coordinator가 expired source lease와 exact immutable source token으로
  durable cleanup을 종결하도록 기다린 뒤 target actor에게 packet을 보낸다.
- 검증: source process 종료는 committed ownership을 source로 rollback하지 않는다. Evidence는 recovery의
  `source_cleanup -> completed -> route_ack -> steady_normalized -> ready -> admission_open -> success_reply`
  순서로 이어진다. Original caller는 recovery가 Ready barrier를 끝낸 뒤 성공을 받으며, Framework가 Ready 전에
  성공을 반환해서는 안 된다. Ready 뒤 packet만 target Actor handler에서 처리된다. Stale source
  release 재시도가 target authority의 object·owner generation을 지우면 실패다.
- 세부 동작: commit 뒤 durable source cleanup recovery와 success barrier.

#### ST-B3 RecreateOnRelocation policy의 adapter 없는 relocation

우선순위: `P0`

**검증 질문:** `RecreateOnRelocation` policy를 등록한 Actor가 application state adapter 없이 factory와 accepted journal만으로
remote relocation을 완료하는가.

- 절차: `actor-recreate` type에 actor factory와 `RecreateOnRelocation` relocation policy를 등록하고 relocation adapter는
  등록하지 않는다. 같은 node
  join이 아니라 반드시 다른 node user Spot으로 remote relocation을 시도한다.
- 검증: remote relocation은 성공한다. Evidence order는 `admission -> source_sealed -> target_factory -> journal_staged ->
  prepared -> authority_committed -> joined -> journal_replayed -> leave -> source_cleanup -> completed ->
  route_ack -> steady_normalized -> ready -> admission_open -> success_reply`다. `Capture`·`Restore`
  marker는 모두 0건이어야 한다.
  source `OnLeaveActor`, target `OnJoinedActor`, target authority commit이 모두 정상 순서로 관찰된다.
  Accepted journal도 비어 있으면 source requirement의 message·byte는 0이고 target capacity offer는 양수다.
  Runtime은 비어 있는 deterministic relocation envelope과 reservation generation을 기록한다.
- 세부 동작: `RecreateOnRelocation` policy의 application state 없는 relocation과 all-zero accepted journal.

#### ST-B4 remote relocation empty state

우선순위: `P0`

**검증 질문:** `PreserveStateWith` relocation adapter가 empty byte sequence를 반환해도 target actor가 만들어지고
domain state를 별도로 읽어 올 수 있는가.

- 절차: `actor-empty-state` type에 `PreserveStateWith` policy와 custom relocation adapter를 등록한다. Source
  `Capture`는 empty byte sequence를 반환한다. Target factory가 Actor를 만든 뒤 `Restore`는 빈 bytes를
  해당 instance에 적용한다. Target `OnJoinedActor`는 actor id로 별도 저장소에서 domain state를 읽고
  marker를 남긴다.
- 검증: remote relocation은 성공한다. Evidence order는 `admission -> source_sealed -> capture_empty -> target_factory ->
  restore_empty -> journal_staged -> prepared -> authority_committed -> joined -> domain_state_loaded -> journal_replayed ->
  leave -> source_cleanup -> completed -> route_ack -> steady_normalized -> ready -> admission_open ->
  success_reply`다. Empty bytes와 adapter 미등록을 같은 의미로 취급하지 않는다.
- 세부 동작: `PreserveStateWith` adapter의 empty application state relocation.

### Track C — failure/recovery

#### ST-C1 source down after admission before commit

우선순위: `P0`

**검증 질문:** source node가 authority commit 전에 비정상 종료되었을 때 durable relocation root publication
전·후 경계에 따라 abort 또는 recovery로 수렴하는가.

- 절차: 독립된 두 topology를 사용한다. (a) `Capture` 전 또는 Relocation Store Put은 완료됐지만
  Location authority에 root reference를 CAS하기 전 source를 `SIGKILL`한다. (b) authority에 `Captured`
  root가 연결되었거나 target이 `Prepared`를 완료한 evidence 후, authority commit 전 source를
  `SIGKILL`한다. 정상 `Relocate`·`Shutdown`은 사용하지 않는다.
- 검증: (a)는 unlinked payload를 orphan cleanup하고 target membership·handler dispatch·hidden request replay가
  0건이다. Original caller는 connection failure 또는 timeout terminal을 따른다. (b)는 authority에 연결된
  immutable root와 accepted journal을 사용해 current target 또는 fenced replacement의 factory·`Restore`·commit을
  재개한다. Commit 뒤 callback·journal replay, durable source cleanup, `Completed`, route ACK와 steady normalization,
  `Ready`를 순서대로 끝낸 뒤에만 target admission과 success reply를 연다. 두 경로 모두 source·target partial
  membership을 동시에 공개하지 않는다.
- 세부 동작: pre-publication abort와 post-publication durable recovery 경계.

#### ST-C2 source down after target commit

우선순위: `P0`

**검증 질문:** target authority commit 직후 source node가 비정상 종료되어도 target recovery가 남은 activation과
completion barrier를 끝내는가.

- 절차: remote relocation 정상 경로에서 `authority_committed` evidence 직후, callback과 journal replay 전에
  `actor-a` process에 `SIGKILL`을 보낸다. Target recovery가 callback·journal replay, source lease expiry 기반
  durable cleanup, `Completed`, route ACK와 steady normalization을 재개하도록 한다. `Relocate`나 `Shutdown`의
  cleanup 결과를 source 장애 evidence로 사용하지 않는다.
- 검증: Actor authority row는 target User Spot과 target node를 계속 가리킨다. 전체 completion barrier 전에는
  target Actor packet, bound session push와 success reply가 모두 0건이다. `ready -> admission_open ->
  success_reply` 뒤에만 packet과 push가 성공한다. Stale source cleanup이나 source process 종료가 target ownership을
  지우면 실패다.
- 세부 동작: authority commit 뒤 target recovery와 rollback 금지.

#### ST-C3 callback과 relocation 실패 분류

우선순위: `P1`

**검증 질문:** relocation 단계별 application 실패가 공통 스펙의 결과로 분류되는가.

- 절차: 같은 actor type으로 `Capture`, source `OnLeaveActor`, `Restore`, target `OnJoinedActor`가
  각각 실패하도록 네 개의 독립 시나리오를 실행한다.
- 검증: `Capture` 또는 `Restore` 실패는 source leave 없이 source membership을 유지한다.
  `OnLeaveActor`와 `OnJoinedActor` 실패는 authority commit 뒤 실패이므로 caller success가 없어야 하지만
  target membership을 source로 rollback하면 안 된다. Target Actor packet dispatch를 차단한 recoverable
  reconciliation 상태에서 callback, replay, cleanup, `Completed`, route ACK와 steady normalization을 재개한다.
  모든 gate를 끝내 `Ready`가 된 뒤에만 target admission과 success가 가능하다.
- 세부 동작: 실패 지점별 join 결과.

### Track D — authority/routing/dispatch

#### ST-D1 authority commit 시점

우선순위: `P0`

**검증 질문:** authority commit 뒤 target `OnJoinedActor`가 완료되기 전에는 committed row와 아직 준비되지
않은 target Actor route가 구분되는가.

- 절차: local join과 remote relocation 각각에서 target `OnJoinedActor`를 지연시킨다. 지연 중 location query
  endpoint와 actor packet route를 반복하지 않고 bounded evidence wait로 한 번씩 관찰한다. 이후 latch를
  해제하고 다시 관찰한다.
- 검증: 두 경로 모두 지연 중 authority row는 committed target owner를 가리키지만 target Actor route는 ready가
  아니며 packet handler를 실행하지 않는다. Local join은 callback과 local membership barrier만 끝낸 뒤 route를
  공개하며 relocation phase와 Store를 사용하지 않는다. Remote relocation은 `OnJoinedActor` 완료만으로 route를 공개하지
  않고 journal replay, durable source cleanup, `Completed`, route ACK와 steady normalization까지 끝낸 뒤에만 새
  owner route가 `Ready`로 공개되고 packet이 처리된다. Remote relocation에서만 owner generation이 증가한다.
- 세부 동작: durable authority commit과 target route activation 구분.

#### ST-D2 stale source release authority fencing

우선순위: `P1`

**검증 질문:** source cleanup이나 stale owner release가 target authority를 변경하지 않는가.

- 절차: remote relocation에서 current source cleanup은 정상 완료하되 같은 exact source token의 중복 cleanup
  retry가 Ready 뒤 늦게 실행되도록 지연한다. Target Actor에게 packet을 보내고 target authority의 object
  generation, owner generation과 store version을 기록한 뒤 지연된 stale retry를 실행한다.
- 검증: cleanup 전후 target authority 값이 유지된다. cleanup 뒤 follow-up packet도 target에서 처리된다.
  source cleanup이 target owner row를 삭제하거나 stale route로 되돌리면 실패다.
- 세부 동작: expected-version CAS와 stale cleanup 격리.

### Track E — bound session relocation

#### ST-E1 remote relocation 뒤 bound session push

우선순위: `P0`

**검증 질문:** session에 bind된 actor가 remote relocation된 뒤 actor push가 target actor에서 기존 client로
이어지는가.

- 절차: consumer가 `session-a`에 연결해 `actor-bound-relocation`를 bind한다. relocation 전 actor가
  `BeforeRelocationNotify`를 push해 client가 받는지 확인한다. remote relocation을 실행한 뒤 target actor가
  `AfterRelocationNotify`를 push하게 한다.
- 검증: 두 notify는 같은 client connector가 받는다. relocation 뒤 push evidence는 target actor와 target
  node에서 발생해야 한다. Session route switch ACK와 steady normalization 전에는 `AfterRelocationNotify`와
  target admission이 없어야 한다. Source process가 종료되면 recovery가 durable cleanup을 대신 완료한 뒤에만
  route를 전환하며, cleanup이 terminal이 아닌 상태에서 target push를 먼저 성공시키면 실패다.
  ObjectGeneration은 relocation 전후 같아야 하며 같은 Session에 bind된 다른 Actor route는 바뀌지 않는다.
  Evidence는 `owner_membership_committed -> callbacks_journal_replayed -> source_cleanup -> completed ->
  route_switch -> routed_ack -> steady_normalized -> target_admission` 순서다. Command 44·45가 Completed 전에
  나타나거나 relay 과정에서 Location Store read가 발생하면 실패다.
- 세부 동작: bound session route 이전.

#### ST-E1A new incarnation은 explicit bind

우선순위: `P0`

- 절차: bound Actor를 destroy한 뒤 같은 ActorId의 새 ObjectGeneration을 만들고 이전 binding route update와
  새 `ActorRef` explicit bind를 각각 시도한다.
- 검증: 이전 binding은 새 incarnation으로 자동 retarget하지 않고 typed stale error로 끝난다. Explicit bind만
  새 ObjectGeneration을 등록하며 같은 Session의 다른 Actor binding은 유지된다.
- 세부 동작: relocation route update와 incarnation rebind 구분.

#### ST-E2 실패한 relocation은 bound session route를 바꾸지 않음

우선순위: `P0`

**검증 질문:** remote relocation이 commit 전에 실패하면 기존 bound session binding이 성공한 relocation처럼
바뀌지 않는가.

- 절차: Consumer가 actor를 bind한 뒤 remote relocation을 시작하고 target admission accept 뒤 source down
  before commit 또는 relocation adapter 실패를 주입한다. Session ingress seal 뒤 durable `Aborted` authority CAS,
  source-route abort command·ACK, reservation·relocation cleanup과 steady source normalization 경계마다
  coordinator를 한 번씩 종료한다. 이후 source가 계속 실행 중인 경우 기존 actor가
  `AfterFailedRelocationNotify`를 push하게 한다.
- 검증: `Aborted` 결정 전에는 abort route를 보내거나 source ingress를 열지 않는다. Recovery는 `Aborted`
  authority에서 source-route command를 idempotent하게 재전송하고 current session owner의 routed ACK를 받은 뒤
  cleanup과 steady source normalization을 완료한 경우에만 source admission을 다시 연다. 실패한 relocation은
  target bound session route를 만들지 않는다. Source actor가 유지되면 기존 client connector가 follow-up notify를
  받는다. Source가 비정상 종료된 경우에는 client reconnect/recreate 흐름으로 분류되고 target actor push 성공으로
  보이면 실패다.
- 세부 동작: 실패한 relocation의 bound session 비오염.

### Track F — accepted journal과 Message Follow

[Actor model §3·§8](../spec/14-actor-model.ko.md)과
[Spot Actor §4·§8](../spec/15-spot-actor.ko.md)에 따라 seal 전에 Actor queue가 수락한 work만
accepted journal로 고정한다. Seal 뒤 새 operation은 `Unavailable`로 끝나며 journal이나 Message Follow
queue에 숨겨서 보관하지 않는다. Commit 뒤 이전 owner route로 늦게 도착한 operation만
`MessageFollowDuration` 안에서 Message Follow 대상이 된다. 모든 시나리오는 operation ID와 accepted
sequence로 수락, staging, replay와 terminal 결과를 대조한다.

#### ST-F1 in-flight handoff order

우선순위: `P0`

**검증 질문:** seal 전에 수락한 Actor packet이 target staging queue에 같은 순서로 준비되고, commit 뒤
Ready 공개 전에 replay되는가.

- 절차: `actor-inflight-order`의 Actor queue claim을 bounded barrier로 지연해 handler가 시작되지 않게 하고
  controller가 lease-backed packet `P1 -> P2 -> P3`을 source Actor queue에 순서대로 수락시킨다.
  `journal_accepted` evidence를 확인한 뒤
  `actor-b`의 user Spot으로 remote relocation을 시작한다. Source seal 뒤 추가 packet `P4`도 보낸다.
- 검증: `P1 -> P2 -> P3`은 accepted boundary와 frozen journal에 같은 sequence로 기록되고 target factory와
  optional `Restore` 뒤 handler를 실행하지 않은 채 staging queue에 준비된다. Commit 뒤 callback과 함께
  `P1 -> P2 -> P3` 순서로 replay되고, source handler가 실행한 evidence는 없어야 한다. `P4`는
  `Unavailable`로 끝나며 journal, Message Follow queue와 어느 Actor handler에도 들어가지 않는다. Journal replay,
  source cleanup, `Completed`, route ACK와 steady normalization이 끝나기 전에 target을 `Ready`로 공개하면 실패다.
- 세부 동작: seal 전 accepted journal 고정, seal 뒤 `Unavailable`, Ready 전 replay.

#### ST-F2 direct overtakes prevented

우선순위: `P0`

**검증 질문:** Ready 공개 직후 새 owner가 수락한 direct packet이 accepted journal replay보다 먼저 처리되지
않는가.

- 절차: `actor-inflight-overtake`의 source queue가 `B1 -> B2`를 수락했지만 처리하지 않은 상태에서 remote
  relocation을 시작한다. Target의 `journal_staged`와 authority commit을 확인하되 route ACK와 steady normalization을
  지연한다. Target route가 `Ready`로 공개된 직후 controller가 global Actor ID로 direct packet `D1`을 보낸다.
- 검증: target 처리 순서는 `B1 -> B2 -> D1`이다. Evidence order는 `journal_staged -> authority_committed ->
  journal_replayed -> source_cleanup -> completed -> route_ack -> steady_normalized -> ready -> D1_accepted`다.
  `D1`이 Ready 전에 수락되거나 `B1`·`B2`보다 먼저 처리되면 실패다.
- 세부 동작: accepted journal replay와 Ready barrier를 사용한 direct 추월 차단.

#### ST-F3 bound session cross-move order

우선순위: `P0`

**검증 질문:** bound session의 connection-bound work를 `Captured` 전에 terminal drain하고, route switch 뒤
새 packet이 이전 sequence를 추월하지 않는가.

- 절차: consumer가 `session-a`에 연결해 `actor-bound-order`를 bind한다. Client가 `S1 -> S2`를 보내 source
  Actor queue가 수락한 뒤 remote relocation을 시작한다. Session owner는 binding ingress를 reversible하게 seal하고
  마지막 accepted binding sequence를 source에 전달한다. `S1`과 `S2`가 terminal state가 될 때까지 `Captured`
  전 drain하고, 이 connection-bound work가 durable accepted journal에 들어가지 않았음을 확인한다. `Completed`
  뒤 current owner fence를 검증한 route ACK와 steady normalization으로 binding route를 바꾼 다음 ingress를 열어
  `S3 -> S4`를 보낸다.
- 검증: Actor 처리와 request terminal evidence는 `S1 -> S2 -> S3 -> S4` 순서를 유지한다. `S1`·`S2`를
  accepted journal에서 replay하거나 terminal 전 `Captured`로 진행하면 실패다. `S3`·`S4`는 current binding token과
  owner generation을 가진 route ACK 뒤에만 target으로 전달되고 어느 operation도 누락·중복되지 않는다.
- 세부 동작: bound-session pre-capture drain과 route-switch sequence barrier.

#### ST-F3A paused session owner lease fence

우선순위: `P0`

**검증 질문:** Session owner process가 pause되어 host lease가 만료돼도 transport I/O가 유지되는 경우, 늦은
seal·route ACK가 Actor binding route를 바꾸지 못하는가.

- 절차: `ST-F3`의 binding ingress seal 뒤 session owner process의 application runtime을 pause하고 transport
  fixture는 connection을 유지한다. Host owner lease가 만료된 뒤 이전 owner의 `sessionRelocationSealed`와
  `sessionRelocationRouted`를 전달한다. Successor session owner는 새 owner token으로 같은 binding을 복구한다.
- 검증: Sender의 local monotonic admission deadline을 넘긴 command와 receiver가 current descriptor의 owner
  ID·owner lease generation을 다시 확인했을 때 일치하지 않는 command는 route publication과 unseal에 사용하지
  않는다. Successor token과 exact session·binding generation을 가진 ACK 하나만 route를 바꾸며 packet 누락,
  중복과 FIFO 역전이 없다.
- 세부 동작: Bound-session barrier의 host lease fencing과 transport liveness 분리.

#### ST-F4 Message Follow duration 전후 결과

우선순위: `P1`

**검증 질문:** authority commit 전에 이전 owner route로 전송된 message가
`MessageFollowDuration` 기본값 30초 안에서는 target으로 전달되고, duration 경과 뒤에는 typed stale
결과로 끝나는가.

- 절차: `actor-message-follow`의 relocation authority commit 전에 source owner route로 이미 전송된 packet
  `G1`과 `G2`를 transport fixture에서 각각 지연한다. `G1`은 commit 뒤 Message Follow duration 안에,
  `G2`는 duration이 지난 뒤 source에 전달한다. Public caller는 global Actor ID만 사용하고 transport fixture가 resolve된 이전
  owner route의 delivery만 지연한다. Object generation은 relocation 전후 동일하며 fixture가 caller에게 이전
  owner route를 노출하거나 선택하게 해서는 안 된다. 공통 기본값 30초도 option snapshot으로 확인하되 이
  시나리오는 topology의 `MessageFollowDuration`을 짧게 설정해 실행 시간을 줄일 수 있다.
- 검증: `G1`은 original operation ID, generation, payload와 reply route를 보존한다. Target admission이
  아직 닫혀 있으면 Ready까지 handler 실행을 기다린 뒤 final owner의 application handler가 정확히 한 번
  처리한다. Source handler는 실행되지 않는다. `G2`는 `Unavailable`로 끝나고 final owner handler는
  실행되지 않는다. Caller는 terminal 결과를 확인한 뒤 명시적으로 다시 제출해야 한다.
  framework가 `G2`를 자동 저장·재전송한 evidence가 있으면 실패다.
- 세부 동작: Message Follow duration 안의 전달과 duration 경과 뒤 거부.

#### ST-F5 Message Follow route 제거

우선순위: `P1`

**검증 질문:** duration 경과 뒤 Message Follow route가 제거되어 누수가 없고, duration 안 재이동은
route를 갱신하는가([Spot Actor §8](../spec/15-spot-actor.ko.md#8-message-follow)).

- 절차: 두 부분으로 실행한다. (a) `actor-follow-remove`를 remote relocation한 뒤 duration 경과를 bounded
  wait로 둔다. 그 전에 resolve해 둔 delivery를 wait 뒤 release한다. (b) public global Actor ID로 message를
  제출하고 최초 resolve가 고른 delivery만 transport fixture에서 지연한다. 그 Actor를 duration 안에
  `actor-a -> actor-b -> actor-c`처럼 **다른 node로 두 번** 연속 이동시키고 각 node의
  Message Follow route snapshot을 관찰한 뒤 지연한 delivery를 첫 node에서 다시 진행한다.
  Caller와 application DTO에는 이전 `ActorRef`, owner RID, generation 또는 hop을 노출하지 않는다.
- 검증: (a) duration 경과 뒤 release한 request는 `Unavailable`이고 final owner handler는 실행되지
  않는다. (b) final owner handler는 늦은 message를 정확히 한 번 처리하고 이전 owner handler는 실행하지
  않는다. **각 source node는 그 actor와 source owner generation에 대해 route가
  최대 하나**이고 그 route는 자기 **다음 hop**을 가리킨다(첫 node는
  두 번째 node를, 두 번째 node는 최종 target을). 첫 node에 주입한 늦은 message는 hop을 따라 최종 target까지
  전달된다. 한 node 안에 이전 target을 가리키는 잔여 route가 함께 남으면 실패다. 각 duration 경과 후 모든
  Message Follow route가 제거되어 누수가 없어야 한다. Process E2E는 public terminal과 handler 횟수만
  판정한다. Route entry와 next-hop 내부값은 provider/runtime contract test에서 검증하며, runtime log marker를
  process 완료 증거로 사용하지 않는다.
- 세부 동작: Message Follow route의 bounded lifecycle과 node별 chained relay.

#### ST-F6 in-flight request reply correlation과 timeout

우선순위: `P1`

**검증 질문:** seal 전에 수락한 lease-backed request가 replay된 뒤 original reply route로 correlate되고,
timeout과 seal 뒤 `Unavailable`이 서로 다른 terminal 결과로 유지되는가
([Actor model §8](../spec/14-actor-model.ko.md#8-실패와-관측),
[Flow Correlation §7](../spec/27-flow-correlation.ko.md#7-reply와-failure)).

- 절차: 세 부분으로 실행한다. (a) 충분히 긴 timeout의 lease-backed request를 source가 수락한 직후 handler
  실행 전에 remote relocation을 시작한다. (b) 같은 accepted-before-seal 흐름에서 caller timeout을 Ready보다
  짧게 두고 target의 replay completion을 지연한다. (c) Source seal evidence 뒤 새 request를 제출한다.
- 검증: (a) accepted journal은 original operation ID와 reply route를 보존하고 target replay 결과가 원래
  caller에 한 번만 correlate된다. Durable terminal completion과 reply relay ACK를 끝내지 않고 source cleanup이나
  `Completed`를 통과하면 실패다. (b)는 normal request timeout으로 끝나며 뒤늦은 replay reply는 late-reply
  drop된다. Framework가 새 operation으로 숨겨서 재제출하지 않는다. (c)는 `Unavailable`로 즉시 끝나며
  journal, Message Follow queue와 target handler에 들어가지 않는다.
- 세부 동작: accepted request replay, durable reply correlation, timeout과 moving rejection 분리.

### Track G — execution lane barrier와 aggregate capacity

#### ST-G1 yielded continuation을 포함한 relocation barrier

우선순위: `P0`

- 절차: `SpotWide` User Spot member Actor가 request `Yield` 상태이고 다른 Actor·Spot handler·timer가
  실행 중일 때 host `Relocate`를 시작한다. 별도 반복에서는 `PerActor` User Spot의 여러 Actor lane,
  Spot lane과 서로 다른 Actor timer lane을 동시에 실행한다.
- 검증: `SpotWide`는 새 application admission과 membership 변경을 먼저 seal하고, yielded continuation과
  이미 실행 중인 모든 lane이 안전한 turn 경계에 도달한 뒤 Spot 전체를 하나의 aggregate로 Capture한다.
  `PerActor`는 Spot 전체 lane을 한 번에 정지하지 않는다. 각 Actor는 현재 turn 하나를 끝낸 뒤 자기 queue,
  accepted journal과 timer만 seal하고 독립적으로 이전한다. Spot-level application state나 timer를
  Relocation Store에 넣으면 실패다.
- 세부 동작: SpotWide aggregate barrier와 PerActor User Spot의 Actor별 barrier.

#### ST-G2 User Spot aggregate capacity all-or-none

우선순위: `P0`

- 절차: Actor N개를 포함한 stable type `room` User Spot을 (a) Actor total slot이 N보다 하나 부족한
  target, (b) Spot total과 Actor total은 충분하지만 `room` stable type slot만 부족한 target,
  (c) 세 bucket이 모두 충분한 target으로 차례로 relocation한다. 기본 반복은 여러 inventory
  chunk가 필요한 N=10,000으로 실행한다.
- 검증: (a)와 (b)에는 Spot total 1개, `room` stable type 1개와 Actor total N개 가운데 어떤 reservation도
  남지 않고 factory·Restore·inventory publication이 0건이다. (c)는 participant를 최대
  1,024개씩 immutable leaf chunk에 저장하고 root의 전체 count와 digest를 확인한다.
  마지막 aggregate authority CAS가 owner, generation, inventory root와 typed capacity
  bundle을 함께 전환해야 한다. CAS 전에는 Actor 하나도 target owner로 보이면 안 되고
  CAS 뒤에는 10,000개 모두 target owner로 조회되어야 한다.
- 세부 동작: 큰 participant inventory 준비와 aggregate root의 atomic publication.

#### ST-G3 PerActor Spot authority 선전환과 Actor별 route

우선순위: `P0`

- 절차: Actor 100개가 있는 `PerActor` User Spot을 준비한다. 일부 Actor의 현재 turn을 지연한 상태에서
  host `Relocate`를 시작한다. Target에는 같은 public Spot ID와 ObjectGeneration을 사용하는 runtime-private
  Spot shell을 만들고, Spot authority를 바꾸기 전후에 `ToSpot`, 새 Actor Create·Join과 기존 Actor
  `ToActor`를 반복한다.
- 검증: Target shell은 Spot authority CAS 전에는 public lookup과 messaging 대상으로 보이지 않는다.
  임시 public Spot ID를 만들거나 public Spot ID를 rename하지 않는다. Spot authority CAS 뒤 새 `ToSpot`,
  Create와 Join은 target으로 간다. 아직 이전하지 않은 Actor의 `ToActor`는 source로 가고, 이전한 Actor의
  `ToActor`는 target으로 간다. Actor는 준비되는 순서대로 독립적으로 이전하며 모든 Actor가 끝날 때까지
  source와 target에 나뉘어 존재할 수 있다. 마지막 Actor와 relay가 끝난 뒤 source shell을 제거한다.
- 세부 동작: public Spot identity 유지, authority-first shell 전환, Actor별 owner lookup.

#### ST-G4 이동 중 ToActor Message Follow와 target queue 순서

우선순위: `P0`

- 절차: Actor queue에 실행 전 job과 accepted journal을 넣고 Actor relocation을 시작한다. Authority가
  바뀌는 경계에 old source route로 request와 send를 주입하고, 동시에 target owner를 새로 조회한
  request도 제출한다.
- 검증: Source는 stale message를 버리거나 handler에 다시 제출하지 않고 Message Follow로 current target에 전달한다.
  Message Follow는 operation ID, ObjectGeneration, absolute deadline, request correlation과 reply route를 그대로
  유지한다. Target 처리 순서는 이전된 실행 전 queue·accepted journal, source ingress hold와 relay 완료,
  새 target direct queue 순서다. Request reply는 원래 caller에 한 번만 전달된다.
- 세부 동작: Message Follow, request correlation 보존과 target admission order.

#### ST-G5 Relocation unit별 interruption 목표

우선순위: `P0`

- 절차: 아래 selector를 각각 독립 process에서 실행한다. `SMALL`은 운영에 가까운 작은
  payload를 사용한다. `SLOW-CAPTURE`와 `SLOW-RESTORE`는 해당 callback을 1.25초
  지연한다. 모든 selector는 relocation 전부터 target admission이 다시 열린 뒤까지
  request와 one-way traffic을 계속 보낸다.

| Selector 접두사 | 이전 단위 | `unit_kind` | `execution_mode` | 연속 traffic |
|---|---|---|---|---|
| `ST-G5-ENTRY-ACTOR-*` | Entry Spot에 속한 Actor 하나 | `actor` | `entry` | `ToActor` request·one-way |
| `ST-G5-PER-ACTOR-*` | `PerActor` User Spot에 속한 Actor 하나 | `actor` | `per_actor` | `ToActor` request·one-way |
| `ST-G5-PER-ACTOR-SPOT-*` | `PerActor` User Spot의 direct admission | `user_spot` | `per_actor` | `ToSpot` request·one-way |
| `ST-G5-SPOT-WIDE-*` | `SpotWide` User Spot aggregate 하나 | `user_spot` | `spot_wide` | `ToSpot`과 member `ToActor` request·one-way |
| `ST-G5-INSTANCE-SPOT-*` | Instance Spot 하나 | `instance_spot` | 기록하지 않음 | `ToSpot` request·one-way |

`ST-G5-SPOT-WIDE`는 member Actor 수에 따라 다음 profile을 별도 실행한다.

| Profile | User Spot 수 | Spot당 Actor 수 | Actor당 state | 판정 |
|---|---:|---:|---:|---|
| `ACTORS-10` | 1 | 10 | 64 KiB | 빠른 회귀와 순서 검증 |
| `ACTORS-100` | 1 | 100 | 64 KiB | 운영 규모의 정식 1초 SLO gate |

두 profile 모두 Spot과 모든 member Actor를 하나의 relocation unit으로 측정한다.
`ACTORS-100`이 1초를 넘으면 정식 SLO gate가 실패한다.
Actor state 합계는 각각 640 KiB와 6.25 MiB다. Spot state, queue,
accepted journal, timer와 framing은 encoded payload에 추가된다.

각 접두사에 `SMALL`, `SLOW-CAPTURE`, `SLOW-RESTORE` suffix를 붙인다. 기존
`ST-G5-SMALL`, `ST-G5-SLOW-CAPTURE`, `ST-G5-SLOW-RESTORE`,
`ST-G5-SLOW-CLEANUP`은 Entry Actor 호환 selector다. 새 정본에서는 각각
`ST-G5-ENTRY-ACTOR-SMALL`, `ST-G5-ENTRY-ACTOR-SLOW-CAPTURE`,
`ST-G5-ENTRY-ACTOR-SLOW-RESTORE`, `ST-G5-ENTRY-ACTOR-SLOW-CLEANUP`과
같은 case로 기록한다.
- 검증: source seal 직전 마지막 handler completion부터 target admission 뒤 첫 handler
  start·reply까지의 traffic gap을 별도로 기록하고 operation loss·duplicate·순서를 확인한다.
  `PerActor` User Spot은 Spot direct admission과 각 Actor admission을 서로 다른 unit으로
  측정한다. 정상 profile의
  `zlink.relocation.interruption`은 1초 이하다. 느린 profile은 1초를 넘고 warning을
  남기지만 같은 relocation과 application traffic은 완료된다. `unit_kind`는 `actor`,
  `instance_spot`, `user_spot`을 사용하고 필요한 경우에만 `execution_mode`를 기록한다.
  Infrastructure relocation callback은 0건이다.
- 세부 동작: unit별 서비스 중단 시간 SLO와 host deadline 분리.

#### ST-G6 SpotWide application-signaled relocation 경계

우선순위: `P0`

- 절차: 같은 Spot type을 기본 `AnyTurnBoundary`와 `ApplicationSignaled` readiness
  mode로 각각 등록한다. ApplicationSignaled Spot은 round 종료 handler에서
  `RelocationReady().Defer()`를 호출하고 completion callback에서 다음 round marker를
  기록한다. (a) relocation 요청 없음, (b) precommit abort, (c) target relocation
  성공, (d) callback을 override하지 않은 기본 no-op을 각각 실행한다.
- 검증: (a)와 (b)는 source에서 `Continued`, (c)는 target에서 `Relocated` callback을
  호출한다. 네 반복 모두 callback 또는 기본 no-op completion 뒤에만 보류한 일반
  message와 timer를 실행한다. Callback 전에 다음 round marker나 일반 handler가
  실행되면 실패다. Callback 실행 중 target process를 한 번 종료한 반복에서는 같은
  logical boundary가 recovery될 수 있으므로 application의 round ID guard가 중복
  side effect를 막아야 한다.
- 오류 검증: `AnyTurnBoundary`, `PerActor`, Entry Spot, Instance Spot, Spot turn 밖과
  같은 turn의 두 번째 `Defer()`는 queue·timer·relocation state를 바꾸기 전에
  `InvalidOperation`으로 끝난다. `Defer()` 뒤 같은 turn에서 다른 Framework
  operation을 시작해도 같은 오류다. Invalid 호출에는 completion callback이 0건이어야
  한다.
- 세부 동작: application safe-point barrier, source·target completion owner, default
  no-op callback과 retry-safe recovery.

### Track H — deferred Join과 Context 계약

#### ST-H1 Join registration, queue barrier와 immutable request

우선순위: `P0`

- 절차: Actor handler와 User·Entry Spot의 Spot·Timer handler에서 각각 Join call을 만들고 request와
  deadline을 설정한 뒤 `Defer`를 호출한다. Defer 뒤 application이 원본 request 객체를 변경하고 같은
  Actor로 packet을 계속 제출한다.
- 검증: Defer는 동기 registration만 수행하며 handler의 마지막 continuation 전에는 target I/O와
  Location mutation이 없다. Join은 Defer 시점의 immutable request와 absolute deadline을 사용한다.
  같은 Actor의 후속 packet은 source seal 전에는 barrier 뒤 Actor queue에 수락되고 cross-node
  relocation에서는 accepted journal·실행 전 queue와 함께 이관된다. Source seal 이후 CAS 전과 Message Follow
  구간의 payload만 bounded ingress hold에 들어간다. 다른 Actor와 Spot lane은 계속 진행된다.
- 세부 동작: handler-scoped barrier, request snapshot과 Actor-scoped queue hold.

#### ST-H2 completion outcome과 operation ID

우선순위: `P0`

- 절차: Same-node Accepted, cross-node Accepted, target Rejected, precommit failure 세 갈래를 실행하고
  completion callback 자체도 한 번 실패시킨다. 각 갈래에서 current process retry를 확인하고 별도 crash
  반복은 handler terminal 전, Location commit 전, cross-node commit 뒤와 same-node commit 뒤에 주입한다.
- 검증: Actor는 `Accepted`, `Rejected`, `Failed` 가운데 하나를 받는다. 128-bit non-zero operation ID는
  current process retry에서 동일하다. Cross-node commit 뒤 Accepted만 Relocation manifest의 operation ID,
  optional reply와 cursor로 target replacement 뒤 durable at-least-once 복구된다. Handler activation·Location
  commit 전 crash는 Join intent 복구를 요구하지 않고 source authority·membership을 유지한다. Same-node
  outcome과 Rejected·precommit Failed는 process replacement 뒤 completion replay를 요구하지 않는다.
  Callback retry가 끝나기 전에는 같은 process의 Actor application queue를 열지 않는다.
- 세부 동작: durable completion과 idempotency key.

#### ST-H3 Context identity와 relocation fence

우선순위: `P0`

- 절차: Actor·User·Entry·Instance Spot factory에 전달한 Context와 생성 object의 `Context` accessor가 같은
  Context identity를 가리키는지 비교한다. Reference identity를 제공하는 언어는 같은 reference를 확인하고,
  C++은 factory parameter를 application member로 move한 뒤에도 같은 내부 handle identity인지 확인한다.
  Same-node Join과 cross-node Join·Spot relocation을 각각 실행한다.
- 검증: Factory는 ID를 중복 인자로 받지 않는다. Same-node Actor Join은 같은 Actor·Context 객체에서
  membership만 commit 시점에 바꾼다. Cross-node Actor는 같은 ObjectGeneration을 유지한 새 target Context를
  사용한다. Spot relocation도 ObjectGeneration을 유지하고 새 AuthorityOwnerGeneration과 target owner에
  결합한 Context를 사용한다. Commit 뒤 source Context의 operation은 fence되고 current target으로 자동
  전달되지 않는다. 별도 반복에서는 Actor·User·Entry·Instance Spot factory가 전달받은 것과 다른 Context를
  노출하게 하고 factory completion 뒤 initialize, Ready authority, message admission과 active capacity가
  모두 0건임을 확인한다. `PreserveStateWith`는 handler tail의 application state와 Framework queue·timer를 복원한다.
  `RecreateOnRelocation`은 ObjectGeneration을 유지한 새 instance에 Framework queue·timer만 복원하고 application state는
  capture하지 않는다.
- 세부 동작: Context composition, generation 유지와 source fencing.

#### ST-H4 허용 문맥, 중복 등록과 relocation 오류 parity

우선순위: `P0`

- 절차: 허용된 Actor handler와 User·Entry Spot의 Spot·Timer handler에서 성공 반복을 실행한다. Instance
  Spot handler·timer, factory, `Configure`, lifecycle callback, relocation adapter와 detached/background
  task에서는 각각 Defer를 시도한다. 같은 call 두 번째 Defer, 같은 Actor의 pending transition 중 Defer,
  `DisableRelocation` policy, eligible target 부재와 target 확정 뒤 precommit failure도 각각 실행한다.
- 검증: Instance Spot과 turn scope 밖의 모든 시도는 `InvalidOperation`으로 동기 실패하고 barrier,
  target I/O와 Location mutation이 모두 0건이다. 두 번째 Defer와 pending transition은
  `InvalidOperation`과 `Unavailable`로 각각 동기 실패한다. 나머지는 completion에서 `Rejected`,
  `RelocationTargetUnavailable(38)`, `RelocationFailed(39)`를 다섯 언어가 같은 숫자와 의미로 보고한다.
- 세부 동작: closed execution context와 error-kind parity.

#### ST-H4A registration limit, timeout과 transition race

우선순위: `P0`

- 절차: handler 하나에서 Join 64개와 encoded request 합계 8 MiB 경계를 채운 뒤 65번째·합계 초과·개별
  1 MiB 초과를 각각 시도한다. Request 없는 overload, 기본 timeout과 min/max boundary도 실행한다.
  Join claim과 Relocate·Shutdown seal race, same-target User·Entry Join도 반복한다.
- 검증: 초과·invalid timeout은 partial record 없이 동기 argument 또는 configuration error다. Request 생략은 empty
  message, 기본은 5초, 명시는 millisecond 올림 finite `1..INT_MAX` ms이며 Defer 시 monotonic deadline을
  고정한다. Join winner면 maintenance가 기다리고 Relocate winner는 `Unavailable`, Shutdown winner는
  `ShuttingDown`이다. Same-target는 lifecycle·Store mutation 없이 Accepted completion을 실행한다.

#### ST-H4B Yield, awaited cycle과 reply terminal

우선순위: `P0`

- 절차: Defer 뒤 request `Yield`, self awaited request, Spot handler가 barrier 대상 Actor를 await하는 request,
  reply encoding failure와 encoding 뒤 transport admission failure·disconnect를 각각 실행한다.
- 검증: Yield 중 inactive claim이 유지되고 두 awaited cycle은 submission 전에 `InvalidOperation`이다.
  Reply encoding failure는 handler failure로 barrier를 폐기한다. Encoding 뒤 transport failure·disconnect는
  정상 handler가 등록한 Join을 취소하지 않는다.

#### ST-H5 MessageContext와 Actor handler signature parity

우선순위: `P1`

- 절차: contract snapshot으로 public declaration을 검사한 뒤 send·request·RouteMesh·publish·session과
  User·Entry Spot Actor handler를 실제 public registration과 transport를 통해 호출한다.
- 검증: declaration에는 공통 `MessageContext`와 Route·Publish·Session specialized MessageContext만 있고
  Send·Request·SpotActor marker context와 Actor request reply option이 없다. 실제 dispatch에서는
  MeshName·ChannelName·PacketName·ContentType·Metadata·nullable CorrelationId가 원본 envelope와 일치하고,
  Route source RID, Publish topic·source와 Session `CanReply`도 operation kind에 맞는 값을 제공한다.
  Send와 request, Mesh 소속 여부, ContentType·CorrelationId 유무를 각각 바꿔 nullable field의 실제 null과
  non-null 경로를 모두 확인한다. Handler가 Metadata snapshot을 변경할 수 없으며 보관하려면 복사해야 한다.
  Universal MessageContext에는 reply operation과 connection cancellation이 없고, 둘은 Session specialization
  또는 언어별 cancellation 인자에서만 확인한다. `HandlerInvocation`은 current MessageContext, descriptor,
  payload와 chain을 제공하며 MessageContext field를 별도 복제하지 않는다. Actor handler는 containing Spot,
  Actor, MessageContext와 payload를 받으며 다섯 언어가 같은 정보를 제공한다.
- 세부 동작: public context naming과 handler information parity.

### Track I-A — 현실 부하와 서비스 연속성

이 Track은 correctness contract와 이 config의 workload SLO를 분리한다. Authority, queue 순서,
operation identity, deadline과 reply correlation 위반은 runtime correctness 실패다. 아래에 고정한
처리량과 1초 interruption 목표를 넘으면 relocation operation 자체를 취소하거나 실패로 바꾸지 않는다.
Runtime terminal을 끝까지 관찰한 뒤 `workload_slo_missed` evidence와 해당 측정값을 남기고 E2E
scenario를 실패시킨다. 따라서 SLO 실패와 `RelocationFailed`를 같은 결과로 집계하면 안 된다.

Process E2E는 application이 관찰할 수 있는 결과를 검증한다. Public `RelocateAsync`가
`Relocated` terminal result를 반환한 뒤 `Find`로 `ObjectGeneration` 유지와 owner 전환을
확인한다. 그 뒤 새 operation을 제출해 final owner의 handler에서만 처리되는지 확인한다.
이전 owner의 application handler가 같은 operation을 처리하면 실패다. `SpotWide`는 Spot과
모든 member Actor가 같은 final owner에 도달하는지도 확인한다.

`AuthorityOwnerGeneration`의 숫자와 Location Store의 단일 CAS 호출은 application public
API로 노출하지 않는다. Exact owner-generation fence와 aggregate CAS 자체는 Location Store
provider contract test가 검증한다. Authority commit과 target admission의 정확한 순서는
runtime contract test가 검증한다. Process E2E는 terminal 이후의 final location과 generation,
새 operation의 final owner 처리와 이전 owner 처리 0건, 이전 generation operation의 typed
stale 결과와 Message Follow의 한 번 전달로 같은 correctness를 검증한다. 이 내부 값을
E2E에서 읽기 위해 새 public API나 private hook을 추가하면 실패다.

#### 현실 부하 profile

Payload 크기는 application object만 직렬화한 크기가 아니라 Relocation Store에 저장하는 encoded
payload 전체를 기준으로 측정한다. 각 fixture는 압축되지 않는 deterministic byte pattern과 SHA-256을
사용한다. Application adapter는 `Capture`가 반환한 state byte를 기록한다. E2E용 Store wrapper는 public
opaque Store SPI를 그대로 구현하면서 write·read마다 blob 크기와 checksum을 기록한다. Runner는
application state 합계, Store blob별 크기, Store 전체 byte, read-back checksum과 process peak RSS를
report에 남긴다. Queue·accepted journal·timer·participant 목록과 framing은 private envelope를 해석하지
않고 `Store 전체 byte - application state와 fixture가 넣은 raw message byte`의 합산 overhead로 기록한다.
예상 크기나 object allocator 크기만 기록하면 실패다.

이 측정을 위해 Framework private envelope decoder, 내부 Store key 또는 runtime field를 E2E에 노출하지
않는다. Provider wrapper는 opaque key·value의 크기와 checksum만 관찰하고 값을 해석하지 않는다.

| Profile | Actor·Instance Spot application state | `SpotWide` aggregate application state | Queue·journal·timer 구성 | 사용 목적 |
|---|---:|---:|---|---|
| `small` | 4 KiB | 64 KiB | queue 8×1 KiB, journal 2×1 KiB, timer 4개 | 자주 이동하는 작은 상태 |
| `normal` | 64 KiB | 1 MiB | queue 32×4 KiB, journal 8×4 KiB, timer 16개 | 일반 업무 상태와 대기 작업 |
| `large` | 8 MiB | 32 MiB | queue 64×64 KiB, journal 16×64 KiB, timer 64개 | 큰 state와 backlog |
| `boundary` | state-preserving object 하나당 64 MiB | participant 5개의 합계 320 MiB | ingress hold는 별도 반복에서 1,024 records와 16 MiB 경계를 사용 | participant 상한과 oversized aggregate 단독 실행 |

`boundary`의 320 MiB aggregate는 process의 기본 256 MiB payload gate를 줄여서 통과시키지 않는다.
다른 payload 이동이 없을 때 한 unit만 실행되는지 확인한다. Actor 하나와 Instance Spot은 256 MiB
gate 안에서만 진행하고 state-preserving object 하나가 64 MiB를 넘는 반복은
`Blocked/StateIncompatible`이어야 한다. Message payload 자체의 상한은 negotiated
`MaxMessageSize`를 따르므로 relocation state 크기와 혼동하지 않는다.

#### 고정 workload SLO

공통 CI profile은 아래 값을 사용한다. 개발자가 local 진단에서 더 작은 수로 실행할 수는 있지만 그
결과는 공통 E2E 완료 evidence로 사용할 수 없다. 처리량은 첫 unit을 seal한 시점부터 마지막 unit의
target admission 또는 safe abort가 끝난 시점까지의 전체 host wall time으로 계산한다. 성공한 unit만
분모로 사용하거나 factory·Restore 시간, Store 대기 시간을 빼면 실패다.

| Workload | 고정 수와 payload | 전체 host 목표 |
|---|---|---|
| Entry·`PerActor` Actor bulk | `RecreateOnRelocation` Actor 10,000개 | 180초 이내, 64 units/s 이상 |
| `PreserveStateWith` Actor bulk | `normal` Actor 1,000개 | 90초 이내, 16 units/s 이상 |
| Instance Spot bulk | `normal` Instance Spot 1,000개 | 150초 이내, 8 units/s 이상 |
| `SpotWide` bulk | Actor 100개씩 포함한 User Spot 100개, 전체 participant 10,100개 | 180초 이내, 1 aggregate/s 이상 |

처리량 계산에는 `unit_count`, `completed`, `safe_aborted`, `blocked`, elapsed time, units/s,
encoded bytes/s와 payload profile별 p50·p95·p99·max를 남긴다. `SpotWide`는 Spot aggregate
수를 unit으로 세고 Spot과 member Actor의 terminal 확인 수를 `verified_participants`로
따로 남긴다. Entry·`PerActor` Actor는 queue seal부터
target admission까지 interruption도 unit별로 기록한다. 장애를 주입하지 않은 모든 Actor profile에서
1초를 넘긴 unit 수는 0이어야 한다. 의도적으로 1초 넘게 지연한 adapter 반복은 SLO population에서
분리한 negative control이다. 이 반복은 `workload_slo_missed` evidence를 남기면서 runtime이 해당
unit을 rollback하거나 취소하지 않고 safe terminal까지 진행해야 통과한다.

#### ST-I1 payload 크기와 relocation gate 측정

우선순위: `P0`

- 절차: Actor, Instance Spot과 `SpotWide` User Spot을 `small`, `normal`, `large`, `boundary` profile로
  각각 이전한다. 같은 process에서 outbound·inbound unit, Capture·Restore callback과 payload byte
  permit을 하나씩 부족하게 만든 반복도 실행한다.

| 대상 | `small` | `normal` | `large` | `boundary` |
|---|---:|---:|---:|---:|
| Actor | 4 KiB | 64 KiB | 8 MiB | 64 MiB |
| Instance Spot | 64 KiB | 1 MiB | 32 MiB | 64 MiB |
| `SpotWide` User Spot | 64 KiB | 1 MiB | 32 MiB | 64 MiB |

- 검증: 모든 permit을 얻기 전에는 source queue를 seal하지 않는다. `Capture` 뒤 실제 payload가 예상보다
  작으면 byte reservation을 줄일 수 있지만 늘리지 않는다. `boundary` aggregate는 다른 payload
  relocation과 겹치지 않고 단독 실행한다. Profile별 encoded 구성 byte의 합과 Relocation Store
  read-back 크기, checksum이 일치해야 한다. Gate 대기 중인 unit은 message와 timer를 계속 처리한다.
  Permit 부족, participant 64 MiB 초과와 aggregate 단독 실행 결과를 같은 failure로 합치지 않는다.
- 세부 동작: 현실적인 payload 분포, 실제 encoded byte accounting과 permit-before-seal.

#### ST-I2 다량 Actor relocation과 서비스 연속성

우선순위: `P0`

- 절차: 고정 workload SLO의 두 Actor bulk를 fresh host process에서 각각 실행한다. `RecreateOnRelocation`과
  `PreserveStateWith`는 서로 다른 selector로 실행하며 elapsed time과 units/s를 합치지 않는다. Relocation 전
  60초 동안 source와 target의
  control Actor·Spot에 request 200/s와 one-way 200/s를 균등하게 보내 baseline을 만든다. Host
  relocation 동안 같은 offered load를 유지하고, 이동 대상 Actor에도 global Actor ID로 request와
  one-way를 계속 보낸다. Request에는 absolute deadline과 correlation ID, 모든 operation에는 단조
  증가 sequence를 넣는다. 정본 실행에서는 이동 대상 request와 one-way를 끌 수 없으며, 어느 한쪽의
  accepted count가 0이면 실패다.
- 검증: control traffic은 request 오류 0건, accepted one-way 누락·중복 0건이어야 한다. Relocation
  구간의 control throughput은 baseline의 90% 이상이고 request p99는 `max(baseline p99×2, 250 ms)`
  이하여야 한다. 이동 대상에 seal 전에 수락된 operation은 queue·journal 순서를 유지하고, host
  relocation의 seal 뒤 commit 전 operation은 bounded relocation ingress hold를 거친다. Commit 뒤
  이전 route operation은 Message Follow duration 안에서 current owner가 한 번 처리한다. Request reply는
  original operation과 correlation이 일대일로 연결되고 duplicate reply는 0건이어야 한다.
  Queue·journal 검증은 sequence 집합만 비교하지 않는다. 같은 sender connection에서 수락한 순서와
  application handler가 관찰한 도착 순서가 같아야 한다.
- SLO 검증: 10,000개와 1,000개 반복이 각각 고정 전체 host 목표를 만족하고 장애를 주입하지 않은 Actor의
  interruption 초과가 0건이어야 한다. 목표를 넘겨도 runtime relocation은 safe terminal까지 계속하며,
  E2E result만 `workload_slo_missed`로 실패한다.
- 세부 동작: 전체 host Actor 처리량, Actor별 interruption과 이동 중 application service 연속성.

#### ST-I3 다량 Spot relocation과 서비스 연속성

우선순위: `P0`

- 절차: 고정 workload SLO의 Instance Spot과 `SpotWide` bulk를 각각 실행한다. Control traffic은
  `ST-I2`와 같은 baseline과 offered load를 사용한다. 이동하는 Spot에는 `ToSpot` one-way와 request를
  계속 제출하고 `SpotWide` member Actor에도 `ToActor` traffic을 함께 보낸다.
- 검증: control service의 오류, throughput과 latency 기준은 `ST-I2`와 같다. `SpotWide`는 aggregate
  authority CAS 전 participant 어느 것도 target owner로 공개하지 않고 CAS 뒤 전체를 target owner로
  공개한다. `PerActor` Spot authority 전환 반복에서는 `ToSpot`·Create·Join이 target으로 가는 동안
  아직 이전하지 않은 Actor의 `ToActor`는 source에서 계속 처리된다. 느린 Spot 하나가 다른 Spot,
  Actor, target `ToSpot`, Create와 Join을 막으면 실패다.
- 관측 경계: Spot request flow는 relocation traffic을 시작하기 직전의 flow ID를 watermark로
  고정한다. 그 이전 baseline flow는 relocation correlation 증거로 사용할 수 없다. `SpotWide`의
  최종 owner가 같다는 사실만으로 aggregate publication의 원자성을 통과시키지 않는다. Commit 전
  participant 0개 공개와 commit 뒤 전체 공개를 모두 관찰하지 못하면 명시적인 blocker다.
- SLO 검증: Instance Spot 1,000개와 `SpotWide` 100개 반복이 각각 고정 전체 host 목표를 만족해야 한다.
  목표를 넘겨도 이미 시작한 unit의 commit·abort·recovery는 끝까지 확인하고 E2E SLO failure로만
  기록한다.
- 세부 동작: 다량 Spot 처리량, aggregate atomicity와 이동 중 Spot·Actor service 연속성.

1초 interruption, encoded bytes/s, payload latency 분포, process CPU·peak RSS와 Store byte를 실제로
측정하지 않은 실행은 해당 값을 추정하거나 0으로 출력하지 않는다. 누락한 항목을 diagnostic blocker로
기록하며 정본 workload 완료 evidence로 사용하지 않는다.

### Track I-B — Message Follow

Relocation commit 뒤 이전 owner로 늦게 도착한 message를 일정 기간 current owner에 전달하는 기능을
[Message Follow](../spec/01-glossary.ko.md#message-follow)라고 한다. Message flow tracing, session relay와
commit 전 [relocation ingress hold](../spec/01-glossary.ko.md#relocation-ingress-hold)는 다른 기능이다.
Public caller가 owner RID나 이전 route를 선택하는 API를 추가하지 않는다.

Message Follow 검증은 workload SLO와 별도로 실행한다. 아래 각 case는 독립 selector와 독립 evidence를
가져야 한다. Actor 결과로 Spot case를 통과시키거나 request 결과로 one-way case를 통과시키면 안 된다.
같은 실행에서 여러 case를 묶더라도 case별 시작·종료와 terminal count를 따로 기록한다.

| 도착 경계 | 사용하는 기능 | 검증 위치 |
|---|---|---|
| Source seal 전 | Source queue 또는 accepted journal | `MF-AO-QUEUE`, `MF-SO-QUEUE` |
| Host relocation seal 뒤, authority commit 전 | Relocation ingress hold | `MF-AR-HOLD`, `MF-SR-HOLD` |
| Authority commit 직후, duration 안 | Message Follow | `MF-AO-FOLLOW`, `MF-AR-FOLLOW`, `MF-SO-FOLLOW`, `MF-SR-FOLLOW` |
| Route 없음, duration 0 또는 만료 뒤 | Stale-route 처리 | `MF-EXP` |
| 연속 relocation 뒤 이전 route 도착 | Multi-hop Message Follow 또는 loop·hop 거부 | `MF-LOOP`, `MF-HOP`, `ST-I6` |

Commit 직후 case는 commit 뒤 첫 operation을 이전 physical route에 전달한다. 새 owner를 다시 resolve한
정상 operation은 Message Follow 증거로 사용하지 않는다. Duration 만료 case도 만료 전에 resolve해 둔
delivery를 만료 뒤 이전 physical route에 전달해야 한다.

#### ST-I4 authority 경계별 Message Follow matrix

우선순위: `P0`

아래 matrix는 Actor와 Spot, one-way와 request를 모두 실제 public target API로 제출한다. Transport
fixture는 public resolver가 선택한 route의 delivery만 지연하며 caller에게 owner RID, `ActorRef`·`SpotRef`
내부 route나 Message Follow hop을 노출하지 않는다.

| Case | Target·operation | 이전 owner 도착 시점 | 기대 결과 |
|---|---|---|---|
| `MF-AO-QUEUE` | Actor one-way | source seal 전 | source queue 또는 accepted journal에 한 번 수락한다. Message Follow route는 사용하지 않는다. |
| `MF-AR-HOLD` | Actor request | host relocation seal 뒤, authority commit 전 | bounded ingress hold에 operation ID, absolute deadline, correlation과 reply route를 보존한다. Commit이면 target queue, abort이면 source queue에 원래 순서로 넣는다. |
| `MF-AO-FOLLOW` | Actor one-way | authority commit 뒤, Message Follow duration 안 | Committed source→target Message Follow route로 한 번 relay하고 current Actor handler가 한 번 처리한다. |
| `MF-AR-FOLLOW` | Actor request | authority commit 뒤, Message Follow duration 안 | Original request operation과 reply route를 보존하고 target reply를 original caller correlation에 한 번만 연결한다. |
| `MF-SO-QUEUE` | Instance·`SpotWide` Spot one-way | source seal 전 | Spot queue 또는 aggregate payload에 한 번 수락하고 target direct queue보다 먼저 처리한다. |
| `MF-SR-HOLD` | Instance·`SpotWide` Spot request | seal 뒤, authority commit 전 | 1,024 records·16 MiB ingress hold 안에서 보관한다. Commit과 abort에 따라 target 또는 source queue로 되돌린다. |
| `MF-SO-FOLLOW` | Instance·`SpotWide` Spot one-way | authority commit 뒤, Message Follow duration 안 | Current Spot owner로 한 번 relay하며 source application handler는 실행하지 않는다. |
| `MF-SR-FOLLOW` | Instance·`SpotWide` Spot request | authority commit 뒤, Message Follow duration 안 | Deadline과 reply correlation을 보존하고 current Spot handler의 terminal reply 하나만 반환한다. |
| `MF-PA-SPLIT` | `PerActor` User Spot의 `ToSpot`·`ToActor` | Spot authority commit 뒤, Actor owner commit 전후 | `ToSpot`은 target Spot authority, `ToActor`는 해당 Actor의 current owner를 사용한다. Actor commit 뒤 old Actor route만 Message Follow 대상으로 삼는다. |

Cross-node Actor `Join`에서 source seal 뒤 새 operation을 `Unavailable`로 끝내는 Track F와 host
`Relocate`가 bounded ingress hold를 사용하는 이 matrix를 같은 경로로 합치지 않는다. 각 case는
`operation_id`, object·owner generation, source arrival, hold/relay enqueue, hop, target admission,
handler와 terminal reply 시각을 하나의 message flow로 대조한다.

#### ST-I5 Message Follow expiry, duplicate, deadline, loop와 bound

우선순위: `P0`

| Case | 주입 방법 | 기대 결과 |
|---|---|---|
| `MF-DUP` | 같은 operation의 이전 route delivery를 두 번 완료한다. | Actor·Spot, one-way·request 모두 target handler 실행은 한 번이다. Request terminal reply도 하나이며 두 번째 delivery는 dedup evidence를 남긴다. |
| `MF-EXP` | Message Follow route 없음, `MessageFollowDuration=0`, duration 만료 뒤 delivery를 각각 실행한다. | Typed stale-route 결과로 끝나며 handler, Store lookup과 fresh owner 자동 재제출은 0건이다. One-way는 caller result를 만들지 않고 moving/stale drop evidence를 남긴다. |
| `MF-DEADLINE` | Actor·Spot request가 duration 안에서 relay되는 중 absolute deadline을 넘긴다. | Request는 timeout terminal 하나로 끝나고 late reply는 drop한다. Relay가 deadline을 새로 계산하거나 연장하면 실패다. |
| `MF-CORR` | 서로 다른 correlation ID를 가진 request를 순서를 바꿔 relay한다. | Reply payload와 correlation이 original request에 각각 한 번 연결되며 operation ID를 correlation 대신 사용하지 않는다. |
| `MF-GEN` | 같은 ID의 이전 `ObjectGeneration` message를 current Message Follow route에 전달한다. | Typed generation-stale로 끝나고 새 incarnation handler는 실행하지 않는다. |
| `MF-LOOP` | 정상 relocation으로 owner가 `actor-a -> actor-b -> actor-c -> actor-a`가 된 뒤, 이전 owner generation의 delayed operation을 전달한다. | Owner generation이 증가하지 않는 hop이나 이미 방문한 Message Follow route는 typed stale-route로 끝나며 application handler를 실행하지 않는다. |
| `MF-HOP` | Message Follow duration 안에서 세 node를 번갈아 사용해 8 hops와 9번째 hop을 만든다. | 8 hops 안의 operation은 current owner에 도달하고 추가 hop은 typed stale-route로 끝난다. |
| `MF-BOUND` | Message Follow route 하나에 1,024 messages·16 MiB 경계를 정확히 채운 뒤 message 수와 byte를 각각 하나씩 넘긴다. | 경계 안 operation은 순서대로 relay한다. 초과 request는 typed stale-route·overload terminal, one-way는 drop evidence로 끝나며 일부 payload를 handler에 전달하지 않는다. |

Loop와 hop 반복은 public relocation으로 commit된 Message Follow route와 transport delivery 지연만 사용한다.
Framework private route를 쓰거나 바꾸는 테스트 adapter는 사용할 수 없다. 모든 failure case에서 Location Store를
읽어 fresh owner로 같은 operation을 다시 제출한 evidence가 있으면 실패다.

#### ST-I6 Actor·Spot multi-hop Message Follow와 route 정리

우선순위: `P0`

- 절차: Actor, Instance Spot과 `SpotWide` User Spot을 각각 Message Follow duration 안에
  `actor-a -> actor-b -> actor-c -> actor-a`로 연속 relocation한다. 첫 owner route에 one-way와
  request를 commit 전에 보내 transport에서 지연한 뒤 마지막 relocation이 Ready가 된 다음 전달한다.
  같은 반복에서 중간 owner를 한 번 비정상 종료하고 recovery가 committed Message Follow route를 복원한 뒤 delayed
  operation을 다시 전달한다.
- 검증: 각 source node에는 object·source owner generation별 current next-hop Message Follow route만 남는다.
  Relay hop마다 operation ID, `ObjectGeneration`, absolute deadline, payload checksum, request
  correlation과 reply route가 같고 owner generation만 committed chain에 맞게 증가한다. One-way는
  final owner handler에서 한 번, request는 final owner handler와 original caller terminal에서 각각
  한 번 관찰된다. 중간 node가 비정상 종료되어도 Message Follow route를 추측하거나 Location Store에서 fresh route를
  찾아 같은 operation을 다시 만들지 않는다.
- 정리 검증: 각 Message Follow route의 duration이 끝나면 node별 entry와 queued byte가 0이 된다. 만료 뒤 첫
  owner route delivery는 typed stale-route 결과이며 final owner handler가 실행되지 않는다. Actor와
  Spot 가운데 한 종류만 정리되거나 이전 owner process의 Message Follow route가 무기한 유지되면 실패다.
- 세부 동작: Actor·Spot multi-hop Message Follow, recovery와 bounded route lifecycle.

## 5. 완료 기준

- Track A, Track B, Track C의 `P0` 시나리오는 모든 framework 언어가 같은 의미로 구현해야 한다.
- Track D와 Track E의 `P0` 시나리오는 네 runtime lane과 다섯 public 언어 표현에서 public API만으로 모두
  통과해야 한다. 구현 gap이나 필요한 public 표면 누락은 이 config의 완료를 차단한다.
- Track F의 `P0` 시나리오(ST-F1~F3, ST-F3A)는 네 runtime lane과 다섯 public 언어 표현에서 같은 순서 의미로
  통과해야 한다. `P1`(ST-F4~F6)은 지원 범위를 ledger에 기록하되 formal contract에 포함된 기능의 구현 gap을
  completion으로 처리하지 않는다.
- Track G의 `P0` 시나리오는 Actor, Instance Spot, SpotWide User Spot aggregate와
  PerActor User Spot의 direct admission·Actor 이전을 모두 사용한다. Public Spot ID,
  authority 전환, Actor별 route·Message Follow, queue·timer 순서, unit별 1초 interruption 목표와 typed capacity를 public
  evidence로 검증해야 한다. SpotWide의 기본 turn 경계와 application-signaled
  completion callback도 source·target·abort 반복에서 검증해야 한다.
- Track H의 `P0` 시나리오는 deferred barrier, completion, Context identity·fencing과 error-kind parity를
  다섯 언어에서 검증해야 한다.
- Track I의 `P0` 시나리오는 payload profile별 실제 encoded byte, 다량 Actor·Spot의 전체 host 처리량,
  Actor·Instance Spot·User Spot unit별 1초 interruption, relocation 중 control service와 이동 대상 service, Actor·Spot
  one-way·request의 Message Follow matrix를 다섯 언어에서 검증해야 한다. Correctness failure,
  runtime relocation terminal과 workload SLO failure를 서로 다른 evidence field로 기록해야 한다.
- callback order는 단순 로그 문자열 grep이 아니라 역할 server evidence와 message flow correlation id로
  검증한다.
- Location 검증은 public Actor·Spot manager의 `Find`, RouteMesh·host status 또는 역할 server endpoint로
  관찰한다. 내부 Store key를 client가
  직접 읽어 성공 판정하면 안 된다.
- failure injection은 application endpoint 또는 runner process control로만 한다. framework private state를
  직접 조작하는 테스트 전용 adapter는 사용하지 않는다.
- 모든 `P0` 시나리오는 source/target node rid, actor id, relocation id, callback order marker, location
  snapshot, bound session snapshot을 실패 evidence에 남겨야 한다.
