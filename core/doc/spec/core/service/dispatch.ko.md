[English](dispatch.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md)

# Service dispatch

이 문서는 ZLink Core 10.0.0의 정식 공개 계약을 정의한다. 대상 독자는 C API와 bindings에서
MeshNode service work를 수신하는 개발자다. 이 문서는 “Node·Spot·Actor와 infrastructure work를 callback
payload 없이 어떻게 공정하고 안전하게 수신하는가?”에 답한다.

## 1. 공개 타입과 상수

모든 value type은 값으로 복사할 수 있다. `version`은 `1`, `struct_size`는 해당 공개 구조체의
`sizeof(...)`로 설정한다.

```c
#define ZLINK_MESH_DISPATCH_ABI_VERSION 1u

typedef uint32_t zlink_mesh_ready_domain_mask_t;

enum {
  ZLINK_MESH_READY_NONE           = 0u,
  ZLINK_MESH_READY_APPLICATION    = 1u << 0,
  ZLINK_MESH_READY_INFRASTRUCTURE = 1u << 1,
  ZLINK_MESH_READY_ALL            = (1u << 0) | (1u << 1)
};

typedef enum zlink_mesh_owner_kind_t {
  ZLINK_MESH_OWNER_NODE  = 1,
  ZLINK_MESH_OWNER_SPOT  = 2,
  ZLINK_MESH_OWNER_ACTOR = 3
} zlink_mesh_owner_kind_t;

typedef enum zlink_mesh_record_kind_t {
  ZLINK_MESH_RECORD_NODE_SEND          = 1,
  ZLINK_MESH_RECORD_NODE_REQUEST       = 2,
  ZLINK_MESH_RECORD_CHANNEL_SEND       = 3,
  ZLINK_MESH_RECORD_CHANNEL_REQUEST    = 4,
  ZLINK_MESH_RECORD_SPOT_SEND          = 5,
  ZLINK_MESH_RECORD_SPOT_REQUEST       = 6,
  ZLINK_MESH_RECORD_SPOT_MULTICAST     = 7,
  ZLINK_MESH_RECORD_SPOT_CONTROL       = 8,
  ZLINK_MESH_RECORD_ACTOR_SEND         = 9,
  ZLINK_MESH_RECORD_ACTOR_REQUEST      = 10,
  ZLINK_MESH_RECORD_COMPLETION         = 11,
  ZLINK_MESH_RECORD_SEND_READY         = 12,
  ZLINK_MESH_RECORD_TRANSFER_CONTROL   = 13
} zlink_mesh_record_kind_t;

typedef enum zlink_mesh_operation_kind_t {
  ZLINK_MESH_OPERATION_NODE_REQUEST          = 1,
  ZLINK_MESH_OPERATION_CHANNEL_REQUEST       = 2,
  ZLINK_MESH_OPERATION_SPOT_REQUEST          = 3,
  ZLINK_MESH_OPERATION_ACTOR_REQUEST         = 4,
  ZLINK_MESH_OPERATION_ACTOR_LOOKUP          = 5,
  ZLINK_MESH_OPERATION_ACTOR_DESTROY         = 6,
  ZLINK_MESH_OPERATION_ACTOR_JOIN            = 7,
  ZLINK_MESH_OPERATION_ACTOR_LEAVE           = 8,
  ZLINK_MESH_OPERATION_STREAM_BIND           = 9,
  ZLINK_MESH_OPERATION_STREAM_UNBIND         = 10,
  ZLINK_MESH_OPERATION_STREAM_CLOSE          = 11,
  ZLINK_MESH_OPERATION_ACTOR_TRANSFER        = 12
} zlink_mesh_operation_kind_t;

typedef enum zlink_mesh_destination_kind_t {
  ZLINK_MESH_DESTINATION_NODE          = 1,
  ZLINK_MESH_DESTINATION_CHANNEL       = 2,
  ZLINK_MESH_DESTINATION_SPOT          = 3,
  ZLINK_MESH_DESTINATION_ACTOR         = 4,
  ZLINK_MESH_DESTINATION_BOUND_SESSION = 5
} zlink_mesh_destination_kind_t;

typedef struct zlink_mesh_operation_id_t {
  uint64_t high;
  uint64_t low;
} zlink_mesh_operation_id_t;

typedef struct zlink_mesh_reply_token_t {
  uint64_t opaque[4];
} zlink_mesh_reply_token_t;

typedef struct zlink_mesh_claim_t {
  uint64_t opaque[4];
} zlink_mesh_claim_t;

typedef struct zlink_mesh_ready_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_owner_kind_t owner_kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t spot_rid;
  zlink_actor_ref_t actor;
} zlink_mesh_ready_record_t;

typedef struct zlink_mesh_receive_requirements_t {
  uint32_t struct_size;
  uint32_t version;
  size_t message_count;
  size_t part_count;
  size_t byte_count;
} zlink_mesh_receive_requirements_t;

typedef struct zlink_mesh_receive_record_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_record_kind_t kind;
  zlink_mesh_ready_domain_mask_t domain;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_actor_ref_t source_actor;
  zlink_mesh_operation_id_t operation_id;
  zlink_mesh_operation_kind_t operation_kind;
  zlink_mesh_reply_token_t reply_token;
  const char *channel_name;
  size_t channel_name_size;
  const char *topic;
  size_t topic_size;
  const uint8_t *application_metadata;
  size_t application_metadata_size;
  const void *kind_data;
  size_t kind_data_size;
  size_t part_offset;
  size_t part_count;
  int32_t terminal_result;
  int32_t failure_errno;
} zlink_mesh_receive_record_t;

typedef struct zlink_mesh_send_ready_data_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_destination_kind_t destination_kind;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  zlink_actor_ref_t target_actor;
  const char *channel_name;
  size_t channel_name_size;
} zlink_mesh_send_ready_data_t;

typedef zlink_mesh_ready_domain_mask_t (*zlink_mesh_ready_handler_fn)(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t ready_domains,
  void *userdata);
```

