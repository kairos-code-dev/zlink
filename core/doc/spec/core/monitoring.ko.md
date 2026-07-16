[English](monitoring.md) | 한국어

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md) · [Events](events.ko.md) · [MeshNode](service/mesh-node.ko.md) · [errno map](errno-map.ko.md)

# Monitoring

이 문서는 ZLink Core 10.0.0의 raw socket monitor와 MeshNode monitor 공개 계약을 정의한다. 대상 독자는
연결, peer admission, Logical Multicast, backpressure와 lifecycle을 관측하는 C API와 bindings 개발자다.
monitor는 상태를 관측할 뿐 routing, admission과 queue 상태를 변경하지 않는다.

## 1. Raw socket monitor

```c
typedef enum zlink_monitor_source_kind_t {
  ZLINK_MONITOR_SOURCE_SOCKET = 1
} zlink_monitor_source_kind_t;

typedef uint32_t zlink_socket_monitor_event_mask_t;

typedef enum zlink_socket_monitor_event_e {
  ZLINK_SOCKET_MONITOR_EVENT_CONNECTED                  = 1u << 0,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED            = 1u << 1,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED            = 1u << 2,
  ZLINK_SOCKET_MONITOR_EVENT_LISTENING                  = 1u << 3,
  ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED                = 1u << 4,
  ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED                   = 1u << 5,
  ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED              = 1u << 6,
  ZLINK_SOCKET_MONITOR_EVENT_CLOSED                     = 1u << 7,
  ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED               = 1u << 8,
  ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED               = 1u << 9,
  ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED            = 1u << 10,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL = 1u << 11,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY           = 1u << 12,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL  = 1u << 13,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH      = 1u << 14,
  ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED        = 1u << 15,
  ZLINK_SOCKET_MONITOR_EVENT_ALL                        = 0xFFFFu,

  ZLINK_EVENT_CONNECTED                  = ZLINK_SOCKET_MONITOR_EVENT_CONNECTED,
  ZLINK_EVENT_CONNECT_DELAYED            = ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED,
  ZLINK_EVENT_CONNECT_RETRIED            = ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED,
  ZLINK_EVENT_LISTENING                  = ZLINK_SOCKET_MONITOR_EVENT_LISTENING,
  ZLINK_EVENT_BIND_FAILED                = ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED,
  ZLINK_EVENT_ACCEPTED                   = ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED,
  ZLINK_EVENT_ACCEPT_FAILED              = ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED,
  ZLINK_EVENT_CLOSED                     = ZLINK_SOCKET_MONITOR_EVENT_CLOSED,
  ZLINK_EVENT_CLOSE_FAILED               = ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED,
  ZLINK_EVENT_DISCONNECTED               = ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED,
  ZLINK_EVENT_MONITOR_STOPPED            = ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED,
  ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL =
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL,
  ZLINK_EVENT_CONNECTION_READY           = ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY,
  ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL  =
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL,
  ZLINK_EVENT_HANDSHAKE_FAILED_AUTH      =
    ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH,
  ZLINK_EVENT_PEER_WEIGHT_CHANGED        =
    ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED,
  ZLINK_EVENT_ALL                        = ZLINK_SOCKET_MONITOR_EVENT_ALL
} zlink_socket_monitor_event_e;

typedef uint32_t zlink_monitor_state_mask_t;

typedef enum zlink_monitor_state_flag_e {
  ZLINK_MONITOR_STATE_READY       = 1u << 0,
  ZLINK_MONITOR_STATE_BOUND_READY = 1u << 1,
  ZLINK_MONITOR_STATE_CLOSED      = 1u << 3
} zlink_monitor_state_flag_e;

typedef uint32_t zlink_monitor_status_detail_mask_t;

typedef enum zlink_monitor_status_detail_flag_e {
  ZLINK_MONITOR_STATUS_DETAIL_SND_PENDING_MSGS = 1u << 1,
  ZLINK_MONITOR_STATUS_DETAIL_RCV_PENDING_MSGS = 1u << 2,
  ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUDGET  = 1u << 3,
  ZLINK_MONITOR_STATUS_DETAIL_AUTO_HWM_BUFFERS = 1u << 4
} zlink_monitor_status_detail_flag_e;

typedef enum zlink_auto_hwm_recalc_reason_t {
  ZLINK_AUTO_HWM_RECALC_REASON_NONE            = 0,
  ZLINK_AUTO_HWM_RECALC_REASON_INITIAL         = 1,
  ZLINK_AUTO_HWM_RECALC_REASON_ROLE_CHANGE     = 2,
  ZLINK_AUTO_HWM_RECALC_REASON_POLICY_TOGGLE   = 3,
  ZLINK_AUTO_HWM_RECALC_REASON_REFRESH         = 4,
  ZLINK_AUTO_HWM_RECALC_REASON_DEFERRED_SHRINK = 5
} zlink_auto_hwm_recalc_reason_t;

typedef enum zlink_disconnect_reason_t {
  ZLINK_DISCONNECT_REASON_UNKNOWN          = 0,
  ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED = 3,
  ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR  = 4,
  ZLINK_DISCONNECT_REASON_CTX_TERM         = 5
} zlink_disconnect_reason_t;

#define ZLINK_DISCONNECT_UNKNOWN ZLINK_DISCONNECT_REASON_UNKNOWN
#define ZLINK_DISCONNECT_HANDSHAKE_FAILED ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED
#define ZLINK_DISCONNECT_TRANSPORT_ERROR ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR
#define ZLINK_DISCONNECT_CTX_TERM ZLINK_DISCONNECT_REASON_CTX_TERM

typedef enum zlink_protocol_error_t {
  ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO = 0x10000013
} zlink_protocol_error_t;

typedef struct zlink_monitor_event_t {
  uint64_t event;
  uint64_t value;
  zlink_routing_id_t routing_id;
  char local_addr[256];
  char remote_addr[256];
} zlink_monitor_event_t;

typedef void (*zlink_monitor_handler_fn)(
  const zlink_monitor_event_t *event,
  void *userdata);

typedef zlink_monitor_event_t zlink_socket_monitor_event_t;
typedef zlink_monitor_handler_fn zlink_socket_monitor_handler_fn;

ZLINK_EXPORT void zlink_monitor_ignore_handler (const zlink_monitor_event_t *event_,
                                                void *userdata_);

typedef struct zlink_socket_monitor_open_options_t {
  zlink_socket_monitor_event_mask_t events;
} zlink_socket_monitor_open_options_t;

typedef struct zlink_monitor_status_t {
  zlink_monitor_source_kind_t source_kind;
  zlink_monitor_state_mask_t state_flags;
  zlink_monitor_status_detail_mask_t detail_flags;
  uint64_t snd_pending_msgs;
  uint64_t rcv_pending_msgs;
  uint32_t auto_hwm_enabled;
  uint32_t auto_hwm_profile;
  uint32_t auto_hwm_role;
  uint32_t auto_hwm_policy_class;
  uint64_t auto_hwm_unit_budget_bytes;
  uint32_t auto_hwm_size_cap;
  uint64_t auto_hwm_socket_message_slots;
  uint32_t auto_hwm_connection_bucket_enabled;
  uint32_t auto_hwm_connection_bucket_count;
  uint32_t auto_hwm_connection_bucket_index;
  uint32_t auto_hwm_connection_bucket_hwm_4k;
  uint32_t auto_hwm_connection_bucket_hysteresis_retained;
  uint64_t auto_hwm_effective_message_bytes;
  int32_t auto_hwm_applied_sndhwm;
  int32_t auto_hwm_applied_rcvhwm;
  int32_t auto_hwm_effective_sndbuf;
  int32_t auto_hwm_effective_rcvbuf;
  uint64_t auto_hwm_last_recalc_ms;
  uint32_t auto_hwm_last_recalc_reason;
  uint32_t auto_hwm_send_blocked_ratio_ppm;
  int32_t auto_hwm_deferred_sndhwm;
  int32_t auto_hwm_deferred_rcvhwm;
} zlink_monitor_status_t;

void *zlink_socket_monitor_open(
  void *socket,
  const zlink_socket_monitor_open_options_t *options);
zlink_handler_result_t zlink_socket_monitor_handler(
  void *monitor,
  zlink_socket_monitor_handler_fn handler,
  void *userdata);
zlink_recv_result_t zlink_socket_monitor_recv(
  void *monitor,
  zlink_socket_monitor_event_t *event_out,
  zlink_recv_flags_t flags);
zlink_config_result_t zlink_monitor_status(
  void *monitor,
  zlink_monitor_status_t *status_out);
zlink_close_result_t zlink_monitor_close(void **monitor_p);
```

