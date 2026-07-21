<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: To-actor messaging](config-9-to-actor-messaging.ko.md) | [다음: 관측·운영 배포](config-11-observability-ops.ko.md)
<!-- framework-adapter-nav:end -->

# Config 10 — Spot actor join/transfer 배포

actor가 Entry Spot과 user Spot 사이를 이동하거나, 다른 MeshNode의 user Spot으로 transfer될 때
[Spot Actor Join / Transfer 공통 스펙](../../spec/server/23-spot-actor.ko.md)을 실제 배포 형태에서 만족하는지
검증한다. 이 config는 단순 Spot request 성공 여부가 아니라 admission, leave, transfer, joined,
authority commit, bound session route, failure cleanup이 같은 순서와 의미로 관찰되는지 본다.

이 문서는 e2e 시나리오 정의만 둔다. 언어별 구현은 public framework API와 역할 server endpoint로
작성해야 한다. 현재 언어별 구현이 공통 스펙의 목표 공개 계약을 아직 제공하지 못하면 skip으로 완료
처리하지 않고 feature-map에 public contract parity gap으로 남긴다. 내부 helper, raw frame 조작,
테스트 전용 adapter로 이 config를 통과시키면 안 된다.

이 config의 actor 이동은 공개 transfer 계약을 호출해 처리 주체를 명시적으로 바꾸는 동작이다.
MeshNode를 추가하는 scale-out만으로 기존 owner가 자동 변경되는 동작은 계약하지 않는다. MeshNode
증설과 신규 배치는 Config 2 SM-G2에서 검증하고, 운영자가 node를 drain해 기존 actor를 다른 node로
인계하는 동작은 Config 11의 drain handoff 시나리오에서 검증한다.

## 1. 목적과 범위

- 다룬다: 같은 node join 순서, remote actor transfer 정상 경로, transfer state 복원, 빈 state transfer, admission/commit
  분리, source node down 전후 동작, authority commit 시점, moving 중 actor packet dispatch 차단,
  transfer adapter 미등록 기본 동작과 callback 실패, bound session 이전, **actor 이동 중 in-flight packet
  handoff(순서 보존·publish 전 replay·straggler forwarding과 mapping 축출·request reply correlation과 timeout)**.
- 여기서 다루지 않는다: 일반 spot messaging 전체(Config 2), 비동기 handler 완료 후 mailbox 재개(Config 8),
  actor id 기반 no-bind send/request(Config 9), location store 자체 장애(Config 6).
- 계약 근거: `OnActorJoin`은 admission만 담당하고 actor instance를 받지 않는다. admission accept 뒤
  durable Actor authority row는 CAS commit 시점부터 target owner를 가리킨다. target Actor route와 성공
  reply는 `OnJoinedActor` 정상 완료와 activation 뒤에만 관찰되어야 한다. remote transfer에서는 source
  `TransferOut`, target `TransferIn`, authority commit, source `OnLeaveActor`, target `OnJoinedActor` 순서가
  evidence로 남아야 한다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. Actor·Spot authority row와 owner lease는 framework lifecycle이 관리한다. |
| actor 노드 | 2 (`actor-a`, `actor-b`) | Entry Spot + user Spot + actor mailbox host. 각 node는 같은 actor type을 등록하고, state 이동이 필요한 actor type에는 같은 transfer adapter를 등록한다. lifecycle callback과 transfer adapter는 order marker를 남긴다. |
| session gateway | 2 (`session-a`, `session-b`) | stream session을 받고 actor bind와 actor push를 관찰한다. remote transfer 뒤 bound session push가 target actor로 이어지는지 검증한다. |
| transfer controller | 1 | 실제 사용자 요청을 받는 역할 server. HTTP endpoint 안에서 actor 생성, join, transfer, packet send, failure injection을 public framework API로 실행한다. |
| consumer | 시나리오별 | HTTP client wrapper로 transfer controller endpoint를 호출하고, 필요한 경우 stream connector로 session gateway에 연결해 push와 bind 상태를 관찰한다. |

