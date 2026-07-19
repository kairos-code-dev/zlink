[English](01-mesh-node.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md) · [Dispatch](02-dispatch.ko.md)

# MeshNode

이 문서는 ZLink Core 10.1.0의 정식 공개 계약을 정의한다. 대상 독자는 RouteMesh에 참여하는 Core
C API와 bindings를 구현하는 개발자다. 이 문서는 “하나의 MeshNode가 물리 mesh, 논리 channel
membership과 Node·Channel 메시징을 어떤 공개 계약으로 제공하는가?”에 답한다.

## 1. 범위와 불변 조건

MeshNode 하나는 `MeshName` 하나, routing ID 하나와 ROUTER bind endpoint 하나를 가진다. 하나 이상의
`ChannelName`에 참여할 수 있다. 같은 process에는 같은 `MeshName`의 MeshNode를 하나만 만들 수 있지만
서로 다른 이름의 MeshNode는 여러 개 만들 수 있다. mesh 사이의 자동 메시징은 없다.

`selectNode`, `selectOne`과 `selectMany`는 공개 함수가 아니다. Node/Channel send·request와 publish가
선택과 submit을 한 호출 안에서 수행한다.

## 2. 공개 상수와 타입

```c
#define ZLINK_MESH_NODE_ABI_VERSION 1u
#define ZLINK_MESH_NAME_MAX 255u
#define ZLINK_CHANNEL_NAME_MAX 255u
#define ZLINK_MESH_ENDPOINT_MAX 511u
#define ZLINK_MESH_APPLICATION_METADATA_MAX 1024u
#define ZLINK_MESH_TOPIC_MAX 255u

typedef enum zlink_mesh_node_state_t {
  ZLINK_MESH_NODE_CREATED       = 1,
  ZLINK_MESH_NODE_STARTED       = 2,
  ZLINK_MESH_NODE_PARTIAL_READY = 3,
  ZLINK_MESH_NODE_READY         = 4,
  ZLINK_MESH_NODE_DRAINING      = 5,
  ZLINK_MESH_NODE_STOPPED       = 6,
  ZLINK_MESH_NODE_ERROR         = 7
} zlink_mesh_node_state_t;

typedef enum zlink_mesh_peer_source_t {
  ZLINK_MESH_PEER_MANUAL    = 1,
  ZLINK_MESH_PEER_DISCOVERY = 2,
  ZLINK_MESH_PEER_MIXED     = 3
} zlink_mesh_peer_source_t;

typedef enum zlink_mesh_peer_state_t {
  ZLINK_MESH_PEER_CONFIGURED = 1,
  ZLINK_MESH_PEER_CONNECTING = 2,
  ZLINK_MESH_PEER_ADMITTED   = 3,
  ZLINK_MESH_PEER_DRAINING   = 4,
  ZLINK_MESH_PEER_CLOSED     = 5,
  ZLINK_MESH_PEER_ERROR      = 6
} zlink_mesh_peer_state_t;

typedef enum zlink_mesh_node_option_t {
  ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE       = 0x3620,
  ZLINK_MESH_NODE_OPT_ROUTER_HWM               = 0x3621,
  ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET   = 0x3622,
  ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET      = 0x3623
} zlink_mesh_node_option_t;

typedef struct zlink_mesh_node_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *mesh_name;
  size_t mesh_name_size;
  const char *trust_profile;
  size_t trust_profile_size;
} zlink_mesh_node_options_t;

typedef struct zlink_mesh_peer_connection_options_t {
  uint32_t struct_size;
  uint32_t version;
  const char *endpoint;
  size_t endpoint_size;
  uint32_t has_expected_rid;
  zlink_routing_id_t expected_rid;
} zlink_mesh_peer_connection_options_t;

typedef struct zlink_mesh_metadata_view_t {
  const uint8_t *data;
  size_t size;
} zlink_mesh_metadata_view_t;

typedef struct zlink_mesh_publish_detail_t {
  uint32_t struct_size;
  uint32_t version;
  uint32_t snapshot_remote_target_count;
  uint32_t admitted_remote_target_count;
  uint32_t dropped_remote_target_count;
  uint32_t unreachable_remote_target_count;
  uint32_t snapshot_local_spot_count;
  uint32_t admitted_local_spot_count;
  uint32_t dropped_local_spot_count;
} zlink_mesh_publish_detail_t;

typedef struct zlink_mesh_node_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_mesh_node_state_t state;
  zlink_routing_id_t routing_id;
  char mesh_name[ZLINK_MESH_NAME_MAX + 1];
  char local_endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  uint32_t channel_count;
  uint32_t configured_peer_count;
  uint32_t admitted_peer_count;
  uint32_t draining_peer_count;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint64_t multicast_submitted;
  uint64_t multicast_dropped_targets;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_node_status_t;

typedef struct zlink_mesh_peer_entry_t {
  uint32_t struct_size;
  uint32_t version;
  uint64_t connection_intent_id;
  zlink_mesh_peer_source_t source;
  zlink_mesh_peer_state_t state;
  zlink_routing_id_t routing_id;
  uint64_t lifecycle_generation;
  uint64_t descriptor_revision;
  char endpoint[ZLINK_MESH_ENDPOINT_MAX + 1];
  uint32_t channel_count;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_mesh_peer_entry_t;
```

