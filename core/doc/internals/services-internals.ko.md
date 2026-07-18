[English](services-internals.md) | [한국어](services-internals.ko.md)

# 서비스 계층 내부 설계

이 문서는 core 유지보수자가 10.0.0 서비스 계층(MeshNode·Spot·Actor·STREAM
session)의 실제 내부 구조를 빠르게 파악하도록 돕는 내부 문서다. 공개 계약은
[`doc/spec/core/service/`](../spec/core/service/README.ko.md)의 정식 spec이
소유하며, 이 문서는 그 계약을 구현한 현재 소스 구조만 설명한다.

## 1. 소스 배치와 책임 경계

| 위치 | 책임 |
|---|---|
| `src/runtime/services/mesh/mesh_runtime.{hpp,cpp}` | 객체 모델과 프로세스-로컬 상태 기계. `mesh_node_t`, owner mailbox, ready index, claim, budget, monitor queue, handle registry |
| `src/runtime/services/mesh/mesh_wire*.{hpp,cpp}` | 원격 wire 4모듈 — `mesh_wire_codec`(wire 포맷 결정), `mesh_wire_admission`(peer admission 상태 기계), `mesh_wire_ingress`(수신 프레임의 서비스 라우팅과 ingress 스레드), `mesh_wire`(transport 수명과 발신 submit). 공유 선언은 `mesh_wire_internal.hpp` |
| `src/api/mesh/mesh_node_api.cpp` | lifecycle, membership, peer intent, option, status·query C API |
| `src/api/mesh/mesh_messaging_api.cpp` | node/channel/Spot direct send·request, Logical Multicast publish |
| `src/api/mesh/mesh_dispatch_api.cpp` | ready handler, drain, ready/receive batch, claim, reply token |
| `src/api/mesh/mesh_actor_api.cpp` | Actor 생성·조회·파괴·join·메시징 |
| `src/api/mesh/mesh_transfer_api.cpp` | Actor transfer prepare/commit/activate/abort와 fence |
| `src/api/mesh/mesh_monitor_api.cpp` | MeshNode monitor open/handler/recv/status/close |
| `src/api/mesh/mesh_stream_session_api.cpp` | STREAM session service와 Actor binding |
| `src/api/mesh/mesh_api.cpp` | poller·timer 등 세로 관심사가 mesh로 들어오는 seam |

계층 규칙: `api/mesh/*`는 공개 signature 검증과 결과 매핑을 소유하고, 상태
변경은 `mesh_runtime`/`mesh_wire`의 함수로 내려보낸다. 예외는 세로 관심사
seam인 `mesh_api.cpp`다: Spot timer registry(cancellation 포함)와 turn
admission 상태(`timer_turn_active`·`timer_count`)는 이 seam이 직접
소유·변경한다 — timer 기계는 hook만 제공하고 Spot 결합 지식은 mesh 쪽 한
곳에 모은다. raw socket 계층
(`runtime/sockets/`)은 mesh를 모른다. mesh가 raw ROUTER에 요구하는 유일한
확장은 비소비 기록 조사인 `routed_target_writable()`(NODROP 원자 reserve용)
하나다.

## 2. 객체 모델

```
zlink_ctx
 └─ mesh_node_t (프로세스당 MeshName 하나, immortal handle registry로 검증)
     ├─ 소유: raw ROUTER socket 1개 (bind 1개, 모든 peer pipe)
     ├─ ingress 스레드 1개 (recv + socket monitor drain)
     ├─ peers: vector<peer_state_t>            — intent와 admitted peer 단일 표
     ├─ channels: map<ChannelName, weight>     — start 후 이름 불변, weight 가변
     ├─ owners: map<owner_id_t, owner_state_t> — Node·Spot·Actor mailbox
     │    └─ domains[2]: application / infrastructure mailbox
     ├─ ready: set<(owner, domain)>            — 레벨 트리거 ready index
     ├─ reply_routes: map<serial, reply_route_t> — one-shot reply 봉인 저장소
     ├─ operations: map<op_low, pending_operation_t>
     ├─ transfers: map<serial, transfer_state_t>
     ├─ spots / actors: 논리 registry (facade는 얇은 핸들)
     └─ monitor: monitor_state_t 1개 (bounded queue + counter)
```

- 핸들 유효성은 전역 immortal registry(`registry ()`)가 판정한다. 정적 소멸
  순서 문제를 피하기 위해 registry 저장소는 의도적으로 누수시킨다(요청
  timeout scheduler와 같은 패턴).
