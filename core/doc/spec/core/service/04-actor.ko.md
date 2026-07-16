[English](04-actor.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md) · [MeshNode](01-mesh-node.ko.md) · [Spot](03-spot.ko.md) · [STREAM session](05-stream-session.ko.md) · [Dispatch](02-dispatch.ko.md)

# Actor service

이 문서는 ZLink Core 10.0.0의 정식 공개 계약을 정의한다. 대상 독자는 Actor address, mailbox,
Spot membership과 transfer fence를 사용하는 C API와 bindings 개발자다. 이 문서는 “Actor payload와
lifecycle을 Spot dispatch에서 분리하면서 위치 이동 중 ordering을 어떻게 보존하는가?”에 답한다.

## 1. 공개 타입

```c
#define ZLINK_ACTOR_ABI_VERSION 1u
#define ZLINK_ACTOR_ID_MAX 255u

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX + 1];
  uint64_t generation;
} zlink_actor_ref_t;

typedef enum zlink_actor_lifecycle_kind_t {
  ZLINK_ACTOR_LIFECYCLE_CREATED      = 1,
  ZLINK_ACTOR_LIFECYCLE_JOINED       = 2,
  ZLINK_ACTOR_LIFECYCLE_LEFT         = 3,
  ZLINK_ACTOR_LIFECYCLE_DISCONNECTED = 4,
  ZLINK_ACTOR_LIFECYCLE_DESTROYED    = 5
} zlink_actor_lifecycle_kind_t;

typedef enum zlink_actor_join_result_t {
  ZLINK_ACTOR_JOIN_ACCEPTED = 0,
  ZLINK_ACTOR_JOIN_REJECTED = 1
} zlink_actor_join_result_t;

typedef enum zlink_actor_transfer_role_t {
  ZLINK_ACTOR_TRANSFER_SOURCE = 1,
  ZLINK_ACTOR_TRANSFER_TARGET = 2
} zlink_actor_transfer_role_t;

typedef enum zlink_actor_transfer_phase_t {
  ZLINK_ACTOR_TRANSFER_PREPARING = 1,
  ZLINK_ACTOR_TRANSFER_FENCED    = 2,
  ZLINK_ACTOR_TRANSFER_COMMITTED = 3,
  ZLINK_ACTOR_TRANSFER_ACTIVATED = 4,
  ZLINK_ACTOR_TRANSFER_ABORTED   = 5
} zlink_actor_transfer_phase_t;

typedef struct zlink_actor_location_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_ref_t actor;
  zlink_routing_id_t spot_rid;
  uint64_t spot_generation;
  uint64_t membership_epoch;
} zlink_actor_location_t;

typedef struct zlink_actor_control_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_lifecycle_kind_t kind;
  zlink_actor_ref_t previous_actor;
  zlink_actor_ref_t current_actor;
  zlink_routing_id_t previous_spot_rid;
  zlink_routing_id_t current_spot_rid;
  uint64_t previous_spot_generation;
  uint64_t current_spot_generation;
  uint64_t previous_membership_epoch;
  uint64_t current_membership_epoch;
  int32_t result_code;
} zlink_actor_control_record_t;

typedef struct zlink_actor_join_completion_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_join_result_t join_result;
  zlink_actor_ref_t actor;
  zlink_actor_location_t location;
} zlink_actor_join_completion_t;

typedef struct zlink_actor_transfer_id_t {
  uint64_t high;
  uint64_t low;
} zlink_actor_transfer_id_t;

typedef struct zlink_actor_transfer_token_t {
  uint64_t opaque[8];
} zlink_actor_transfer_token_t;

typedef struct zlink_actor_transfer_prepare_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t expected_membership_epoch;
  zlink_routing_id_t peer_node_rid;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_t;

typedef struct zlink_actor_transfer_prepare_result_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t final_sequence;
  uint64_t reserve_message_count;
  uint64_t reserve_byte_count;
} zlink_actor_transfer_prepare_result_t;

typedef struct zlink_actor_transfer_control_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_actor_transfer_phase_t phase;
  zlink_actor_transfer_role_t role;
  zlink_actor_transfer_id_t transfer_id;
  zlink_actor_ref_t actor;
  uint64_t membership_epoch;
  uint64_t final_sequence;
  int32_t result_code;
  int32_t failure_errno;
} zlink_actor_transfer_control_t;
```