actor 노드는 아래 evidence를 공통으로 남긴다.

- actor id, actor type, actor generation 또는 ref snapshot, source/target spot rid, source/target node rid.
- callback order marker: `admission`, `transfer_out`, `commit_request`, `transfer_in`, `authority_committed`,
  `leave`, `joined`, `commit_ack`, `source_cleanup`.
- in-flight handoff marker: `handoff_backlog`(moving 중 보존한 packet), `backlog_enqueued`(target queue 적재),
  `straggler_forward`(공개 뒤 forward), `mapping_evicted`(window 후 축출), `stale_fail_fast`(축출 뒤 old ref 거부).
  각 actor packet은 arrival sequence index를 함께 남겨 target 처리 순서와 대조할 수 있어야 한다.
- `OnActorJoin` 입력 snapshot. actor id 외에 actor instance나 route metadata가 전달되었거나 저장되면 실패 evidence로 남긴다.
- transfer state marker. state를 담는 actor type은 source actor state version과 target actor 복원 state가 같은지 확인할 수 있어야 한다. 빈 state actor type은 target `OnJoinedActor` 이후 별도 조회 marker를 남긴다.
- actor packet handler marker. moving 중 source와 target 양쪽 handler가 동시에 처리하지 않았는지 대조한다.
- bound session snapshot. transfer 전후 push 대상 session gateway와 client connector를 비교한다.

transfer controller는 시나리오 실행 전용 driver가 아니다. consumer는 실제 app endpoint를 호출하고,
그 endpoint 내부에서 framework의 public actor/spot API가 실행되어야 한다. 장애 주입은 endpoint가
application 상태를 바꾸거나 `run_e2e.sh`가 역할 process를 중단하는 방식으로만 수행한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → actor 노드 → session gateway → transfer controller
순으로 시작한 뒤 client 시나리오를 순차 실행한다. 각 시나리오는 독립 actor id와 spot id를 사용해야
한다. 시나리오가 process 중단이나 route 차단을 사용하면 follow-up 검증 뒤 반드시 원래 topology를
복구하거나 다음 시나리오가 새 prefix를 쓰게 한다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남기고, message flow 추적을 지원하는 언어는 최소 `key_transitions`로 켠다. 실패 시에는
actor id, transfer id, source/target node rid, callback order marker, authority snapshot, bound
session snapshot을 함께 남긴다.

## 4. 시나리오

### Track A — local join

#### ST-A1 local join accept 순서

우선순위: `P0`

**검증 질문:** 같은 node에서 actor가 Entry Spot에서 user Spot으로 join할 때 admission, leave, location
commit, joined, success reply가 정해진 순서로 관찰되는가.

- 절차: consumer가 transfer controller endpoint를 호출해 `actor-local-ok`를 만들고 같은 node의 user
  Spot으로 `JoinSpot`을 실행한다. controller는 join reply를 받은 뒤 같은 ActorRef로 actor packet을 보내
  committed target membership에서 처리되는지 확인한다.
- 검증: evidence order는 `admission -> authority_committed -> leave -> joined -> success_reply`다.
  `OnActorJoin` evidence에는 actor id와 request만 있고 actor instance snapshot이나 route metadata가 없어야 한다.
  success reply 전에 authority row가 committed target User Spot을 가리켜야 한다. join 이후 packet은 target
  Actor handler에서만 처리되고 Spot lifecycle callback을 경유하지 않는다.
- 세부 동작: 같은 node join 완료 조건과 actor instance 미노출 admission.

#### ST-A2 local join reject side effect 없음

우선순위: `P0`

**검증 질문:** target admission이 reject하면 source membership, source location, handler dispatch가
그대로 유지되는가.

- 절차: `actor-local-reject`를 Entry Spot에 만든 뒤 target User Spot의 join handler가 typed rejection reply와
  함께 거부하도록 request를 보낸다. 이후 같은 ActorRef로 actor packet을 보낸다.
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