모든 이름, trust profile과 endpoint는 NUL을 포함하지 않는 UTF-8 byte sequence다. 이름과 trust profile은 1..255 bytes이며 비교는
byte 단위로 대소문자를 구분한다. Core는 이름을 정규화하지 않는다. endpoint는 1..511 bytes다.
options와 query 구조체는 `version == 1`이고 `struct_size`가 문서의 구조체 크기 이상이어야 한다.

## 3. 생성과 lifecycle

```c
ZLINK_EXPORT void *zlink_mesh_node_new(
  void *ctx,
  const zlink_mesh_node_options_t *options);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_bind(
  void *mesh_node,
  const char *endpoint);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_start(void *mesh_node);
ZLINK_EXPORT zlink_request_result_t zlink_mesh_node_shutdown(
  void *mesh_node,
  uint32_t timeout_ms);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_destroy(void **mesh_node_p);
```

`new`는 MeshName을 복사하고 `CREATED` handle을 반환한다. 같은 process에 같은 MeshName이 있으면
`NULL`, `errno == EEXIST`다. routing ID는 `zlink_set_routing_id()`, TLS는 기존 공통 TLS API, option은
§9의 API로 `start` 전에 설정한다.

`start`에는 routing ID, bind endpoint와 ChannelName 하나 이상이 필요하다. 누락은
`ZLINK_CONFIG_INVALID_STATE`, `errno == EINVAL`이다. 성공 뒤 identity, bind와 membership은 바꿀 수
없다. port 0 bind는 허용하며 실제 endpoint는 status에서 확인한다.

`shutdown`은 새 application admission을 중단하고 `DRAINING`으로 전환한다. active claim, reply와
completion을 `timeout_ms`까지 진행시킨다. 모두 종료되면 `ZLINK_REQUEST_OK`와 `STOPPED`, deadline이
끝나면 `ZLINK_REQUEST_TIMED_OUT`, `errno == ETIMEDOUT`을 반환하고 claim storage를 revoke 가능한 상태로
유지한다. `timeout_ms == 0`은 기다리지 않는다.

`destroy`는 먼저 child handle이 없는지 검사한다. Child가 남아 있으면 lifecycle을 바꾸지 않고
`ZLINK_CLOSE_BUSY`, `errno == EBUSY`를 반환한다. Child가 없고 `STOPPED`가 아니면 강제 종료를 수행하고
outstanding operation을 `ESHUTDOWN` terminal completion으로 만든 뒤 handle ownership을 해제한다. 호출 뒤
기존 claim의 release는 계속 안전하다.

MeshNode가 소유하는 child handle은 다음 수명 규칙을 따른다.