비어 있는 routing ID와 ActorRef는 해당 field가 적용되지 않음을 뜻한다. `reply_token`은 Node·Channel·Spot·Actor
request record와 reply를 요구하는 Actor join `SPOT_CONTROL` record에서만 유효하다. `operation_id`는 request
및 completion record에만 유효하다. `kind_data`는 record kind가 정한
versioned 구조체 view다. `channel_name`, `topic`, metadata, kind data와 part view는 batch가 소유한다.
completion record는 `operation_kind`, `terminal_result`와 `failure_errno`를 항상 채운다.

| Record kind | `kind_data` type |
|---|---|
| Node·Channel·Spot·Actor send/request | 없음 |
| `COMPLETION` | Actor lookup은 `zlink_actor_location_t`, Actor join은 `zlink_actor_join_completion_t`, 그 밖의 operation은 owner service가 정의한 versioned result 구조체 또는 없음 |
| `SEND_READY` | `zlink_mesh_send_ready_data_t` |
| `SPOT_CONTROL` | `zlink_actor_control_record_t` |
| `TRANSFER_CONTROL` | `zlink_actor_transfer_control_t` |

`TRANSFER_CONTROL`은 record의 Actor generation이 owner인 infrastructure domain에서만 반환한다. 이 record는
operation completion이 아니므로 `operation_id`는 zero이며 application domain에 들어가지 않는다.

## 2. Ready handler

```c
ZLINK_EXPORT zlink_handler_result_t zlink_mesh_node_set_ready_handler(
  void *mesh_node,
  zlink_mesh_ready_handler_fn handler,
  void *userdata);
```

handler는 readable domain mask만 받으며 payload, claim 또는 owner 수명을 받지 않는다. 반환 mask는 이번
호출에서 consumer가 drain 책임을 인수한 domain이다. 반환하지 않은 readable domain은 Core가 bounded
rate로 다시 통지한다.

