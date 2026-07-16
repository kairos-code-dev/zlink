[English](03-spot.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md) · [MeshNode](01-mesh-node.ko.md) · [Dispatch](02-dispatch.ko.md)

# Spot

이 문서는 ZLink Core 10.0.0의 정식 공개 계약을 정의한다. 대상 독자는 MeshNode 안의 logical Spot을
생성하고 메시지를 수신하는 C API와 bindings 개발자다. 이 문서는 “Spot direct 메시지와 Logical
Multicast를 어떤 Spot claim으로 수신하는가?”에 답한다.

## 1. 책임 경계

Spot은 하나의 MeshNode가 소유하는 logical destination이다. Spot은 network socket, peer connection 또는
remote subscription을 소유하지 않는다. direct Spot 메시지와 Logical Multicast subscription record는 Spot
application claim으로 수신한다. Actor payload는 Actor claim으로 직접 전달되며 Spot claim을 경유하지
않는다.

classic PUB/SUB는 독립적인 raw socket 계약이다. Spot Logical Multicast는 MeshNode ROUTER와 channel
membership을 사용하며 PUB, SUB, XPUB 또는 XSUB socket을 만들지 않는다.

## 2. 공개 타입

```c
#define ZLINK_SPOT_ABI_VERSION 1u

typedef enum zlink_spot_kind_t {
  ZLINK_SPOT_KIND_INVALID = 0,
  ZLINK_SPOT_KIND_ENTRY   = 1,
  ZLINK_SPOT_KIND_USER    = 2
} zlink_spot_kind_t;

typedef enum zlink_spot_subscription_kind_t {
  ZLINK_SPOT_SUBSCRIPTION_EXACT  = 1,
  ZLINK_SPOT_SUBSCRIPTION_PREFIX = 2
} zlink_spot_subscription_kind_t;

typedef struct zlink_spot_status_t {
  uint32_t struct_size;
  uint32_t version;
  zlink_routing_id_t spot_rid;
  zlink_spot_kind_t spot_kind;
  uint64_t lifecycle_generation;
  uint64_t pending_application_messages;
  uint64_t pending_infrastructure_messages;
  uint64_t pending_bytes;
  uint32_t active_actor_count;
  uint32_t draining;
  int32_t last_error;
  uint64_t last_changed_ms;
} zlink_spot_status_t;
```

Spot routing ID는 1..255 bytes다. 같은 MeshNode에서 routing ID와 lifecycle generation의 조합이 logical
Spot을 식별한다. 같은 routing ID로 Spot을 다시 만들면 generation이 증가한다.

## 3. 생성, 조회와 종료

```c
ZLINK_EXPORT void *zlink_spot_new(void *mesh_node);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_entry_spot(
  void *mesh_node,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_lookup(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out);
ZLINK_EXPORT zlink_config_result_t zlink_mesh_node_spot_get_or_new(
  void *mesh_node,
  const zlink_routing_id_t *spot_rid,
  void **spot_out,
  uint32_t *created_out);
ZLINK_EXPORT zlink_close_result_t zlink_spot_destroy(void **spot_p);
ZLINK_EXPORT zlink_config_result_t zlink_spot_status(
  void *spot,
  zlink_spot_status_t *status_out);
```

`zlink_spot_new()`은 owner MeshNode의 entry Spot facade를 만든다. entry Spot은 MeshNode당 하나이며 node
routing ID와 같은 Spot RID를 사용한다. `entry_spot`도 같은 logical Spot을 가리키는 새 owned facade를
반환한다.

`lookup`은 존재하는 local Spot의 새 facade를 반환한다. 없으면 `ZLINK_CONFIG_NOT_FOUND`,
`errno == ENOENT`다. `get_or_new`는 local Spot을 원자적으로 확보하고 새로 만들었으면
`*created_out = 1`이다. remote Spot을 만들거나 조회하지 않는다.

facade는 caller가 소유한다. facade destroy는 logical Spot을 즉시 제거하지 않는다. 마지막 facade,
Actor membership, timer와 active claim이 해제되고 owner MeshNode가 더 이상 Spot을 참조하지 않을 때
logical Spot이 종료된다. MeshNode shutdown은 새 Spot 생성과 lookup facade 생성을 `ESHUTDOWN`으로
거부한다.