| Child | Parent destroy 전 조건 | Shutdown 뒤 새 operation |
|---|---|---|
| Mesh publisher | `zlink_mesh_node_publisher_destroy()` 완료 | `ZLINK_SUBMIT_INVALID_STATE`/`ESHUTDOWN` |
| Spot facade와 Spot timer | facade와 timer handle close 완료 | 새 send·request·timer 등록은 `ESHUTDOWN` |
| MeshNode monitor | monitor close 완료 | 이미 queue에 있는 event와 terminal status만 drain 가능 |
| STREAM session service | service destroy 완료 | 새 bind·send·request는 `ESHUTDOWN` |

Child가 없어서 destroy가 진행된 경우 MeshNode pointer를 `NULL`로 바꾼다. Claim과 retained message
reference는 child handle이 아니므로 shutdown deadline 뒤 revoke할 수 있고 MeshNode destroy 뒤에도
release할 수 있다.

start 직후 active peer connection intent가 없으면 `READY`다. intent가 하나 이상이면 모든 intent가 admitted일
때 `READY`, 하나라도 connecting 또는 error면 `PARTIAL_READY`다. `STARTED`는 첫 readiness 평가 전의 전이
상태다. discovery가 새 intent를 추가하면 같은 규칙으로 `PARTIAL_READY` 또는 `READY`를 다시 계산한다.

## 4. Channel membership

```c
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_add_channel_name(
  void *mesh_node,
  const char *channel_name);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_set_channel_weight(
  void *mesh_node,
  const char *channel_name,
  uint32_t weight);
```

membership은 `CREATED`에서만 추가한다. 중복은 `ZLINK_CONFIG_CONFLICT`, `errno == EEXIST`다. start 뒤
추가·제거는 `ZLINK_CONFIG_INVALID_STATE`, `errno == EBUSY`다.

weight는 0..100이며 기본값은 100이다. start 전과 실행 중 모두 바꿀 수 있다. 0은 해당 channel의 새
round-robin과 multicast remote target에서 제외한다. RID direct, 이미 admission된 message와 다른 channel
membership에는 영향을 주지 않는다. `descriptor_revision`은 start할 때 1이며 변경마다 증가한다.
lifecycle generation은 같은 RID로 다시 시작한 수명만 구분하며 weight 변경으로 증가하지 않는다.

## 5. Peer connection과 admission

```c
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_connect_peer(
  void *mesh_node,
  const zlink_mesh_peer_connection_options_t *options,
  uint64_t *connection_intent_id_out);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_remove_peer_connection(
  void *mesh_node,
  uint64_t connection_intent_id);
ZLINK_EXPORT zlink_connect_result_t zlink_mesh_node_disconnect_peer(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation);
```

caller는 endpoint와 선택적인 expected RID만 제공한다. MeshName, 실제 RID, ChannelName set, weight,
generation과 security identity는 admission handshake가 관측한다. manual과 discovery endpoint는 같은
handshake와 message path를 사용한다.

admission descriptor는 lifecycle generation과 descriptor revision을 별도 필드로 전달한다. ChannelName
set은 lifecycle 동안 바뀌지 않지만 weight는 실행 중 바뀔 수 있다. weight 변경은 revision을 증가시키고
현재 admitted peer에 control update를 보낸다. 수신 peer는 같은 lifecycle generation에서 더 큰 revision만
적용하고 channel selection index를 원자적으로 교체한다. Redis discovery adapter도 같은 revision으로
descriptor row와 change stamp를 갱신한다. update 유실 뒤에는 다음 handshake 또는 discovery snapshot이
같은 최신 revision으로 수렴해야 한다. weight update만으로 pipe를 다시 만들거나 lifecycle generation을
바꾸지 않는다.

