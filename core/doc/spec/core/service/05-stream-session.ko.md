[English](05-stream-session.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md) · [MeshNode](01-mesh-node.ko.md) · [Actor](04-actor.ko.md) · [Dispatch](02-dispatch.ko.md) · [raw STREAM](../socket/08-stream.ko.md)

# STREAM session service

이 문서는 ZLink Core 10.0.0의 정식 공개 계약을 정의한다. 대상 독자는 raw STREAM session과
MeshNode Actor를 연결하는 C API와 bindings 개발자다. 이 문서는 “범용 STREAM 소켓 계약을 바꾸지 않고
session별 Actor binding, 양방향 메시징과 Actor 이동 barrier를 어떻게 제공하는가?”에 답한다.

## 1. 책임과 handle

raw STREAM은 transport 연결, session routing ID와 byte/packet 수신만 담당한다. STREAM session service는
raw STREAM 하나와 MeshNode 하나의 관계를 소유하고, session–Actor binding과 Actor 이동에 필요한 FIFO
barrier를 관리한다. framework의 실제 Actor 객체, packet codec과 application handler는 Core가 소유하지
않는다.

```c
#define ZLINK_STREAM_SESSION_ABI_VERSION 1u

typedef enum zlink_stream_session_state_t {
  ZLINK_STREAM_SESSION_CREATED  = 1,
  ZLINK_STREAM_SESSION_STARTED  = 2,
  ZLINK_STREAM_SESSION_DRAINING = 3,
  ZLINK_STREAM_SESSION_STOPPED  = 4,
  ZLINK_STREAM_SESSION_ERROR    = 5
} zlink_stream_session_state_t;

typedef struct zlink_stream_session_binding_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t session_rid;
  zlink_actor_ref_t actor;
  uint64_t binding_generation;
  uint64_t membership_epoch;
} zlink_stream_session_binding_t;

typedef struct zlink_stream_session_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_stream_session_state_t state;
  uint64_t lifecycle_generation;
  uint64_t session_count;
  uint64_t binding_count;
  uint64_t pending_message_count;
  uint64_t pending_byte_count;
  int32_t last_error;
} zlink_stream_session_status_t;
```

service handle 하나는 raw STREAM 하나와 MeshNode 하나에만 연결된다. 같은 raw STREAM을 두 service handle에
등록하거나 서로 다른 MeshNode에 연결하면 `ZLINK_CONFIG_CONFLICT`, `errno == EEXIST`다.

## 2. lifecycle

```c
ZLINK_EXPORT void *zlink_stream_session_service_new(
  void *mesh_node,
  void *stream);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_start(
  void *service);
ZLINK_EXPORT zlink_request_result_t zlink_stream_session_service_shutdown(
  void *service,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_stream_session_service_destroy(
  void **service_p);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_service_status(
  void *service,
  zlink_stream_session_status_t *status_out);
```

`new`는 MeshNode와 raw STREAM handle을 빌리지 않고 참조를 유지한다. 두 handle 모두 유효해야 하며
MeshNode는 `CREATED` 또는 실행 상태여야 한다. service가 destroy될 때까지 caller는 raw STREAM이나
MeshNode를 먼저 destroy할 수 없다. 위반하면 해당 destroy는 `ZLINK_CLOSE_BUSY`, `errno == EBUSY`다.

`start`는 raw STREAM이 bind된 뒤 호출한다. 성공한 뒤 service는 session connect/disconnect를 관찰하지만
raw receive mode를 차지하지 않는다. raw recv, raw callback과 packet callback의 상호 배제 계약은 그대로다.

`shutdown`은 새 bind와 session ingress를 막고 accepted message, operation과 transfer barrier를 deadline까지
진행한다. 성공은 `ZLINK_REQUEST_OK`, timeout은 `ZLINK_REQUEST_TIMED_OUT`/`ETIMEDOUT`이다. destroy는 남은
operation을 `ZLINK_REQUEST_TERMINATED` terminal completion으로 만들고 binding을 제거한다.

## 3. Session과 Actor binding

```c
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_bind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_unbind_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_config_result_t zlink_stream_session_bindings(
  void *service,
  const zlink_routing_id_t *session_rid,
  zlink_stream_session_binding_t *entries,
  size_t *count_inout);
```

한 session은 여러 Actor와 연결할 수 있지만 같은 Actor generation은 동시에 한 session에만 연결된다.
bind는 ActorRef generation과 현재 membership epoch를 검증한다. 동일한 binding은 성공하는 idempotent
operation이며 새 generation을 만들지 않는다. 다른 session의 active binding은
`ZLINK_SUBMIT_INVALID_STATE`, `errno == EBUSY`다.

bind와 unbind 성공은 non-zero operation ID를 반환한다. terminal 결과는 MeshNode의 Node infrastructure
claim에 정확히 한 번 전달된다. timeout 또는 실패 뒤에는 호출 전 binding 상태를 유지한다. unbind는
`expected_binding_generation`이 현재 값과 다르면 `ZLINK_SUBMIT_INVALID_STATE`, `errno == ESTALE`이다.
이미 없는 binding은 expected generation이 0일 때만 성공하는 idempotent operation이다.

`bindings`는 caller-owned snapshot을 채운다. `entries == NULL`이면 필요한 개수를 `count_inout`에
기록한다. capacity가 작으면 필요한 개수를 기록하고 `ZLINK_CONFIG_BUFFER_TOO_SMALL`, `errno == ENOBUFS`로
실패하며 entry를 일부 기록하지 않는다.

