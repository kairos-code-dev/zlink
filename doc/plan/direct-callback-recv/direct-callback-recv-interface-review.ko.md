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

- 이 문서는 direct-callback-recv 재작성 범위의 canonical 인터페이스 문서다.
- 구현, 리팩토링, 테스트, 후속 계획 문서 정리는 이 문서를 기준으로 수행한다.
- [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md),
  [`service-option-surface-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/service-option-surface-plan.ko.md),
  [`spot-node-direct-facade-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/spot-node-direct-facade-plan.ko.md)
  를 포함한 다른 계획 문서는 참고 문서이며, 충돌 시 이 문서를 우선한다.
- 이 문서에 나오는 구체 숫자 값(`0x2101`, `0x1001`, `0x3001` 등)은
  이 문서 기준 canonical 값이다.

핵심 원칙:

- 이름은 가능한 한 짧고 직접적으로 유지한다.
- `gateway`의 recv callback shape는 `ROUTER` callback shape와 동일하게 유지하고,
  `service_name`은 handle 생성 시점 identity로만 고정한다.
- 생성 시점에 handle의 정체성(routing id, attach 대상 등)을 가능한 범위에서 고정한다.
- `send`/`recv` 데이터 전달 surface는 `zlink_msg_t` 중심으로 단순화한다.
- `msg_t` 중심 통일은 bytes convenience helper를 제거한다는 뜻이며,
  `zlink_msg_t` 생성/ownership API를 통한 zero-copy 경로는 유지한다.
- option 값은 각 service/socket family별 enum으로 분리하되,
  실제 정수 값은 전역에서 서로 겹치지 않게 배정한다.
- libzmq 숫자와의 정렬 자체는 목표가 아니다. 더 단순하고 견고한 public ABI를
  우선한다.

## 0. 독립 사용용 작업 기준

이 섹션은 새 컨텍스트에서 이 문서 하나만 열고 작업할 때 필요한 최소 canonical
snapshot이다.

- 아래 항목은 본 문서가 참조하던 상위 문서의 현재 결정 상태를 작업용으로 압축한 것이다.
- 구현/리팩토링/테스트 수정 시 우선 이 snapshot을 기준으로 판단한다.
- 코드와 이 문서가 어긋나면, 먼저 코드를 이 snapshot에 맞추거나 이 문서를 갱신해
  다시 일치시킨다.

### 0.1 현재 고정 전제

- recv-capable public surface는 callback-only 방향이다.
- public sync recv API는 유지 대상이 아니다.
- callback 제거 API는 두지 않는다.
- recv-capable raw socket/service/monitor는 생성 또는 open 시 callback 등록을 기본으로 한다.
- 생성/open 이후 callback 교체 API는 두지 않는다.
- monitor/open 계열은 최초 open 시 non-`NULL` callback 설치를 기본으로 한다.
- `gateway`는 service-bound handle이다.
- multipart callback family는 `source_rid + multipart parts` shape를 유지한다.
- `spot_node`는 `service_name`을 생성 시점에 받는 service-bound handle이다.
- `spot`은 service-bound `spot_node`에 attach되는 unified facade다.
- callback 시그니처는 subject 의미별로 분리한다.
  `gateway`는 raw `ROUTER`형 multipart callback을 재사용하고,
  unified `spot`은 `spot_sub`/raw `SUB`/`XSUB`와 같은 topic-aware callback을 재사용하며,
  raw `XPUB`은 subscribe/unsubscribe 전용 callback을 별도로 둔다.
- `spot_node`의 bind/connect/attach-discovery/TLS는 계속 `spot_node_*`가 담당하되,
  service identity는 `zlink_spot_node_new()`에서 고정한다.
- `SpotPub`은 publish-only facade라 public recv callback setter를 두지 않는다.
- representative routing id setter는 삭제보다 first-use restriction 강화를 우선한다.
- option/constant redesign의 목표는 typed enum과 값 대역 분리이지 libzmq 숫자 호환이 아니다.

### 0.2 Callback 시그니처 family

이 문서 기준 callback은 메시지 의미가 같은 subject끼리만 typedef를 공유하고,
raw socket public API는 `zlink_socket_handler_t` descriptor 하나로 이 family를 선택한다.

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *parts,
  size_t part_count);

typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid,
  const char *topic,
  size_t topic_len,
  zlink_msg_t *parts,
  size_t part_count);

typedef void (*zlink_xpub_handler_fn) (
  int subscribed,
  const uint8_t *topic,
  size_t topic_len);

typedef enum zlink_socket_handler_kind_t
{
  ZLINK_SOCKET_HANDLER_MSG = 0x1201,
  ZLINK_SOCKET_HANDLER_SPOT = 0x1202,
  ZLINK_SOCKET_HANDLER_XPUB = 0x1203
} zlink_socket_handler_kind_t;

typedef struct zlink_socket_handler_t
{
  zlink_socket_handler_kind_t kind;
  union
  {
    zlink_socket_msg_handler_fn msg;
    zlink_spot_handler_fn spot;
    zlink_xpub_handler_fn xpub;
  } fn;
} zlink_socket_handler_t;
```

- `zlink_socket_msg_handler_fn`
  - 대상: raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway`
  - 의미: routing id + multipart payload
- `zlink_spot_handler_fn`
  - 대상: unified `spot`, `spot_sub`, raw `SUB`, raw `XSUB`
  - 의미: source rid + topic + multipart payload
- `zlink_xpub_handler_fn`
  - 대상: raw `XPUB`
  - 의미: subscribe/unsubscribe control event

`gateway`는 `zlink_socket_msg_handler_fn`을 재사용하고,
`spot`은 `zlink_spot_handler_fn`을 재사용한다.

추가 규칙:

- `zlink_socket()`는 `zlink_socket_handler_t.kind`와
  대상 socket family를 엄격히 검증해야 한다.
- raw `PAIR` / `DEALER` / `ROUTER` / `STREAM`에는
  `ZLINK_SOCKET_HANDLER_MSG`만 허용한다.
- raw `SUB` / `XSUB`에는 `ZLINK_SOCKET_HANDLER_SPOT`만 허용한다.
- raw `XPUB`에는 `ZLINK_SOCKET_HANDLER_XPUB`만 허용한다.
- raw `PUB`은 send-only 타입이므로 생성 시 `handler == NULL`만 허용한다.
- family가 맞지 않는 socket에 생성/교체를 시도하면 실패해야 하며,
  이 문서 기준 `EINVAL`을 반환한다.
- callback 교체/제거 API는 두지 않는다.

추가 공통 규칙:

- raw socket 생성자는 `zlink_socket_handler_t` descriptor를 함께 받는
  `zlink_socket()` 하나로 두고, recv-capable 타입은 생성 시점에 family에 맞는
  non-`NULL` handler descriptor를 제공해야 한다.
- send-only 타입은 `PUB`만 본다.
- recv-capable raw socket을 애플리케이션이 사실상 send-only처럼 사용하더라도
  public 정책은 동일하다. 이 경우에도 family에 맞는 no-op callback을 등록하는 쪽을 canonical 사용법으로 본다.
- raw `XPUB`의 recv는 subscriber의 subscribe/unsubscribe control message를 받는 의미이므로
  dedicated `XPUB` callback이 필수다.

### 0.3 service option 지원 snapshot

이 표는 새 컨텍스트에서 option membership/support 판단을 위해 필요한 현재 요약본이다.