Actor ID는 NUL을 포함하지 않는 1..255-byte UTF-8이며 byte 단위로 비교한다. ActorRef generation은 같은
Actor ID의 생성 수명을 구분한다. Actor location은 Spot RID와 Spot lifecycle generation을 함께 보존한다.
membership epoch는 같은 Actor generation이 참여하는 Spot 위치의 변경을 구분하며 ActorRef generation과
Spot generation 양쪽과 독립적이다. Spot이 같은 RID로 다시 만들어져도 기존 Actor location이 새 Spot을
가리키지 않는다.

## 2. 생성, 조회와 종료

```c
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_new(
  void *mesh_node,
  const char *actor_id,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_actor_ref_t *actor_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_lookup(
  void *mesh_node,
  const char *actor_id,
  zlink_actor_location_t *location_out);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_lookup_remote(
  void *mesh_node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_destroy(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
```

`new`는 local Actor generation과 entry Spot의 `CREATED` control record admission을 하나의 transaction으로
commit한다. creation payload가 없으면 pointer는 `NULL`, count는 0이다. 같은 ID의 active generation이
있으면 `ZLINK_REQUEST_CONFLICT`/`EEXIST`다.

entry Spot control mailbox에는 MeshNode의 message·byte budget이 적용된다. `DONTWAIT`에서 complete control
record를 수용할 수 없으면 `ZLINK_REQUEST_BACKPRESSURED`/`EAGAIN`이다. blocking 호출은 `timeout_ms`까지
capacity를 기다리고 deadline이 지나면 `ZLINK_REQUEST_TIMED_OUT`/`ETIMEDOUT`이다. timeout 0은 기다리지
않는다.

Core는 next generation, Actor state와 mailbox capacity를 먼저 예약하고 complete creation record enqueue와
함께 generation을 공개한다. 성공 반환 뒤에만 lookup이 새 Actor를 관측하고 `actor_out`을 채운다. conflict,
backpressure, timeout, shutdown 또는 allocation 실패는 예약, Actor state와 control record를 전부 rollback하고
generation을 소비하지 않으며 `actor_out`을 쓰지 않는다. 따라서 retry는 실패한 호출과 같은 next generation을
사용할 수 있다. creation parts는 borrowed read-only이며 모든 결과에서 caller가 소유한다.

local lookup은 caller-owned snapshot을 반환한다. remote lookup과 destroy는 requester Node infrastructure
claim에 terminal completion을 반환한다. lookup completion의 `kind_data`는 `zlink_actor_location_t`다.
ActorRef가 stale이면 `ESTALE`다.

destroy는 새 Actor admission을 막고 active application claim, request completion과 bound session control을
deadline까지 drain한다. 성공 뒤 generation은 재사용하지 않는다.