`handler == NULL`은 등록을 해제한다. 성공한 해제는 이미 시작한 callback이 모두 반환한 뒤 완료된다.
handler 안에서 같은 handler를 해제하면 `ZLINK_HANDLER_DEADLOCK`, `errno == EDEADLK`로 실패한다.

ready handler와 MeshNode `POLLIN` poller 등록은 같은 ready index의 single consumer이므로 함께 사용할 수
없다. 두 번째 등록은 `ZLINK_HANDLER_BUSY`, `errno == EBUSY`로 실패한다. `POLLOUT` poller는 독립적으로
사용할 수 있다.

## 3. Ready batch와 claim

```c
ZLINK_EXPORT void *zlink_mesh_ready_batch_new(size_t record_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_ready_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_node_drain_ready(
  void *mesh_node,
  zlink_mesh_ready_domain_mask_t domains,
  void *batch,
  uint32_t *has_residue_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_ready_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_ready_record_t *zlink_mesh_ready_batch_data(
  const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_ready_batch_take_claim(
  void *batch,
  size_t index,
  zlink_mesh_claim_t *claim_out);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_claim_release(
  zlink_mesh_claim_t *claim);
```

새 batch와 reset이 성공한 batch만 drain에 넘길 수 있다. non-empty batch를 다시 drain하면
`ZLINK_RECV_BUSY`, `errno == EBUSY`다. `record_capacity == 0`은 `NULL`, `errno == EINVAL`로 실패한다.

각 ready record의 `domain`은 `APPLICATION` 또는 `INFRASTRUCTURE` 비트 하나만 가진다. 같은 owner의 두
domain이 모두 readable이면 서로 다른 record와 claim을 반환한다. 따라서 application claim을 보유한 동안
같은 owner의 infrastructure claim을 별도로 가져와 completion과 send-ready를 진행할 수 있다.

각 ready record는 claim 하나를 소유한다. `take_claim` 성공 뒤 claim 소유권은 호출자에게 이동하고 같은
index의 두 번째 호출은 `ZLINK_CONFIG_INVALID_STATE`, `errno == ESTALE`로 실패한다. reset과 destroy는
아직 가져가지 않은 claim을 자동으로 반환한다.

claim은 원래 MeshNode가 destroy된 뒤에도 release할 수 있다. release는 thread-safe이며 어느 thread에서나
호출할 수 있다. zero value, 이미 반환한 claim 또는 stale generation은 `ZLINK_CLOSE_INVALID_HANDLE`,
`errno == ESTALE`다.

`has_residue_out`은 capacity 또는 fairness quantum 때문에 ready owner가 남았으면 `1`이다. 성공한 drain은
record를 contiguous array로 제공한다. `data` pointer는 다음 reset 또는 destroy 전까지만 유효하다.

## 4. Receive batch

```c
ZLINK_EXPORT void *zlink_mesh_receive_batch_new(
  size_t message_capacity,
  size_t part_capacity,
  size_t byte_capacity);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_reset(void *batch);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_receive_batch_destroy(void **batch_p);

ZLINK_EXPORT zlink_recv_result_t zlink_mesh_claim_recv_batch(
  zlink_mesh_claim_t *claim,
  zlink_mesh_ready_domain_mask_t domains,
  void *batch,
  zlink_mesh_receive_requirements_t *required_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT size_t zlink_mesh_receive_batch_count(const void *batch);
ZLINK_EXPORT const zlink_mesh_receive_record_t *zlink_mesh_receive_batch_data(
  const void *batch);
ZLINK_EXPORT size_t zlink_mesh_receive_batch_part_count(const void *batch);
ZLINK_EXPORT const zlink_msg_t *zlink_mesh_receive_batch_parts(const void *batch);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_receive_batch_retain_message(
  const void *batch,
  size_t record_index,
  zlink_msg_t *parts_out,
  size_t *part_count_inout);
```