- 절차: target `OnJoinedActor`가 bounded latch를 기다리도록 설정한 뒤 local join을 시작한다. join이
  moving 상태인 동안 같은 actor로 packet을 보낸다. latch를 해제한 뒤 follow-up packet을 보낸다.
- 검증: moving 구간에는 source와 target Actor handler가 같은 actor packet을 동시에 처리한 evidence가
  없어야 한다. target `OnJoinedActor` 완료 전 target Actor handler 성공 evidence도 없어야 한다. latch 해제
  뒤 follow-up packet은 target Actor handler에서 처리된다.
- 세부 동작: moving 중 dispatch 차단.

### Track B — remote transfer 정상 경로

#### ST-B1 remote transfer 성공 순서와 state 복원

우선순위: `P0`

**검증 질문:** actor가 `actor-a`에서 `actor-b`의 user Spot으로 이동할 때 transfer state가 복원되고
callback 순서가 공통 스펙과 일치하는가.

- 절차: `actor-remote-ok`를 `actor-a`에 만들고 mutable state를 설정한다. controller가 `actor-b`의
  user Spot으로 join을 실행한다. source transfer adapter는 state version을 `ZLinkMessage`로 만들고 target
  transfer adapter는 target actor로 복원한다.
- 검증: evidence order는 `admission -> transfer_out -> commit_request -> transfer_in ->
  authority_committed -> leave -> joined -> commit_ack -> success_reply`다. target `OnJoinedActor`에는 immutable
  membership snapshot만 전달되며, callback이 mutable Actor instance를 받거나 보관하면 실패다. join 완료
  뒤 snapshot의 ActorRef로 state 조회 request를 보내 source state version이 복원되었는지 확인한다. source와
  target Actor ID와 object generation은 유지되고 owner generation과 owner snapshot만 target owner 값으로
  바뀐다.
- 세부 동작: remote admission/commit 분리 + transfer state 복원.

#### ST-B2 source cleanup 실패는 성공을 되돌리지 않음

우선순위: `P0`

**검증 질문:** target commit ack 뒤 source cleanup이 끝나기 전에 source가 사라져도 join 성공과
target ownership이 유지되는가.

- 절차: remote transfer 정상 경로를 실행하되 source node가 `commit_ack`를 받은 뒤 `source_cleanup`
  marker를 남기기 전에 대기하도록 application endpoint로 설정한다. caller가 success reply를 받은 것을
  확인한 뒤 `run_e2e.sh`가 `actor-a` process에 `SIGKILL`을 보낸다. 정상 종료나 drain으로
  `source_cleanup`이 완료되는 경로를 이 시나리오에 섞지 않는다. 이후 target actor에게 packet을 보내고,
  가능한 언어는 cleanup retry evidence를 bounded wait로 확인한다.
- 검증: caller는 target commit ack 이후 success reply를 받는다. target actor packet은 target Actor handler에서
  처리된다. source cleanup 미완료나 source process 종료는 join 실패로 rollback되지 않는다. stale source
  release 재시도가 target authority의 object·owner generation을 지우면 실패다.
- 세부 동작: source cleanup의 사후 멱등 정리.

#### ST-B3 transfer adapter 미등록 기본 빈 state transfer

우선순위: `P0`

**검증 질문:** remote transfer 대상 actor type에 transfer adapter가 없어도 framework 기본 빈 state transfer로 성공하는가.

- 절차: `actor-no-adapter` type에는 actor factory만 등록하고 transfer adapter는 등록하지 않는다. 같은 node
  join이 아니라 반드시 다른 node user Spot으로 remote transfer를 시도한다.
- 검증: remote transfer는 성공한다. evidence order는 `admission -> transfer_out_empty_default ->
  commit_request -> transfer_in_empty_default -> authority_committed -> leave -> joined -> commit_ack ->
  success_reply`다.
  source `OnLeaveActor`, target `OnJoinedActor`, target authority commit이 모두 정상 순서로 관찰된다.
  Accepted journal도 비어 있으면 source requirement의 message·byte는 0이고 target capacity offer는 양수다.
  Runtime은 비어 있는 deterministic checkpoint envelope과 reservation generation을 기록한다.