- 공개 진입점은 registry pin(RAII `mesh_node_pin_t`)으로 node 수명을
  잡는다: destroy는 registry에서 handle을 제거한 뒤 pin이 0이 될 때까지
  대기하고 나서야 storage를 해제한다. shutdown 전용 pin은 destroy claim을
  §11 재진입(`EDEADLK`)으로 거절하고, destroy claim은 이중 destroy를
  `ESTALE`로 거절한다. blocking 대기 경로는 destroy의 forced-stop 통지에
  깨어나 상태 검사로 반환하므로 pin은 유한 시간 안에 빠진다.
- `owner_id_t`는 (kind, key, generation)이다. Spot·Actor generation이 다르면
  다른 mailbox다. 낡은 generation으로의 접근은 mailbox 부재로 끝난다.
- `Spot` facade와 publisher, monitor, STREAM session service는 모두 node의
  strong child reference로 계산되어, 닫히기 전에는 `zlink_mesh_node_destroy`가
  `EBUSY`로 거부한다.

## 3. Mailbox, ready index와 claim

메시지 admission의 단일 관문은 `admit_record()`다.

- application domain은 message/byte budget의 제약을 받고, 초과 시 비차단
  호출은 `EAGAIN`, 차단 호출은 deadline까지 조건변수 대기 뒤 `ETIMEDOUT`이다.
  거부는 같은 함수 안에서 `BACKPRESSURED` monitor event를 방출한다.
- infrastructure domain(completion·transfer control·SEND_READY)은 outstanding
  operation 집합이 상한이므로 budget을 적용하지 않고 항상 전진한다.
- transfer fence가 걸린 owner의 application admission은 commit/abort까지
  `EAGAIN`이다.

ready index는 (owner, domain) 집합이며 레벨 트리거다. `drain_ready`가 ready
entry를 batch claim으로 옮기고, `claim_recv_batch`가 mailbox record를 receive
batch로 이동시키면서 budget을 되돌린다. claim release는 mailbox에 record가
남아 있을 때만 `signal_ready`로 재무장한다(락 해제 뒤 공개 신호 경로 사용).

claim 신원은 (node generation, owner, domain, serial)이고, serial은 **프로세스
전역 원자 카운터**에서 발급되어 여러 MeshNode 사이에서도 충돌하지 않는다.
serial→owner 역해석 표는 immortal side table이 가지며, node가 파괴된 뒤의
release도 안전하게 무해한 no-op이 된다.

wakeup 경로는 셋이고 서로 배타 규칙이 있다.

1. blocking `drain_ready` — node 조건변수.
2. ready handler — `notify_consumer_locked`가 등록된 handler를 깨우는
   wakeup-only 콜백. handler 재진입은 `EDEADLK`.
3. poller 연동 — node 내장 `signaler_t`를 `poller_subject_mesh_node`로 등록.
   POLLIN은 "ready index 비어 있지 않음"의 레벨 신호로, drain이 신호 바이트를
   소비한 뒤 남은 work가 있으면 재무장한다. handler와 poller 등록은 상호
   배타(`EBUSY`)다.

## 4. Request transaction, reply token과 completion

request를 전달하기 전에 `operation_submission_t`가 operation,
`reply_routes[serial]`의 one-shot route와 timeout 실행에 필요한 저장소를
한 묶음으로 준비한다. 준비 과정에서 실패하면 이 객체의 소멸자가 등록한
operation과 route를 지우고 timeout을 취소한다. target mailbox나 wire로
request를 전달한 호출부만 `commit()`을 호출하므로, 호출자가 실패를 받은
request가 target에 전달되거나 식별할 수 없는 operation으로 남지 않는다.

reply token은 32-byte sealed token이며 route serial만 밖에 내보낸다. 소비
시점 규칙:

- 성공한 reply가 route를 소비한다. 두 번째 reply는 `EALREADY`.
- requester가 먼저 timeout되면 reply는 token을 소비하고 조용히 폐기된다
  (두 번째 completion을 만들지 않는다).
- node가 STOPPED면 reply는 `ESHUTDOWN`이다. DRAINING 동안은 허용된다 —
  잡고 있는 claim turn이 끝까지 응답할 수 있어야 하기 때문이다.
- Actor join token은 kind가 봉인되어 있어 generic reply에 넣으면 `EINVAL`이며
  소비되지 않는다.