`ZLINK_SOCKET_MONITOR_EVENT_*`가 event mask의 정식 이름이며 `ZLINK_EVENT_*`는 같은 숫자의 짧은 이름이다.
`ZLINK_DISCONNECT_*` macro는 같은 이름의 `ZLINK_DISCONNECT_REASON_*` enum 값에 대한 ABI 유지 alias다.
`events == 0`은 event를 선택하지 않고 `EVENT_ALL`은 모든 bit를 선택한다. raw socket monitor status의
`source_kind`는 `ZLINK_MONITOR_SOURCE_SOCKET`이다. `detail_flags`에 없는 선택 field는 0이며
`auto_hwm_connection_bucket_index`는 bucket이 없으면 `UINT32_MAX`다.

socket monitor는 bind, accept, connect, disconnect, handshake, protocol error와 close event를 제공한다.
handler mode와 recv mode는 상호 배타이며 두 번째 mode는 `EBUSY`다. event의 address와 routing ID는
recv에서는 caller-owned output 구조체 안의 값이다. callback의 event pointer와 그 안의 값은 callback이
반환될 때까지만 유효한 borrowed view다. `DISCONNECTED`의 `value`는 `zlink_disconnect_reason_t`,
`HANDSHAKE_FAILED_PROTOCOL`의 `value`는 `zlink_protocol_error_t`, `PEER_WEIGHT_CHANGED`의 `value`는
새 `0..100` weight다. 다른 실패 event의 `value`는 해당 실패의 errno다.