- 세부 동작: transfer adapter 미등록 기본 빈 state와 all-zero inventory transfer.

#### ST-B4 remote transfer empty state

우선순위: `P0`

**검증 질문:** custom transfer adapter가 빈 `ZLinkMessage`를 반환해도 target actor가 만들어지고
domain state를 별도로 읽어 올 수 있는가.

- 절차: `actor-empty-state` type에는 custom transfer adapter를 등록한다. source `TransferOut`은 빈
  `ZLinkMessage`를 반환한다. target `TransferIn`은 actor id와 public actor 생성 경로로 target actor를
  만든다. target `OnJoinedActor`는 actor id로 별도 저장소에서 domain state를 읽고 marker를 남긴다.
- 검증: remote transfer는 성공한다. evidence order는 `admission -> transfer_out_empty -> commit_request ->
  transfer_in_empty -> authority_committed -> leave -> joined -> domain_state_loaded -> commit_ack
  -> success_reply`다. adapter 미등록 기본 빈 state transfer와 같은 성공 의미지만, custom adapter
  경로가 빈 state를 반환해도 정상이라는 점을 별도로 확인한다.
- 세부 동작: custom adapter 빈 state transfer.

### Track C — failure/recovery

#### ST-C1 source down after admission before commit

우선순위: `P0`

**검증 질문:** target admission accept 뒤 source node가 commit 전에 비정상 종료되면 transfer가 완료되지 않고
target pending admission만 정리되는가.

- 절차: remote transfer를 시작하고 target `OnActorJoin` accept evidence와 source node의 `before_commit_gate`
  marker를 모두 기다린다. `before_commit_gate`는 source가 admission accepted 응답을 받은 뒤 commit 요청을
  보내기 전에 application endpoint가 걸어 둔 public callback/evidence gate다. 두 marker가 모두 나온 뒤
  `run_e2e.sh`가 `actor-a` process에 `SIGKILL`을 보내 transport를 즉시 끊는다. 정상 종료나 drain은
  진행 중 outbound transfer를 완료할 수 있으므로 사용하지 않는다. `actor-b`의 pending admission
  deadline이 지나도록 bounded
  wait를 둔다.
- 검증: target `OnJoinedActor`, target `TransferIn`, target authority commit evidence가 없어야 한다.
  target은 source down signal을 기다리지 않고 pending admission timeout cleanup evidence를 남긴다.
  같은 actor id에 대해 성공한 target membership이나 target handler dispatch가 없어야 한다.
- 세부 동작: accept / before commit 상태의 timeout cleanup.

#### ST-C2 source down after target commit

우선순위: `P0`

**검증 질문:** target `OnJoinedActor`와 commit ack가 끝난 뒤 source node가 비정상 종료되어도 target ownership이
유지되는가.

- 절차: remote transfer 정상 경로에서 target `OnJoinedActor`와 commit ack evidence를 확인한 직후
  `actor-a` process에 `SIGKILL`을 보낸다. 이후 target actor에게 packet과 bound session push를
  발생시킨다. 정상 종료나 drain cleanup 결과를 source 장애 evidence로 사용하지 않는다.
- 검증: Actor authority row는 target User Spot과 target node를 가리킨다. target Actor packet과 bound
  session push가 성공한다. stale source owner cleanup 실패나 source process 종료가 target ownership을
  지우면 실패다.
- 세부 동작: target commit 뒤 source 장애.

#### ST-C3 callback과 transfer 실패 분류

우선순위: `P1`

**검증 질문:** transfer 단계별 application 실패가 공통 스펙의 결과로 분류되는가.

- 절차: 같은 actor type으로 `TransferOut`, source `OnLeaveActor`, `TransferIn`, target `OnJoinedActor`가
  각각 실패하도록 네 개의 독립 시나리오를 실행한다.
