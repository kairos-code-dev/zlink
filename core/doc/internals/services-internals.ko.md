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
| `src/runtime/services/mesh/mesh_wire.{hpp,cpp}` | 원격 wire. node 소유 raw ROUTER, ingress 스레드, envelope codec, HELLO admission, 원격 메시징·transfer data plane |
| `src/api/mesh/mesh_node_api.cpp` | lifecycle, membership, peer intent, option, status·query C API |
| `src/api/mesh/mesh_messaging_api.cpp` | node/channel/Spot direct send·request, Logical Multicast publish |
| `src/api/mesh/mesh_dispatch_api.cpp` | ready handler, drain, ready/receive batch, claim, reply token |
| `src/api/mesh/mesh_actor_api.cpp` | Actor 생성·조회·파괴·join·메시징 |
| `src/api/mesh/mesh_transfer_api.cpp` | Actor transfer prepare/commit/activate/abort와 fence |
| `src/api/mesh/mesh_monitor_api.cpp` | MeshNode monitor open/handler/recv/status/close |
| `src/api/mesh/mesh_stream_session_api.cpp` | STREAM session service와 Actor binding |
| `src/api/mesh/mesh_api.cpp` | poller·timer 등 세로 관심사가 mesh로 들어오는 seam |

계층 규칙: `api/mesh/*`는 공개 signature 검증과 결과 매핑을 소유하고, 상태
변경은 전부 `mesh_runtime`/`mesh_wire`의 함수로 내려보낸다. raw socket 계층
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

claim 신원은 (node generation, owner, domain, serial)이고, serial→owner 역해석
표는 전역 side table(`g_claim_keys`)이 가진다. 이 표 덕분에 node가 파괴된 뒤의
release도 안전하게 무해한 no-op이 된다.

wakeup 경로는 셋이고 서로 배타 규칙이 있다.

1. blocking `drain_ready` — node 조건변수.
2. ready handler — `notify_consumer_locked`가 등록된 handler를 깨우는
   wakeup-only 콜백. handler 재진입은 `EDEADLK`.
3. poller 연동 — node 내장 `signaler_t`를 `poller_subject_mesh_node`로 등록.
   POLLIN은 "ready index 비어 있지 않음"의 레벨 신호로, drain이 신호 바이트를
   소비한 뒤 남은 work가 있으면 재무장한다. handler와 poller 등록은 상호
   배타(`EBUSY`)다.

## 4. Reply token과 completion

request 등록은 `reply_routes[serial]`에 one-shot route를 만들고 32-byte sealed
token으로 serial만 밖에 내보낸다. 소비 시점 규칙:

- 성공한 reply가 route를 소비한다. 두 번째 reply는 `EALREADY`.
- requester가 먼저 timeout되면 reply는 token을 소비하고 조용히 폐기된다
  (두 번째 completion을 만들지 않는다).
- node가 STOPPED면 reply는 `ESHUTDOWN`이다. DRAINING 동안은 허용된다 —
  잡고 있는 claim turn이 끝까지 응답할 수 있어야 하기 때문이다.
- Actor join token은 kind가 봉인되어 있어 generic reply에 넣으면 `EINVAL`이며
  소비되지 않는다.

completion은 언제나 requester owner의 infrastructure mailbox로 들어간다.
timeout completion은 전역 timeout scheduler(immortal singleton, 전용 스레드)가
deadline에 `complete_operation`을 호출해 만든다.

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
  lifecycle generation은 기존 admitted entry를 즉시 교체하며, 이 교체가
  `PEER_DRAINING` event의 방출 지점이다. inbound로 관측된(로컬 intent 없는)
  peer는 source가 `DISCOVERY`이고, 같은 endpoint에 수동 intent가 겹치면
  `MIXED`로 병합된다.
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
3. commit은 local fanout(`zlink_msg_copy` refcount 공유)과 peer당 정확히 1회의
   wire submit이다. 수신 node는 자기 local 구독 match에만 fan-out하고 재전파
   경로가 없다(구조적 no-relay).

publish detail과 `MULTICAST_COMMITTED`/`MULTICAST_DROPPED` event가 snapshot·
admitted·dropped 수치를 그대로 보고한다.

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

monitor는 node당 하나(bounded queue). `emit_monitor_event`는 mask를 적용해
queue에 넣고, 넘치면 MESSAGE_SUBMITTED·BACKPRESSURED 같은 고빈도 event를
aggregate하며 peer state·protocol error·lifecycle event를 우선 보존한다.
counter는 event 소비와 무관하게 누적되고 status 조회는 원자 snapshot이다.
handler와 recv는 같은 queue의 single consumer이며 handler 등록 중 재진입
해제는 `EDEADLK`다. `CLAIM_REVOKED`는 shutdown deadline 초과 시 revoke 루프가
락을 놓은 뒤 방출한다.

## 9. 잠금 규칙과 스레드 목록

| 스레드 | 소유자 | 역할 |
|---|---|---|
| 앱 스레드 | 호출자 | 모든 공개 API, wire 송신(직접 send) |
| ingress 스레드 | node당 1 | ROUTER recv, socket monitor drain, 원격 record admission, completion 마감 |
| timeout scheduler | 프로세스 1 (immortal) | operation deadline 도래 시 timeout completion |

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