## 4. Session에서 Actor로 보내기

```c
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_send_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_stream_session_request_to_actor(
  void *service,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  const zlink_mesh_metadata_view_t *actor_metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

호출은 complete multipart 하나를 같은 session FIFO에서 Actor mailbox로 제출한다. session과 ActorRef의
active binding이 없으면 `ZLINK_SUBMIT_NOT_FOUND`/`ENOENT`, Actor route가 없으면
`ZLINK_SUBMIT_NOT_CONNECTED`/`ENOTCONN`, queue admission이 불가능하면
`ZLINK_SUBMIT_BACKPRESSURED`/`EAGAIN`이다. part 단위 API나 multipart 중간 상태는 공개하지 않는다.

입력 part와 metadata는 borrowed read-only다. 성공하면 Core가 반환 전에 필요한 reference를 확보하고
성공과 실패 모두에서 caller가 원본 소유권을 유지한다. 같은 session에서 같은 Actor binding으로 성공한
message는 Actor mailbox에서 FIFO다. 서로 다른 session 사이의 전역 순서는 보장하지 않는다.

request completion은 MeshNode의 Node infrastructure claim에 전달한다. application reply는 Actor request가
가진 one-shot token을 `zlink_mesh_reply()`에 넘긴다.

## 5. Actor에서 bound session으로 보내기

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_send_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_actor_close_bound_session(
  void *mesh_node,
  const zlink_actor_ref_t *actor,
  uint64_t expected_binding_generation,
  zlink_mesh_operation_id_t *operation_id_out,
  uint32_t timeout_ms);
```

send는 ActorRef의 current binding을 한 번 snapshot하고 complete multipart를 해당 raw STREAM session에
제출한다. binding이 없으면 `ZLINK_SUBMIT_NOT_FOUND`, session 연결이 종료되었으면
`ZLINK_SUBMIT_NOT_CONNECTED`, HWM을 넘으면 `ZLINK_SUBMIT_BACKPRESSURED`다. 성공한 message의 순서는 같은
Actor binding의 session FIFO에서 보존된다.

close는 binding generation CAS에 성공한 경우 session 연결을 종료하고 binding을 제거한다. terminal 결과는
호출 Actor의 infrastructure claim에 전달한다. stale generation은 `ESTALE`이며 다른 binding을 종료하지
않는다.

## 6. Actor 이동 barrier

Actor transfer prepare가 시작되면 active binding 각각은 transfer participant가 된다. service는 같은 session
FIFO에서 마지막 old-epoch message 뒤 barrier를 기록하고 participant-local sequence를 부여한다. barrier 뒤에
수용한 message는 bounded pending queue에 유지하며 source Actor mailbox로 보내지 않는다. target prepare의
private readiness negotiation은 각 queue의 최대 transfer allowance를 예약하며 caller에게 participant 목록이나
pending message batch를 요구하지 않는다.

service는 reserved allowance 안의 message만 pending queue에 수용한다. allowance가 소진되면
`zlink_stream_session_send_to_actor()`와 `zlink_stream_session_request_to_actor()`는
`ZLINK_SUBMIT_BACKPRESSURED`와 `EAGAIN`을 반환하고 raw session ingress admission은 allowance가 반환될 때까지
중단한다. 수용하지 않은 message는 transfer 대상이 아니다. status의 `pending_message_count`와
`pending_byte_count`에는 수용한 pending traffic만 포함한다.

target readiness 뒤 source Core는 pending message를 session FIFO 순서로 자동 전송한다. target commit을
시작하면 service는 마지막 pending sequence를 terminal high-water로 봉인하고 그 뒤 traffic을 new-epoch
forwarding queue로 분리한다. target은 terminal high-water까지 연속 staging한 뒤 ACK하며 target commit은 이
ACK 전에는 성공하지 않는다. activate는 frozen Actor backlog 뒤에 participant FIFO와 new-epoch traffic을
순서대로 공개한다. 서로 다른 session participant 사이에는 전역 순서를 보장하지 않는다.

session disconnect는 이미 수용한 message와 terminal high-water를 순서대로 반영한 뒤 terminal participant
상태가 된다. disconnect만으로 high-water가 완료된 것으로 간주하지 않는다. barrier 전에 transport가 끊겨
순서를 증명할 수 없으면 transfer prepare는 실패하고, prepared 뒤 peer loss와 재전송 결과는
[Actor transfer 계약](04-actor.ko.md#6-transfer-fence)을 따른다.

commit 뒤 service는 pending binding과 message를 target Actor generation의 staging으로 유지하며 activation
전에는 application dispatch를 시작하지 않는다. activate는 새 epoch FIFO를 공개한다. authority abort에서는
target staging을 폐기한 뒤 source binding, pending FIFO와 new-epoch forwarding traffic을 원래 순서로
복원한다. transfer ID, Actor generation, membership epoch 또는 binding generation이 stale이면 `ESTALE`이다.

## 7. Thread safety와 오류

서로 다른 session의 send와 binding operation은 동시에 호출할 수 있다. 같은 session의 binding mutation,
send와 transfer barrier는 service가 직렬화한다. callback 안에서 shutdown이나 destroy를 호출하면
`EDEADLK`다. MeshNode 또는 service가 draining이면 새 bind, request와 send는 `ESHUTDOWN`을 반환한다.

결과 enum과 errno의 정확한 대응은 [errno map](../04-errno-map.ko.md)을 따른다.