## 4. Channel send와 request

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_channel(
  void *spot,
  const char *channel_name,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

owner MeshNode가 target channel의 ready member를 positive-weight round-robin으로 선택하고 같은 호출에서
submit한다. source Spot RID와 generation은 routing envelope에 기록한다. 대상 없음, metadata, multipart,
ownership과 timeout 의미는 MeshNode channel API와 같다.

request completion은 이 Spot의 infrastructure claim으로 반환한다. facade가 닫혀도 logical Spot generation이
유효하면 completion을 유지한다. Spot lifecycle이 끝나면 outstanding operation은 `ESHUTDOWN` terminal
completion으로 종료한다.

## 5. Spot direct send와 request

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_to_spot(
  void *spot,
  const zlink_routing_id_t *target_node_rid,
  const zlink_routing_id_t *target_spot_rid,
  uint64_t target_spot_generation,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_operation_id_t *operation_id_out,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

Core는 target node RID의 admitted pipe를 사용하고 수신 node가 explicit `target_spot_generation`을 확인한다.
caller는 exact target node RID, Spot RID와 lifecycle generation을 한 번의 public API 호출에 전달한다.
Core는 logical location을 조회하거나 보완하지 않는다. generation 0은
`ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`이며 현재 generation을 뜻하는 shortcut으로 사용하지 않는다.

Spot direct service envelope의 address section은 다음 순서로 인코딩한다. RID bytes는 `size`만큼 이어지고
generation은 unsigned 64-bit big-endian이다. local delivery도 같은 logical field를 검증하지만 재인코딩하지
않는다.

```text
address_version:u8 (=1) |
source_spot_rid_size:u8 | source_spot_rid:bytes |
source_spot_generation:u64be |
target_spot_rid_size:u8 | target_spot_rid:bytes |
target_spot_generation:u64be
```

target node가 없으면 submit이 `ZLINK_SUBMIT_NOT_CONNECTED`, `errno == ENOTCONN`으로 실패한다. request를
admission한 뒤 target Spot이 없으면 completion의 `terminal_result`는 `ZLINK_REQUEST_NOT_FOUND`,
`failure_errno`는 `ENOENT`다. envelope의 target generation이 같은 RID의 현재 lifecycle generation과 다르면
각각 `ZLINK_REQUEST_CONFLICT`, `ESTALE`다.
one-way send는 remote application acknowledgement를 추가하지 않으므로 submit 성공 뒤 remote Spot 부재를
호출자에게 보고한다고 보장하지 않으며, 이를 위한 monitor event도 10.0.0 event ABI에서 보장하지 않는다.

request record의 reply는 [dispatch 계약](02-dispatch.ko.md)의 `zlink_mesh_reply()`를 사용한다. source route나
request sequence를 재구성하지 않는다.

같은 Spot이 같은 destination pipe에 성공적으로 submit한 direct·channel message는 FIFO다. Channel 선택이
서로 다른 destination을 고르면 channel 전체의 전역 순서는 보장하지 않는다. Logical Multicast ordering은
MeshNode publisher 계약을 따른다.

## 6. Logical Multicast publish

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *channel_name,
  const char *topic,
  const zlink_mesh_metadata_view_t *metadata,
  const zlink_msg_t *parts,
  size_t part_count,
  zlink_mesh_publish_detail_t *detail_out,
  zlink_send_flags_t flags);
ZLINK_EXPORT zlink_config_result_t zlink_spot_set_publish_option(
  void *spot,
  zlink_mesh_publish_option_t option,
  const void *optval,
  size_t optvallen);
ZLINK_EXPORT zlink_config_result_t zlink_spot_get_publish_option(
  void *spot,
  zlink_mesh_publish_option_t option,
  void *optval,
  size_t *optvallen);
```

`ZLINK_MESH_PUBLISH_OPT_NODROP`은 `int` 0 또는 1이며 기본값은 1이다. publish는 owner MeshNode
publisher와 같은 target snapshot, NODROP, timeout, ordering과 submit detail
계약을 사용한다. canonical application metadata도 같은 방식으로 검증하고 모든 matching Spot record에
같은 immutable view로 전달한다. source Spot RID와 generation을 추가로 기록한다. target ChannelName은 필수이며 owner
MeshNode에 channel membership이 하나뿐이어도 생략할 수 없다.

topic은 NUL을 포함하지 않는 1..255-byte UTF-8 문자열이다. 빈 topic, 잘못된 UTF-8과 상한 초과는
`ZLINK_SUBMIT_INVALID_ARGUMENT`, `errno == EINVAL`이다.

publish가 성공하면 target ChannelName 범위에서 topic과 일치하는 각 Spot lifecycle generation은
`SPOT_MULTICAST` record를 정확히 한 번 수신하고, 일치하지 않는 Spot은 수신하지 않는다. 같은 publish를
다른 node로 relay하거나 replay하지 않는다.

## 7. Local subscription

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_set_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);
ZLINK_EXPORT zlink_config_result_t zlink_spot_unset_subscription(
  void *spot,
  const char *channel_name,
  const char *topic_filter,
  zlink_spot_subscription_kind_t kind);
```

subscription key는 `(ChannelName, topic filter, kind, Spot generation)`이다. 이름과 filter는 UTF-8이며
NUL을 포함할 수 없다. filter는 0..255 bytes다. exact는 전체 topic byte sequence가 같을 때,
prefix는 topic이 filter bytes로 시작할 때 일치한다. 빈 prefix는 해당 channel의 모든 topic과 일치한다.

중복 set은 성공하는 idempotent operation이다. 존재하지 않는 key의 unset도 성공한다. 성공한 set 또는
unset이 반환된 뒤 시작한 publish에는 변경된 subscription이 적용된다. set 또는 unset과 동시에 실행된
publish는 변경 전 또는 변경 후 상태 가운데 하나를 적용하며, 부분 상태를 보거나 같은 subscription으로
중복 전달하지 않는다.

subscription은 remote peer에 전파하지 않는다. public subscription inventory query도 제공하지 않는다.
raw `zlink_set_subscription()`, `zlink_unset_subscription()`과 `zlink_subscription_at()`은 SUB/XSUB socket에만
적용되며 Spot handle을 받지 않는다.

## 8. Receive record와 control lane

Spot work는 ready batch에서 `owner_kind == ZLINK_MESH_OWNER_SPOT`인 claim으로 얻고
`zlink_mesh_claim_recv_batch()`로 수신한다.

| Record kind | Domain | 의미 |
|---|---|---|
| `SPOT_SEND` | application | direct Spot send |
| `SPOT_REQUEST` | application | reply token이 있는 direct Spot request |
| `SPOT_MULTICAST` | application | source channel과 topic이 있는 local subscription match |
| `SPOT_CONTROL` | infrastructure 또는 application | Actor join·leave·lifecycle control |
| `COMPLETION` | infrastructure | Spot이 시작한 request의 terminal result |
| `SEND_READY` | infrastructure | backpressure 뒤 재시도 가능 상태 |

multicast record는 source node RID, source Spot RID, target ChannelName, topic과 application metadata를
보존한다. direct record와 multicast record는 application metadata를 별도 immutable view로 제공한다.
Actor application payload는 이 표에 포함되지
않는다.

같은 Spot application claim은 한 번에 하나만 active하다. infrastructure claim은 application claim과
독립적으로 진행한다. Spot의 다음 application turn은 이전 application claim을 release한 뒤에만 시작한다.

## 9. Spot timer

```c
ZLINK_EXPORT void *zlink_spot_timer_new(void *spot);
```

이 함수는 Spot lifecycle generation에 속하는 eventing timer handle을 만든다. Spot 종료 또는 같은 RID의
새 generation 뒤에는 이전 generation의 tick을 전달하지 않는다. 성공한 stop 또는 destroy가 반환된 뒤에는
새 tick을 전달하지 않는다. 같은 Spot generation의 timer handler와 application claim handler는 동시에
실행되지 않는다. timer의 start, stop, recv, handler와 destroy는 일반 timer 계약을 사용한다.

## 10. Option과 thread safety

모든 request는 명시적인 `timeout_ms`를 받는다. Spot publish option은
`ZLINK_MESH_PUBLISH_OPT_NODROP`만 지원하며 다른 값은 `ZLINK_CONFIG_NOT_SUPPORTED`다.

send, request, publish, subscription과 status는 thread-safe다. 같은 Spot handle의 destroy와 다른 호출을
동시에 실행할 수 없다. callback 또는 active claim 안에서 같은 Spot의 destructive close를 호출하면
`EDEADLK`다.
