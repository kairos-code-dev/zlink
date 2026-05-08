# Core API Surface Alignment Draft

> 이 문서는 구현 전에 사용한 초안 형식의 설계 기록이다.
> 현재 공개 계약은 이 문서가 아니라 `core/include/zlink.h`와 정식 spec 문서가
> 기준이다.

## 목적

이 초안은 core 공개 API 표면의 두 가지 정리를 다룬다.

- Actor create / join payload를 aggregate multipart payload로 확장한다.
- Registry 숫자 설정을 typed registry option API로 모은다.

두 변경은 모두 public API가 너무 좁게 고정되어 새 설정이나 payload 확장이 생길 때
wrapper가 계속 늘어나는 문제를 줄이기 위한 것이다.

## Actor create / join payload 계약

Actor create / join 요청은 하나의 논리 요청이다. 따라서 part 단위 streaming API가
아니라 한 번에 넘기고 받는 aggregate multipart API를 사용한다.

```c
ZLINK_EXPORT zlink_request_result_t zlink_spot_node_create_remote_actor(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_actor_create_result_t *out,
  uint32_t timeout_ms);

typedef zlink_actor_admission_result_t (*zlink_actor_admission_handler_fn)(
  void *node,
  const char *actor_id,
  const zlink_msg_t *parts,
  size_t part_count,
  void *userdata);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *parts,
  size_t part_count);
```

소유권 규칙은 send / recv 계열 API와 맞춘다.

- submit 또는 request 제출이 성공하면 `parts[0..part_count)` 소유권은
  라이브러리로 이전된다.
- validation 실패나 submit 전 실패에서는 payload 소유권이 호출자에게 남는다.
- recv 성공 시 `parts_out` payload 소유권은 호출자에게 이전된다. 호출자는
  `zlink_multipart_close()`로 닫거나 각 part를 정확히 한 번 소비해야 한다.
- admission handler는 borrowed payload view만 받는다. handler는 part를 닫거나
  move하지 않는다.

payload 인자 모양은 아래처럼 해석한다.

- `parts == NULL && part_count == 0`: payload 없음
- `parts != NULL && part_count > 0`: multipart payload
- `parts == NULL && part_count > 0`: invalid argument
- `parts != NULL && part_count == 0`: invalid argument

빈 message 하나를 보내려면 size 0인 `zlink_msg_t` 하나를 `part_count == 1`로 넘긴다.

## Registry option 계약

Registry의 숫자 설정은 `zlink_registry_option_t`와 set / get 함수로 다룬다.

```c
typedef enum zlink_registry_option_t {
  ZLINK_REGISTRY_OPT_ID = 0x3801,
  ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802,
  ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803,
  ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804
} zlink_registry_option_t;

ZLINK_EXPORT zlink_config_result_t zlink_registry_set(
  void *registry,
  zlink_registry_option_t option,
  uint32_t value);

ZLINK_EXPORT uint32_t zlink_registry_get(
  void *registry,
  zlink_registry_option_t option,
  zlink_config_result_t *error_out);
```

`zlink_registry_set()`에서 `value == 0`은 invalid argument다. 알 수 없는 option은
`ZLINK_CONFIG_NOT_SUPPORTED`와 `ENOTSUP`로 실패한다.

`zlink_registry_get()`은 성공 시 option 값을 반환하고, `error_out`이 NULL이 아니면
`ZLINK_CONFIG_OK`를 기록한다. 실패 시 반환값은 0이며, `error_out`이 NULL이 아니면
실패 result를 기록한다.

기존 이름별 숫자 setter는 compatibility wrapper로 남긴다. 새 문서와 binding public
surface는 registry option API를 canonical API로 설명한다.