| family | 유지 | `ENOTSUP` | 삭제/비공개 |
|---|---|---|---|
| `Gateway` | `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `LINGER`, `SNDBUF`, `RCVBUF` | 없음 | `RCVTIMEO` |
| `SpotPub` | `SNDHWM`, `SNDTIMEO`, `LINGER`, `NODROP`, `SNDBUF`, `RCVBUF` | `MODE`, `QUEUE_HWM`, `QUEUE_FULL_POLICY` | 없음 |
| `SpotSub` | `RCVHWM`, `LINGER`, `SNDBUF`, `RCVBUF` | 없음 | `RCVTIMEO`, `QUEUE_NODROP`, `QUEUE_FULL_POLICY` |

추가 규칙:

- `spot_node`는 독립 option family를 갖지 않는다.
- `spot_node` pub/sub option setter는 각각 `SpotPub` / `SpotSub` enum을 그대로 쓴다.
- typed enum 도입은 support matrix를 바꾸는 작업이 아니라 existing support 결정을
  타입과 값 체계로 옮기는 작업이다.

### 0.4 작업 시 버리면 안 되는 의미

- `gateway`의 service-bound identity 제거 금지: `service_name`은 handle 생성 시점에 고정된 identity로 유지해야 한다.
- `gateway register/update_weight/unregister`는 제거한다: `gateway`는 data-plane handle로 제한한다.
- `spot_node register/unregister` 추가 금지: public `spot_node`도 `gateway`와 같은 레벨의 data-plane facade로 제한한다.
- `gateway` monitor의 `SERVICE_READY` / `SERVICE_LOST` / `ROUTE_UP` / `ROUTE_DOWN` event 제거 금지.
- `SpotPub` mode/queue policy 숫자의 zero-init 기본값 의미 변경 금지.
- monitor bitmask의 기존 bit 의미 변경 금지.

### 0.5 이 문서만으로 작업할 때 체크리스트

1. 새 API/타입을 추가할 때 먼저 loose `int` 인자를 dedicated enum으로 치환할 수 있는지 본다.
2. callback ABI를 만질 때는 생성 시 non-`NULL`, replace-only, no-remove 계약을 유지한다.
3. `gateway`와 `spot`에서 service/node identity가 생성 시점에 고정되는지 확인한다.
4. `bytes` helper는 public surface에서 삭제하고 `msg_t` 기반 surface만 유지한다.
5. bitmask 상수군은 portable C header 기준으로 `typedef uint32_t + typed macro` 형태로 확정한다.
6. 구현을 바꾼 뒤에는 이 문서의 표/확정 시그니처가 실제 코드와 다시 일치하는지 확인한다.

## 1. Discovery / Gateway 인터페이스 변경안

| 현재 인터페이스 | 변경안 | 변경 이유 |
|---|---|---|
| `void *zlink_discovery_new(void *ctx, zlink_service_type_t service_type)` | 유지 | 현재 public shape도 간결한 typed discovery 생성자이므로 canonical 문서에서는 이 이름과 타입을 유지한다. |
| `typedef void (*zlink_gateway_handler_fn)(zlink_gateway_msg_kind_t kind, const zlink_routing_id_t *source_rid, zlink_msg_t *parts, size_t part_count)` | 삭제하고 `zlink_socket_msg_handler_fn` 재사용 | 현행 public callback은 `kind`를 포함하지만, canonical 방향에서는 internal demux 정보를 사용자에게 노출하지 않고 raw socket 기본 handler를 재사용한다. |
| `void *zlink_gateway_new(void *ctx, const char *service_name, const char *routing_id, zlink_gateway_handler_fn handler)` | `void *zlink_gateway_new(void *ctx, const char *service_name, const char *routing_id, zlink_socket_msg_handler_fn handler)` | `gateway`는 service-bound identity만 생성 시점에 고정하고, discovery 연결은 선택적 attach 단계로 분리한다. callback typedef도 raw socket 기본 handler로 정리한다. |
| `int zlink_gateway_attach_discovery(void *gateway, void *discovery)` | 유지 | 현재 public surface도 attach 모델을 사용하므로, canonical 문서에서는 이 contract를 유지한 채 topology ownership 규칙만 명확히 한다. |
| `int zlink_gateway_register(void *gateway, const char *advertise_endpoint, uint32_t weight)` | 삭제 상태 유지 | public `gateway`는 data-plane/LB handle로 제한하고, 별도 public register API는 두지 않는다. |
| `int zlink_gateway_update_weight(void *gateway, uint32_t weight)` | 삭제 상태 유지 | 공개 가중치 변경 surface는 peer 단위 `zlink_gateway_update_peer_weight()` 하나로 정리한다. |
| `int zlink_gateway_unregister(void *gateway)` | 삭제 상태 유지 | unregister도 별도 public API로 두지 않고 discovery/registry runtime의 topology 수렴으로 처리한다. |
| `int zlink_gateway_send(void *gateway, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 유지 | 현재 public shape도 service-bound send surface이므로 per-send `service_name` 없이 유지한다. |
| `int zlink_gateway_send_bytes(void *gateway, const void *data, size_t size, zlink_send_flags_t flags)` | 삭제 상태 유지 | `gateway` send/recv public surface는 `msg_t` 기반으로만 유지한다. |
| `int zlink_gateway_send_rid(void *gateway, const zlink_routing_id_t *routing_id, zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags)` | 유지 | direct send/reply도 target RID만 필요하고 service context는 handle이 제공한다. |
| `int zlink_gateway_send_rid_bytes(void *gateway, const zlink_routing_id_t *routing_id, const void *data, size_t size, zlink_send_flags_t flags)` | 삭제 상태 유지 | direct send/reply도 `msg_t` 기반 surface만 남긴다. |
| `#define ZLINK_GATEWAY_LB_ROUND_ROBIN`, `#define ZLINK_GATEWAY_LB_WEIGHTED` | `typedef enum zlink_gateway_lb_strategy_t { ... } zlink_gateway_lb_strategy_t;` | 닫힌 값 집합이므로 macro보다 enum이 명확하다. |
| `int zlink_gateway_set_lb_strategy(void *gateway, int strategy)` | `int zlink_gateway_set_lb_strategy(void *gateway, zlink_gateway_lb_strategy_t strategy)` | strategy도 handle이 대표하는 단일 service에 적용하므로 `service_name`을 별도 인자로 받지 않는다. |
| `zlink_gateway_peer_info_t` 신규 | 적용 | `gateway`에서만 의미 있는 service peer weight를 공용 `zlink_peer_info_t`에 섞지 않고, gateway 전용 peer info struct로 분리한다. |
| `int zlink_gateway_connection_count(void *gateway, const char *service_name)` | `int zlink_gateway_connection_count(void *gateway)` | handle이 대표하는 service의 connected peer 집합만 집계하면 되므로 추가 `service_name`이 필요 없다. |
| `int zlink_gateway_peer_info(void *gateway, const zlink_routing_id_t *routing_id, zlink_peer_info_t *info)` | `int zlink_gateway_peer_info(void *gateway, const zlink_routing_id_t *routing_id, zlink_gateway_peer_info_t *info)` | gateway peer snapshot은 weight를 포함해야 하므로 gateway 전용 struct를 사용한다. |
| `int zlink_gateway_router_peers(void *gateway, zlink_peer_info_t *peers, size_t *count)` | `int zlink_gateway_router_peers(void *gateway, zlink_gateway_peer_info_t *peers, size_t *count)` | bulk peer query도 동일하게 gateway 전용 struct로 맞춘다. |
| `없음` | `int zlink_gateway_update_peer_weight(void *gateway, const zlink_routing_id_t *routing_id, uint32_t weight)` | peer 단위 weight 조정은 gateway의 LB domain과 직접 연결되므로 public surface로 제공한다. |
| `int zlink_gateway_set_routing_id(void *gateway, const void *data, size_t size)` | 유지하되 “첫 `bind`/`connect` 이전에만 유효” 계약을 문서화 | identity 고정 원칙과 충돌하지 않게 representative RID setter의 허용 시점을 더 엄격히 적어야 한다. |
| `int zlink_discovery_set_routing_id(void *discovery, const void *data, size_t size)` | 유지, 단 “첫 subscribe/query/connect 이전에만 유효”로 제한 | discovery도 representative RID setter를 두더라도 first-use 이전으로 제한한다. |

확정 시그니처:

```c
typedef enum zlink_gateway_lb_strategy_t
{
  ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN = 0,
  ZLINK_GATEWAY_LB_STRATEGY_WEIGHTED = 1
} zlink_gateway_lb_strategy_t;

typedef struct zlink_gateway_peer_info_t
{
  zlink_routing_id_t routing_id;
  char remote_addr[256];
  uint64_t connected_time;
  uint64_t msgs_sent;
  uint64_t msgs_received;
  uint64_t snd_pending_msgs;
  uint64_t rcv_pending_msgs;
  uint32_t weight;
} zlink_gateway_peer_info_t;

void *zlink_discovery_new (void *ctx, zlink_service_type_t service_type);

void *zlink_gateway_new (void *ctx,
                         const char *service_name,
                         const char *routing_id,
                         zlink_socket_msg_handler_fn handler);

int zlink_gateway_attach_discovery (void *gateway,
                                    void *discovery);

int zlink_gateway_send (void *gateway,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);

int zlink_gateway_send_rid (void *gateway,
                            const zlink_routing_id_t *routing_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            zlink_send_flags_t flags);

int zlink_gateway_connection_count (void *gateway);

int zlink_gateway_peer_info (void *gateway,
                             const zlink_routing_id_t *routing_id,
                             zlink_gateway_peer_info_t *info);

int zlink_gateway_router_peers (void *gateway,
                                zlink_gateway_peer_info_t *peers,
                                size_t *count);

int zlink_gateway_update_peer_weight (void *gateway,
                                      const zlink_routing_id_t *routing_id,
                                      uint32_t weight);

int zlink_gateway_set_lb_strategy (void *gateway,
                                   zlink_gateway_lb_strategy_t strategy);
```

추가 규칙:

- `gateway`는 service-bound handle이다.
- service name은 `zlink_gateway_new()`에서 고정되므로 callback이나 send 계열 함수에
  다시 노출하지 않는다.
- `gateway`는 생성 시 discovery를 받지 않는다.
- `zlink_gateway_attach_discovery()`는 선택적 topology source attach API다.
- discovery를 attach하지 않은 상태에서는 manual `connect` / `disconnect`를 허용한다.
- 이미 manual peer/route가 존재하는 상태에서는 `zlink_gateway_attach_discovery()`를 허용하지 않는다.
  이 경우 `EBUSY`를 반환해 topology ownership 전환 시점을 호출자가 먼저 정리하게 한다.
- discovery를 attach한 이후에는 topology ownership이 discovery로 넘어가므로
  manual `connect` / `disconnect`는 허용하지 않는다.
- `gateway` send/recv public surface는 `msg_t` 기반으로만 유지하고 `*_bytes` helper는 두지 않는다.
- 이는 bytes convenience helper 제거를 의미하며,
  `zlink_msg_t` ownership/zero-copy 경로 자체를 제거한다는 뜻은 아니다.
- `gateway` 공개 surface는 data-plane/LB handle로 제한하고, 별도 public register/unregister API는 두지 않는다.
- `discovery`는 이미 service type / service name scope, representative identity,
  registry bootstrap/control-plane 연결 정보를 가지며, attach 후 runtime 내부 동기화의 anchor 역할을 한다.
- local bound route의 초기 advertisement 정보는 service-bound `gateway`의 `service_name`,
  `routing_id`, 그리고 `zlink_gateway_bind()`로 확정된 bound endpoint 조합으로 결정한다.
- local bound route의 초기/default weight는 `0`으로 시작한다.
- local bound route의 advertised weight 변경도 별도 전용 public API를 두지 않고
  `zlink_gateway_update_peer_weight()` 하나로 통일한다.
  호출 `routing_id`가 local representative routing id와 일치하면 local bound route weight update로
  해석하고, 그렇지 않으면 remote service peer weight update로 해석한다.
- 즉 별도 public `register()` 호출 없이도 runtime은 bound endpoint와 local weight state를
  discovery/registry 경로에 반영할 수 있어야 한다.
- 공개 가중치 변경 surface는 `zlink_gateway_update_peer_weight()` 하나로 정리한다.
- 따라서 topology/control/status 성격 정보는 `gateway` recv callback이 아니라 monitor/event 경로로 공개한다.
- `gateway` monitor는 register/unregister 결과 이벤트를 공개하지 않는다.
- representative routing id setter는 삭제보다 “first `bind`/`connect` 이전에만 허용”
  계약 명문화가 우선이다.
- peer-level weight snapshot은 `zlink_gateway_peer_info_t` / `zlink_gateway_router_peers()`
  로 공개한다.
- `zlink_gateway_update_peer_weight()`는 peer 단위 authoritative weight update 요청으로 본다.
- local representative routing id를 대상으로 한 `zlink_gateway_update_peer_weight()`도
  유효하며, local bound route advertised weight를 갱신하는 canonical public 경로로 본다.
- `zlink_gateway_update_peer_weight()` 성공 후에는 호출 `gateway`의 local LB snapshot이 먼저 갱신되고,
  이어서 discovery/registry runtime을 통해 같은 service를 보는 다른 handle에도 동기화되어야 한다.
- unknown `routing_id`에 대한 `zlink_gateway_update_peer_weight()`는 `ENOENT`를 반환하는 쪽이 자연스럽다.

관련 테스트 정리:

- `zlink_gateway_peer_info()`가 `weight`를 포함한 `zlink_gateway_peer_info_t`를 반환한다.
- `zlink_gateway_router_peers()` bulk snapshot에도 각 peer `weight`가 포함된다.
- `zlink_gateway_update_peer_weight()` 성공 후 같은 `gateway`의 peer snapshot이 즉시 갱신된다.
- 같은 service를 보는 다른 `gateway`/discovery view는 discovery/registry 동기화 후 변경된 `weight`를 관찰한다.
- 존재하지 않는 `routing_id`에 대한 `zlink_gateway_update_peer_weight()`는 `ENOENT`를 반환한다.

### 1.1 Discovery / Registry Transport 정책

- discovery/registry control-plane 연결에서 허용하는 transport는
  `tcp`, `ws`, `wss`, `tls` 네 가지로 제한한다.
- 이 정책은 `zlink_discovery_connect_registry()`와 registry peer sync에 모두 적용한다.
- registry peer sync는 각 registry가 광고한 peer PUB endpoint를 기준으로 연결되며,
  허용 transport 안에서는 endpoint별 mixed deployment를 코드상 허용한다.
- 다만 mixed transport deployment는 운영 복잡도와 장애 분석 비용을 늘리므로
  비권장으로 본다. 동일 registry cluster는 가능한 한 하나의 transport family로
  통일하는 쪽을 canonical 운영 정책으로 본다.
- `ipc`, `inproc`, `pgm`, `epgm`, `tipc` 등 허용 목록 밖 transport는 public canonical
  범위에 포함하지 않는다.
- 허용되지 않은 transport scheme로
  `zlink_discovery_connect_registry()` 또는 registry peer endpoint 구성을 시도하면
  즉시 실패해야 하며, 이 문서 기준 `EPROTONOSUPPORT`를 반환한다.

## 2. Service option enum / 값 재배치 원칙

현재 header에는 다음처럼 서로 다른 family가 동일한 option 값 `1`부터 다시 시작하는
형태가 존재한다.

- `ZLINK_GATEWAY_OPT_*`
- `ZLINK_SPOT_PUB_OPT_*`
- `ZLINK_SPOT_SUB_OPT_*`

이 구조는 C API 함수 인자가 대부분 `int option`인 현실과 맞지 않는다.
이 섹션은 어떤 service option을 public surface에 남길지,
각 option의 support/`ENOTSUP` 상태를 어떻게 둘지,
그리고 그 값을 어떤 typed enum/전역 유일 값 체계로 배치할지를
이 문서 기준으로 확정한다.
따라서 아래 내용은 enum typing/값 배치 원칙과 support 매트릭스를 함께 포함하는
canonical 계약으로 읽는다.

| 항목 | 변경안 | 이유 |
|---|---|---|
| option 정의 방식 | `#define` 나열 대신 `typedef enum ..._option_t` 사용 | option 집합의 소속과 의미를 타입 수준에서 더 명확히 한다. |
| option 정수 값 | enum type이 달라도 실제 정수 값은 전역에서 겹치지 않게 배정 | 구현 내부 switch/logging/binding layer에서 family 구분이 섞여도 충돌하지 않도록 한다. |
| gateway option setter | `zlink_gateway_set_option(void *gateway, zlink_gateway_option_t option, ...)` | family별 enum을 직접 받는다. |
| spot node pub/sub option setter | `zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t option, ...)`, `zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t option, ...)` | node 내부 pub/sub option도 standalone pub/sub와 같은 enum namespace를 직접 사용한다. |
| unified `zlink_spot_set_option(..., role, option, ...)` | `zlink_spot_set_pub_option()` / `zlink_spot_set_sub_option()`로 분리 | `role + int option` 조합은 enum 타입 이점을 약화시킨다. role별 enum을 제대로 살리려면 setter도 분리하는 편이 낫다. |

확정 값 배정:

주의:

- 아래 표와 코드 블록의 숫자 값은 이 문서 기준 canonical 값이다.
- service option도 raw socket option과 동일하게 16진수 대역을 고정하고,
  family 간 값 충돌이 없도록 순차 배정한다.

| enum type | 값 대역 | 대표 값 |
|---|---|---|
| `zlink_gateway_option_t` | `0x2100` 대역 | `ZLINK_GATEWAY_OPT_SNDHWM = 0x2101` |
| `zlink_spot_pub_option_t` | `0x2200` 대역 | `ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201` |
| `zlink_spot_sub_option_t` | `0x2300` 대역 | `ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301` |

`spot_node`에 대해서는 별도 `zlink_spot_node_option_t`를 정의하지 않는다.

- `spot_node`는 pub/sub option을 직접 소유하는 별도 family가 아니다.
- node 내부 pub는 `zlink_spot_pub_option_t`를 사용한다.
- node 내부 sub는 `zlink_spot_sub_option_t`를 사용한다.
- 따라서 option enum은 `pub` / `sub` 두 개만 공개하고, `node`는 setter surface만 제공한다.

확정 enum:

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
- option membership와 실제 public 지원/`ENOTSUP` 매트릭스도 이 문서 기준으로 읽는다.
- 이 문서에 남겨진 enum member와 숫자 값 자체는 canonical 값으로 본다.
- 따라서 `zlink_gateway_option_t` enum에서
  `ZLINK_GATEWAY_OPT_RCVTIMEO`를 제외하고 `ZLINK_GATEWAY_OPT_SNDBUF` /
  `ZLINK_GATEWAY_OPT_RCVBUF`를 포함한 것은
  이 문서의 canonical 결정이다.
- `ZLINK_SPOT_PUB_OPT_RCVBUF` / `ZLINK_SPOT_SUB_OPT_SNDBUF`처럼
  facade의 논리적 publish/subscribe 방향과 직관이 어긋나는 transport buffer 항목은
  이 문서의 canonical 결정으로 유지한다.
  이 값들은 "pub이 recv를 한다 / sub가 send를 한다"는 의미가 아니라,
  underlying full-duplex transport socket의 OS buffer tuning을 public surface에 남길지
  여부를 표현하는 항목으로 읽는다.

## 3. SPOT 인터페이스 추가 재검토

`spot` 계열도 `gateway`와 비슷하게 "handle identity를 생성 시점에 고정하고,
data 전달은 `msg_t` 중심으로 단순화"하는 쪽이 더 일관적이다.

| 현재 인터페이스 | 변경안 | 변경 이유 |
|---|---|---|
| `int zlink_spot_publish_bytes(void *spot, const char *topic_id, const void *data, size_t size, zlink_send_flags_t flags)` | 삭제 상태 유지 | `spot` public send/recv surface도 `msg_t` 기반으로만 유지한다. |
| `int zlink_spot_pub_publish_bytes(void *pub, const char *topic_id, const void *data, size_t size, zlink_send_flags_t flags)` | 삭제 상태 유지 | standalone `spot_pub`도 `msg_t` 기반 surface만 유지한다. |
| `int zlink_spot_node_publish_bytes(void *node, const char *topic_id, const void *data, size_t size, zlink_send_flags_t flags)` | 삭제 상태 유지 | `spot_node` publish 경로도 `msg_t` 기반 surface만 유지한다. |
| `void *zlink_spot_node_new(void *ctx, const char *service_name, zlink_spot_handler_fn handler)` | 유지 | `spot_node`는 service-bound handle이므로 `service_name`을 생성 시점에 고정하고, recv-capable surface이므로 최초 handler도 생성 시점에 받는다. |
| `int zlink_spot_set_pub_option(void *spot, zlink_spot_pub_option_t option, const void *optval, size_t optvallen)`, `int zlink_spot_set_sub_option(void *spot, zlink_spot_sub_option_t option, const void *optval, size_t optvallen)` | 유지 | pub/sub option namespace를 함수 시그니처에서 직접 분리하는 현재 public shape를 유지한다. |
| `int zlink_spot_peers_pub(void *spot, zlink_peer_info_t *peers, size_t *count)`, `int zlink_spot_peers_sub(void *spot, zlink_peer_info_t *peers, size_t *count)` | 유지 | unified facade에서도 split peer API를 제공하는 현재 public shape를 유지한다. |
| `int zlink_spot_node_register(void *node, const char *advertise_endpoint)` | 삭제 상태 유지 | public `spot_node`도 `gateway`와 같은 레벨로 맞추고 별도 public register API는 두지 않는다. discovery/runtime이 service-bound node identity를 기준으로 topology를 수렴한다. |
| `int zlink_spot_node_unregister(void *node)` | 삭제 상태 유지 | unregister도 별도 public API로 두지 않는다. |
| `int zlink_spot_node_attach_discovery(void *node, void *discovery)` | 유지 | discovery watch 대상 service도 `spot_node` 생성 시점 identity를 사용하므로 service 인자를 받지 않는 attach API를 유지한다. |
| `int zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t option, const void *optval, size_t optvallen)` | 유지 | node pub option도 standalone `spot_pub`와 동일 enum을 사용한다. |
| `int zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t option, const void *optval, size_t optvallen)` | 유지 | node sub option도 standalone `spot_sub`와 동일 enum을 사용한다. |
| `void *zlink_spot_node_default_pub(void *node)` | 삭제 | `spot_node` 내부 lazy child handle을 public에 노출하지 않는다. public split child accessor는 unified `spot` / `spot_node_*` contract와 충돌하므로 제거한다. |
| `void *zlink_spot_node_default_sub(void *node)` | 삭제 | 위와 동일. |
| `int zlink_spot_pub_set_option(void *pub, zlink_spot_pub_option_t pub_option, const void *optval, size_t optvallen)` | 유지 | standalone pub도 같은 pub option enum을 유지한다. |
| `int zlink_spot_sub_set_option(void *sub, zlink_spot_sub_option_t sub_option, const void *optval, size_t optvallen)` | 유지 | standalone sub도 같은 sub option enum을 유지한다. |
| `ZLINK_SPOT_NODE_PUB_MODE_*` / `QUEUE_HWM` / `QUEUE_FULL_POLICY` | typed enum으로 유지, 실제 public 지원/`ENOTSUP`도 이 문서 기준으로 고정 | enum naming과 runtime support 매트릭스를 같은 canonical 문서에서 함께 고정한다. |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` / `QUEUE_NODROP` / `QUEUE_FULL_POLICY` | 삭제/비공개 | 현재 canonical option surface에서는 삭제 대상으로 확정하므로 enum namespace에도 남기지 않는다. |
| `void *zlink_spot_new(void *spot_node, zlink_spot_handler_fn handler)` | 유지 | unified `spot`은 내부에 pub/sub를 함께 가진 facade이므로 생성 시 역할을 고르지 않는다. |
| `int zlink_spot_pub_set_routing_id(void *pub, const void *data, size_t size)` / `int zlink_spot_sub_set_routing_id(void *sub, const void *data, size_t size)` | 유지, 단 “첫 publish/subscribe/connect 이전에만 유효”로 제한 | identity 고정 원칙과 충돌하지 않도록 first-use 이후 변경을 금지한다. |

확정 시그니처:

```c
/* Existing selector enum; keep as-is and reuse in public signatures. */
typedef enum zlink_spot_role_t
{
  ZLINK_SPOT_ROLE_PUB = 1,
  ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

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
                      zlink_spot_handler_fn handler);

void *zlink_spot_node_new (void *ctx,
                           const char *service_name,
                           zlink_spot_handler_fn handler);

int zlink_spot_publish (void *spot,
                        const char *topic_id,
                        zlink_msg_t *parts,
                        size_t part_count,
                        zlink_send_flags_t flags);

int zlink_spot_node_attach_discovery (void *node,
                                      void *discovery);

int zlink_spot_set_pub_option (void *spot,
                               zlink_spot_pub_option_t spot_option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_set_sub_option (void *spot,
                               zlink_spot_sub_option_t spot_option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_peers_pub (void *spot,
                          zlink_peer_info_t *peers,
                          size_t *count);

int zlink_spot_peers_sub (void *spot,
                          zlink_peer_info_t *peers,
                          size_t *count);

int zlink_spot_node_set_pub_option (void *node,
                                    zlink_spot_pub_option_t pub_option,
                                    const void *optval,
                                    size_t optvallen);

int zlink_spot_node_set_sub_option (void *node,
                                    zlink_spot_sub_option_t sub_option,
                                    const void *optval,
                                    size_t optvallen);

int zlink_spot_pub_set_option (void *pub,
                               zlink_spot_pub_option_t pub_option,
                               const void *optval,
                               size_t optvallen);

int zlink_spot_sub_set_option (void *sub,
                               zlink_spot_sub_option_t sub_option,
                               const void *optval,
                               size_t optvallen);

```

추가 원칙:

- `spot` public send/recv surface도 `msg_t` 기반으로만 유지하고 `*_bytes` helper는 두지 않는다.
- 이는 bytes convenience helper 제거를 의미하며,
  `zlink_msg_t` ownership/zero-copy 경로 자체를 제거한다는 뜻은 아니다.
- `spot_node`는 `service_name`을 생성 시점에 받는 service-bound handle이다.
- `spot` / `spot_pub` / `spot_sub`는 이미 service-bound인 `spot_node`에 attach되는 facade다.
- unified `spot`은 생성 시 역할을 선택하지 않으며, 항상 pub/sub를 함께 가진 facade로 본다.
- `spot_node` 내부 pub/sub와 attached `spot` facade가 공존하더라도
  pub/sub option namespace는 동일 enum 체계를 공유하도록 정리한다.
- standalone `spot_pub` / `spot_sub`를 유지하더라도 unified `spot` facade와
  option/publish 계약이 다르게 보이지 않도록 맞춘다.
- `spot_node`는 별도 option namespace를 갖지 않는다.
  node 내부 pub/sub도 각각 `zlink_spot_pub_option_t`,
  `zlink_spot_sub_option_t`를 그대로 사용한다.
- 즉 `zlink_spot_node_option_t`는 도입하지 않는다.
- `spot_node`의 discovery attach lifecycle은 계속 `spot_node_*` API가 담당한다.
- 이때 service identity는 discovery attach 호출이 아니라
  `zlink_spot_node_new()`에서 고정된다는 점을 `gateway`의 service-bound 방향과 같은 축으로 본다.
- SPOT registration에는 gateway-style `weight`를 두지 않는다.
  SPOT는 load-balancing service-peer registry가 아니라 peer discovery/mesh 구성 정보의
  광고이기 때문이다.
- `zlink_spot_node_set_pub_option()`은 attach된 child pub에 적용되는
  baseline option setter로 정의한다.
- `zlink_spot_node_set_sub_option()`은 attach된 child sub에 적용되는
  baseline option setter로 정의한다.
- public `spot_node`는 별도 `register()` / `unregister()` surface를 두지 않는다.
- `zlink_spot_node_attach_discovery()`는 service name을 인자로 받지 않는다.
  대상 service는 `spot_node` 생성 시점 identity를 그대로 사용한다.
- discovery를 attach하지 않은 상태에서는 manual `connect` / `disconnect`를 허용한다.
- 이미 manual peer가 존재하는 상태에서는 `zlink_spot_node_attach_discovery()`를 허용하지 않는다.
  이 경우 `EBUSY`를 반환해 topology ownership 전환 시점을 호출자가 먼저 정리하게 한다.
- discovery를 attach한 이후에는 topology ownership이 discovery로 넘어가므로
  manual `connect` / `disconnect`는 허용하지 않는다.
- unified `zlink_spot_monitor_open()`은 attached unified `spot` facade를 위한 API로
  유지하고, split handle 사용자에게는 `zlink_spot_pub_monitor_open()` /
  `zlink_spot_sub_monitor_open()`을 병행 제공한다.
- unified facade의 peer 조회는 C API 이름 충돌을 피하기 위해
  `zlink_spot_peers_pub()` / `zlink_spot_peers_sub()`로 분리한다.
- unified `spot` naming policy는 다음으로 고정하는 편이 일관적이다.
  - query/list API는 return subject를 suffix로 드러낸다:
    `*_peers_pub()`, `*_peers_sub()`
  - mutator/setter API는 verb 단계에서 subject를 분리한다:
    `*_set_pub_option()`, `*_set_sub_option()`
  - monitor open은 unified facade entrypoint를 유지하되,
    attached facade 자체가 단일 handle이므로 `role` selector를 인자로 받는다:
    `zlink_spot_monitor_open(..., role, ...)`
- `SpotPub` / `SpotSub` option의 실제 public 지원 여부와 `ENOTSUP` 항목도
  이 문서 기준 canonical 계약으로 읽는다.

## 4. 일반 socket option enum 재설계 검토

이제 raw socket option도 libzmq 숫자와의 정렬을 목표로 둘 필요가 없다고 본다.
핵심은 외부 ABI를 더 단순하고 견고하게 만드는 것이다.

따라서 일반 socket option도 service option과 같은 철학으로 재정리한다.

| 항목 | 변경안 | 이유 |
|---|---|---|
| `#define ZLINK_AFFINITY 4`, `#define ZLINK_ROUTING_ID 5`, ... | `typedef enum zlink_socket_option_t { ... } zlink_socket_option_t;` 로 교체 | raw socket option도 macro보다 enum이 더 명확하다. |
| raw socket option 값 | 기존 libzmq 계열 값 유지 의무 없음 | 외부 호환성보다 내부 일관성과 ABI 명확성이 우선이다. |
| raw socket option 값 배정 | 별도 전역 대역을 예약해 새로 재배치 | service option과 섞여도 값 충돌이 없게 한다. |
| `int zlink_setsockopt(void *s, int option, const void *optval, size_t optvallen)` | `int zlink_setsockopt(void *s, zlink_socket_option_t option, const void *optval, size_t optvallen)` | option 소속을 타입으로 드러낸다. |
| `int zlink_getsockopt(void *s, int option, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, zlink_socket_option_t option, void *optval, size_t *optvallen)` | getter도 동일. |
| enum 범위 | 현행 raw socket option 전체를 `zlink_socket_option_t`에 포함한다. | `RECONNECT_IVL`, `BACKLOG`, `ROUTER_MANDATORY`, TCP keepalive, TLS, XPUB, `BLOCKY` 등까지 모두 포함한다. |

관련 함수 변경안:

| 현재 함수 | 변경안 | 이유 |
|---|---|---|
| `int zlink_setsockopt(void *s, int option, const void *optval, size_t optvallen)` | `int zlink_setsockopt(void *s, zlink_socket_option_t option, const void *optval, size_t optvallen)` | raw socket option namespace를 타입으로 고정한다. |
| `int zlink_getsockopt(void *s, int option, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, zlink_socket_option_t option, void *optval, size_t *optvallen)` | getter도 동일한 enum typing을 사용한다. |
| `void *zlink_socket(void *ctx, zlink_socket_type_t type, zlink_socket_msg_handler_fn handler)` | `void *zlink_socket(void *ctx, zlink_socket_type_t type, const zlink_socket_handler_t *handler)` | raw socket 생성 시점에 handler family를 함께 확정하고, constructor에서 socket type과 handler kind를 즉시 검증한다. |
| `int zlink_socket_set_msg_handler(void *s, zlink_socket_msg_handler_fn handler)` 계열 | 삭제 | raw socket callback은 생성 시점에만 설치한다. |
| `int zlink_getsockopt(void *s, ZLINK_TYPE, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, ZLINK_SOCKOPT_TYPE, void *optval, size_t *optvallen)` | option 값 재배치에 맞춰 type 조회도 새 socket option enum 값으로 정렬한다. |
| `int zlink_getsockopt(void *s, ZLINK_EVENTS, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, ZLINK_SOCKOPT_EVENTS, void *optval, size_t *optvallen)` | `EVENTS`도 raw socket option enum의 일부로 명시한다. |
| `int zlink_getsockopt(void *s, ZLINK_FD, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, ZLINK_SOCKOPT_FD, void *optval, size_t *optvallen)` | read-only socket option도 동일 enum 체계에 포함한다. |
| `int zlink_getsockopt(void *s, ZLINK_LAST_ENDPOINT, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, ZLINK_SOCKOPT_LAST_ENDPOINT, void *optval, size_t *optvallen)` | endpoint 조회 역시 socket option enum namespace에 포함한다. |

추가 정리 원칙:

- raw socket 관련 public 함수는 `int type`, `int option` 같은 loosely typed 인자를
  가능한 한 dedicated enum 타입으로 치환한다.
- `getsockopt/setsockopt`는 그대로 유지하되, 이름을 바꾸기보다 인자 타입을 강화하는
  쪽이 C ABI 변화 대비 효과가 크다.
- option value를 재배치할 때는 getter 전용/readonly 항목(`FD`, `EVENTS`, `TYPE`,
  `LAST_ENDPOINT`)도 별도 예외 없이 같은 enum에 포함한다.

확정 값 대역:

주의:

- 아래 값 대역은 이 문서 기준 최종 배치 원칙으로 읽는다.
- raw socket / gateway / spot pub / spot sub option은 서로 겹치지 않는
  16진수 대역으로 재정의한다.
- 기존 libzmq/현행 헤더 숫자를 유지하려고 하지 않는다.

| enum type | 값 대역 | 대표 값 |
|---|---|---|
| `zlink_socket_option_t` | `0x1100` 대역 | `ZLINK_SOCKOPT_AFFINITY = 0x1101` |
| `zlink_socket_handler_kind_t` | `0x1200` 대역 | `ZLINK_SOCKET_HANDLER_MSG = 0x1201` |
| `zlink_gateway_option_t` | `0x2100` 대역 | `ZLINK_GATEWAY_OPT_SNDHWM = 0x2101` |
| `zlink_spot_pub_option_t` | `0x2200` 대역 | `ZLINK_SPOT_PUB_OPT_SNDHWM = 0x2201` |
| `zlink_spot_sub_option_t` | `0x2300` 대역 | `ZLINK_SPOT_SUB_OPT_RCVHWM = 0x2301` |

확정 enum:

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

typedef enum zlink_socket_handler_kind_t
{
  ZLINK_SOCKET_HANDLER_MSG = 0x1201,
  ZLINK_SOCKET_HANDLER_SPOT = 0x1202,
  ZLINK_SOCKET_HANDLER_XPUB = 0x1203
} zlink_socket_handler_kind_t;

typedef enum zlink_socket_option_t
{
  ZLINK_SOCKOPT_AFFINITY = 0x1101,
  ZLINK_SOCKOPT_ROUTING_ID = 0x1102,
  ZLINK_SOCKOPT_SUBSCRIBE = 0x1103,
  ZLINK_SOCKOPT_UNSUBSCRIBE = 0x1104,
  ZLINK_SOCKOPT_RATE = 0x1105,
  ZLINK_SOCKOPT_RECOVERY_IVL = 0x1106,
  ZLINK_SOCKOPT_SNDBUF = 0x1107,
  ZLINK_SOCKOPT_RCVBUF = 0x1108,
  ZLINK_SOCKOPT_RCVMORE = 0x1109,
  ZLINK_SOCKOPT_FD = 0x110A,
  ZLINK_SOCKOPT_EVENTS = 0x110B,
  ZLINK_SOCKOPT_TYPE = 0x110C,
  ZLINK_SOCKOPT_LINGER = 0x110D,
  ZLINK_SOCKOPT_RECONNECT_IVL = 0x110E,
  ZLINK_SOCKOPT_BACKLOG = 0x110F,
  ZLINK_SOCKOPT_RECONNECT_IVL_MAX = 0x1110,
  ZLINK_SOCKOPT_MAXMSGSIZE = 0x1111,
  ZLINK_SOCKOPT_SNDHWM = 0x1112,
  ZLINK_SOCKOPT_RCVHWM = 0x1113,
  ZLINK_SOCKOPT_MULTICAST_HOPS = 0x1114,
  ZLINK_SOCKOPT_RCVTIMEO = 0x1115,
  ZLINK_SOCKOPT_SNDTIMEO = 0x1116,
  ZLINK_SOCKOPT_LAST_ENDPOINT = 0x1117,
  ZLINK_SOCKOPT_ROUTER_MANDATORY = 0x1118,
  ZLINK_SOCKOPT_TCP_KEEPALIVE = 0x1119,
  ZLINK_SOCKOPT_TCP_KEEPALIVE_CNT = 0x111A,
  ZLINK_SOCKOPT_TCP_KEEPALIVE_IDLE = 0x111B,
  ZLINK_SOCKOPT_TCP_KEEPALIVE_INTVL = 0x111C,
  ZLINK_SOCKOPT_IMMEDIATE = 0x111D,
  ZLINK_SOCKOPT_XPUB_VERBOSE = 0x111E,
  ZLINK_SOCKOPT_IPV6 = 0x111F,
  ZLINK_SOCKOPT_PROBE_ROUTER = 0x1120,
  ZLINK_SOCKOPT_CONFLATE = 0x1121,
  ZLINK_SOCKOPT_ROUTER_HANDOVER = 0x1122,
  ZLINK_SOCKOPT_TOS = 0x1123,
  ZLINK_SOCKOPT_CONNECT_ROUTING_ID = 0x1124,
  ZLINK_SOCKOPT_HANDSHAKE_IVL = 0x1125,
  ZLINK_SOCKOPT_XPUB_NODROP = 0x1126,
  ZLINK_SOCKOPT_BLOCKY = 0x1127,
  ZLINK_SOCKOPT_XPUB_MANUAL = 0x1128,
  ZLINK_SOCKOPT_XPUB_WELCOME_MSG = 0x1129,
  ZLINK_SOCKOPT_STREAM_NOTIFY = 0x112A,
  ZLINK_SOCKOPT_INVERT_MATCHING = 0x112B,
  ZLINK_SOCKOPT_HEARTBEAT_IVL = 0x112C,
  ZLINK_SOCKOPT_HEARTBEAT_TTL = 0x112D,
  ZLINK_SOCKOPT_HEARTBEAT_TIMEOUT = 0x112E,
  ZLINK_SOCKOPT_XPUB_VERBOSER = 0x112F,
  ZLINK_SOCKOPT_CONNECT_TIMEOUT = 0x1130,
  ZLINK_SOCKOPT_TCP_MAXRT = 0x1131,
  ZLINK_SOCKOPT_MULTICAST_MAXTPDU = 0x1132,
  ZLINK_SOCKOPT_USE_FD = 0x1133,
  ZLINK_SOCKOPT_BINDTODEVICE = 0x1134,
  ZLINK_SOCKOPT_TLS_CERT = 0x1135,
  ZLINK_SOCKOPT_TLS_KEY = 0x1136,
  ZLINK_SOCKOPT_TLS_CA = 0x1137,
  ZLINK_SOCKOPT_TLS_VERIFY = 0x1138,
  ZLINK_SOCKOPT_XPUB_MANUAL_LAST_VALUE = 0x1139,
  ZLINK_SOCKOPT_TLS_REQUIRE_CLIENT_CERT = 0x113A,
  ZLINK_SOCKOPT_TLS_HOSTNAME = 0x113B,
  ZLINK_SOCKOPT_TLS_TRUST_SYSTEM = 0x113C,
  ZLINK_SOCKOPT_TLS_PASSWORD = 0x113D,
  ZLINK_SOCKOPT_ONLY_FIRST_SUBSCRIBE = 0x113E,
  ZLINK_SOCKOPT_TOPICS_COUNT = 0x113F,
  ZLINK_SOCKOPT_ZMP_METADATA = 0x1140,
  ZLINK_SOCKOPT_TCP_NODELAY = 0x1141
} zlink_socket_option_t;

typedef struct zlink_socket_handler_t
{
  zlink_socket_handler_kind_t kind;
  union
  {
    zlink_socket_msg_handler_fn msg;
    zlink_spot_handler_fn spot;
    zlink_xpub_handler_fn xpub;
  } fn;
} zlink_socket_handler_t;

int zlink_setsockopt (void *s,
                      zlink_socket_option_t option,
                      const void *optval,
                      size_t optvallen);

int zlink_getsockopt (void *s,
                      zlink_socket_option_t option,
                      void *optval,
                      size_t *optvallen);

void *zlink_socket (void *ctx,
                    zlink_socket_type_t type,
                    const zlink_socket_handler_t *handler);
```

별도 alias 정리:

- `ZLINK_FD` -> `ZLINK_SOCKOPT_FD`
- `ZLINK_EVENTS` -> `ZLINK_SOCKOPT_EVENTS`
- `ZLINK_TYPE` -> `ZLINK_SOCKOPT_TYPE`
- `ZLINK_LAST_ENDPOINT` -> `ZLINK_SOCKOPT_LAST_ENDPOINT`

관련 함수 최종안:

| 현재 함수 | 변경안 | 비고 |
|---|---|---|
| `int zlink_setsockopt(void *s, int option, const void *optval, size_t optvallen)` | `int zlink_setsockopt(void *s, zlink_socket_option_t option, const void *optval, size_t optvallen)` | raw socket option은 전부 typed enum 사용 |
| `int zlink_getsockopt(void *s, int option, void *optval, size_t *optvallen)` | `int zlink_getsockopt(void *s, zlink_socket_option_t option, void *optval, size_t *optvallen)` | getter도 동일 enum 사용 |
| `#define ZLINK_FD`, `ZLINK_EVENTS`, `ZLINK_TYPE`, `ZLINK_LAST_ENDPOINT` | `zlink_socket_option_t` alias 또는 동일 enum member로 정리 | getsockopt 전용 항목도 같은 enum namespace에 둔다 |

raw socket option에 대한 결론:

- 일반 socket option도 service option과 동일하게 값 재배치 대상이다.
- 즉 raw socket option도 "enum type화 + 전역 유일 값 재배치"가 적절하다.
- 기존 libzmq 숫자와의 정렬은 설계 목표에서 제외한다.
- raw socket option도 `0x1101`부터 시작하는 16진수 순차값으로 다시 정의한다.

### 4.1 Registry / Context option 확정안

이번 callback-recv rewrite의 직접 대상은 아니지만,
header 전반의 typed-constant 정리 범위에서는 아래 surface도 같은 원칙으로 확정한다.

| 현재 인터페이스 | 변경안 | 이유 |
|---|---|---|
| `#define ZLINK_REGISTRY_SOCKET_PUB`, `..._ROUTER`, `..._PEER_SUB` | `zlink_registry_socket_role_t` | internal registry socket role도 닫힌 값 집합이다. |
| `int zlink_registry_setsockopt(void *registry, int socket_role, int option, const void *optval, size_t optvallen)` | `int zlink_registry_setsockopt(void *registry, zlink_registry_socket_role_t socket_role, zlink_socket_option_t option, const void *optval, size_t optvallen)` | raw `int` 두 개보다 role/option 소속을 명확히 한다. |
| `#define ZLINK_IO_THREADS`, `ZLINK_MAX_SOCKETS`, ... | `zlink_ctx_option_t` | context option도 닫힌 값 집합이다. |
| `int zlink_ctx_set(void *ctx, int option, int optval)` | `int zlink_ctx_set(void *ctx, zlink_ctx_option_t option, int optval)` | context option namespace를 타입으로 고정한다. |
| `int zlink_ctx_get(void *ctx, int option)` 계열 | `int zlink_ctx_get(void *ctx, zlink_ctx_option_t option)` 계열 | getter도 동일한 enum typing을 사용한다. |

추가 규칙:

- `registry` / `ctx` option은 callback-only recv 의미와 직접 연결되지는 않지만,
  public header의 loosely typed 상수 집합을 정리한다는 관점에서는 같은 migration 작업에
  포함해 typed enum과 전역 값 체계 원칙을 동일하게 적용한다.
- 구현 우선순위는 raw socket / service option / monitor / send-ready notification보다 낮을 수 있지만,
  인터페이스 shape 자체는 이 문서 기준으로 확정한다.

## 5. 기타 상수군 enum 확정안

option 외에도 다음 상수군은 macro보다 enum이 더 자연스럽다.

| 상수군 | 확정 타입 |
|---|---|
| socket type (`ZLINK_PAIR`, `ZLINK_PUB`, ...) | `zlink_socket_type_t` |
| socket handler kind | `zlink_socket_handler_kind_t` |
| socket option (`ZLINK_AFFINITY`, `ZLINK_SNDHWM`, ...) | `zlink_socket_option_t` |
| service type (`ZLINK_SERVICE_TYPE_GATEWAY`, `ZLINK_SERVICE_TYPE_SPOT`) | `zlink_service_type_t` |
| service kind (`ZLINK_SERVICE_KIND_*`) | `zlink_service_kind_t` |
| gateway LB strategy | `zlink_gateway_lb_strategy_t` |
| gateway option | `zlink_gateway_option_t` |
| representative routing id policy | dedicated setter 유지, first-use restriction 강화 |
| SPOT role selector | 기존 `zlink_spot_role_t` 유지 |
| SPOT node option | 별도 enum 없음 |
| SPOT pub/sub option | `zlink_spot_pub_option_t`, `zlink_spot_sub_option_t` |
| SPOT pub mode / queue full policy | dedicated enum 추가 |
| send flags (`ZLINK_DONTWAIT`, `ZLINK_SNDMORE`) | `zlink_send_flags_t` |
| send-ready callback | `zlink_send_ready_handler_fn` |
| disconnect reason (`ZLINK_DISCONNECT_*`) | `zlink_disconnect_reason_t` |
| topology source (`ZLINK_TOPOLOGY_SOURCE_*`) | `zlink_topology_source_t` |
| topology state (`ZLINK_TOPOLOGY_STATE_*`) | `zlink_topology_state_t` |

관련 함수 변경안:

| 상수군 | 현재 함수 | 변경안 |
|---|---|---|
| socket type | `void *zlink_socket(void *ctx, zlink_socket_type_t type, zlink_socket_msg_handler_fn handler)` | `void *zlink_socket(void *ctx, zlink_socket_type_t type, const zlink_socket_handler_t *handler)` |
| service type | `void *zlink_discovery_new(void *ctx, zlink_service_type_t service_type)` | 유지 |
| service type | 내부적으로 service type을 받는 향후 service factory 계열 | 모두 `zlink_service_type_t` 사용 |
| service kind | `zlink_service_event_t.service_kind` | `zlink_service_kind_t` 사용 |
| service kind | `zlink_registry_topology_entry_t.service_kind` / `zlink_registry_topology_filter_t.service_kind` | `zlink_service_kind_t` 사용 |
| gateway LB strategy | `int zlink_gateway_set_lb_strategy(void *gateway, int strategy)` | `int zlink_gateway_set_lb_strategy(void *gateway, zlink_gateway_lb_strategy_t strategy)` |
| representative routing id policy | `int zlink_discovery_set_routing_id(void *discovery, const void *data, size_t size)` | first-use 이전에만 허용 |
| representative routing id policy | `int zlink_gateway_set_routing_id(void *gateway, const void *data, size_t size)` | first-use 이전에만 허용 |
| representative routing id policy | `int zlink_spot_pub_set_routing_id(void *pub, const void *data, size_t size)` | first publish/connect 이전에만 허용 |
| representative routing id policy | `int zlink_spot_sub_set_routing_id(void *sub, const void *data, size_t size)` | first subscribe/connect 이전에만 허용 |
| SPOT role selector | 없음 (이미 split setter 사용) | 삭제 상태 유지 |
| unified `spot` 생성 | `void *zlink_spot_new(void *spot_node, zlink_spot_handler_fn handler)` | 유지 |
| SPOT pub option | `int zlink_spot_node_set_pub_option(void *node, zlink_spot_pub_option_t pub_option, const void *optval, size_t optvallen)` | 유지 |
| SPOT pub option | `int zlink_spot_pub_set_option(void *pub, zlink_spot_pub_option_t pub_option, const void *optval, size_t optvallen)` | 유지 |
| SPOT sub option | `int zlink_spot_node_set_sub_option(void *node, zlink_spot_sub_option_t sub_option, const void *optval, size_t optvallen)` | 유지 |
| SPOT sub option | `int zlink_spot_sub_set_option(void *sub, zlink_spot_sub_option_t sub_option, const void *optval, size_t optvallen)` | 유지 |
| registry socket role | `int zlink_registry_setsockopt(void *registry, int socket_role, int option, const void *optval, size_t optvallen)` | `int zlink_registry_setsockopt(void *registry, zlink_registry_socket_role_t socket_role, zlink_socket_option_t option, const void *optval, size_t optvallen)` |
| context option | `int zlink_ctx_set(void *ctx, int option, int optval)` | `int zlink_ctx_set(void *ctx, zlink_ctx_option_t option, int optval)` |
| raw socket callback | `int zlink_socket_set_msg_handler(void *s, zlink_socket_msg_handler_fn handler)` 계열 | 삭제 |
| send-ready callback | 없음 | `int zlink_socket_set_send_ready_handler(void *s, zlink_send_ready_handler_fn handler)` |
| send-ready callback | 없음 | `int zlink_gateway_set_send_ready_handler(void *gateway, zlink_send_ready_handler_fn handler)` |
| send-ready callback | 없음 | `int zlink_spot_node_set_send_ready_handler(void *node, zlink_send_ready_handler_fn handler)` |
| send-ready callback | 없음 | `int zlink_spot_set_send_ready_handler(void *spot, zlink_send_ready_handler_fn handler)` |
| send-ready callback | 없음 | `int zlink_spot_pub_set_send_ready_handler(void *pub, zlink_send_ready_handler_fn handler)` |
| poller API | `zlink_poll()`, `zlink_poller_*` 전 계열 | 삭제 |
| monitor event mask | `void *zlink_socket_monitor_open(void *s, int events, zlink_monitor_handler_fn handler)` | `void *zlink_socket_monitor_open(void *s, zlink_socket_monitor_event_mask_t events, zlink_monitor_handler_fn handler)` |
| discovery monitor event mask | `void *zlink_discovery_monitor_open(void *discovery, int events, zlink_service_monitor_handler_fn handler)` | `void *zlink_discovery_monitor_open(void *discovery, zlink_discovery_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` |
| gateway monitor event mask | `void *zlink_gateway_monitor_open(void *gateway, int events, zlink_service_monitor_handler_fn handler)` | `void *zlink_gateway_monitor_open(void *gateway, zlink_gateway_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` |
| SPOT monitor event mask | `void *zlink_spot_monitor_open(void *spot, int role, int events, zlink_service_monitor_handler_fn handler)` | `void *zlink_spot_monitor_open(void *spot, zlink_spot_role_t role, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` |
| SPOT sub monitor event mask | `void *zlink_spot_sub_monitor_open(void *sub, int events, zlink_service_monitor_handler_fn handler)` | `void *zlink_spot_sub_monitor_open(void *sub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` |
| SPOT pub monitor event mask | `void *zlink_spot_pub_monitor_open(void *pub, int events, zlink_service_monitor_handler_fn handler)` | `void *zlink_spot_pub_monitor_open(void *pub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` |
| service event detail mask | `zlink_service_event_t.detail_flags` | `zlink_service_event_detail_mask_t` 값 집합으로 문서화 |
| send flags | `zlink_send(..., int flags)` 등 send 계열 전반 | `zlink_send_flags_t flags` 사용 |
| disconnect reason | `zlink_monitor_event_t.value`가 disconnect reason일 때 | `zlink_disconnect_reason_t` 값 집합으로 문서화 |
| topology source | `zlink_registry_topology_entry_t.source` / `zlink_registry_topology_filter_t.source` | `zlink_topology_source_t` 사용 |
| topology state | `zlink_registry_topology_entry_t.state` / `zlink_registry_topology_filter_t.state` | `zlink_topology_state_t` 사용 |

### 5.1 callback setter 계약

메인 스펙과 정렬된 현재 확정 계약은 다음이다.

대상 API:

비고:

- callback은 생성/open 시점에만 설치한다.
- raw socket은 `zlink_socket()`에서 family를 확정한다.
- `spot_node`는 `zlink_spot_node_new()`에서 node-owned default sub callback을 고정한다.
- unified `spot`은 `zlink_spot_new()`에서 callback을 고정한다.
- standalone `spot_sub`는 `zlink_spot_sub_new()`에서 callback을 고정한다.
- `SpotPub`은 publish-only facade로서 public 수신 callback 경로를 갖지 않는다.

공통 규칙:

- 생성/open 이후 callback 교체는 허용하지 않는다.
- `NULL` callback은 허용하지 않는다.
- 생성/open 시 최초 callback을 받는 API에도 같은 non-`NULL` 원칙을 적용한다.
  즉 `zlink_gateway_new()`,
  `zlink_spot_node_new()`,
  `zlink_socket_monitor_open()`, `zlink_discovery_monitor_open()`,
  `zlink_gateway_monitor_open()`, `zlink_spot_monitor_open()`,
  `zlink_spot_sub_monitor_open()`,
  `zlink_spot_pub_monitor_open()`은 `NULL` callback을 허용하지 않는다.
- raw recv-capable socket은 `zlink_socket()` 생성 시 family에 맞는 non-`NULL`
  `zlink_socket_handler_t`를 제공해야 한다.
- `zlink_socket()`는 socket type을 검증해야 하며,
  family가 맞지 않는 handler kind를 주면 `EINVAL`로 실패해야 한다.
- raw `PUB`만 예외적으로 `zlink_socket(..., handler = NULL)`를 허용한다.
- `zlink_spot_new()`는 unified `spot`이 항상 recv-capable facade이므로
  `NULL` handler를 허용하지 않는다.
- in-flight callback에는 해당 dispatch 진입 시점에 고정된 handler가 적용된다.
- callback은 생성/open 이후 고정되므로, public contract로서의 replace visibility 규칙은 두지 않는다.

확정 타입/값:

주의:

- 아래 타입/값은 이 문서 기준 canonical 값이다.
- bitmask/event 상수의 exported C 표현은 `typedef uint32_t + typed macro` 형식으로 확정한다.

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

typedef void (*zlink_send_ready_handler_fn) (void *subject);

typedef uint32_t zlink_socket_monitor_event_mask_t;

#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTED                  ((zlink_socket_monitor_event_mask_t) 0x0001u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED            ((zlink_socket_monitor_event_mask_t) 0x0002u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED            ((zlink_socket_monitor_event_mask_t) 0x0004u)
#define ZLINK_SOCKET_MONITOR_EVENT_LISTENING                  ((zlink_socket_monitor_event_mask_t) 0x0008u)
#define ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED                ((zlink_socket_monitor_event_mask_t) 0x0010u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED                   ((zlink_socket_monitor_event_mask_t) 0x0020u)
#define ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED              ((zlink_socket_monitor_event_mask_t) 0x0040u)
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSED                     ((zlink_socket_monitor_event_mask_t) 0x0080u)
#define ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED               ((zlink_socket_monitor_event_mask_t) 0x0100u)
#define ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED               ((zlink_socket_monitor_event_mask_t) 0x0200u)
#define ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED            ((zlink_socket_monitor_event_mask_t) 0x0400u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL ((zlink_socket_monitor_event_mask_t) 0x0800u)
#define ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY           ((zlink_socket_monitor_event_mask_t) 0x1000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL  ((zlink_socket_monitor_event_mask_t) 0x2000u)
#define ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH      ((zlink_socket_monitor_event_mask_t) 0x4000u)
#define ZLINK_SOCKET_MONITOR_EVENT_ALL                        ((zlink_socket_monitor_event_mask_t) 0xFFFFu)

typedef uint32_t zlink_discovery_monitor_event_mask_t;

#define ZLINK_DISCOVERY_MONITOR_EVENT_READY             ((zlink_discovery_monitor_event_mask_t) (1u << 0))
#define ZLINK_DISCOVERY_MONITOR_EVENT_LOST              ((zlink_discovery_monitor_event_mask_t) (1u << 1))
#define ZLINK_DISCOVERY_MONITOR_EVENT_ERROR             ((zlink_discovery_monitor_event_mask_t) (1u << 4))
#define ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP        ((zlink_discovery_monitor_event_mask_t) (1u << 5))
#define ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN      ((zlink_discovery_monitor_event_mask_t) (1u << 6))
#define ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED ((zlink_discovery_monitor_event_mask_t) (1u << 7))
#define ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED            ((zlink_discovery_monitor_event_mask_t) (1u << 17))

typedef uint32_t zlink_gateway_monitor_event_mask_t;

#define ZLINK_GATEWAY_MONITOR_EVENT_ERROR                    ((zlink_gateway_monitor_event_mask_t) (1u << 4))
#define ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY            ((zlink_gateway_monitor_event_mask_t) (1u << 8))
#define ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_LOST             ((zlink_gateway_monitor_event_mask_t) (1u << 9))
#define ZLINK_GATEWAY_MONITOR_EVENT_CONNECTION_COUNT_CHANGED ((zlink_gateway_monitor_event_mask_t) (1u << 10))
#define ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_UP                 ((zlink_gateway_monitor_event_mask_t) (1u << 11))
#define ZLINK_GATEWAY_MONITOR_EVENT_ROUTE_DOWN               ((zlink_gateway_monitor_event_mask_t) (1u << 12))
#define ZLINK_GATEWAY_MONITOR_EVENT_CLOSED                   ((zlink_gateway_monitor_event_mask_t) (1u << 17))

typedef uint32_t zlink_spot_monitor_event_mask_t;

#define ZLINK_SPOT_MONITOR_EVENT_READY                ((zlink_spot_monitor_event_mask_t) (1u << 0))
#define ZLINK_SPOT_MONITOR_EVENT_LOST                 ((zlink_spot_monitor_event_mask_t) (1u << 1))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_UP              ((zlink_spot_monitor_event_mask_t) (1u << 2))
#define ZLINK_SPOT_MONITOR_EVENT_PEER_DOWN            ((zlink_spot_monitor_event_mask_t) (1u << 3))
#define ZLINK_SPOT_MONITOR_EVENT_ERROR                ((zlink_spot_monitor_event_mask_t) (1u << 4))
#define ZLINK_SPOT_MONITOR_EVENT_SUB_FILTER_APPLIED   ((zlink_spot_monitor_event_mask_t) (1u << 13))
#define ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY   ((zlink_spot_monitor_event_mask_t) (1u << 14))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_FULL       ((zlink_spot_monitor_event_mask_t) (1u << 15))
#define ZLINK_SPOT_MONITOR_EVENT_PUB_QUEUE_DRAINED    ((zlink_spot_monitor_event_mask_t) (1u << 16))
#define ZLINK_SPOT_MONITOR_EVENT_CLOSED               ((zlink_spot_monitor_event_mask_t) (1u << 17))

typedef uint32_t zlink_service_event_detail_mask_t;

#define ZLINK_SERVICE_EVENT_DETAIL_SERVICE_NAME ((zlink_service_event_detail_mask_t) 0x0001u)
#define ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT     ((zlink_service_event_detail_mask_t) 0x0002u)
#define ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID  ((zlink_service_event_detail_mask_t) 0x0004u)
#define ZLINK_SERVICE_EVENT_DETAIL_PEER_RID     ((zlink_service_event_detail_mask_t) 0x0008u)

typedef uint32_t zlink_send_flags_t;

#define ZLINK_SEND_FLAG_DONTWAIT ((zlink_send_flags_t) 0x0001u)
#define ZLINK_SEND_FLAG_SNDMORE  ((zlink_send_flags_t) 0x0002u)

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
- bitmask 성격의 상수군은 이름만 `*_mask_t` / `*_flags_t`로 끝나는 enum 설명에
  머물지 말고, final C header 표현까지 함께 확정해야 한다.
- portable C header 기준 baseline은 plain enum typedef보다
  `typedef uint32_t ..._mask_t;` + typed macro/상수 조합으로 고정한다.
  OR 결과가 `int`가 되는 C enum 규칙 때문에, 단순 `typedef enum ..._mask_t`는
  final exported form으로는 부적합할 수 있다.
- compiler-specific `flag_enum`류 확장은 선택 사항일 뿐,
  portable baseline으로 가정하지 않는다.
- `int`, `short`, `uint16_t` 같은 원시 타입으로 상수 집합 의미를 암묵적으로 표현하는 방식을
  줄이는 것이 목표다.
- 다만 socket monitor bit, zero-init default 의미를 가지는 mode/policy처럼
  기존 비트/기본값 의미가 중요한 상수군은 enum으로 승격하더라도 기존 수치 값을 유지한다.
- service monitor 계열은 discovery / gateway / spot의 공개 이벤트 집합이 서로 다르므로
  공통 enum 하나로 뭉뚱그리지 않고 함수 family별 mask enum으로 분리한다.
- `ZLINK_SOCKET_MONITOR_EVENT_ALL`은 raw socket monitor가 legacy aggregate mask를 이미
  갖고 있는 전제를 반영한 canonical 정의다.
- discovery / gateway / spot monitor는 service별 공개 이벤트 집합이 더 자주 달라지므로,
  대응 `*_ALL` sentinel을 두지 않는다.
- `gateway` monitor는 공통 `READY/LOST`나 `PEER_UP/DOWN` 대신
  `SERVICE_READY/SERVICE_LOST`, `ROUTE_UP/ROUTE_DOWN`을 공개한다.
  service-bound gateway도
  "이 handle이 대표하는 service route가 usable/unusable 해졌는지"가 핵심 의미이고,
  SPOT의 일반 peer lifecycle과 동일한 vocabulary를 강제할 필요가 없다.
- `zlink_service_event_t.detail_flags`도 별도 `zlink_service_event_detail_mask_t`로
  승격해 event mask와 detail mask를 섞지 않는다.

단, 아래는 bitmask 성격이 강하므로 enum으로 바꾸더라도 "flag enum"임을 문서에
명확히 적어야 한다.

- monitor event mask (`ZLINK_EVENT_*`)
- discovery / gateway / spot monitor event mask
- service event detail mask

### 5.2 Send-ready notification 계약

비동기 nonblocking send 경로의 backpressure 신호는 poller가 아니라
send-ready callback으로 통일한다.

확정 시그니처:

```c
typedef void (*zlink_send_ready_handler_fn) (void *subject);

int zlink_socket_set_send_ready_handler (void *s,
                                         zlink_send_ready_handler_fn handler);

int zlink_gateway_set_send_ready_handler (void *gateway,
                                          zlink_send_ready_handler_fn handler);

int zlink_spot_node_set_send_ready_handler (void *node,
                                            zlink_send_ready_handler_fn handler);

int zlink_spot_set_send_ready_handler (void *spot,
                                       zlink_send_ready_handler_fn handler);

int zlink_spot_pub_set_send_ready_handler (void *pub,
                                           zlink_send_ready_handler_fn handler);
```

추가 규칙:

- public poller API는 canonical 범위에서 제거한다.
- 즉 one-shot `zlink_poll()`과 stateful `zlink_poller_*` API를 모두 public surface에서 제거한다.
- 삭제 대상 poller public type도 함께 제거한다:
  `zlink_poller_event_mask_t`,
  `zlink_pollitem_t`,
  `zlink_poller_event_t`,
  `ZLINK_POLLOUT`,
  `ZLINK_POLLERR`,
  `ZLINK_POLLPRI`,
  `ZLINK_HAVE_POLLER`,
  `ZLINK_POLLITEMS_DFLT`.
- 구체 삭제 대상 함수는 다음과 같다:
  `zlink_poll()`,
  `zlink_poller_new()`,
  `zlink_poller_destroy()`,
  `zlink_poller_size()`,
  `zlink_poller_add()`,
  `zlink_poller_add_spot_pub()`,
  `zlink_poller_add_gateway()`,
  `zlink_poller_add_monitor()`,
  `zlink_poller_add_fd()`,
  `zlink_poller_modify()`,
  `zlink_poller_modify_spot_pub()`,
  `zlink_poller_modify_gateway()`,
  `zlink_poller_modify_monitor()`,
  `zlink_poller_modify_fd()`,
  `zlink_poller_remove()`,
  `zlink_poller_remove_spot_sub()`,
  `zlink_poller_remove_spot_pub()`,
  `zlink_poller_remove_gateway()`,
  `zlink_poller_remove_monitor()`,
  `zlink_poller_remove_fd()`,
  `zlink_poller_wait()`,
  `zlink_poller_wait_all()`.
- blocking send가 기본 backpressure 경로다.
- `set_send_ready_handler()`는 nonblocking send가 `EAGAIN`을 반환한 뒤 재시도 시점을
  감지하는 보조 메커니즘으로 사용한다.
- `zlink_socket_set_send_ready_handler()`는 send-capable raw socket에만 허용한다.
  즉 raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `PUB`, `XPUB`에는 허용하고,
  recv-only raw `SUB`, `XSUB`에는 허용하지 않는다.
- raw `SUB` / `XSUB`에 `zlink_socket_set_send_ready_handler()`를 호출하면
  `EINVAL`로 실패해야 한다.
- `zlink_gateway_set_send_ready_handler()`,
  `zlink_spot_node_set_send_ready_handler()`,
  `zlink_spot_set_send_ready_handler()`,
  `zlink_spot_pub_set_send_ready_handler()`는 허용한다.
- `spot_sub`에는 별도 send-ready handler surface를 두지 않는다.
- send-ready callback은 "지금 한 번의 send 성공이 보장된다"는 의미가 아니라,
  "queue full -> writable transition이 있었으니 drain을 다시 시도하라"는 힌트다.
- callback은 가능한 한 lightweight signal 용도에만 사용한다.
  canonical 사용법은 atomic flag set, condition notify, worker wake-up 중 하나다.
- callback 안에서 직접 drain loop를 수행할 수는 있지만, canonical 사용법으로 권장하지 않는다.
- nonblocking send 경로는 `EAGAIN` 시 애플리케이션 queue에 적재하고,
  send-ready callback 후 worker가 drain loop를 다시 수행하는 구조를 기본으로 본다.
- drain loop는 send가 성공하는 동안 계속 비우고, 다시 `EAGAIN`을 만나면 중단한다.
- `set_send_ready_handler()`도 callback setter family와 동일하게 replace-only surface로 본다.
  `NULL` 제거 API는 두지 않는다.

## 6. 정책 회귀 테스트 추가 항목

이 절은 시그니처/enum 값 자체를 단순 대조하는 테스트보다,
이번 문서가 고정한 정책이 구현에서 다시 무너지지 않도록 막는 회귀 테스트 항목을 정리한다.

### 6.1 Callback-only recv 정책

- recv-capable raw socket은 생성 시점에 socket family에 맞는 non-`NULL`
  `zlink_socket_handler_t`를 제공해야 한다.
  `PUB`만 예외로 `handler == NULL`을 허용하는 send-only 타입으로 본다.
- `ROUTER` / `DEALER` / `PAIR` / `STREAM`에 `ZLINK_SOCKET_HANDLER_MSG`,
  `SUB` / `XSUB`에 `ZLINK_SOCKET_HANDLER_SPOT`,
  `XPUB`에 `ZLINK_SOCKET_HANDLER_XPUB`를 넘긴 생성은 성공해야 한다.
- `zlink_socket()`는 socket type을 검증해야 하며,
  family가 맞지 않는 handler kind에 대한 호출은 `EINVAL`로 실패해야 한다.
- `SUB` / `XSUB`에 `ZLINK_SOCKET_HANDLER_MSG`,
  `XPUB`에 `ZLINK_SOCKET_HANDLER_SPOT`,
  `PAIR` / `DEALER` / `ROUTER` / `STREAM`에 `ZLINK_SOCKET_HANDLER_XPUB`를 넘기는
  대표 mismatch 조합은 모두 `EINVAL`로 실패해야 한다.
- raw `PUB`에 non-`NULL` handler를 넘긴 생성 시도는 모두 `EINVAL`로 실패해야 한다.
- `DEALER` / `ROUTER` / `PAIR` / `XPUB` 등을 애플리케이션이 사실상 send-only처럼
  사용하더라도 recv-capable raw socket 분류는 유지되며, family에 맞는 no-op callback
  등록 경로가 성공해야 한다.
- unified `spot`은 항상 recv-capable facade이므로
  `zlink_spot_new()`에서 non-`NULL` handler가 없으면 실패해야 한다.
- `XPUB`은 publish socket 계열이지만 recv-capable로 분류되어,
  subscribe/unsubscribe control event를 dedicated callback으로 받아야 한다.
- callback 교체/제거 API가 없으므로 생성/open 이후 handler mutation은 public surface에서 지원하지 않는다.

### 6.2 Service-bound identity 정책

- `gateway`는 생성 시 고정된 `service_name`으로만 동작하고,
  send/callback/public query 경로에서 per-call service override가 없어야 한다.
- `spot_node`는 생성 시 고정된 `service_name`으로만 동작하고,
  attach된 `spot` / `spot_pub` / `spot_sub`는 그 identity를 공유해야 한다.
- `zlink_spot_node_default_pub()` / `zlink_spot_node_default_sub()`는 public API에
  존재하지 않아야 한다.
- representative routing id setter는 first-use 이전에는 성공하고,
  첫 bind/connect/publish/subscribe 이후에는 실패해야 한다.

### 6.3 Discovery ownership 정책

- `gateway_attach_discovery()` / `spot_node_attach_discovery()` 이전에는
  manual `connect` / `disconnect`가 가능해야 한다.
- manual peer/route가 하나라도 존재하는 상태에서
  `gateway_attach_discovery()` / `spot_node_attach_discovery()`를 호출하면
  `EBUSY`를 반환해야 한다.
- discovery attach 이후에는 manual `connect` / `disconnect`가 금지되어야 한다.
- discovery attach 이후 peer/topology 변화는 discovery-driven convergence로만 반영되어야 한다.

### 6.3.1 Discovery / Registry transport 회귀

- `zlink_discovery_connect_registry()`는 `tcp`, `ws`, `wss`, `tls` endpoint에서
  성공 경로를 가져야 한다.
- registry peer sync도 `tcp`, `ws`, `wss`, `tls` peer PUB endpoint에서
  정상적으로 topology/service list convergence를 형성해야 한다.
- 허용 transport 안의 mixed deployment
  (예: registry A=`tcp`, B=`ws`, C=`tls`)도 peer sync와 topology convergence가
  정상 동작해야 한다.
- `ipc`, `inproc`, `pgm`, `epgm`, `tipc` 등 허용 목록 밖 transport로
  discovery bootstrap 또는 registry peer 구성을 시도하면 `EPROTONOSUPPORT`로
  실패해야 한다.

### 6.4 Public register 제거 정책

- `gateway`는 별도 public `register/update_weight/unregister` surface 없이도
  bind된 endpoint와 local weight state를 runtime이 반영해야 한다.
- `spot_node`는 별도 public `register/unregister` 없이도
  discovery attach/manual connect 정책만으로 topology가 수렴해야 한다.
- `gateway` monitor에 register/unregister 결과 전용 event가 다시 생기지 않아야 한다.

### 6.5 Weight ownership 정책

- `zlink_gateway_peer_info()` / `zlink_gateway_router_peers()` snapshot에는
  `weight`가 포함되어야 한다.
- local representative routing id에 대한 초기 local bound route weight는 `0`이어야 한다.
- local representative routing id를 대상으로 한
  `zlink_gateway_update_peer_weight()`는 성공해야 하며,
  local bound route weight update로 해석되어야 한다.
- local representative routing id를 대상으로 한 update 성공 직후
  같은 `gateway`의 local snapshot에는 새 weight가 즉시 반영되어야 한다.
- `zlink_gateway_update_peer_weight()` 성공 후 호출 handle의 local snapshot은 즉시 갱신되어야 한다.
- 같은 service를 보는 다른 handle은 discovery/registry runtime 동기화 후
  변경된 `weight`를 eventually 관찰해야 한다.
- 존재하지 않는 `routing_id`에 대한 weight update는 `ENOENT`를 반환해야 한다.

### 6.6 msg_t-only data path 정책

- `gateway`, unified `spot`, `spot_node`, `spot_pub` public send/recv 경로는
  `msg_t` 기반으로만 동작해야 하며, bytes helper가 다시 추가되지 않아야 한다.
- `zlink_msg_init_data()` 등 `zlink_msg_t` 생성/ownership API를 통한 zero-copy 경로는
  계속 지원되어야 하며, `msg_t` only 정책과 충돌하지 않아야 한다.
- callback에서 받은 multipart ownership 규약이 worker handoff 이후에도 유지되어야 한다.
- zero-copy `msg_t`를 send/publish/callback handoff에 사용하더라도 free callback,
  close, move ownership 규약이 깨지지 않아야 한다.
- same-handle callback 안 `send` / `send_rid` / `publish`가 deadlock 없이 가능해야 한다.

### 6.7 Send-ready / backpressure 정책

- public poller API(`zlink_poll()`, `zlink_poller_*`)와 poller public type
  (`zlink_poller_event_mask_t`, `zlink_pollitem_t`, `zlink_poller_event_t`)은
  존재하지 않아야 한다.
- `ZLINK_POLLOUT`, `ZLINK_POLLERR`, `ZLINK_POLLPRI`, `ZLINK_HAVE_POLLER`,
  `ZLINK_POLLITEMS_DFLT`도 public header에 존재하지 않아야 한다.
- nonblocking send가 `EAGAIN`을 반환한 뒤
  `zlink_socket_set_send_ready_handler()`,
  `zlink_gateway_set_send_ready_handler()`,
  `zlink_spot_node_set_send_ready_handler()`,
  `zlink_spot_set_send_ready_handler()`,
  `zlink_spot_pub_set_send_ready_handler()`가 재시도 신호로 동작해야 한다.
- raw `SUB` / `XSUB`에 대한 `zlink_socket_set_send_ready_handler()` 호출은
  `EINVAL`로 실패해야 한다.
- send-ready callback은 writable transition hint이며, callback 직후 단 한 번의 send 성공을
  보장하지 않아야 한다.
- send-ready callback이 올라온 뒤 drain loop가 일부 메시지를 전송하고 다시 `EAGAIN`으로
  중단되는 연속 backpressure 시나리오도 정상 동작해야 한다.
- canonical 사용법대로 callback이 flag set / worker wake-up 용도로만 쓰여도
  async nonblocking send queue가 다시 배출될 수 있어야 한다.
- blocking send 경로는 send-ready callback 유무와 무관하게 기본 backpressure 경로로 유지되어야 한다.

### 6.8 Option/support 정책

- `Gateway`, `SpotPub`, `SpotSub` option 지원/`ENOTSUP` 매트릭스가
  이 문서의 snapshot과 일치해야 한다.
- `spot_node`는 별도 option family 없이 pub/sub enum을 그대로 사용해야 한다.
- raw socket / gateway / spot pub / spot sub option 값은 전역에서 겹치지 않아야 한다.

### 6.9 Monitor/event 정책

- `gateway` monitor는 `SERVICE_READY` / `SERVICE_LOST` / `ROUTE_UP` / `ROUTE_DOWN`
  중심 vocabulary를 유지해야 한다.
- `spot` monitor는 일반 peer lifecycle vocabulary를 유지해야 한다.
- socket/service monitor callback도 생성/open 시 non-`NULL`이 강제되어야 한다.

## 7. Change Log

### 2026-03-11

- `1`, `0.2`, `5.1`:
  `gateway` public recv callback에서 `zlink_gateway_msg_kind_t`를 제거하고
  raw multipart callback family를 재사용하도록 정리했다.
- `3`, `0.2`:
  unified `spot` 생성자에서 role selector를 제거하고,
  `spot`/`spot_sub` callback family를 공통 `zlink_spot_handler_fn`으로 정리했다.
- `0.2`, `4`, `5.1`:
  raw socket callback surface를 `zlink_socket_handler_t` descriptor 기반으로 통합했다.
- `1`, `3`, `6.6`:
  public send/recv surface를 `zlink_msg_t` only로 고정하고,
  bytes helper 삭제가 zero-copy 제거를 의미하지 않는다고 명시했다.
- `1`, `6.4`, `6.5`:
  `gateway` public register/update_weight/unregister surface를 제거하고,
  `zlink_gateway_update_peer_weight()`를 canonical weight update path로 고정했다.
- `1`, `6.5`:
  `zlink_gateway_peer_info_t`와 local bound route weight 정책
  (초기값 `0`, local RID update 허용)을 확정했다.
- `3`, `6.2`:
  `spot_node`를 service-bound handle로 고정하고,
  `zlink_spot_node_default_pub()` / `zlink_spot_node_default_sub()`를 삭제 대상으로 명시했다.
- `1.1`, `6.3.1`:
  discovery/registry control-plane transport를 `tcp`, `ws`, `wss`, `tls`로 제한하고,
  mixed deployment 허용/비권장 정책과 회귀테스트를 추가했다.
- `5`, `5.2`, `6.7`:
  public poller API와 poller public type을 canonical 범위에서 제거하고,
  `set_send_ready_handler()` 기반 backpressure notification 정책으로 대체했다.