- 검증: `TransferOut` 또는 `TransferIn` 실패는 source leave 없이 source membership을 유지한다.
  `OnLeaveActor`와 `OnJoinedActor` 실패는 authority commit 뒤 실패이므로 caller success가 없어야 하지만
  target membership을 source로 rollback하면 안 된다. Target Actor packet dispatch를 차단한 recoverable
  reconciliation 상태에서 target activation을 계속한다.
- 세부 동작: 실패 지점별 join 결과.

### Track D — authority/routing/dispatch

#### ST-D1 authority commit 시점

우선순위: `P0`

**검증 질문:** authority commit 뒤 target `OnJoinedActor`가 완료되기 전에는 committed row와 아직 준비되지
않은 target Actor route가 구분되는가.

- 절차: local join과 remote transfer 각각에서 target `OnJoinedActor`를 지연시킨다. 지연 중 location query
  endpoint와 actor packet route를 반복하지 않고 bounded evidence wait로 한 번씩 관찰한다. 이후 latch를
  해제하고 다시 관찰한다.
- 검증: 지연 중 authority row는 committed target owner와 더 높은 owner generation을 가리키지만 target Actor
  route는 ready가 아니며 packet handler를 실행하지 않는다. `OnJoinedActor` 완료와 target activation 뒤에만
  새 owner route가 ready로 공개되고 packet이 처리된다.
- 세부 동작: durable authority commit과 target route activation 구분.

#### ST-D2 stale source release authority fencing

우선순위: `P1`

**검증 질문:** source cleanup이나 stale owner release가 target authority를 변경하지 않는가.

- 절차: remote transfer 성공 뒤 source cleanup retry가 늦게 실행되도록 지연한다. 그 사이 target Actor에게
  packet을 보내고 target authority의 object generation, owner generation과 store version을 기록한다. 이후
  지연된 source cleanup을 실행한다.
- 검증: cleanup 전후 target authority 값이 유지된다. cleanup 뒤 follow-up packet도 target에서 처리된다.
  source cleanup이 target owner row를 삭제하거나 stale route로 되돌리면 실패다.
- 세부 동작: expected-version CAS와 stale cleanup 격리.

### Track E — bound session transfer

#### ST-E1 remote transfer 뒤 bound session push

우선순위: `P0`

**검증 질문:** session에 bind된 actor가 remote transfer된 뒤 actor push가 target actor에서 기존 client로
이어지는가.

- 절차: consumer가 `session-a`에 연결해 `actor-bound-transfer`를 bind한다. transfer 전 actor가
  `BeforeTransferNotify`를 push해 client가 받는지 확인한다. remote transfer를 실행한 뒤 target actor가
  `AfterTransferNotify`를 push하게 한다.
- 검증: 두 notify는 같은 client connector가 받는다. transfer 뒤 push evidence는 target actor와 target
  node에서 발생해야 한다. source bound session cleanup 실패가 있더라도 target push가 성공해야 한다.
- 세부 동작: bound session route 이전.

#### ST-E2 실패한 transfer는 bound session route를 바꾸지 않음

우선순위: `P0`

**검증 질문:** remote transfer가 commit 전에 실패하면 기존 bound session binding이 성공한 transfer처럼
바뀌지 않는가.

- 절차: Consumer가 actor를 bind한 뒤 remote transfer를 시작하고 target admission accept 뒤 source down
  before commit 또는 transfer adapter 실패를 주입한다. Session ingress seal 뒤 durable `Aborted` authority CAS,
  source-route abort command·ACK, reservation·checkpoint cleanup과 steady source normalization 경계마다
  coordinator를 한 번씩 종료한다. 이후 source가 계속 실행 중인 경우 기존 actor가
  `AfterFailedTransferNotify`를 push하게 한다.
- 검증: `Aborted` 결정 전에는 abort route를 보내거나 source ingress를 열지 않는다. Recovery는 `Aborted`
  authority에서 source-route command를 idempotent하게 재전송하고 current session owner의 routed ACK를 받은 뒤
  cleanup과 steady source normalization을 완료한 경우에만 source admission을 다시 연다. 실패한 transfer는
  target bound session route를 만들지 않는다. Source actor가 유지되면 기존 client connector가 follow-up notify를
  받는다. Source가 비정상 종료된 경우에는 client reconnect/recreate 흐름으로 분류되고 target actor push 성공으로
  보이면 실패다.