admission은 MeshName, RID, lifecycle generation과 local MeshNode 생성 시
`zlink_mesh_node_options_t.trust_profile`에 설정한 trust profile을 검증한다. peer connection options는
trust profile을 소유하지 않는다. MeshName 불일치는 `ZLINK_CONNECT_CONFLICT`, `errno == EEXIST`다.
expected RID 또는 lifecycle generation 불일치는 `ZLINK_CONNECT_CONFLICT`, `errno == ESTALE`다. trust
profile 검증이나 peer authentication이 실패한 경우에만 `ZLINK_CONNECT_AUTH_FAILED`, `errno == EACCES`다.
같은 MeshName 안의 중복 RID/generation은 새 connection을 거부한다. 더 높은 generation은 이전
generation을 draining한 뒤 교체한다.

같은 endpoint가 manual과 discovery로 관측되면 하나의 intent로 합치고 source를 `MIXED`로 표시한다.
source 하나를 제거해도 다른 source가 남으면 connection을 유지한다. 아직 admission되지 않은 intent는
intent ID로 제거하고 admitted peer는 RID와 generation으로 disconnect한다.

peer가 `ADMITTED`이고 weight가 0이 아니며 local node가 draining이 아닐 때만 channel 선택 대상이다.
peer drain은 새 snapshot에서 제외하지만 이미 commit한 message를 취소하지 않는다.

## 6. Node와 Channel 메시징