claim은 owner kind, generation과 domain 하나를 소유하므로 recv 함수는 MeshNode, Spot 또는 Actor handle을 다시 받지
않는다. record kind가 claim owner와 맞지 않으면 Core가 반환하지 않는다. application과 infrastructure
domain은 서로 다른 claim으로 독립적으로 drain한다. `domains`에는 claim이 소유한 domain 비트만 허용하며
다른 비트나 `ALL`을 넘기면 `ZLINK_RECV_INVALID_STATE`, `errno == EINVAL`이다.

receive batch는 complete multipart message만 반환한다. 첫 message 하나도 capacity에 들어가지 않으면
`ZLINK_RECV_BUFFER_TOO_SMALL`, `errno == ENOBUFS`이며 `required_out`에 그 message를 담는 데 필요한 최소
message, part와 byte 수를 기록한다. 이 결과에서는 batch가 empty다. 하나 이상 담은 뒤 다음 message가
들어가지 않으면 성공하고 남은 work는 claim을 release할 때 다시 ready가 된다.

record, string, metadata와 part pointer는 batch reset 또는 destroy 전까지만 유효하다. reply token은
batch가 아니라 claim 수명에 묶이므로 batch reset 뒤에도 claim을 release하기 전까지 유효하다.
`retain_message`는 record의 모든 part를 caller-provided array에 `zlink_msg_copy()` 의미로 복사한다.
capacity가 작으면 필요한 part 수를 `part_count_inout`에 기록하고 `ZLINK_CONFIG_BUFFER_TOO_SMALL`,
`errno == ENOBUFS`로 실패하며 output part를 초기화하지 않는다.

batch 하나를 여러 thread가 동시에 drain, reset 또는 destroy하면 `ZLINK_RECV_BUSY` 또는
`ZLINK_CONFIG_BUSY`, `errno == EBUSY`다. 서로 다른 batch는 동시에 사용할 수 있다.

## 5. Operation과 reply

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_reply(
  const zlink_mesh_reply_token_t *token,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);
```

request admission이 성공하면 Core가 non-zero operation ID를 발급한다. requester owner의 infrastructure
claim은 operation마다 terminal completion을 정확히 한 번 반환한다. local timeout 뒤 도착한 reply는
폐기하며 두 번째 completion을 만들지 않는다.

reply token은 source route와 owner generation을 숨기는 32-byte value다. request claim을 release하기
전까지 유효하고 한 번의 성공한 reply만 허용한다. 두 번째 reply는 `ZLINK_SUBMIT_INVALID_STATE`,
`errno == EALREADY`다. stale generation은 `ESTALE`, shutdown으로 route를 사용할 수 없으면 `ESHUTDOWN`을
사용한다. reply에는 application metadata를 붙이지 않는다.

token에는 record kind가 봉인되어 있다. `zlink_mesh_reply()`는 Node·Channel·Spot·Actor request token만
허용한다. Actor join `SPOT_CONTROL` token은 `zlink_actor_join_reply()`에서만 사용할 수 있으며 generic
reply에 넘기면 `ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`이다. 잘못된 API 호출은 token을 소비하지
않는다.

submit 입력 part는 borrowed read-only다. 성공하면 Core가 함수 반환 전에 필요한 reference를 확보한다.
성공과 실패 모두에서 호출자가 원본 part의 소유권을 유지한다.

## 6. Close와 progress

claim release는 owner mailbox에 work가 남으면 ready index를 rearm한다. Core는 claim release를 기다리는
application domain과 별개로 completion·send-ready infrastructure domain을 진행시킨다.

MeshNode graceful shutdown은 새 application admission을 중단한 뒤 active claim과 infrastructure work를
deadline까지 기다린다. deadline이 끝나도 이미 반환된 batch storage를 회수하지 않는다. outstanding
claim은 revoke 상태가 되어 새 recv가 `ZLINK_RECV_INVALID_STATE`, `errno == ESHUTDOWN`으로 실패하지만
release는 계속 안전하다.