- 세부 동작: 실패한 transfer의 bound session 비오염.

### Track F — in-flight packet handoff (source queue handoff)

[23-spot-actor.ko.md §10](../../spec/server/23-spot-actor.ko.md)의 source queue handoff 계약을 배포 형태로 검증한다.
모든 시나리오는 arrival sequence index로 "보낸 순서 vs target 처리 순서"를 대조한다.

#### ST-F1 in-flight handoff order

우선순위: `P0`

**검증 질문:** actor가 moving 상태인 동안 도착한 actor packet이 유실 없이 target에서 도착 순서대로
처리되는가([Spot Actor §10.2](../../spec/server/23-spot-actor.ko.md#102-ordering)의 1·2항).

- 절차: `actor-inflight-order`를 `actor-a`에 만들고 `actor-b`의 user Spot으로 remote transfer를
  시작한다. target `OnJoinedActor`를 bounded latch로 지연시켜 moving 구간을 연다. 그 사이 controller가
  이 actor로 향하는 actor packet `P1 -> P2 -> P3`를 순서대로 보낸다. latch를 해제한 뒤 처리 순서를 관찰한다.
- 검증: 세 packet 모두 유실 없이 **target actor handler에서 `P1 -> P2 -> P3` 순서로** 처리된다.
  moving 구간에 source Actor handler가 이 packet들을 처리한 evidence가 없어야 한다
  ([Spot Actor §10.1](../../spec/server/23-spot-actor.ko.md#101-moving-상태의-admission)). source는
  `handoff_backlog` marker에 arrival index를 순서대로 남기고, target은 `backlog_enqueued`로 같은 순서를 남긴다.
- 세부 동작: moving 중 packet 보존과 정렬 handoff.

#### ST-F2 direct overtakes prevented

우선순위: `P0`

**검증 질문:** 이동 완료 직후 새 location으로 온 direct packet이 handoff backlog보다 먼저 처리되지
않는가([Spot Actor §10.2](../../spec/server/23-spot-actor.ko.md#102-ordering)의 3항).

- 절차: `actor-inflight-overtake`를 remote transfer한다. moving 구간에 source로 backlog packet
  `B1 -> B2`를 넣는다. Target route가 ready로 공개된 직후, controller가 새 location으로
  re-resolve해 direct packet `D1`을 보낸다.
- 검증: target 처리 순서는 `B1 -> B2 -> D1`이다. `D1`이 `B1`/`B2`보다 먼저 처리되면 실패다. evidence
  order는 `authority_committed -> backlog_enqueued -> target_activated -> route_published`여야 한다. Durable
  commit 뒤 backlog를 queue에 넣고, 그 뒤에 새 owner route를 ready로 공개해야 direct packet이 추월하지 않는다.
- 세부 동작: route 공개 전 backlog enqueue로 direct 추월 차단.

#### ST-F3 bound session cross-move order

우선순위: `P0`

**검증 질문:** 한 bound session이 이동을 가로질러 보낸 packet이 보낸 순서대로 actor에 도달하는가
([Spot Actor §10.2](../../spec/server/23-spot-actor.ko.md#102-ordering)의 4항).

- 절차: consumer가 `session-a`에 연결해 `actor-bound-order`를 bind한다. client가 연속 packet
  `S1 -> S2 -> S3 -> S4`를 보내는 도중에 controller가 `actor-b`로 remote transfer를 실행해, 일부는
  rebind 전(source 경유), 일부는 rebind 후(target 직행)가 되도록 한다. Session owner가 binding ingress를
  reversible seal한 뒤 session owner ID·host lease generation·node RID·lifecycle generation, session RID·binding
  generation과 마지막 accepted session sequence를 기록한다. Source가 그 sequence까지 frozen journal에 포함한
  evidence와 target이 같은 participant identity를 복원해 sequence까지 replay한 evidence를 남긴다. Target route
  publication과 session unseal은 current session owner의 local lease-derived deadline과 receiver가 다시 확인한
  current host lease를 모두 통과한 route ACK 뒤에만 수행한다.
- 검증: target actor가 네 packet을 **`S1 -> S2 -> S3 -> S4` 순서로** 받는다. session route rebind 경계에서
  역전된 evidence가 있으면 실패다. rebind 전 packet의 backlog handoff가 rebind 후 direct packet보다 먼저
  target queue에 적재되어야 한다. Seal과 동시에 수락된 packet은 last accepted sequence 이하이면 journal에
  포함되고, 그보다 크면 새 route가 ACK된 뒤 target으로 전달된다. 어느 쪽에도 속하지 않거나 두 번 처리되는
  packet이 없어야 한다.
- 세부 동작: bound session의 per-binding sequence barrier와 cross-move FIFO.

#### ST-F3A paused session owner lease fence

우선순위: `P0`

**검증 질문:** Session owner process가 pause되어 host lease가 만료돼도 transport I/O가 유지되는 경우, 늦은
seal·route ACK가 Actor binding route를 바꾸지 못하는가.

- 절차: `ST-F3`의 binding ingress seal 뒤 session owner process의 application runtime을 pause하고 transport
  fixture는 connection을 유지한다. Host owner lease가 만료된 뒤 이전 owner의 `sessionTransferSealed`와
  `sessionTransferRouted`를 전달한다. Successor session owner는 새 owner token으로 같은 binding을 복구한다.
- 검증: Sender의 local monotonic admission deadline을 넘긴 command와 receiver가 current descriptor의 owner
  ID·owner lease generation을 다시 확인했을 때 일치하지 않는 command는 route publication과 unseal에 사용하지
  않는다. Successor token과 exact session·binding generation을 가진 ACK 하나만 route를 바꾸며 packet 누락,
  중복과 FIFO 역전이 없다.
- 세부 동작: Bound-session barrier의 host lease fencing과 transport liveness 분리.

#### ST-F4 straggler forward then fail-fast

우선순위: `P1`

**검증 질문:** authority commit 전에 이전 owner route로 전송된 straggler가 window(기본 5초) 안에서는 target으로
forward되고, window 초과분은 fail-fast로 분류되는가
([Spot Actor §10.4](../../spec/server/23-spot-actor.ko.md#104-straggler-forwarding)).

- 절차: `actor-straggler`의 transfer authority commit 전에 source owner route로 이미 전송된 packet `G1`과
  `G2`를 transport fixture에서 각각 지연한다. `G1`은 commit 뒤 forwarding window 안에, `G2`는 window가
  지난 뒤 source에 전달한다. Public `ActorRef`의 object generation은 transfer 전후 동일하며 fixture가
  caller에게 이전 owner route를 노출하거나 선택하게 해서는 안 된다. Window 값은 controller가 짧게
  설정해 실행 시간을 줄일 수 있다.
- 검증: `G1`은 `straggler_forward`를 거쳐 target actor에서 처리된다. `G2`는 `stale_fail_fast`로
  분류되고 target에서 처리되지 않으며, caller는 terminal 결과를 확인한 뒤 명시적으로 다시 제출해야 한다.
  framework가 `G2`를 자동 저장·재전송한 evidence가 있으면 실패다.
- 세부 동작: straggler bounded forwarding과 cutoff.

#### ST-F5 forwarding mapping eviction

우선순위: `P1`

**검증 질문:** window 후 forwarding mapping이 축출되어 누수가 없고, window 안 재이동은 entry를 갱신하는가
([Spot Actor §10.4](../../spec/server/23-spot-actor.ko.md#104-straggler-forwarding)).

- 절차: 두 부분으로 실행한다. (a) `actor-map-evict`를 remote transfer한 뒤 window 경과를 bounded wait로
  두고 `mapping_evicted` marker를 관찰한다. (b) `actor-map-chain`을 window 안에 `actor-a -> actor-b ->
  actor-a의 다른 user Spot`처럼 **다른 node로 두 번** 연속 이동시키고 각 node의 forwarding entry snapshot을
  관찰한다. 첫 node(`actor-a`)의 old ref로 straggler를 주입한다.
- 검증: (a) window 경과 후 `mapping_evicted`가 남고, 이후 old ref packet은 `stale_fail_fast`다. (b) **각
  source node는 그 actor에 대해 entry가 최대 하나**이고 그 entry는 자기 **다음 hop**을 가리킨다(첫 node는
  두 번째 node를, 두 번째 node는 최종 target을). 첫 node에 주입한 straggler는 hop을 따라 최종 target까지
  전달된다. 한 node 안에 이전 target을 가리키는 잔여 entry가 함께 남으면 실패다. 각 window 경과 후 모든
  entry가 축출되어 누수가 없다.
- 세부 동작: mapping retained state의 bounded 축출과 node별 chained forward(hop 전달).

#### ST-F6 in-flight request reply correlation과 timeout

우선순위: `P1`

**검증 질문:** 이동 중 도착한 **request**(reply 대기)가 이동 후 target에서 처리되어 reply가 원래
caller로 correlate되고, timeout은 caller 기존 경로로 동작하는가
([Spot Actor §10.1](../../spec/server/23-spot-actor.ko.md#101-moving-상태의-admission),
[Flow Correlation §7](../../spec/server/53-flow-correlation.ko.md#7-reply와-failure)). ST-F1~F3은 Send만 쓰므로
request의 reply correlation·timeout 경로는 이 시나리오가 검증한다.

- 절차: 두 부분으로 실행한다. (a) **reply correlation** — `actor-inflight-req`를 remote transfer한다.
  moving 구간에 controller가 이 actor로 request(충분히 긴 timeout)를 보낸다. 이동 완료 후 target actor가
  처리해 reply를 낸다. (b) **timeout** — 같은 흐름에서 request timeout을 이동 완료보다 짧게 두거나 target
  처리를 지연시켜 caller timeout이 나게 하고, 그 뒤 늦은 reply가 생기게 한다.
- 검증: (a) reply가 **원래 caller로 request id correlate**되어 도착한다(중복·오배달 없음). handoff/forward
  evidence에 request id·flags가 보존됐음을 남긴다. reply가 source를 다시 거친 evidence가 있으면 안 된다
  (target→caller 직행). (b) timeout 케이스는 caller가 **normal request timeout 실패**로 분류하고(이동 전용
  예외 경로 없음), 뒤늦은 reply는 late-reply drop된다.
- 세부 동작: in-flight request의 reply correlation·timeout·framing 보존.

## 5. 완료 기준

- Track A, Track B, Track C의 `P0` 시나리오는 모든 framework 언어가 같은 의미로 구현해야 한다.
- Track D와 Track E의 `P0` 시나리오는 location store와 stream connector가 있는 언어에서 public API만으로
  구현해야 한다. 필요한 public 표면이 없으면 feature-map에 public contract parity gap으로 남긴다.
- Track F의 `P0` 시나리오(ST-F1~F3, ST-F3A)는 remote transfer를 지원하는 모든 언어가 같은 순서 의미로 구현해야
  한다. `P1`(ST-F4~F6)은 straggler forwarding window, mapping 축출, request reply correlation과 timeout을
  public 관찰 수단으로 검증할 수 있는 언어에서 구현하고, 없으면 parity gap으로 남긴다.
- callback order는 단순 로그 문자열 grep이 아니라 역할 server evidence와 message flow correlation id로
  검증한다.
- location 검증은 public resolver/query 또는 역할 server endpoint로 관찰한다. 내부 store key를 client가
  직접 읽어 성공 판정하면 안 된다.
- failure injection은 application endpoint 또는 runner process control로만 한다. framework private state를
  직접 조작하는 테스트 전용 adapter는 사용하지 않는다.
- 모든 `P0` 시나리오는 source/target node rid, actor id, transfer id, callback order marker, location
  snapshot, bound session snapshot을 실패 evidence에 남겨야 한다.