## 3. Spot membership

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_join_entry_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *target_node_rid,
  const zlink_msg_t *creation_parts,
  size_t creation_part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_leave_spot(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_membership_epoch,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_join_reply(
  const zlink_mesh_reply_token_t *token,
  zlink_actor_join_result_t join_result,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
```

join은 target node RID, Spot RID와 nonzero lifecycle generation을 하나의 대상 주소로 snapshot하고 target
Spot infrastructure/application control claim에 `SPOT_CONTROL` record를 넣는다. 수신 node는 record를
admit하기 전에 세 값이 같은 active Spot을 가리키는지 확인한다. generation이 0이면
`ZLINK_SUBMIT_INVALID_ARGUMENT`/`EINVAL`, target Spot과 다르면 `ZLINK_SUBMIT_INVALID_STATE`/`ESTALE`이고
다른 generation을 자동으로 선택하지 않는다. `kind_data`는
`zlink_actor_control_record_t`, optional creation payload는 record parts, reply route는 one-shot token이다.
전용 join recv queue나 request pointer를 공개하지 않는다.

`ZLINK_ACTOR_JOIN_ACCEPTED`만 membership을 commit한다. `ZLINK_ACTOR_JOIN_REJECTED`는 source의 현재
membership을 유지하고 reply parts를 rejection detail로 전달한다. 다른 값은
`ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`이다. 성공한 submit은 accept와 reject 모두 token을
소비한다. `DONTWAIT`에서 queue를 수락할 수 없으면 `ZLINK_SUBMIT_BACKPRESSURED`/`EAGAIN`, blocking 호출은
MeshNode `SNDTIMEO`까지 기다린 뒤 `ZLINK_SUBMIT_BACKPRESSURED`/`ETIMEDOUT`이다. 두 실패에서는 token이
유효하므로 claim을 release하기 전에 다시 호출할 수 있다.

accepted join reply가 membership epoch를 증가시키는 유일한 commit point다. source operation completion의
`kind_data`는 `zlink_actor_join_completion_t`이며 enum `join_result`, ActorRef와 location snapshot을 가진다.
location과 joined·left·disconnected control record는 이전·현재 Spot RID와 각각의 lifecycle generation을
함께 보존한다.
accepted completion은 `terminal_result == ZLINK_REQUEST_OK`, `failure_errno == 0`이고 새 location을 제공한다.
rejected completion은 `join_result == ZLINK_ACTOR_JOIN_REJECTED`,
`terminal_result == ZLINK_REQUEST_REJECTED`, `failure_errno == EACCES`이며 source의 현재 location을 제공한다.
transport·timeout 실패는 각각의 terminal result와 errno를 사용한다. leave도 expected epoch CAS가 성공해야
하며 stale epoch는 `ESTALE`이다.

joined, left와 disconnected lifecycle은 같은 Spot control record family를 사용한다. lifecycle record를
위한 별도 receive 함수는 제공하지 않는다.

## 4. Actor 메시징

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_send_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_actor_request_to_actor(
  void *mesh_node,
  const zlink_actor_ref_t *source_actor,
  const zlink_actor_ref_t *target_actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

ActorRef의 node RID가 target pipe를 선택한다. destination은 generation과 현재 route epoch를 검증하고 Actor
mailbox에 직접 enqueue한다. Actor payload는 Spot routed 또는 Spot control mailbox를 거치지 않는다.

`zlink_mesh_node_*_to_actor`는 Node에서 시작한 호출이며 request completion은 Node infrastructure claim에
전달한다. `zlink_actor_*_to_actor`는 source ActorRef를 검증하고 request completion을 source Actor의
infrastructure claim에 전달한다. source Actor와 target Actor가 같아도 같은 계약을 적용한다.

Actor metadata는 Actor 전용 application metadata이며 canonical frame 검증을 사용한다. Actor request
reply는 generic one-shot `zlink_mesh_reply()`를 사용한다. Actor application claim이 active여도 독립적인
infrastructure claim에서 completion과 send-ready를 진행한다.

같은 sender에서 같은 ActorRef로 성공한 message는 destination Actor mailbox에서 FIFO다. 서로 다른
sender 사이의 global order는 제공하지 않는다.

## 5. Actor claim

Actor work는 `owner_kind == ZLINK_MESH_OWNER_ACTOR` ready record의 claim으로 받고
`zlink_mesh_claim_recv_batch()`를 사용한다. `ACTOR_SEND`, `ACTOR_REQUEST`, `COMPLETION`, `SEND_READY`와
`TRANSFER_CONTROL`만 Actor claim에서 반환한다. `ACTOR_SEND`와 `ACTOR_REQUEST`는 application domain,
`COMPLETION`, `SEND_READY`와 `TRANSFER_CONTROL`은 infrastructure domain에만 들어간다.

같은 Actor application claim은 하나만 active하다. 다음 application turn은 이전 claim release 뒤 시작한다.
Actor가 application claim을 보유한 채 request 또는 send readiness를 기다려도 infrastructure claim은
독립적으로 결과를 drain한다.

## 6. Transfer fence

framework location store가 transfer authority record, participant-set CAS, lease와 durable
prepared·committed·activated·aborted 상태를 소유한다. Core는 store를 조회하지 않는다. In-process framework가
authority 결정을 완료한 뒤 commit을 호출하며, Core는 prepare에서 발급한 opaque token과 membership epoch로
자기 mailbox 및 session ingress 상태를 검증한다.

```c
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_actor_transfer_prepare(
  void *mesh_node,
  const zlink_actor_transfer_prepare_t *prepare,
  uint32_t timeout_ms,
  zlink_actor_transfer_token_t *token_out,
  zlink_actor_transfer_prepare_result_t *result_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_commit(
  const zlink_actor_transfer_token_t *token,
  uint64_t new_membership_epoch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_activate(
  const zlink_actor_transfer_token_t *token);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_actor_transfer_abort(
  const zlink_actor_transfer_token_t *token);
```

`prepare`와 `result_out`의 role별 계약은 다음과 같다. caller는 공통 versioned 구조체 규칙에 따라
`result_out`을 초기화한다.

| Role | `prepare` input | `result_out` |
|---|---|---|
| source | identity, expected epoch와 target node RID를 설정하고 sequence·reserve field는 0 | Core가 계산한 `final_sequence`, frozen backlog의 message·byte reserve count |
| target | authority record에서 읽은 source result 세 값을 그대로 설정하고 source node RID를 지정 | Core가 검증하고 예약한 같은 세 값 |

source prepare는 새 application claim을 막고 active claim, responder token과 Actor-originated operation이
끝날 때까지 infrastructure progress를 유지한다. peer sender와 bound STREAM session은 old-epoch FIFO 뒤
fence marker를 기록하고 새 traffic을 bounded pending queue에 둔다. Core는 모든 participant marker와 local
mailbox high-water를 하나의 `final_sequence`로 봉인하고 frozen backlog의 message·byte 수를 계산한 뒤에만
source prepare를 성공시킨다.

framework는 성공한 source `result_out`의 transfer ID, ActorRef, expected epoch, `final_sequence`와 두 reserve
count를 durable authority prepared record에 함께 기록한다. target prepare를 호출할 때 이 세 계산 값을
변경하지 않고 `prepare`에 복사한다. target Core는 authority를 직접 조회하지 않지만 token identity와 함께
값을 봉인하고 frozen backlog capacity를 예약한다. 이어지는 private readiness negotiation에서 source Core가
participant 목록과 각 bounded post-barrier queue의 최대 transfer allowance를 전달하며 target은 이 allowance를
추가로 원자 예약한다. private allowance는 새 public field가 아니며 source·target token에 봉인된다.
부족하면 `ZLINK_REQUEST_BACKPRESSURED`, `errno == ENOBUFS`이며 token과 partial result를 만들지 않는다.

Core는 frozen backlog나 session pending traffic을 caller가 읽고 다시 보내는 공개 API를 제공하지 않는다.
source·target prepare token과 두 MeshNode 사이 peer route만으로 transfer data plane을 자동으로 진행한다.
framework는 durable authority 결정과 기존 prepare·commit·activate·abort 호출만 담당하며 message batch,
participant 목록, sequence ACK나 재전송 상태를 전달하지 않는다.

target prepare는 local reservation을 설치한 뒤 source의 같은 transfer ID·Actor generation·expected epoch와
prepared readiness를 교환해야 성공한다. peer route가 없으면 `ZLINK_REQUEST_NOT_CONNECTED`와 `ENOTCONN`,
이 교환이 prepare deadline 안에 끝나지 않으면 `ZLINK_REQUEST_TIMED_OUT`과 `ETIMEDOUT`이며 target token을
만들지 않는다. readiness가 확인되면 source Core는 frozen Actor mailbox snapshot을 원래 mailbox 순서와
`final_sequence`까지 자동으로 보내고, 각 bound STREAM session의 barrier 뒤 pending traffic도 해당 session
FIFO 순서로 보낸다. source는 target activation을 확인하고 source commit이 성공할 때까지 snapshot과 pending
traffic의 authoritative reference를 유지한다.
성공한 target prepare의 `timeout_ms` deadline은 token에 봉인되어 target commit의 data-plane 완료까지
적용된다.

Core는 frozen mailbox와 각 peer sender·session binding을 별도 participant로 관리하고 participant마다 단조
증가하는 sequence와 contiguous high-water를 유지한다. target commit을 시작하면 source는 각 participant의
마지막 pending sequence를 terminal high-water로 봉인한다. 그 뒤 수용한 traffic은 새 epoch forwarding
queue로 분리되며 transferred range보다 먼저 application에 보이지 않는다. target은
`transfer_id`·Actor generation·participant·sequence가 같은 재전송을 한 번만 staging하고, 같은 key의 다른
payload는 protocol failure로 처리한다.

source는 participant별 reserved allowance를 확보한 traffic만 post-barrier queue 또는 new-epoch forwarding
queue에 수용한다. allowance가 소진되면 해당 Actor submit은 `ZLINK_SUBMIT_BACKPRESSURED`와 `EAGAIN`을
반환하고 STREAM service는 추가 raw ingress admission을 중단한다. 따라서 commit에서 봉인하는 terminal
high-water는 target prepare가 예약한 범위를 넘지 않으며 target commit은 capacity 부족을 새로 반환하지 않는다.

target은 frozen mailbox의 `final_sequence`와 participant별 terminal high-water까지 연속으로 staging한 뒤
각 high-water를 source에 ACK한다. source는 ACK되지 않은 range만 재전송하며 동일 ACK와 동일 payload 재전송은
idempotent하다. target commit은 source가 aggregate `final_sequence`와 모든 participant high-water ACK를
확인하고 target이 binding generation과 new-epoch forwarding barrier까지 staging한 뒤에만
`ZLINK_CONFIG_OK`를 반환한다. target application dispatch는 여전히 activate 전까지 시작하지 않는다.

frozen mailbox record는 post-barrier traffic보다 먼저 보이고, 같은 participant의 pending traffic은 FIFO를
유지한다. 서로 다른 session participant 사이에는 별도 전역 순서를 보장하지 않는다. activate 뒤에도
terminal high-water 뒤의 new-epoch traffic은 transferred range보다 먼저 dispatch되지 않는다. 따라서
target에서는 각 accepted message가 한 번만 보이며 source에서는 같은 message를 다시 dispatch하지 않는다.

prepared 뒤 peer가 끊기거나 deadline이 만료되면 target commit은 각각
`ZLINK_CONFIG_INVALID_STATE`와 `ENOTCONN`, `ETIMEDOUT`을 반환한다. sequence 또는 payload가 충돌하면 같은
result와 `EPROTO`를 반환한다. 부분 staging과 두 token은 terminal 상태가 아니며,
retriable failure 뒤 같은 target commit을 호출하면 ACK된 range를 건너뛰고 이어서 전송한다. protocol
failure에서는 framework authority가 abort를 결정해야 한다. 각 data-plane failure는 현재 local phase의
`TRANSFER_CONTROL` record를 추가한다. source phase는 `FENCED`, target phase는 `PREPARING`으로 유지하며
peer loss는 `ZLINK_REQUEST_NOT_CONNECTED`/`ENOTCONN`, deadline은
`ZLINK_REQUEST_TIMED_OUT`/`ETIMEDOUT`, sequence 또는
payload 충돌은 `ZLINK_REQUEST_PROTOCOL_ERROR`/`EPROTO`를 `result_code`/`failure_errno`에 기록한다.
framework는 기존 infrastructure claim으로 원인을 관찰할 수 있다.

prepare는 operation ID를 만들지 않는 synchronous lifecycle request다. source에서는 fence와 result 계산,
target에서는 capacity reservation이 commit된 뒤에만 `ZLINK_REQUEST_OK`를 반환하며 token과 `result_out`을
같은 반환 전에 모두 채운다. 실패하면 `token_out`은 zero value이고 caller가 초기화한 `result_out` payload를
부분적으로 쓰지 않는다.

각 local transfer state transition은 해당 Actor generation이 owner인 infrastructure claim에
`TRANSFER_CONTROL` record를 넣는다. source prepare 성공은 `FENCED`, target prepare 성공은 `PREPARING`,
target commit은 non-terminal `COMMITTED`, target activate는 terminal `ACTIVATED`, source commit은 terminal
`COMMITTED`, abort는 terminal `ABORTED` record를 해당 API가 성공을 반환하기 전에 enqueue한다.
이 record의 `operation_id`는 zero이며 별도 `COMPLETION` record를 만들지 않는다. application claim에는
transfer control을 전달하지 않는다. 성공한 transition record는 `result_code == ZLINK_REQUEST_OK`,
`failure_errno == 0`이다.

token은 Core가 생성한 64-byte sealed value다. transfer ID, role, Actor generation, expected membership
epoch, MeshNode lifecycle generation과 reservation을 포함하며 caller가 만들거나 수정할 수 없다.

target token의 commit은 framework authority가 확정한 새 membership epoch를 설치하고 위 automatic data
plane의 모든 ACK가 끝난 뒤 target forwarding과 reserved staging을 확정하지만 Actor application dispatch는
시작하지 않는다. committed target token의 activate는 participant flush, session binding과 terminal
high-water가 모두 확인된 뒤 Actor ready를 공개하고
target token을 terminal `ACTIVATED` 상태로 만든다. source token의 commit은 target activation ACK를 framework가
확인한 뒤에만 호출한다. 이 호출은 old route와 admission을 제거하고 authoritative snapshot을 해제하며 source
token을 terminal `COMMITTED` 상태로 만든다. 따라서 target commit과 source commit은 같은 authority 결정을
사용하지만 호출 시점과 상태 전이가 다르다. source token의 terminal 상태는 `COMMITTED` 또는 `ABORTED`이고,
target token의 terminal 상태는 `ACTIVATED` 또는 `ABORTED`다. target의 `COMMITTED` 상태는 activate 전의
non-terminal 상태다.

같은 token과 같은 `new_membership_epoch`로 완료된 commit을 다시 호출하거나 activated target token에 activate를
다시 호출하는 것은 성공하는 idempotent retry다. commit 전 source token의 abort는 기존 source route와 admission을
복원하고 fence를 해제한다. commit 전 target token의 abort는 예약한 staging capacity와 prepared target
admission 상태를 제거한다. 두 경우 모두 token은 terminal `ABORTED` 상태가 되며 같은 token의 abort 재호출도
성공한다. authority abort의 rollback은 target abort가 모든 staged copy와 reservation을 폐기하고 source
abort가 authoritative frozen backlog를 그대로 유지한 채 session pending FIFO와 new-epoch forwarding
traffic을 source route 뒤에 복원한 뒤 완료된다. target이 activate되지 않았으므로 rollback 중 application에
중복 message가 보이지 않는다.
terminal token에 서로 다른 종결 operation을 호출하거나 같은 token의 commit에 다른 epoch를 전달하면
`ZLINK_CONFIG_INVALID_STATE`, `errno == EALREADY`이며 terminal 상태는 바뀌지 않는다.

`new_membership_epoch`는 token의 expected membership epoch보다 정확히 1 커야 한다. token의 role, transfer
ID, Actor generation, expected epoch와 MeshNode lifecycle generation이 active transfer state와 다르면
`ZLINK_CONFIG_INVALID_STATE`, `errno == ESTALE`다. 첫 `activate`는 committed target token에만 허용하며 source
token이나 prepare 상태 token은 `ZLINK_CONFIG_INVALID_STATE`, `errno == EINVAL`이다.

prepare는 Actor callback 또는 해당 Actor claim 안에서 호출하면 `EDEADLK`다. timeout 전 실패는 기존
membership을 유지한다. stale node generation, transfer ID, epoch 또는 Core token은 `ESTALE`다.

## 7. Shutdown과 thread safety

Actor create, lookup, messaging과 control submit은 thread-safe다. 같은 Actor의 lifecycle mutation과 transfer는
직렬화한다. MeshNode `DRAINING` 뒤 새 create, join, leave와 transfer prepare는 `ESHUTDOWN`이다.

shutdown은 prepared transfer를 authority 결정에 따라 commit/activate 또는 abort한 뒤 Actor route와
session binding을 제거한다. 이미 commit한 transfer를 source로 rollback하지 않는다. outstanding claim과
retained message storage는 마지막 release까지 유지한다.

Actor와 raw STREAM session의 binding, 양방향 complete multipart 전송과 transfer barrier는
[STREAM session service](05-stream-session.ko.md)가 소유한다. Actor service는 binding transport나 raw
STREAM receive mode를 직접 공개하지 않는다.