completion은 언제나 requester owner의 infrastructure mailbox로 들어간다.
각 operation은 등록할 때 빈 completion record와 list node를 함께 할당한다.
terminal 경로는 payload를 이 record에 준비한 뒤 `list::splice`로 mailbox에
옮기고, admission이 성공한 시점에만 operation과 reply token을 소비한다.
Actor membership이나 STREAM binding처럼 completion과 함께 바뀌어야 하는
domain state도 같은 node lock 아래에서 commit한다. 따라서 payload 준비,
ready-index admission 또는 wire 송신이 실패하면 operation과 token은
재시도할 수 있는 상태이고 domain state도 바뀌지 않는다.

bound STREAM session close는 raw socket의 session observer를 호출할 수 있어
node lock 안에서 disconnect하지 않는다. 이 경로는 먼저 operation, terminal
record와 ready-index node를 모두 준비하고 competing completion을 막은 뒤
node lock을 놓는다. disconnect 성공 뒤에는 allocation 없이 준비한 terminal
record를 commit한다. 준비 실패는 disconnect 전에 반환되고, disconnect 실패는
예약을 취소하므로 node와 raw STREAM의 잠금 순서가 순환하지 않는다.

timeout completion은 전역 timeout scheduler(immortal singleton, 전용 스레드)가
deadline에 `complete_pending_operation()`을 호출해 만든다. timeout callback은
request transaction이 commit 또는 취소 결정을 내릴 때까지 기다리므로 request
전달보다 먼저 terminal completion을 만들지 않는다. shutdown이 deadline을
넘기면 남은 operation(무기한 포함)을 정확히 한 번의
`REQUEST_TERMINATED`/`ESHUTDOWN` completion으로 마감한다. shutdown 재진입은
`shutdown_active` 플래그로만 `EDEADLK`이고, TIMED_OUT 뒤의 순차 재호출은
합법이다.

commit한 operation은 자신의 timeout task를 `pending_operation_t::timeout_task`로
소유한다. `operation_timeout_guard_t::commit()`이 scheduler gate를 열기 전에
task를 이 필드에 인계하므로, commit된 timer가 operation보다 오래 살아 재활용된
node 주소나 serial을 조기 timeout시킬 수 없다. timeout callback은
`operation_id.high`(node lifecycle generation)와 `low`를 모두 확인해 ABA를
막는다. 모든 terminal 경로(정상·owner 부재·2단계 STREAM commit)와 node
destroy·shutdown은 operation을 지울 때 `detach_pending_operation_locked()`
하나를 거친다. 이 primitive가 task 인계와 erase를 소유하고 반환한 task를
호출자가 node lock **밖에서** 취소하므로(firing 중인 timeout handler가 그 lock을
잡고, scheduler는 self-cancel을 인식한다), 새 terminal 경로가 task 인계를
빠뜨릴 구조가 아니다.

`lifecycle_generation`은 node 생성 시 `allocate_lifecycle_generation()`이
발급한다. clock 소스는 boot-relative `now_ms()`가 아니라 epoch wall-clock
microsecond이고, 프로세스 안에서는 CAS로 강단조다. Core는 durable 상태를
소유하지 않으므로(location authority는 framework 소관) 같은 RID의 동일-µs
재시작이나 clock 역행은 §5의 duplicate/stale generation admission 충돌로
표면화되며 무단 교체가 아니다.

## 5. Wire: ROUTER, ingress 스레드와 envelope

- node의 raw ROUTER는 thread-safe이므로 송신은 앱 스레드에서 직접 수행하고,
  수신은 전용 ingress 스레드 하나가 담당한다.
- ingress 스레드는 socket monitor도 함께 drain한다. `CONNECTION_READY`(rid,
  remote_addr 포함)로 outbound intent를 매칭해 HELLO를 보낸다. peer 단절은
  `handle_peer_down`으로 상태 전이와 `PEER_CLOSED` event를 만든다.
- envelope v1: `'Z' 'M' | ver | type | flags`. request/reply 계열은 correlation
  u64, reply는 terminal result·errno, channel 계열은 channel 이름을 뒤에
  싣는다. application metadata는 별도 frame(flag 0x01)이고 수신 시
  `validate_metadata`가 통과해야만 mailbox admission으로 넘어간다. 실패는
  전체 메시지를 버리고 `PROTOCOL_ERROR` event를 남긴다.