`zlink_monitor_ignore_handler()`는 전달된 event와 `userdata`를 보관하거나 해제하지 않는 no-op 함수다.
`event`는 호출 동안만 유효한 borrowed view다. Core는 callback dispatch 없이 recv와 snapshot을 사용하는
socket monitor를 열 때 이 exported symbol을 내부 sentinel로 사용한다. 이 함수를 handler API에 직접
등록하면 일반 callback consumer처럼 event queue를 소비하되 각 event에 아무 작업도 하지 않는다.

## 2. MeshNode monitor type

```c
#define ZLINK_MESH_MONITOR_ABI_VERSION 1u
#define ZLINK_MESH_MONITOR_CHANNEL_MAX 255u

typedef uint64_t zlink_mesh_monitor_event_mask_t;

typedef enum zlink_mesh_monitor_event_kind_t {
  ZLINK_MESH_MONITOR_STATE_CHANGED       = 1,
  ZLINK_MESH_MONITOR_PEER_CONNECTING     = 2,
  ZLINK_MESH_MONITOR_PEER_ADMITTED       = 3,
  ZLINK_MESH_MONITOR_PEER_DRAINING       = 4,
  ZLINK_MESH_MONITOR_PEER_CLOSED         = 5,
  ZLINK_MESH_MONITOR_PEER_REJECTED       = 6,
  ZLINK_MESH_MONITOR_CHANNEL_CHANGED     = 7,
  ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED   = 8,
  ZLINK_MESH_MONITOR_MULTICAST_COMMITTED = 9,
  ZLINK_MESH_MONITOR_MULTICAST_DROPPED   = 10,
  ZLINK_MESH_MONITOR_BACKPRESSURED       = 11,
  ZLINK_MESH_MONITOR_OPERATION_COMPLETED = 12,
  ZLINK_MESH_MONITOR_PROTOCOL_ERROR      = 13,
  ZLINK_MESH_MONITOR_CLAIM_REVOKED       = 14
} zlink_mesh_monitor_event_kind_t;

typedef struct zlink_mesh_monitor_open_options_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_monitor_event_mask_t events;
} zlink_mesh_monitor_open_options_t;

typedef struct zlink_mesh_monitor_event_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_monitor_event_kind_t kind;
  uint64_t timestamp_ms;
  uint64_t mesh_lifecycle_generation;
  zlink_mesh_node_state_t mesh_state;
  zlink_routing_id_t peer_rid;
  uint64_t peer_lifecycle_generation;
  zlink_mesh_owner_kind_t owner_kind;
  zlink_routing_id_t spot_rid;
  zlink_actor_ref_t actor;
  char channel_name[ZLINK_MESH_MONITOR_CHANNEL_MAX + 1];
  uint64_t operation_id_high;
  uint64_t operation_id_low;
  uint32_t snapshot_target_count;
  uint32_t admitted_target_count;
  uint32_t dropped_target_count;
  int32_t result_code;
  int32_t failure_errno;
} zlink_mesh_monitor_event_t;

typedef struct zlink_mesh_monitor_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_node_state_t state;
  uint64_t peer_admitted;
  uint64_t peer_rejected;
  uint64_t submitted_messages;
  uint64_t completed_operations;
  uint64_t backpressured_submits;
  uint64_t multicast_messages;
  uint64_t multicast_dropped_targets;
  uint64_t active_claims;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
} zlink_mesh_monitor_status_t;

typedef void (*zlink_mesh_monitor_handler_fn)(
  const zlink_mesh_monitor_event_t *event,
  void *userdata);
```