```c
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_node(
  void *mesh_node,
  const zlink_routing_id_t *target_rid,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_send_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_request_to_channel(
  void *mesh_node,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

Node API는 target RID의 admitted pipe에 직접 submit한다. 없으면 `ZLINK_SUBMIT_NOT_CONNECTED`,
`errno == ENOTCONN`이다. Channel API는 호출 시점의 ready member 가운데 positive weight round-robin으로
하나를 선택하고 같은 호출에서 submit한다. 대상이 없으면 `ZLINK_SUBMIT_NOT_FOUND`, `errno == ENOENT`다.
선택 RID를 caller에게 반환하지 않는다.

Local MeshNode도 requested ChannelName에 참여하고 local state가 `READY`, membership weight가 양수이며 drain
중이 아니면 channel selection 대상이다. Local selection은 remote selection과 같은 round-robin cursor를
사용하고 Node application mailbox admission을 거친다. 따라서 peer가 없는 single-node RouteMesh에서도 local
membership으로 channel send/request를 처리할 수 있다.

request 성공은 non-zero operation ID를 반환한다. terminal reply, timeout, shutdown 또는 route failure는
requester Node infrastructure claim의 completion record로 정확히 한 번 전달한다. send에는 completion이
없다.

### 6.1 Node application claim 수신

Node 수신 work가 준비되면 ready record의 `owner_kind`는 `ZLINK_MESH_OWNER_NODE`다. application payload는
`domain == ZLINK_MESH_READY_APPLICATION`인 claim에서 다음 record kind로만 수신한다.

| Record kind | 의미 |
|---|---|
| `NODE_SEND` | RID로 직접 보낸 one-way message |
| `NODE_REQUEST` | RID로 직접 보낸 request |
| `CHANNEL_SEND` | ChannelName 선택으로 전달된 one-way message |
| `CHANNEL_REQUEST` | ChannelName 선택으로 전달된 request |

`NODE_REQUEST`와 `CHANNEL_REQUEST` record의 `reply_token`은
[`zlink_mesh_reply()`](02-dispatch.ko.md#5-operation과-reply)에 전달한다. application claim에는
`COMPLETION`과 `SEND_READY`가 나타나지 않는다. Node가 시작한 request의 terminal result와
backpressure 뒤 재시도 가능 상태가 있으면, 같은 Node owner의 별도
`ZLINK_MESH_READY_INFRASTRUCTURE` claim에 각각 `COMPLETION`과 `SEND_READY` record로 나타난다.

ready batch에서 claim을 인수하는 방법, receive batch의 완전한 multipart 단위, record·part view의
수명, message retain과 claim release 규칙은 [Dispatch 계약](02-dispatch.ko.md#3-ready-batch와-claim)을
따른다.

`parts != NULL`, `part_count > 0`이어야 한다. input은 borrowed read-only이며 모든 결과에서 caller가
소유한다. 성공하면 Core가 반환 전에 필요한 reference를 확보한다. complete multipart 전체가 하나의
admission 단위다.

같은 origin Node가 같은 destination pipe에 성공적으로 submit한 message는 FIFO다. Channel 선택이 서로 다른
destination을 고르면 channel 전체의 전역 순서는 보장하지 않는다. request reply와 다른 origin 사이에도
전역 순서를 제공하지 않는다.

## 7. Logical Multicast publisher

```c
ZLINK_EXPORT void *zlink_mesh_node_publisher_new(void *mesh_node);
ZLINK_EXPORT zlink_submit_result_t zlink_mesh_node_publisher_publish(
  void *publisher,
  const char *channel_name,
  const char *topic,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_close_result_t zlink_mesh_node_publisher_destroy(
  void **publisher_p);
```

publish는 target channel의 ready remote member와, local MeshNode가 그 channel에 참여할 때의 local
Spot match를 한 번 snapshot한다. 선택된 각 remote member에는 내부 ROUTER로 message를 한 번씩
submit한다. remote target 전체를 미리 reserve하거나 여러 target의 commit을 전부-또는-전무로 묶지
않는다. 한 target에 성공한 submit은 뒤 target의 실패로 취소하지 않는다.

`flags`는 각 remote ROUTER submit에 그대로 적용한다. `DONTWAIT`에서 pipe capacity가 부족하면 해당
target은 `EAGAIN`이다. blocking submit은 MeshNode의 `SNDTIMEO` 동안 기다리며, 시간이 끝나도 기존
ROUTER와 같이 `EAGAIN`이다.
하나 이상의 remote target이 capacity 때문에 실패하면 publish는
`ZLINK_SUBMIT_BACKPRESSURED`를 반환하지만, 그 전에 성공한 target의 message는 유지한다.

origin MeshNode의 local Spot mailbox와 remote MeshNode가 wire message를 받은 뒤 선택한 local Spot
mailbox는 각각 현재 capacity가 있는 target만 수락한다. capacity가 없는 local target은 기다리지 않고
drop count에 포함한다. remote ROUTER pipe submit 성공은 상대 MeshNode의 local Spot mailbox 수락을
보장하지 않는다. relay와 replay는 하지 않는다.

`metadata`는 Node·Channel 메시징과 같은 canonical application metadata frame이다. `NULL`은 metadata가
없음을 뜻한다. Core는 publish snapshot을 만들기 전에 metadata 전체를 검증하고, remote multicast record와
local Spot match 모두에 같은 immutable metadata view를 전달한다. metadata가 잘못되면 어떤 target도
reserve하지 않고 `ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`을 반환한다.
metadata 입력은 borrowed read-only이며 모든 결과에서 caller가 원본 storage를 소유한다. 성공하면 Core가
함수 반환 전에 publish operation에 필요한 reference 또는 복사본을 확보한다.

pipe가 종료된 admitted remote target은 backpressure가 아니라 §5의 peer 이탈이다. 그런 target은
drop으로 세지 않고 detail의 unreachable count로 보고한다.

detail은 remote와 local 각각의 snapshot, admission과 drop 수, 그리고 remote unreachable 수를
`ZLINK_SUBMIT_OK`와 `ZLINK_SUBMIT_BACKPRESSURED` 결과에서 제공한다. remote snapshot은 admitted,
dropped, unreachable의 합과 같다. remote와 local snapshot target이 모두 0이면
`ZLINK_SUBMIT_NOT_FOUND`, `errno == ENOENT`다.

topic은 NUL을 포함하지 않는 1..`ZLINK_MESH_TOPIC_MAX`-byte UTF-8 문자열이다. 빈 topic, 잘못된 UTF-8과
상한 초과는 `ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`이다.

같은 origin에서 성공한 publish의 commit 순서는 destination마다 FIFO다. 서로 다른 origin 또는 서로
다른 destination 사이의 global order는 보장하지 않는다.

## 8. Application metadata와 wire message

metadata가 없으면 pointer를 `NULL`로 전달한다. metadata frame은 최대 1024 bytes이며 다음 형식이다.

```text
version:u8 | count:u8 | repeated(key_len:u8 | key:utf8 |
value_len:u16be | value:utf8)
```

Core는 version, count, 모든 length, 빈 key, duplicate key, trailing bytes, UTF-8과 전체 상한을 검증한다.
잘못된 outbound는 `ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`, 잘못된 ingress는 protocol failure로
complete message를 mailbox admission 전에 거부한다.

wire message는 versioned service envelope, 선택적인 metadata frame과 application payload parts로
구성한다. routing envelope와 operation correlation은 application payload 또는 metadata로 노출하지 않는다.
local admission이 성공하면 complete application payload를 정확히 한 번 전달하며 remote delivery와 같은
metadata와 result 계약을 적용한다.

## 9. Option과 handle 지원

```c
ZLINK_EXPORT zlink_config_result_t zlink_set_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_get_mesh_node_option(
  void *mesh_node,
  zlink_mesh_node_option_t option,
  void *optval,
  size_t *optvallen);
```

MeshNode 전용 option은 start 전에만 설정한다. 공통 option 가운데 `ZLINK_OPT_MAXMSGSIZE`만 실행 중에도
바꿀 수 있고 새로 수신하는 complete message부터 적용한다. HWM profile 기본값은 `BALANCED`다. mailbox는
message와 byte budget을 함께 적용한다. Core dispatch worker 수 option은 제공하지 않는다.

| MeshNode option | `optval` 타입과 길이 | 허용 값 | 기본값과 의미 |
|---|---|---|---|
| `ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE` | `int`, `sizeof(int)` | `ZLINK_AUTO_HWM_PROFILE_COMPACT`(0), `LOW_LATENCY`(1), `BALANCED`(2), `THROUGHPUT`(3) | `BALANCED`; routed admission queue의 자동 HWM profile |
| `ZLINK_MESH_NODE_OPT_ROUTER_HWM` | `int`, `sizeof(int)` | `0..INT_MAX` | `0`; profile로 계산한 값을 사용하며 양수는 routed admission queue의 숫자 HWM override |
| `ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET` | `uint64_t`, `sizeof(uint64_t)` | `0..UINT64_MAX` | `0`; profile로 계산한 유한 message budget을 사용하며 양수는 owner mailbox별 override |
| `ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET` | `uint64_t`, `sizeof(uint64_t)` | `0..UINT64_MAX` | `0`; profile로 계산한 유한 byte budget을 사용하며 양수는 owner mailbox별 override |

message budget과 byte budget은 동시에 적용하며 먼저 도달한 한도가 admission을 backpressure한다. setter의
길이가 정확하지 않으면 `ZLINK_CONFIG_INVALID_ARGUMENT`, `errno == EMSGSIZE`, 값이 허용 범위를 벗어나면
`ZLINK_CONFIG_INVALID_ARGUMENT`, `errno == EINVAL`이다. getter에서 `*optvallen`이 필요한 크기보다 작으면
필요한 크기를 기록하고 `ZLINK_CONFIG_BUFFER_TOO_SMALL`, `errno == ENOBUFS`를 반환하며 `optval`을 일부
기록하지 않는다.

공개 handle별 option 지원은 다음과 같다. 표에 없는 조합은 `ZLINK_CONFIG_NOT_SUPPORTED`,
`errno == ENOTSUP`이다.

| Option family | raw socket | MeshNode | Spot | Mesh publisher |
|---|---:|---:|---:|---:|
| `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM` | 지원 | 지원 | 지원하지 않음 | 지원하지 않음 |
| `ZLINK_OPT_SNDTIMEO`, `ZLINK_OPT_RCVTIMEO` | 지원 | 지원 | 지원하지 않음 | 지원하지 않음 |
| `ZLINK_OPT_MAXMSGSIZE` | 지원 | 지원, 실행 중 변경 가능 | 지원하지 않음 | 지원하지 않음 |
| routing ID | 지원하는 socket type만 | start 전 지원 | 지원하지 않음 | 지원하지 않음 |
| TLS server/client | 지원하는 network socket만 | start 전 지원 | 지원하지 않음 | 지원하지 않음 |
| raw ROUTER·DEALER option | 해당 raw type만 | 지원하지 않음 | 지원하지 않음 | 지원하지 않음 |
| raw PUB·XPUB option | 해당 raw type만 | 지원하지 않음 | 지원하지 않음 | 지원하지 않음 |
| raw SUB·XSUB option | 해당 raw type만 | 지원하지 않음 | 지원하지 않음 | 지원하지 않음 |

Spot과 Mesh publisher는 publish 전용 option을 제공하지 않는다. Logical Multicast의 remote submit은
MeshNode의 물리 ROUTER에 설정된 HWM, `SNDTIMEO`와 호출별 `flags`를 사용한다. classic PUB의
subscriber별 전달 범위와 option은 raw PUB 계약을 따르며 Logical Multicast에 적용하지 않는다.

MeshNode 열의 공통 option은 `zlink_set_option()`과 `zlink_get_option()`을 사용한다. `SNDHWM`과 `RCVHWM`은
물리 ROUTER pipe queue에, `SNDTIMEO`는 blocking submit에, `RCVTIMEO`는 blocking ready drain에 적용된다.
`MAXMSGSIZE`는 `int64_t` byte 값이며 `-1`은 무제한, 0 이상은 complete inbound message 상한이다. 상한을
넘은 message는 어떤 payload part도 mailbox에 admission하지 않는다.
routing ID는 `zlink_set_routing_id()`와 `zlink_get_routing_id()`, TLS는 bind-side에
`zlink_set_tls_server()`, outbound peer에 `zlink_set_tls_client()`를 사용한다. setter는 `start` 전에
호출하되 `MAXMSGSIZE`만 유효한 실행 상태에서도 바꿀 수 있다. getter는 유효한 모든 lifecycle state에서
호출할 수 있다.

## 10. Status와 query

```c
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_status(
  void *mesh_node,
  zlink_mesh_node_status_t *status_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peers(
  void *mesh_node,
  zlink_mesh_peer_entry_t *entries,
  size_t *count_inout);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_peer_channels(
  void *mesh_node,
  const zlink_routing_id_t *peer_rid,
  uint64_t lifecycle_generation,
  char (*channel_names_out)[ZLINK_CHANNEL_NAME_MAX + 1],
  uint32_t *weights_out,
  size_t *count_inout);
```

query는 호출 시점 snapshot이다. `entries == NULL`이면 필요한 count만 반환한다. capacity가 작으면 필요한
count를 기록하고 `ZLINK_CONFIG_BUFFER_TOO_SMALL`, `errno == ENOBUFS`다. query output은 caller가
소유하며 Core 내부 pointer를 포함하지 않는다. public subscription inventory와 internal mailbox 자료구조
query는 제공하지 않는다.

## 11. Thread safety와 오류 우선순위

send, request, publish, weight 변경, peer intent와 query는 thread-safe다. lifecycle configure 함수는 start와
동시에 호출할 수 없다. 같은 handle의 shutdown과 destroy를 재진입하면 `EDEADLK`다. destroy는 이미 진입해
있는 다른 호출이 반환할 때까지 handle storage를 해제하지 않고 대기하며, destroy가 handle을 registry에서
제거한 뒤에 진입하는 호출은 `EFAULT`다. 같은 handle의 두 번째 동시 destroy는 `ESTALE`이다.

입력 검증은 argument, state, target lookup, backpressure 순서로 수행한다. 따라서 draining 상태의 새 submit은
target 상태와 관계없이 `ZLINK_SUBMIT_INVALID_STATE`, `errno == ESHUTDOWN`이다. invalid argument는 state
조회 전에 검증한다.