- wire type: HELLO/ADMIT/REJECT/UPDATE(제어), node/channel send·request와
  REPLY, SPOT_SEND=21·SPOT_REQUEST=22(target spot rid+generation),
  MULTICAST=23(channel·topic·source spot rid), ACTOR 계열 24~29
  (SEND/REQUEST/LOOKUP/DESTROY/JOIN/LEFT), TRANSFER 계열 30~33
  (READY/DATA/ACK/REPLY_RELAY).
- admission 검증(`validate_admission_locked`)은 MeshName(`EEXIST`),
  trust profile(`EACCES`), expected RID·stale generation(`ESTALE`)을 본다.
  거부는 REJECT frame과 `PEER_REJECTED` event(양쪽 모두)로 관측된다. 더 높은
  lifecycle generation은 새 entry로 별도 수명을 시작하고 이전 admitted entry를
  `DRAINING`으로 남긴다(새 snapshot 제외, transport 종료나 명시적 disconnect로
  close, `PEER_DRAINING` event 방출). descriptor는 자기 bind endpoint를
  advertised endpoint로 싣고, inbound로 관측된(로컬 intent 없는) peer는 source
  `DISCOVERY`에 그 endpoint를 기록한다 — 같은 endpoint의 수동 intent는 하나의
  entry로 병합되어 `MIXED`가 되고, 한 source 제거는 남은 source로 연결을
  유지한다.
- 원격 request는 requester가 correlation=op.low로 operation을 등록하고,
  responder가 remote-origin reply route(원 rid + 원 correlation 봉인)를 만들어
  `zlink_mesh_reply`가 wire REPLY로 회신한다. requester ingress가 pending
  operation을 completion으로 마감한다.

## 6. Logical Multicast와 NODROP

publish는 한 호출 안에서 snapshot→검사→commit을 마친다.

1. node mutex 아래에서 local 구독 match와 admitted 양수-weight channel member
   peer snapshot을 뜬다.
2. NODROP(기본 1)이면 node별 `wire_send_mutex`로 발신을 직렬화한 채 local
   mailbox budget과 모든 remote pipe의 `routed_target_writable()`을 선검사한다.
   하나라도 불가면 아무것도 commit하지 않는다(전부-또는-전무). 비차단은
   `EAGAIN`, 차단은 SNDTIMEO까지 reserve를 재시도한 뒤 `ETIMEDOUT`이다.
3. reserve가 통과하면 remote commit 전에 local 슬롯(mailbox list node와
   ready-index 키)을 선예약해 마지막 fallible 지점을 commit 앞으로 당긴다.
   실패 시 전량 롤백 후 `ENOMEM`이고, 예약물은 node mutex 보유 중에만
   존재한다(모든 실패 경로가 unlock 전에 롤백).
4. commit은 local fanout(`zlink_msg_copy` refcount 공유)과 peer당 정확히 1회의
   wire submit이다. reserve와 commit 사이에 pipe를 잃은 admitted peer는
   drop이 아닌 peer 이탈로 분류해 unreachable로 센다(spec §7). 수신 node는
   자기 local 구독 match에만 fan-out하고 재전파 경로가 없다(구조적 no-relay).

publish detail과 `MULTICAST_COMMITTED`/`MULTICAST_DROPPED` event가 snapshot·
admitted·dropped·unreachable 수치를 그대로 보고한다
(snapshot_remote = admitted + dropped + unreachable).

## 7. Actor와 transfer fence

Actor 상태는 owner mailbox와 같은 표에 산다. join의 membership commit은
소스 쪽 wire reply 처리(`actor_apply_remote_join_reply`) 한 곳이며 epoch+1,
`spot_node_rid` 기록, 이전 remote spot으로의 ACTOR_LEFT 통지가 여기서 함께
일어난다.

transfer는 `transfer_state_t`(serial, role, phase, frozen snapshot, staged map,
ack high-water)로 추적한다. 핵심 결정:

- 토큰 64B = node ptr + serial + magic (reply token과 같은 봉인 패턴).
- fence 집행 지점은 세 곳: `admit_record`(application `EAGAIN`),
  `take_claim`(`EBUSY`), `drain_ready`(fenced app lane 스킵).
- data plane은 ACK당 1 record의 stop-and-wait이다. source가 frozen mailbox
  snapshot을 순서대로 재전송하고 target이 staged map에 쌓았다가 activate에서
  app mailbox로 옮긴다.
