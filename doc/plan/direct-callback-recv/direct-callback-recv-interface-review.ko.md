# Direct Callback Recv 인터페이스 재검토

이 문서는
[`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
의 본문과 분리된 인터페이스 재검토 노트다.

목적:

- `core/include/zlink.h`의 public ABI를 다시 검토한다.
- callback-only 방향에 맞지 않는 surface를 별도로 정리한다.
- 이름 단순화, handle identity 고정, `msg_t` 중심 data path, enum 기반 상수 체계를
  후속 작업으로 명확히 남긴다.

우선순위:

- 이 문서는 후속 인터페이스 검토 노트다.
- 메인 규범은
  [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
  이다.
- service option의 "어떤 항목을 public에 남길지"라는 상위 surface 결정은
  [`service-option-surface-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/service-option-surface-plan.ko.md)
  을 우선한다.
- `SpotNode` direct facade 관련 `SpotPub` / `SpotSub`의 정확한 지원/`ENOTSUP`
  매트릭스는
  [`spot-node-direct-facade-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/spot-node-direct-facade-plan.ko.md)
  을 우선한다.
- 특히 `SpotPub` / `SpotSub` option 지원 여부에서 두 문서가 충돌하면,
  direct facade의 더 구체적인 계약을 다루는
  `spot-node-direct-facade-plan.ko.md`를 우선한다.
- 이 문서는 주로 enum typing, 값 배치, 이름 단순화 같은 ABI shape 후보를 정리하며,
  위에서 명시한
  `direct-callback-recv-rewrite-spec.ko.md`,
  `service-option-surface-plan.ko.md`,
  `spot-node-direct-facade-plan.ko.md`
  가 이미 확정한 계약을 덮어쓰지 않는다.

핵심 원칙:

- 이름은 가능한 한 짧고 직접적으로 유지한다.
- `gateway`의 recv callback shape는 가능한 한 `ROUTER` callback shape를 재사용하되,
  unified gateway가 request/reply/control을 한 handle에서 받는 현재 메인 스펙에서는
  `kind`와 `service_name`을 추가로 유지한다.
- 생성 시점에 handle의 정체성(routing id, attach 대상, role bitmask 등)을 가능한 범위에서 고정한다.
- `send`/`recv` 데이터 전달 surface는 `zlink_msg_t` 중심으로 단순화한다.
- option 값은 각 service/socket family별 enum으로 분리하되,
  실제 정수 값은 전역에서 서로 겹치지 않게 배정한다.
- libzmq 숫자와의 정렬 자체는 목표가 아니다. 더 단순하고 견고한 public ABI를
  우선한다.

## 1. Discovery / Gateway 인터페이스 변경안

| 현재 인터페이스 | 변경안 | 변경 이유 |
|---|---|---|
| `zlink_discovery_new_typed(void *ctx, uint16_t service_type)` | `zlink_discovery_new(void *ctx, zlink_service_type_t service_type)` | `typed`는 불필요하게 장황하다. 인자 자체가 type scope를 이미 표현하므로 이름을 단순화하고, service type도 dedicated enum으로 고정한다. |
| `zlink_gateway_msg_kind_t` | 유지 | 메인 스펙 기준 unified gateway callback은 `REQUEST` / `REPLY` / `CONTROL` demux를 `kind`로 구분한다. |
| `typedef void (*zlink_gateway_handler_fn)(zlink_gateway_msg_kind_t kind, const char *service_name, size_t service_name_len, const zlink_routing_id_t *source_rid, zlink_msg_t *parts, size_t part_count)` | 유지 | unified gateway가 inbound request, outbound reply, control plane event를 한 handle에서 받는 구조에서는 `kind`와 `service_name`이 필요하다. |
| `zlink_gateway_new(void *ctx, void *discovery, const char *routing_id, zlink_gateway_handler_fn handler)` | 유지 | 메인 스펙의 unified gateway는 service-bound handle이 아니라 multi-service capable handle이다. 대표 identity는 `routing_id`이고, service name은 register/send 시점에 지정한다. |
| `zlink_gateway_register()` | 유지 | 메인 스펙 기준 gateway registration lifecycle은 계속 `gateway` handle이 담당한다. |
| `zlink_gateway_update_weight()` | 유지 | 현재 메인 스펙의 canonical weight update path다. peer 단위 weight API는 별도 후속 검토 후보로 남긴다. |
| `zlink_gateway_unregister()` | 유지 | unregister 역시 현재 메인 스펙 기준 `gateway` 책임이다. |
| `zlink_gateway_send(void *gateway, const char *service_name, zlink_msg_t *parts, size_t part_count, int flags)` | 유지 | unified gateway는 service-bound handle이 아니므로 per-send `service_name`이 필요하다. |
| `zlink_gateway_send_bytes(void *gateway, const char *service_name, const void *data, size_t size, int flags)` | 현재 스펙에서는 유지, 장기적으로 단순화 후보 | core surface를 `msg_t` 중심으로 줄이는 아이디어는 가능하지만 현재 메인 스펙은 기존 helper 유지와 충돌하지 않는다. |
| `zlink_gateway_send_rid(void *gateway, const char *service_name, const zlink_routing_id_t *routing_id, zlink_msg_t *parts, size_t part_count, int flags)` | 유지 | direct send/reply도 unified gateway에서는 service context를 유지하는 편이 메인 스펙과 맞다. |
| `zlink_gateway_send_rid_bytes(...)` | 현재 스펙에서는 유지, 장기적으로 단순화 후보 | 위와 동일. |
| `#define ZLINK_GATEWAY_LB_ROUND_ROBIN`, `#define ZLINK_GATEWAY_LB_WEIGHTED` | `typedef enum zlink_gateway_lb_strategy_t { ... } zlink_gateway_lb_strategy_t;` | 닫힌 값 집합이므로 macro보다 enum이 명확하다. |
| `zlink_gateway_set_lb_strategy(void *gateway, const char *service_name, int strategy)` | `strategy`는 `zlink_gateway_lb_strategy_t`로 typed enum화 검토, `service_name`은 유지 | 메인 스펙 기준 strategy는 service별 정책이므로 `service_name`을 유지해야 한다. |
| `zlink_gateway_peer_info_t` 신규 | 후속 검토 후보 | provider weight snapshot을 peer info로 노출하는 아이디어는 가능하지만, 메인 스펙의 현재 계약은 `zlink_peer_info_t` 유지다. |
| `zlink_gateway_connection_count(void *gateway, const char *service_name)` | 유지 | unified gateway는 service별 provider 집합을 다루므로 `service_name`이 필요하다. |
| `zlink_gateway_update_peer_weight()` | 후속 검토 후보 | 메인 스펙과 별개로 peer 단위 weight 조정 surface가 필요한지는 추가 검토할 수 있다. |
| `zlink_gateway_set_routing_id()` | 유지하되 “첫 `bind`/`connect`/`register` 이전에만 유효” 계약을 문서화 | identity 고정 원칙과 충돌하지 않게 representative RID setter의 허용 시점을 더 엄격히 적어야 한다. |
| `zlink_discovery_set_routing_id()` | 유지하되 “첫 subscribe/query/connect 이후 변경 불가” 계약 검토 | discovery도 representative RID setter를 두더라도 first-use 이전으로 제한하는 쪽이 맞다. |

권장 시그니처 예시:

```c
typedef void (*zlink_gateway_handler_fn) (
  zlink_gateway_msg_kind_t kind,
  const char *service_name,
  size_t service_name_len,
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *parts,
  size_t part_count);

typedef enum zlink_gateway_lb_strategy_t
{
  ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN = 0,
  ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED = 1
} zlink_gateway_lb_strategy_t;

void *zlink_discovery_new (void *ctx, zlink_service_type_t service_type);

void *zlink_gateway_new (void *ctx,
                         void *discovery,
                         const char *routing_id,
                         zlink_gateway_handler_fn handler);

int zlink_gateway_register (void *gateway,
                            const char *service_name,
                            const char *advertise_endpoint,
                            uint32_t weight);

int zlink_gateway_update_weight (void *gateway,
                                 const char *service_name,
                                 uint32_t weight);

int zlink_gateway_unregister (void *gateway,
                              const char *service_name);

int zlink_gateway_send (void *gateway,
                        const char *service_name,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);

int zlink_gateway_send_rid (void *gateway,
                            const char *service_name,
                            const zlink_routing_id_t *routing_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);

int zlink_gateway_connection_count (void *gateway,
                                    const char *service_name);

int zlink_gateway_set_lb_strategy (void *gateway,
                                   const char *service_name,
                                   zlink_gateway_lb_strategy_t strategy);
```

추가 규칙:

- unified `gateway`는 service-bound handle이 아니라 multi-service capable handle이다.
- callback의 `kind`와 `service_name`은 unified gateway의 inbound request / outbound reply /
  control plane event를 구분하기 위해 유지한다.
- registration lifecycle은 현재 메인 스펙 기준으로 `gateway`가 담당한다.
- 따라서 `ZLINK_GATEWAY_REGISTER_OK`,
  `ZLINK_GATEWAY_REGISTER_FAILED`,
  `ZLINK_GATEWAY_UNREGISTER_OK`,
  `ZLINK_GATEWAY_UNREGISTER_FAILED`
  monitor event도 gateway 공개 surface에 남긴다.
- representative routing id setter는 삭제보다 “first-use 이전에만 허용” 계약 명문화가 우선이다.
- peer-level weight snapshot/update surface는 현행 메인 스펙 범위 밖의 후속 검토 후보다.

## 2. Service option enum / 값 재배치 원칙

현재 header에는 다음처럼 서로 다른 family가 동일한 option 값 `1`부터 다시 시작하는
형태가 존재한다.

- `ZLINK_GATEWAY_OPT_*`
- `ZLINK_SPOT_PUB_OPT_*`
- `ZLINK_SPOT_SUB_OPT_*`

이 구조는 C API 함수 인자가 대부분 `int option`인 현실과 맞지 않는다.
다만 이 섹션은 "어떤 옵션을 남길지"를 다시 결정하는 문서가 아니라,
`service-option-surface-plan.ko.md`와
`spot-node-direct-facade-plan.ko.md`에서 남기기로 한 option을
typed enum과 전역 유일 값 체계로 옮길 때의
ABI shape 후보를 정리하는 섹션이다.
따라서 아래 내용은 support/`ENOTSUP` 매트릭스가 아니라
enum typing/값 배치 원칙으로 읽는다.

| 항목 | 변경안 | 이유 |
|---|---|---|
| option 정의 방식 | `#define` 나열 대신 `typedef enum ..._option_t` 사용 | option 집합의 소속과 의미를 타입 수준에서 더 명확히 한다. |
| option 정수 값 | enum type이 달라도 실제 정수 값은 전역에서 겹치지 않게 배정 | 구현 내부 switch/logging/binding layer에서 family 구분이 섞여도 충돌하지 않도록 한다. |
| gateway option setter | `zlink_gateway_set_option(void *gateway, zlink_gateway_option_t option, ...)` | family별 enum을 직접 받는다. |
| spot node pub/sub default option setter | `zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t option, ...)`, `zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t option, ...)` | node-owned default pub/sub option도 standalone pub/sub와 같은 enum namespace를 직접 사용한다. |
| unified `zlink_spot_set_option(..., role, option, ...)` | 장기적으로 `zlink_spot_set_pub_option()` / `zlink_spot_set_sub_option()`로 분리 권장 | `role + int option` 조합은 enum 타입 이점을 약화시킨다. role별 enum을 제대로 살리려면 setter도 분리하는 편이 낫다. |

권장 값 배정 예시:

| enum type | 값 대역 | 예시 |
|---|---|---|
| `zlink_gateway_option_t` | `0x2100` 대역 | `ZLINK_GATEWAY_OPT_SNDHWM = 0x2101` |
| `zlink_spot_pub_option_t` | `0x2200` 대역 | `ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201` |
| `zlink_spot_sub_option_t` | `0x2300` 대역 | `ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301` |

`spot_node`에 대해서는 별도 `zlink_spot_node_option_t`를 정의하지 않는다.

- `spot_node`는 pub/sub option을 직접 소유하는 별도 family가 아니다.
- node-owned default pub는 `zlink_spot_pub_option_t`를 사용한다.
- node-owned default sub는 `zlink_spot_sub_option_t`를 사용한다.
- 따라서 option enum은 `pub` / `sub` 두 개만 공개하고, `node`는 setter surface만 제공한다.

권장 enum 예시:

```c
typedef enum zlink_gateway_option_t
{
  ZLINK_GATEWAY_OPT_SNDHWM = 0x2101,
  ZLINK_GATEWAY_OPT_RCVHWM = 0x2102,
  ZLINK_GATEWAY_OPT_SNDTIMEO = 0x2103,
  ZLINK_GATEWAY_OPT_LINGER = 0x2104,
  ZLINK_GATEWAY_OPT_SNDBUF = 0x2105,
  ZLINK_GATEWAY_OPT_RCVBUF = 0x2106
} zlink_gateway_option_t;

typedef enum zlink_spot_pub_option_t
{
  ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201,
  ZLINK_SPOT_PUB_OPT_SNDTIMEO = 0x2202,
  ZLINK_SPOT_PUB_OPT_LINGER = 0x2203,
  ZLINK_SPOT_PUB_OPT_NODROP = 0x2204,
  ZLINK_SPOT_PUB_OPT_MODE = 0x2205,
  ZLINK_SPOT_PUB_OPT_QUEUE_HWM = 0x2206,
  ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY = 0x2207,
  ZLINK_SPOT_PUB_OPT_SNDBUF = 0x2208,
  ZLINK_SPOT_PUB_OPT_RCVBUF = 0x2209
} zlink_spot_pub_option_t;

typedef enum zlink_spot_sub_option_t
{
  ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301,
  ZLINK_SPOT_SUB_OPT_LINGER = 0x2302,
  ZLINK_SPOT_SUB_OPT_SNDBUF = 0x2303,
  ZLINK_SPOT_SUB_OPT_RCVBUF = 0x2304
} zlink_spot_sub_option_t;
```

정리:

- C API에서도 enum을 사용하는 것은 문제되지 않는다.
- 중요한 것은 "enum type을 분리"하는 것만이 아니라
  "실제 정수 값도 전역에서 겹치지 않게 유지"하는 것이다.
- 특히 binding과 logging, generic option dispatch helper를 고려하면
  값 대역을 family별로 미리 예약하는 방식이 가장 단순하다.
- 다만 option membership와 실제 public 지원/`ENOTSUP` 매트릭스는
  `service-option-surface-plan.ko.md`와
  `spot-node-direct-facade-plan.ko.md`가 우선하며,
  여기의 enum 예시는 naming/typing과 값 배치 방향을 보여주는 용도다.

## 3. SPOT 인터페이스 추가 재검토

`spot` 계열도 `gateway`와 비슷하게 "handle identity를 생성 시점에 고정하고,
data 전달은 `msg_t` 중심으로 단순화"하는 쪽이 더 일관적이다.

| 현재 인터페이스 | 변경안 | 변경 이유 |
|---|---|---|
| `zlink_spot_publish_bytes()` | 현재 스펙에서는 유지, 장기적으로 단순화 후보 | `msg_t` 중심 surface로 줄이는 아이디어는 가능하지만 현재 메인 스펙은 helper 유지와 충돌하지 않는다. |
| `zlink_spot_pub_publish_bytes()` | 현재 스펙에서는 유지, 장기적으로 단순화 후보 | 위와 동일. |
| `zlink_spot_node_publish_bytes()` | 현재 스펙에서는 유지, 장기적으로 단순화 후보 | 위와 동일. |
| `zlink_spot_set_option(void *spot, int role, int option, ...)` | `zlink_spot_set_pub_option()` / `zlink_spot_set_sub_option()`로 분리 | `role + int option` 조합은 role별 enum typing을 약화시킨다. pub/sub option namespace를 함수 시그니처에서 직접 분리하는 편이 명확하다. |
| `zlink_spot_peers(void *spot, int role, ...)` | generic API는 제거하고, unified facade용으로 `zlink_spot_peers_pub()` / `zlink_spot_peers_sub()`를 추가 | standalone handle용 `zlink_spot_pub_peers()` / `zlink_spot_sub_peers()`와 이름 충돌 없이, unified facade에서도 split peer API를 제공해야 C API에서 구현 가능하다. |
| `zlink_spot_node_register(void *node, const char *service_name, const char *advertise_endpoint)` | 유지 | 메인 스펙 기준 bind/connect/register/discovery/TLS 같은 node-global 동작은 계속 `spot_node_*` API가 담당한다. |
| `zlink_spot_node_unregister(void *node, const char *service_name)` | 유지 | 위와 동일. |
| `zlink_spot_node_set_pub_option(void *node, int option, ...)` | `zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t option, ...)` | node default pub option도 standalone `spot_pub`와 동일 enum을 사용해야 한다. |
| `zlink_spot_node_set_sub_option(void *node, int option, ...)` | `zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t option, ...)` | node default sub option도 standalone `spot_sub`와 동일 enum을 사용해야 한다. |
| `zlink_spot_pub_set_option(void *pub, int option, ...)` | `zlink_spot_pub_set_option(void *pub, zlink_spot_pub_option_t option, ...)` | standalone pub도 같은 pub option enum으로 고정한다. |
| `zlink_spot_sub_set_option(void *sub, int option, ...)` | `zlink_spot_sub_set_option(void *sub, zlink_spot_sub_option_t option, ...)` | standalone sub도 같은 sub option enum으로 고정한다. |
| `ZLINK_SPOT_NODE_PUB_MODE_*` / `QUEUE_HWM` / `QUEUE_FULL_POLICY` | typed enum 후보로는 유지, 실제 지원/`ENOTSUP`는 `spot-node-direct-facade-plan.ko.md`를 따른다 | enum naming과 runtime support 매트릭스를 분리해 적는다. |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` / `QUEUE_NODROP` / `QUEUE_FULL_POLICY` | 후보에서 제외 | 현재 canonical option surface에서는 삭제 대상으로 정리되므로 enum namespace 예시에도 남기지 않는다. |
| `zlink_spot_new(void *spot_node, int roles, zlink_spot_handler_fn handler)` | `zlink_spot_new(void *spot_node, zlink_spot_role_mask_t roles, zlink_spot_handler_fn handler)` | `roles`는 단일 selector가 아니라 bitmask다. 단일 role selector와 별도 타입으로 분리해야 의미가 명확하다. |
| `zlink_spot_pub_set_routing_id()` / `zlink_spot_sub_set_routing_id()` | 유지하되 “첫 publish/subscribe/connect 이전에만 유효” 계약 검토 | identity 고정 원칙과 충돌하지 않도록 first-use 이후 변경 금지를 문서화해야 한다. |

권장 시그니처 예시:

```c
/* Existing selector enum; keep as-is and reuse in public signatures. */
typedef enum zlink_spot_role_t
{
  ZLINK_SPOT_ROLE_PUB = 1,
  ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

typedef enum zlink_spot_role_mask_t
{
  ZLINK_SPOT_ROLE_MASK_PUB = 0x0001,
  ZLINK_SPOT_ROLE_MASK_SUB = 0x0002
} zlink_spot_role_mask_t;

typedef enum zlink_spot_pub_mode_t
{
  ZLINK_SPOT_PUB_MODE_SYNC = 0,
  ZLINK_SPOT_PUB_MODE_ASYNC = 1
} zlink_spot_pub_mode_t;

typedef enum zlink_spot_pub_queue_full_policy_t
{
  ZLINK_SPOT_PUB_QUEUE_FULL_EAGAIN = 0,
  ZLINK_SPOT_PUB_QUEUE_FULL_DROP = 1
} zlink_spot_pub_queue_full_policy_t;

void *zlink_spot_new (void *spot_node,
                      zlink_spot_role_mask_t roles,
                      zlink_spot_handler_fn handler);

int zlink_spot_publish (void *spot,
                        const char *topic_id,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);

int zlink_spot_node_register (void *node,
                              const char *service_name,
                              const char *advertise_endpoint);

int zlink_spot_node_unregister (void *node,
                                const char *service_name);

int zlink_spot_set_pub_option (void *spot,
                               zlink_spot_pub_option_t option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_set_sub_option (void *spot,
                               zlink_spot_sub_option_t option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_peers_pub (void *spot,
                          zlink_peer_info_t *peers,
                          size_t *count);

int zlink_spot_peers_sub (void *spot,
                          zlink_peer_info_t *peers,
                          size_t *count);

int zlink_spot_node_set_pub_option (void *node,
                                    zlink_spot_pub_option_t option,
                                    const void *optval,
                                    size_t optvallen);

int zlink_spot_node_set_sub_option (void *node,
                                    zlink_spot_sub_option_t option,
                                    const void *optval,
                                    size_t optvallen);

int zlink_spot_pub_set_option (void *pub,
                               zlink_spot_pub_option_t option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_sub_set_option (void *sub,
                               zlink_spot_sub_option_t option,
                               const void *optval,
                               size_t optvallen);
```

추가 원칙:

- `spot`의 `bytes` helper는 현재 메인 스펙에서는 유지하고,
  장기적으로 `msg_t` 중심 surface로 단순화할 수 있는 후보로 본다.
- `spot_node` default facade와 attached `spot` facade가 공존하더라도
  pub/sub option namespace는 동일 enum 체계를 공유하도록 정리한다.
- standalone `spot_pub` / `spot_sub`를 유지하더라도 unified `spot` facade와
  option/publish 계약이 다르게 보이지 않도록 맞춘다.
- `spot_node`는 별도 option namespace를 갖지 않는다.
  node-owned default pub/sub도 각각 `zlink_spot_pub_option_t`,
  `zlink_spot_sub_option_t`를 그대로 사용한다.
- 즉 `zlink_spot_node_option_t`는 도입하지 않는다.
- `spot_node`의 registration lifecycle은 계속 `spot_node_*` API가 담당한다.
- SPOT registration에는 gateway-style `weight`를 두지 않는다.
  SPOT는 load-balancing provider registry가 아니라 peer discovery/mesh 구성 정보의
  광고이기 때문이다.
- `zlink_spot_node_set_pub_option()`은 default pub와 future child pub에 적용되는
  baseline option setter로 정의한다.
- `zlink_spot_node_set_sub_option()`은 default sub와 future child sub에 적용되는
  baseline option setter로 정의한다.
- unified `zlink_spot_monitor_open()`은 attached unified `spot` facade를 위한 API로
  유지하고, split handle 사용자에게는 `zlink_spot_pub_monitor_open()` /
  `zlink_spot_sub_monitor_open()`을 병행 제공한다.
- unified facade의 peer 조회는 C API 이름 충돌을 피하기 위해
  `zlink_spot_peers_pub()` / `zlink_spot_peers_sub()`로 분리한다.
- `SpotPub` / `SpotSub` option의 실제 public 지원 여부와 `ENOTSUP` 항목은
  별도 option 계획 문서를 따르며, direct facade의 정확한 지원 매트릭스는
  `spot-node-direct-facade-plan.ko.md`를 우선한다.

## 4. 일반 socket option enum 재설계 검토

이제 raw socket option도 libzmq 숫자와의 정렬을 목표로 둘 필요가 없다고 본다.
핵심은 외부 ABI를 더 단순하고 견고하게 만드는 것이다.

따라서 일반 socket option도 service option과 같은 철학으로 재정리한다.

| 항목 | 변경안 | 이유 |
|---|---|---|
| `#define ZLINK_AFFINITY 4`, `#define ZLINK_ROUTING_ID 5`, ... | `typedef enum zlink_socket_option_t { ... } zlink_socket_option_t;` 로 교체 | raw socket option도 macro보다 enum이 더 명확하다. |
| raw socket option 값 | 기존 libzmq 계열 값 유지 의무 없음 | 외부 호환성보다 내부 일관성과 ABI 명확성이 우선이다. |
| raw socket option 값 배정 | 별도 전역 대역을 예약해 새로 재배치 | service option과 섞여도 값 충돌이 없게 한다. |
| `zlink_setsockopt(void *s, int option, ...)` | `zlink_setsockopt(void *s, zlink_socket_option_t option, ...)` | option 소속을 타입으로 드러낸다. |
| `zlink_getsockopt(void *s, int option, ...)` | `zlink_getsockopt(void *s, zlink_socket_option_t option, ...)` | getter도 동일. |
| 예시 enum 범위 | 현재 문서의 enum은 일부 예시이며 최종 header에서는 현행 raw socket option 전체를 포함해야 한다. | `RECONNECT_IVL`, `BACKLOG`, `ROUTER_MANDATORY`, TCP keepalive, TLS, XPUB, `BLOCKY` 등도 최종 enum에 포함되어야 한다. |

관련 함수 변경안:

| 현재 함수 | 변경안 | 이유 |
|---|---|---|
| `zlink_setsockopt(void *s, int option, const void *optval, size_t optvallen)` | `zlink_setsockopt(void *s, zlink_socket_option_t option, const void *optval, size_t optvallen)` | raw socket option namespace를 타입으로 고정한다. |
| `zlink_getsockopt(void *s, int option, void *optval, size_t *optvallen)` | `zlink_getsockopt(void *s, zlink_socket_option_t option, void *optval, size_t *optvallen)` | getter도 동일한 enum typing을 사용한다. |
| `zlink_socket(void *ctx, int type)` | `zlink_socket(void *ctx, zlink_socket_type_t type)` | socket type도 enum으로 고정해 invalid literal 사용 여지를 줄인다. |
| `zlink_socket_with_handler(void *ctx, int type, zlink_socket_msg_handler_fn handler)` | `zlink_socket_with_handler(void *ctx, zlink_socket_type_t type, zlink_socket_msg_handler_fn handler)` | recv-capable raw socket 생성자도 동일한 socket type enum을 사용한다. |
| `zlink_getsockopt(..., ZLINK_TYPE, ...)` | `zlink_getsockopt(..., ZLINK_SOCKOPT_TYPE, ...)` | option 값 재배치에 맞춰 type 조회도 새 socket option enum 값으로 정렬한다. |
| `zlink_getsockopt(..., ZLINK_EVENTS, ...)` | `zlink_getsockopt(..., ZLINK_SOCKOPT_EVENTS, ...)` | `EVENTS`도 raw socket option enum의 일부로 명시한다. |
| `zlink_getsockopt(..., ZLINK_FD, ...)` | `zlink_getsockopt(..., ZLINK_SOCKOPT_FD, ...)` | read-only socket option도 동일 enum 체계에 포함한다. |
| `zlink_getsockopt(..., ZLINK_LAST_ENDPOINT, ...)` | `zlink_getsockopt(..., ZLINK_SOCKOPT_LAST_ENDPOINT, ...)` | endpoint 조회 역시 socket option enum namespace에 포함한다. |

추가 정리 원칙:

- raw socket 관련 public 함수는 `int type`, `int option` 같은 loosely typed 인자를
  가능한 한 dedicated enum 타입으로 치환한다.
- `getsockopt/setsockopt`는 그대로 유지하되, 이름을 바꾸기보다 인자 타입을 강화하는
  쪽이 C ABI 변화 대비 효과가 크다.
- option value를 재배치할 때는 getter 전용/readonly 항목(`FD`, `EVENTS`, `TYPE`,
  `LAST_ENDPOINT`)도 별도 예외 없이 같은 enum에 포함한다.

권장 값 대역 예시:

| enum type | 값 대역 | 예시 |
|---|---|---|
| `zlink_socket_option_t` | `0x1100` 대역 | `ZLINK_SOCKOPT_AFFINITY = 0x1101` |
| `zlink_gateway_option_t` | `0x2100` 대역 | `ZLINK_GATEWAY_OPT_SNDHWM = 0x2101` |
| `zlink_spot_pub_option_t` | `0x2200` 대역 | `ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201` |
| `zlink_spot_sub_option_t` | `0x2300` 대역 | `ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301` |

권장 enum 일부 예시:

```c
typedef enum zlink_socket_type_t
{
  ZLINK_SOCKET_PAIR = 0x1001,
  ZLINK_SOCKET_PUB = 0x1002,
  ZLINK_SOCKET_SUB = 0x1003,
  ZLINK_SOCKET_DEALER = 0x1004,
  ZLINK_SOCKET_ROUTER = 0x1005,
  ZLINK_SOCKET_XPUB = 0x1006,
  ZLINK_SOCKET_XSUB = 0x1007,
  ZLINK_SOCKET_STREAM = 0x1008
} zlink_socket_type_t;

typedef enum zlink_socket_option_t
{
  ZLINK_SOCKOPT_AFFINITY = 0x1101,
  ZLINK_SOCKOPT_ROUTING_ID = 0x1102,
  ZLINK_SOCKOPT_SUBSCRIBE = 0x1103,
  ZLINK_SOCKOPT_UNSUBSCRIBE = 0x1104,
  ZLINK_SOCKOPT_SNDBUF = 0x1105,
  ZLINK_SOCKOPT_RCVBUF = 0x1106,
  ZLINK_SOCKOPT_FD = 0x1107,
  ZLINK_SOCKOPT_EVENTS = 0x1108,
  ZLINK_SOCKOPT_TYPE = 0x1109,
  ZLINK_SOCKOPT_LINGER = 0x110A,
  ZLINK_SOCKOPT_SNDHWM = 0x110B,
  ZLINK_SOCKOPT_RCVHWM = 0x110C,
  ZLINK_SOCKOPT_SNDTIMEO = 0x110D,
  ZLINK_SOCKOPT_LAST_ENDPOINT = 0x110E,
  ZLINK_SOCKOPT_TCP_NODELAY = 0x110F,
  ZLINK_SOCKOPT_ZMP_METADATA = 0x1110
} zlink_socket_option_t;

int zlink_setsockopt (void *s,
                      zlink_socket_option_t option,
                      const void *optval,
                      size_t optvallen);

int zlink_getsockopt (void *s,
                      zlink_socket_option_t option,
                      void *optval,
                      size_t *optvallen);

void *zlink_socket (void *ctx, zlink_socket_type_t type);

void *zlink_socket_with_handler (void *ctx,
                                 zlink_socket_type_t type,
                                 zlink_socket_msg_handler_fn handler);
```

위 예시는 방향 설명용 일부 목록이다.
실제 `zlink_socket_option_t` 최종안에는 현재 공개된 raw socket option 전체가 포함되어야 한다.

raw socket option에 대한 결론:

- 일반 socket option도 service option과 동일하게 값 재배치 대상이다.
- 즉 raw socket option도 "enum type화 + 전역 유일 값 재배치"가 적절하다.
- 기존 libzmq 숫자와의 정렬은 설계 목표에서 제외한다.

## 5. 기타 상수군 enum 후보

option 외에도 다음 상수군은 macro보다 enum이 더 자연스럽다.

| 상수군 | 권장 타입 |
|---|---|
| socket type (`ZLINK_PAIR`, `ZLINK_PUB`, ...) | `zlink_socket_type_t` |
| socket option (`ZLINK_AFFINITY`, `ZLINK_SNDHWM`, ...) | `zlink_socket_option_t` |
| service type (`ZLINK_SERVICE_TYPE_GATEWAY`, `ZLINK_SERVICE_TYPE_SPOT`) | `zlink_service_type_t` |
| service kind (`ZLINK_SERVICE_KIND_*`) | `zlink_service_kind_t` |
| gateway LB strategy | `zlink_gateway_lb_strategy_t` |
| gateway option | `zlink_gateway_option_t` |
| representative routing id policy | dedicated setter 유지, first-use restriction 강화 |
| SPOT role selector | 기존 `zlink_spot_role_t` 유지 |
| SPOT role bitmask | `zlink_spot_role_mask_t` |
| SPOT node option | 별도 enum 없음 |
| SPOT pub/sub option | `zlink_spot_pub_option_t`, `zlink_spot_sub_option_t` |
| SPOT pub mode / queue full policy | dedicated enum 추가 |
| send flags (`ZLINK_DONTWAIT`, `ZLINK_SNDMORE`) | `zlink_send_flags_t` |
| disconnect reason (`ZLINK_DISCONNECT_*`) | `zlink_disconnect_reason_t` |
| topology source (`ZLINK_TOPOLOGY_SOURCE_*`) | `zlink_topology_source_t` |
| topology state (`ZLINK_TOPOLOGY_STATE_*`) | `zlink_topology_state_t` |

관련 함수 변경안:

| 상수군 | 현재 함수 | 변경안 |
|---|---|---|
| socket type | `zlink_socket(void *ctx, int type)` | `zlink_socket(void *ctx, zlink_socket_type_t type)` |
| socket type | `zlink_socket_with_handler(void *ctx, int type, ...)` | `zlink_socket_with_handler(void *ctx, zlink_socket_type_t type, ...)` |
| service type | `zlink_discovery_new_typed(void *ctx, uint16_t service_type)` | `zlink_discovery_new(void *ctx, zlink_service_type_t service_type)` |
| service type | 내부적으로 service type을 받는 향후 service factory 계열 | 모두 `zlink_service_type_t` 사용 |
| service kind | `zlink_service_event_t.service_kind` | `zlink_service_kind_t` 사용 |
| service kind | `zlink_registry_topology_entry_t.service_kind` / `zlink_registry_topology_filter_t.service_kind` | `zlink_service_kind_t` 사용 |
| gateway LB strategy | `zlink_gateway_set_lb_strategy(void *gateway, const char *service_name, int strategy)` | `zlink_gateway_set_lb_strategy(void *gateway, const char *service_name, zlink_gateway_lb_strategy_t strategy)` |
| representative routing id policy | `zlink_discovery_set_routing_id(void *discovery, ...)` | first-use 이전에만 허용 |
| representative routing id policy | `zlink_gateway_set_routing_id(void *gateway, ...)` | first-use 이전에만 허용 |
| representative routing id policy | `zlink_spot_pub_set_routing_id(void *pub, ...)` | first publish/connect 이전에만 허용 |
| representative routing id policy | `zlink_spot_sub_set_routing_id(void *sub, ...)` | first subscribe/connect 이전에만 허용 |
| SPOT role selector | `zlink_spot_set_option(void *spot, int role, ...)` | 분리 API 사용 시 제거, 유지 시 `zlink_spot_role_t role` 사용 |
| SPOT role bitmask | `zlink_spot_new(void *spot_node, int roles, ...)` | `zlink_spot_new(void *spot_node, zlink_spot_role_mask_t roles, ...)` |
| SPOT pub option | `zlink_spot_node_set_pub_option(void *node, int option, ...)` | `zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t option, ...)` |
| SPOT pub option | `zlink_spot_pub_set_option(void *pub, int option, ...)` | `zlink_spot_pub_set_option(void *pub, zlink_spot_pub_option_t option, ...)` |
| SPOT sub option | `zlink_spot_node_set_sub_option(void *node, int option, ...)` | `zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t option, ...)` |
| SPOT sub option | `zlink_spot_sub_set_option(void *sub, int option, ...)` | `zlink_spot_sub_set_option(void *sub, zlink_spot_sub_option_t option, ...)` |
| poller event mask | `zlink_poller_add(..., short events)` | `zlink_poller_add(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_add_spot_sub(..., short events)` | `zlink_poller_add_spot_sub(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_add_spot_pub(..., short events)` | `zlink_poller_add_spot_pub(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_add_gateway(..., short events)` | `zlink_poller_add_gateway(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_add_fd(..., short events)` | `zlink_poller_add_fd(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_modify(..., short events)` | `zlink_poller_modify(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_modify_spot_sub(..., short events)` | `zlink_poller_modify_spot_sub(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_modify_spot_pub(..., short events)` | `zlink_poller_modify_spot_pub(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_modify_gateway(..., short events)` | `zlink_poller_modify_gateway(..., zlink_poller_event_mask_t events)` |
| poller event mask | `zlink_poller_modify_fd(..., short events)` | `zlink_poller_modify_fd(..., zlink_poller_event_mask_t events)` |
| monitor event mask | `zlink_socket_monitor_open(void *s, int events, ...)` | `zlink_socket_monitor_open(void *s, zlink_socket_monitor_event_mask_t events, ...)` |
| discovery monitor event mask | `zlink_discovery_monitor_open(void *discovery, int events, ...)` | `zlink_discovery_monitor_open(void *discovery, zlink_discovery_monitor_event_mask_t events, ...)` |
| gateway monitor event mask | `zlink_gateway_monitor_open(void *gateway, int events, ...)` | `zlink_gateway_monitor_open(void *gateway, zlink_gateway_monitor_event_mask_t events, ...)` |
| SPOT monitor event mask | `zlink_spot_monitor_open(void *spot, int role, int events, ...)` | `zlink_spot_monitor_open(void *spot, zlink_spot_role_t role, zlink_spot_monitor_event_mask_t events, ...)` |
| SPOT sub monitor event mask | `zlink_spot_sub_monitor_open(void *sub, int events, ...)` | `zlink_spot_sub_monitor_open(void *sub, zlink_spot_monitor_event_mask_t events, ...)` |
| SPOT pub monitor event mask | `zlink_spot_pub_monitor_open(void *pub, int events, ...)` | `zlink_spot_pub_monitor_open(void *pub, zlink_spot_monitor_event_mask_t events, ...)` |
| service event detail mask | `zlink_service_event_t.detail_flags` | `zlink_service_event_detail_mask_t` 값 집합으로 문서화 |
| send flags | `zlink_send(..., int flags)` 등 send 계열 전반 | `zlink_send_flags_t flags` 사용 |
| disconnect reason | `zlink_monitor_event_t.value`가 disconnect reason일 때 | `zlink_disconnect_reason_t` 값 집합으로 문서화 |
| topology source | `zlink_registry_topology_entry_t.source` / `zlink_registry_topology_filter_t.source` | `zlink_topology_source_t` 사용 |
| topology state | `zlink_registry_topology_entry_t.state` / `zlink_registry_topology_filter_t.state` | `zlink_topology_state_t` 사용 |

권장 타입 예시:

```c
typedef enum zlink_service_type_t
{
  ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
  ZLINK_SERVICE_TYPE_SPOT = 0x3002
} zlink_service_type_t;

typedef enum zlink_service_kind_t
{
  ZLINK_SERVICE_KIND_DISCOVERY = 1,
  ZLINK_SERVICE_KIND_GATEWAY = 2,
  ZLINK_SERVICE_KIND_SPOT_SUB = 3,
  ZLINK_SERVICE_KIND_SPOT_PUB = 4
} zlink_service_kind_t;

typedef enum zlink_spot_role_t
{
  ZLINK_SPOT_ROLE_PUB = 1,
  ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

typedef enum zlink_spot_role_mask_t
{
  ZLINK_SPOT_ROLE_MASK_PUB = 0x0001,
  ZLINK_SPOT_ROLE_MASK_SUB = 0x0002
} zlink_spot_role_mask_t;

typedef enum zlink_poller_event_mask_t
{
  ZLINK_POLLER_EVENT_OUT = 0x0002,
  ZLINK_POLLER_EVENT_ERR = 0x0004,
  ZLINK_POLLER_EVENT_PRI = 0x0008
} zlink_poller_event_mask_t;

typedef enum zlink_socket_monitor_event_mask_t
{
  ZLINK_SOCKET_MONITOR_EVENT_CONNECTED = 0x0001,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED = 0x0002,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED = 0x0004,
  ZLINK_SOCKET_MONITOR_EVENT_LISTENING = 0x0008,
  ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED = 0x0010,
  ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED = 0x0020,
  ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED = 0x0040,
  ZLINK_SOCKET_MONITOR_EVENT_CLOSED = 0x0080,
  ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED = 0x0100,
  ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED = 0x0200,
  ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED = 0x0400,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL = 0x0800,
  ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY = 0x1000,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL = 0x2000,
  ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH = 0x4000,
  ZLINK_SOCKET_MONITOR_EVENT_ALL = 0xFFFF
} zlink_socket_monitor_event_mask_t;

typedef enum zlink_discovery_monitor_event_mask_t
{
  ZLINK_DISCOVERY_MONITOR_EVENT_READY = (1u << 0),
  ZLINK_DISCOVERY_MONITOR_EVENT_LOST = (1u << 1),
  ZLINK_DISCOVERY_MONITOR_EVENT_ERROR = (1u << 4),
  ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP = (1u << 5),
  ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN = (1u << 6),
  ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED = (1u << 7),
  ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED = (1u << 17)
} zlink_discovery_monitor_event_mask_t;

typedef enum zlink_gateway_monitor_event_mask_t
{
  ZLINK_GATEWAY_MONITOR_EVENT_ERROR = (1u << 4),
  ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY = (1u << 8),
  ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST = (1u << 9),
  ZLINK_GATEWAY_MONITOR_EVENT_CONNECTION_COUNT_CHANGED = (1u << 10),
  ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP = (1u << 11),
  ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_DOWN = (1u << 12),
  ZLINK_GATEWAY_MONITOR_EVENT_CLOSED = (1u << 17),
  ZLINK_GATEWAY_MONITOR_EVENT_REGISTER_OK = (1u << 18),
  ZLINK_GATEWAY_MONITOR_EVENT_REGISTER_FAILED = (1u << 19),
  ZLINK_GATEWAY_MONITOR_EVENT_UNREGISTER_OK = (1u << 20),
  ZLINK_GATEWAY_MONITOR_EVENT_UNREGISTER_FAILED = (1u << 21)
} zlink_gateway_monitor_event_mask_t;

typedef enum zlink_spot_monitor_event_mask_t
{
  ZLINK_SPOT_MONITOR_EVENT_READY = (1u << 0),
  ZLINK_SPOT_MONITOR_EVENT_LOST = (1u << 1),
  ZLINK_SPOT_MONITOR_EVENT_PEER_UP = (1u << 2),
  ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN = (1u << 3),
  ZLINK_SPOT_MONITOR_EVENT_ERROR = (1u << 4),
  ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED = (1u << 13),
  ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY = (1u << 14),
  ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL = (1u << 15),
  ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED = (1u << 16),
  ZLINK_SPOT_MONITOR_EVENT_CLOSED = (1u << 17)
} zlink_spot_monitor_event_mask_t;

typedef enum zlink_service_event_detail_mask_t
{
  ZLINK_SERVICE_EVENT_DETAIL_SERVICE_NAME = 0x0001,
  ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT = 0x0002,
  ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID = 0x0004,
  ZLINK_SERVICE_EVENT_DETAIL_PEER_RID = 0x0008
} zlink_service_event_detail_mask_t;

typedef enum zlink_send_flags_t
{
  ZLINK_SEND_FLAG_DONTWAIT = 0x0001,
  ZLINK_SEND_FLAG_SNDMORE = 0x0002
} zlink_send_flags_t;

typedef enum zlink_disconnect_reason_t
{
  ZLINK_DISCONNECT_REASON_UNKNOWN = 0,
  ZLINK_DISCONNECT_REASON_LOCAL = 1,
  ZLINK_DISCONNECT_REASON_REMOTE = 2,
  ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED = 3,
  ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR = 4,
  ZLINK_DISCONNECT_REASON_CTX_TERM = 5
} zlink_disconnect_reason_t;

typedef enum zlink_topology_source_t
{
  ZLINK_TOPOLOGY_SOURCE_MANUAL = 1,
  ZLINK_TOPOLOGY_SOURCE_DISCOVERY = 2,
  ZLINK_TOPOLOGY_SOURCE_REGISTRY = 3
} zlink_topology_source_t;

typedef enum zlink_topology_state_t
{
  ZLINK_TOPOLOGY_STATE_DISCOVERED = 1,
  ZLINK_TOPOLOGY_STATE_CONNECTING = 2,
  ZLINK_TOPOLOGY_STATE_READY = 3,
  ZLINK_TOPOLOGY_STATE_LOST = 4,
  ZLINK_TOPOLOGY_STATE_ERROR = 5,
  ZLINK_TOPOLOGY_STATE_STOPPED = 6
} zlink_topology_state_t;
```

추가 정리 원칙:

- 단순 상수 집합이면 enum으로 승격하고, 관련 함수 시그니처도 같은 enum 타입을 받도록 맞춘다.
- bitmask 성격의 상수군도 enum으로 표현할 수 있지만, 문서와 이름에서 반드시 `*_mask_t` 또는
  `*_flags_t`임을 드러낸다.
- `int`, `short`, `uint16_t` 같은 원시 타입으로 상수 집합 의미를 암묵적으로 표현하는 방식을
  줄이는 것이 목표다.
- 다만 poller event bit, socket monitor bit, zero-init default 의미를 가지는 mode/policy처럼
  기존 비트/기본값 의미가 중요한 상수군은 enum으로 승격하더라도 기존 수치 값을 유지한다.
- service monitor 계열은 discovery / gateway / spot의 공개 이벤트 집합이 서로 다르므로
  공통 enum 하나로 뭉뚱그리지 않고 함수 family별 mask enum으로 분리한다.
- `gateway` monitor는 공통 `READY/LOST`나 `PEER_UP/DOWN` 대신
  `SERVICE_READY/SERVICE_LOST`, `ROUTE_UP/ROUTE_DOWN`,
  `REGISTER_OK/FAILED`, `UNREGISTER_OK/FAILED`를 공개한다.
  unified gateway는 multi-service capable handle이므로
  "특정 service route가 usable/unusable 해졌는지"가 핵심 의미이고,
  SPOT의 일반 peer lifecycle과 동일한 vocabulary를 강제할 필요가 없다.
- `zlink_service_event_t.detail_flags`도 별도 `zlink_service_event_detail_mask_t`로
  승격해 event mask와 detail mask를 섞지 않는다.

단, 아래는 bitmask 성격이 강하므로 enum으로 바꾸더라도 "flag enum"임을 문서에
명확히 적어야 한다.

- monitor event mask (`ZLINK_EVENT_*`)
- poller event mask (`ZLINK_POLLOUT`, `ZLINK_POLLERR`, `ZLINK_POLLPRI`)
- discovery / gateway / spot monitor event mask
- service event detail mask