event mask의 bit `1ULL << (kind - 1)`이 해당 event를 선택한다. `events == 0`은 모든 event를 선택한다.
적용되지 않는 RID, ActorRef, channel과 operation field는 zero value다. `result_code`는 공개 result enum의
숫자 값이고 `failure_errno`는 같은 실패의 errno다.

## 3. MeshNode monitor API

```c
void *zlink_mesh_node_monitor_open(
  void *mesh_node,
  const zlink_mesh_monitor_open_options_t *options);
zlink_handler_result_t zlink_mesh_node_monitor_handler(
  void *monitor,
  zlink_mesh_monitor_handler_fn handler,
  void *userdata);
zlink_recv_result_t zlink_mesh_node_monitor_recv(
  void *monitor,
  zlink_mesh_monitor_event_t *event_out,
  zlink_recv_flags_t flags);
zlink_config_result_t zlink_mesh_node_monitor_status(
  void *monitor,
  zlink_mesh_monitor_status_t *status_out);
zlink_close_result_t zlink_mesh_node_monitor_close(void **monitor_p);
```

monitor open은 MeshNode의 strong child reference를 유지하므로 monitor를 먼저 닫아야 MeshNode destroy가
성공한다. handler와 recv는 같은 event queue의 single consumer다. handler가 `NULL`이면 등록을 해제하며
같은 callback 안의 해제는 `ZLINK_HANDLER_DEADLOCK`이다.

status는 호출 시점의 atomic snapshot이다. counter는 lifecycle generation 안에서 단조 증가하며 새
generation에서 0부터 시작한다. status를 읽어도 event를 소비하지 않는다.

## 4. Event 의미

peer event는 peer RID와 lifecycle generation을 함께 제공한다. rejected event는 MeshName, expected RID,
generation, trust profile 또는 authentication 실패를 `result_code`와 `failure_errno`로 구분한다.

multicast committed event는 snapshot, admitted와 dropped target count를 함께 제공한다. 기본 NODROP에서
성공한 event의 dropped count는 0이다. drop 허용 mode에서는 target별 event를 만들지 않고 한 publish의
aggregate count만 기록해 event queue를 제한한다.

backpressured event는 owner kind와 가능한 경우 Spot·Actor·channel을 제공한다. raw topic, Actor ID,
application metadata와 payload는 monitor에 복사하지 않는다. 따라서 label cardinality와 sensitive data가
monitor event에 노출되지 않는다.

operation completed event는 operation ID와 terminal result를 제공하지만 reply payload는 포함하지 않는다.
claim revoked event는 shutdown deadline 뒤 claim owner와 generation을 기록한다.

## 5. Overflow, ordering과 thread safety

monitor queue는 bounded다. queue가 가득 차면 동일한 high-frequency submit/backpressure event를 aggregate하고
peer state, protocol error와 lifecycle event를 우선 보존한다. aggregate된 수는 다음 status snapshot에 반드시
반영한다. Core는 monitor consumer 지연 때문에 application submit을 block하지 않는다.

같은 MeshNode lifecycle에서 event는 Core가 상태 전이를 commit한 순서로 queue에 들어간다. 서로 다른 peer
I/O thread 사이의 wall-clock order는 보장하지 않는다. handler, recv, status와 close의 result·errno 대응은
[errno map](errno-map.ko.md)을 따른다.