- 전송 중 도착한 ACTOR_REQUEST의 reply는 target에서 재봉인된 route가
  REPLY_RELAY로 source를 경유해 원 requester에 닿는다. source commit 이후에는
  route가 제거된다.
- source commit은 actors entry를 지우되 owners entry는 남겨 잔여 infra record를
  drain할 수 있게 한다.

## 8. Monitor

monitor는 node당 하나(bounded queue). emitter는 node mutex 아래에서 monitor
포인터를 핀(`monitor_emit_refs`)하고, close는 이 참조가 0이 될 때까지 기다린
뒤에만 객체를 삭제한다. `emit_monitor_event`는 mask를 적용해
queue에 넣고, 넘치면 MESSAGE_SUBMITTED·BACKPRESSURED 같은 고빈도 event를
aggregate하며 peer state·protocol error·lifecycle event를 우선 보존한다.
counter는 event 소비와 무관하게 누적되고 status 조회는 원자 snapshot이다.
handler와 recv는 같은 queue의 single consumer이며 handler 등록 중 재진입
해제는 `EDEADLK`다. `CLAIM_REVOKED`는 shutdown deadline 초과 시 revoke 루프가
락을 놓은 뒤 방출한다.

raw socket monitor(`api/monitoring/`)는 MeshNode monitor와 별도 계층이며 전역
handler registry 하나가 소유한다. status·recv-model·handler 등록·close 검사
경로 등 registry state를 역참조하는 모든 reader는 `monitor_state_pin_t`로 pin을
잡고, close/unregister는 pin이 0으로 drain될 때까지 기다린 뒤에만 storage를
삭제한다(UAF 방지). handler 등록(update)은 pin한 state를 expected로 넘겨
registry lock 아래에서 entry 동일성과 `unregistered`를 확인하고, 불일치 시
`ESHUTDOWN`으로 실패하며 닫히는 socket에 새 registration을 되살리지 않는다.
등록은 registration identity(handler·provider·subject·dispatch task id)를
`dispatch_sync` 아래에서 완결하므로 즉시 dispatch tick의 첫 callback(및 그
callback의 self-close finalizer)이 완성된 task id만 관찰한다. dispatch task를
돌리는 service-control scheduler는 task의 schedule node를 add 시 1회만 할당하고
이후 wakeup·주기 tick을 node handle 재사용으로 무할당·no-throw로 유지하며, task
callback과 worker는 bad_alloc을 봉인해 실패한 tick만 버리고 active-task
epilogue를 항상 실행한다.

## 9. 잠금 규칙과 스레드 목록

| 스레드 | 소유자 | 역할 |
|---|---|---|
| 앱 스레드 | 호출자 | 모든 공개 API, wire 송신(직접 send) |
| ingress 스레드 | node당 1 | ROUTER recv, socket monitor drain, 원격 record admission, completion 마감 |
| timeout scheduler | 프로세스 1 (immortal) | operation deadline 도래 시 timeout completion |
| Spot timer scheduler | MeshNode당 1 (지연 생성, destroy 시 회수) | Spot timer fire·turn 대기. 전역 timer scheduler와 분리되어 claim 대기가 다른 node·일반 timer를 막지 않음 |

- 기본 잠금은 `node->mutex` 하나다. monitor는 자체 mutex를 가지며
  `emit_monitor_event`는 node mutex를 잡지 않은 상태에서 불러야 한다
  (내부에서 node mutex→monitor mutex 순서로 짧게 잡는다).
- `wire_send_mutex`는 NODROP 원자 reserve 동안 node의 wire 발신 전체를
  직렬화한다. node mutex와 함께 잡을 때는 언제나 node mutex가 바깥이다.
- `admit_record`는 스스로 node mutex를 잡는다 — 호출자는 node mutex를 잡은 채
  부르면 안 된다(transfer 코드의 emit 전 unlock이 그 예).

## 10. STREAM session 경계

STREAM session service는 raw STREAM socket과 1:1:1(service:socket:node)로
붙고, Actor binding CAS(generation)와 session→Actor 전달만 소유한다. 세부는
[`stream-socket.ko.md`](stream-socket.ko.md)와 정식 spec
[`05-stream-session.ko.md`](../spec/core/service/05-stream-session.ko.md)를
본다. Actor 이동 barrier의 bounded post-barrier allowance는 transfer 상태의
participant 항목이 집행한다.
