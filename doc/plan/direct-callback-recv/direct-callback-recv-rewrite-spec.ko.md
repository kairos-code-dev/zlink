# Direct Callback Recv 재작성 스펙

> 상태 메모
> 이 문서는 재작성 초안이다. 본문에 남아 있는 `*_set_handler()`와 생성 후
> callback 교체 모델은 현재 canonical public API가 아니다. 최신 기준은
> `direct-callback-recv-interface-review.ko.md`와 `core/include/zlink.h`를 따른다.

## 1. 문서 목적

이 문서는 `zlink`의 수신 모델을 `recv()`/`poll()` 기반 pull 방식에서
**callback-only direct dispatch** 방식으로 전면 전환하기 위한 구현 스펙이다.

이 문서는 기존 direct-callback 아이디어 메모를 구체 구현안으로 확장한 문서다.

관련 문서 관계:

- [`spot-node-direct-facade-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/spot-node-direct-facade-plan.ko.md)
  는 `spot_node + default pub/sub facade` 확장 계획이었고,
  본 문서는 그 구조를 바꾸지 않은 채 recv-side callback 모델을 추가하는 구현 스펙이다.
- [`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/spot-proxy-rewrite-spec.ko.md)
  의 data plane worker / mesh / XPUB subscription propagation 구조는
  본 문서와 충돌하지 않으며, 현재 `spot_node` / `spot` 구현의 내부 기반으로 그대로 재사용할 수 있다.

이번 전환은 다음을 전제로 한다.

- 기존 recv/poller 호환성은 유지하지 않는다.
- 수신 API는 callback-only로 재설계한다.
- recv-side 내부 큐와 pipe/session 기반 수신 적재 경로를 제거한다.
- 별도 `resume_recv()` 같은 명시적 재개 API는 도입하지 않는다.
- 수신이 느리면 transport read가 멈추고, 그 결과 sender 쪽에 자연 backpressure가 전파되게 한다.

이 문서의 목표는 "callback API를 추가"하는 것이 아니라
"`zlink`의 기본 수신 의미를 callback으로 다시 정의"하는 것이다.

## 2. 고정 결정

### 2.1 외부 수신 모델

- 모든 recv-capable raw socket/service는 callback을 통해서만 메시지를 전달한다.
- `recv()` 계열 API는 제거한다.
- `POLLIN` readiness 기반 소비 모델은 제거한다.
- recv-capable raw socket/service는 생성 시 callback 등록을 기본으로 한다.
- 생성 후 callback 교체는 허용하지만, callback 제거는 허용하지 않는다.
- callback 등록 후 message arrival 시 라이브러리가 즉시 사용자 callback을 호출한다.
- request/reply 성격의 관리/control reply도 예외 없이 callback/async로 전환한다.

### 2.2 내부 수신 경로

목표 경로는 다음으로 고정한다.

```text
network -> decoder -> engine -> callback -> return -> next read
```

삭제 대상 경로는 다음이다.

```text
network -> decoder -> session->push_msg -> pipe/fq/inproc queue -> recv()
```

### 2.3 callback 실행 모델

- callback은 owning I/O thread에서 inline으로 실행한다.
- callback return이 곧 "해당 메시지 소비 완료"를 의미한다.
- callback이 끝나기 전에는 같은 connection의 다음 read를 다시 arm하지 않는다.
- callback이 느리면 해당 connection의 recv-side throughput이 감소하고,
  transport/OS/TCP backpressure가 sender 쪽으로 전파된다.

### 2.4 explicit resume API 없음

- `resume_recv()`
- `ack()`
- `continue_read()`
- 사용자 호출형 flow-control API

위 API는 도입하지 않는다.

수신 재개는 callback return 또는 handler 재등록으로만 일어난다.

### 2.5 send 모델

- `send()`/`msg_send()` 계열은 유지한다.
- blocking send는 유지한다.
- nonblocking send와 send-side `POLLOUT`는 유지한다.
- send-side writable readiness는 poller 기반으로 유지하고,
  별도 `POLLOUT` callback push API는 도입하지 않는다.
- recv-side 설계 변경이 send API를 비동기 queue 모델로 강제하지는 않는다.

이유:

- `recv`는 message delivery 자체를 push하는 callback 모델로 자연스럽게 바뀌지만,
  `send`는 애플리케이션이 "지금 보내고 싶은가"를 결정하는 demand-driven 동작이다.
- `POLLOUT`은 message가 아니라 writable 상태 변화 알림이므로,
  recv callback과 같은 의미로 다루기 어렵다.
- `POLLOUT`을 callback push로 바꾸면 one-shot/level-trigger, re-arm 시점,
  callback 폭주 방지 규약까지 새로 정의해야 한다.
- 이번 재작성의 목표는 recv-side 단순화와 queue 제거이지,
  send readiness 모델까지 event push API로 재설계하는 것이 아니다.

### 2.6 thread safety

최신 public contract는 recv 재작성 문서만으로 결정하지 않는다.

- thread-safety canonical 기준은
  [`thread-safe-socket-plan.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/thread-safe/thread-safe-socket-plan.ko.md)
  와 `core/include/zlink.h`를 따른다.
- recv-capable raw socket, `discovery`, `gateway`, `spot` 계열, monitor handle은
  모두 thread-safe only public handle로 본다.
- same-handle operational API는 라이브러리가 직렬화한다.
- `close` / `destroy`는 예외적으로 더 보수적이며, in-flight callback/API가 있으면
  cross-thread 종료는 `EBUSY`다.
- send-ready setter는 replace-only이며, 동일 handle의 send-ready callback 안
  재진입 호출은 `EDEADLK`다.

## 3. 유지되는 의미와 변경되는 의미

### 3.1 유지되는 의미

| 항목 | 유지 방향 |
|---|---|
| peer별 메시지 순서 | 유지 |
| blocking send semantics | 유지 |
| `PUB/SUB` slow peer 정책 (`nodrop`, drop, disconnect 등) | 유지 |
| `STREAM` direct dispatch 개념 | 일반화 |
| `SPOT sub` handler 기반 소비 방향 | 기본 모델로 승격 |

### 3.2 변경되는 의미

| 항목 | 현재 | 변경 후 |
|---|---|---|
| 수신 시작 주체 | 앱이 `recv()` 호출 | 라이브러리가 callback push |
| recv-side flow control | 앱의 recv loop 속도 | callback return 속도 |
| `POLLIN` 의미 | readiness 후 `recv()` | zlink object에는 의미 없음 |
| 메시지 수명 | `recv()` 반환 후 호출자 소유 | callback scope 내 ownership 전달 |
| timeout 기반 recv | `RCVTIMEO`/blocking recv | 제거 |

### 3.3 이 변경이 제품 의미를 크게 바꾸지 않는 이유

현재 제품 범위에서는 다음 조건이 성립한다.

- `PUB/SUB` slow peer 정책은 이미 존재한다.
- `STREAM`과 `SPOT sub`에는 callback 수신의 선행 형태가 이미 있다.

따라서 이번 전환의 본질은 "ZeroMQ 스타일 pull API 제거"이지,
메시징 제품 의미 자체를 전부 다른 것으로 바꾸는 작업은 아니다.

## 4. 적용 범위

### 4.1 raw socket

대상 recv-capable socket은 다음과 같다.

- `ZLINK_SUB`
- `ZLINK_DEALER`
- `ZLINK_ROUTER`
- `ZLINK_PAIR`
- `ZLINK_STREAM`

다음은 수신 callback 대상이 아니다.

- `ZLINK_PUB`
- `ZLINK_XPUB`

비고:

- `XPUB`의 downstream subscription/unsubscription 수신 기능 자체를 제거하는 것은 아니다.
- `XPUB`는 SPOT data plane 내부 구현에서 subscription propagation 용도로 계속 사용할 수 있다.
- 단지 이를 public recv/callback 대상 socket으로 노출하지 않는다는 의미다.

### 4.2 service

대상 service는 다음과 같다.

- `gateway`
- `spot`
- socket monitor
- service monitor

service 통합 방향은 다음으로 고정한다.

- `gateway`와 `receiver`는 `gateway` 하나로 통합한다.
- 통합 `gateway`는 client-side request 발신과 server-side request 수신을 모두 담당한다.
- 기존 `receiver` wrapper가 유지하던 register/unregister cache와 전용 상태는 제거한다.
- registry bootstrap/control의 canonical entry는 계속 `discovery`다.
- `spot_node` / `spot`의 현재 layering은 유지한다.
- `spot`은 내부적으로 `spot_node`에 inproc으로 연결된 facade로 유지한다.
- `spot`의 역할은 `spot_pub + spot_sub`를 하나로 합친 unified facade다.
- 이번 문서는 `spot_node` / `spot` 구조를 재설계하지 않고 recv-side callback만 추가한다.
- standalone `spot_pub` / `spot_sub` public handle은 정리 대상이 될 수 있지만,
  `spot_node` 자체의 위치투명 pub/sub service 의미는 유지한다.

### 4.3 비대상

다음은 이번 문서의 직접 구현 대상이 아니다.

- send path 전체의 비동기 queue화
- 전체 소켓 완전 thread-safe 보장

### 4.4 control plane 규칙

- data plane 수신과 control plane reply를 분리해서 예외 처리하지 않는다.
- request/reply 성격의 관리/control 경로도 public recv API에 의존하지 않는다.
- control/status/ack/reply/event는 모두 callback 또는 내부 async completion으로 처리한다.
- 이번 재작성 이후 public surface에는 "동기 recv로 reply를 기다리는 관리 API"를 남기지 않는다.

## 5. 삭제/변경/추가 API

## 5.1 삭제되는 public API

### 5.1.1 raw socket recv API

| API | 조치 | 이유 |
|---|---|---|
| `zlink_recv` | 삭제 | callback-only 수신 모델로 전환 |
| `zlink_msg_recv` | 삭제 | multipart도 callback으로 직접 전달 |
| `zlink_stream_detach` | 삭제 | callback 제거를 지원하지 않고 attach 재호출로 교체 |

### 5.1.2 service recv API

| API | 조치 | 이유 |
|---|---|---|
| `zlink_gateway_recv` | 삭제 | `gateway`는 handler-only |
| `zlink_receiver_recv` | 삭제 | `receiver` 타입 자체를 제거하고 `gateway`로 통합 |
| `zlink_spot_sub_recv` | 삭제 | `spot_sub`는 callback-only 수신 |
| `zlink_spot_node_recv` | 삭제 | `spot_node`도 callback-only 수신 |
| `zlink_monitor_recv` | 삭제 | monitor event는 callback-only |
| `zlink_service_monitor_recv` | 삭제 | service monitor event는 callback-only |

비고:

- control/status/ack/reply 성격의 public recv API도 동일 원칙으로 삭제 또는 callback API로 전환한다.
- 이번 재작성에서 public 관리/control plane recv 예외는 두지 않는다.

### 5.1.3 service 타입 통합으로 삭제되는 API

#### 5.1.3.1 `receiver` -> `gateway`

| API | 조치 | 대체 |
|---|---|---|
| `zlink_receiver_new` | 삭제 | `zlink_gateway_new` |
| `zlink_receiver_bind` | 삭제 | `zlink_gateway_bind` |
| `zlink_receiver_connect_registry` | 삭제 | `zlink_discovery_connect_registry` |
| `zlink_receiver_register` | 삭제 | 삭제 |
| `zlink_receiver_update_weight` | 삭제 | `zlink_gateway_update_peer_weight` |
| `zlink_receiver_unregister` | 삭제 | 삭제 |
| `zlink_receiver_register_result` | 삭제 | 삭제 |
| `zlink_receiver_set_tls_server` | 삭제 | `zlink_gateway_set_tls_server` |
| `zlink_receiver_last_endpoint` | 삭제 | `zlink_gateway_last_endpoint` |
| `zlink_receiver_peer_info` | 삭제 | `zlink_gateway_peer_info` |
| `zlink_receiver_set_option` | 삭제 | `zlink_gateway_set_option` |
| `zlink_receiver_set_routing_id` | 삭제 | `zlink_gateway_set_routing_id` |
| `zlink_receiver_routing_id` | 삭제 | `zlink_gateway_routing_id` |
| `zlink_receiver_router_peers` | 삭제 | `zlink_gateway_router_peers` |
| `zlink_receiver_monitor_open` | 삭제 | `zlink_gateway_monitor_open` |
| `zlink_receiver_destroy` | 삭제 | `zlink_gateway_destroy` |

정리 원칙:

- `receiver`는 public type에서 제거한다.
- 통합 `gateway`는 단일 ROUTER 기반의 bidirectional service handle이 된다.
- public `gateway`는 data-plane/LB handle로 제한하고 별도 public register/unregister API는 두지 않는다.
- 공개 가중치 변경 surface는 `zlink_gateway_update_peer_weight()` 하나로 정리한다.
- receiver 전용 register/unregister result 상수도 함께 제거한다.

#### 5.1.3.2 `spot_pub` / `spot_sub` 정리, `spot_node` 구조 유지

| API | 조치 | 대체 |
|---|---|---|
| `zlink_spot_pub_new` | 삭제 | `zlink_spot_new` |
| `zlink_spot_pub_destroy` | 삭제 | `zlink_spot_destroy` |
| `zlink_spot_pub_set_option` | 삭제 | `zlink_spot_set_pub_option` 또는 `zlink_spot_node_set_pub_option` |
| `zlink_spot_pub_publish` | 삭제 | `zlink_spot_publish` 또는 `zlink_spot_node_publish` |
| `zlink_spot_pub_publish_bytes` | 삭제 | 삭제 |
| `zlink_spot_sub_new` | 삭제 | `zlink_spot_new` |
| `zlink_spot_sub_destroy` | 삭제 | `zlink_spot_destroy` |
| `zlink_spot_sub_set_option` | 삭제 | `zlink_spot_set_sub_option` 또는 `zlink_spot_node_set_sub_option` |
| `zlink_spot_sub_subscribe` | 삭제 | `zlink_spot_subscribe` 또는 `zlink_spot_node_subscribe` |
| `zlink_spot_sub_subscribe_pattern` | 삭제 | `zlink_spot_subscribe_pattern` 또는 `zlink_spot_node_subscribe_pattern` |
| `zlink_spot_sub_unsubscribe` | 삭제 | `zlink_spot_unsubscribe` 또는 `zlink_spot_node_unsubscribe_filter` |
| `zlink_spot_sub_set_handler` | 삭제 | 생성 시 callback 고정 모델로 대체 |
| `zlink_spot_sub_monitor_open` | 삭제 | `zlink_spot_monitor_open(..., role, ...)` |

정리 원칙:

- `spot_node`는 public SPOT service의 canonical handle로 유지한다.
- `spot`은 지금처럼 내부적으로 `spot_node`에 inproc으로 연결된 facade로 유지한다.
- `spot`은 `spot_pub` + `spot_sub`를 하나로 감싼 public facade다.
- standalone public `spot_pub` / `spot_sub` 생성자는 유지하지 않는다.
- node-owned default pub/sub facade는 internal/runtime 개념으로만 유지한다.
- 이번 재작성의 SPOT 범위는 구조 재설계가 아니라 recv/callback 의미 정렬이다.
- `spot` facade 생성은 기존 `spot_node`를 인자로 받는 attach 모델로 정렬한다.

### 5.1.4 recv 전용 옵션/의미

| 항목 | 조치 |
|---|---|
| `ZLINK_RCVMORE` | 삭제 |
| `ZLINK_RCVTIMEO` | 삭제 |
| `ZLINK_GATEWAY_OPT_RCVTIMEO` | 삭제 |
| `ZLINK_RECEIVER_OPT_RCVTIMEO` | 삭제 |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | 삭제 |
| `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | 삭제 |
| `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | 삭제 |
| zlink object에 대한 `POLLIN` | 제거 |
| `ZLINK_EVENTS`에서 `POLLIN` 노출 | 제거 |

비고:

- raw file descriptor poller의 `POLLIN`은 유지한다.
- zlink socket/service object에만 `POLLIN` 의미를 제거한다.

## 5.2 유지되는 public API

| API/개념 | 유지 방향 |
|---|---|
| `zlink_send`, `zlink_msg_send` | 유지 |
| `zlink_close` | 유지 |
| `bind/connect/disconnect/setsockopt/getsockopt` | 유지 |
| send-side `POLLOUT` | 유지 |
| `zlink_stream_send`, `zlink_stream_send_msg` | 유지 |
| `zlink_socket_monitor_open` | 유지 |
| `zlink_discovery_connect_registry` | registry bootstrap의 canonical entry로 유지 |
| `zlink_poller_*` 자체 | 유지, 단 zlink object에는 `POLLOUT` 중심으로 축소 |

## 5.3 추가되는 public API

### 5.3.1 공통 raw socket callback

```c
typedef void (*zlink_socket_msg_handler_fn) (
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *parts,
  size_t part_count);

void *zlink_socket (void *ctx_,
                    int type_);

void *zlink_socket_with_handler (void *ctx_,
                                 int type_,
                                 zlink_socket_msg_handler_fn handler_);

int zlink_socket_set_msg_handler (void *s_,
                                  zlink_socket_msg_handler_fn handler_);
```

계약:

- generic recv-capable raw socket callback은 `zlink_socket_with_handler()` 경로에만 유효하다.
- `SUB`, `DEALER`, `ROUTER`, `PAIR`는 생성 시 callback을 반드시 받아야 하며
  `zlink_socket_with_handler()`로만 생성한다.
- `PUB`, `XPUB`, `STREAM`은 `zlink_socket()`를 사용한다.
- `source_rid`는 "이 메시지를 누가 보냈는지"를 식별하는 sender peer routing id다.
- 메시지 shape는 과거 `recv()`가 노출하던 multipart shape를 최대한 유지한다.
- `ROUTER`는 routing id를 callback 첫 인자로 전달하고, payload multipart에서는 routing-id frame을 제거한다.
- `SUB`의 topic frame은 기존 frame layout을 유지한다.
- 각 `zlink_msg_t`의 ownership은 callback에 전달된다.
- callback은 각 part를 정확히 한 번 `zlink_msg_close()` 하거나,
  다른 message로 `zlink_msg_move()` 하거나, send API에 소비시켜야 한다.
- array 포인터 자체는 callback return 후 유지하면 안 된다.
- `userdata` 인자는 의도적으로 제공하지 않는다.
- `zlink_socket_with_handler()`의 `handler_ == NULL`은 허용하지 않으며 `EINVAL`을 반환한다.
- 생성 후 handler 교체가 필요하면 `zlink_socket_set_msg_handler()`를 다시 호출해 새 callback으로 덮어쓴다.
- callback 제거 API는 제공하지 않는다.
- `zlink_socket()`로 generic recv-capable type을 생성하려고 하면 `EINVAL`이다.
- `zlink_socket_with_handler()`로 `PUB`, `XPUB`, `STREAM`을 생성하려고 하면 `EINVAL`이다.

`userdata` 제거 이유:

- 이번 재작성은 callback 고정 비용이 낮은 binding 구현을 우선한다.
- binding은 handle별로 callback closure를 매번 새로 pinning하지 않고,
  고정 callback 하나를 등록한 뒤 socket/service handle 기반 registry lookup으로
  사용자 객체/컨텍스트를 찾는 방식을 기본 전략으로 삼는다.
- `userdata`를 공통 C ABI에 넣으면 binding마다 callback/context 쌍을 별도로 유지하게 되어
  callback 설치/교체 비용과 관리 복잡도가 커진다.
- `source_rid`는 sender 식별용 메타데이터로 계속 전달되지만,
  binding-side callback owner lookup key는 아니다.

### 5.3.2 stream callback API 정리

기존 `STREAM` callback API는 유지하되, 의미를 다음으로 정렬한다.

- `zlink_stream_attach_raw`
- `zlink_stream_attach_len32be`
- `zlink_stream_attach`

정렬 규칙:

- `STREAM`은 raw socket 공통 `zlink_socket_set_msg_handler()` 대신
  기존 stream 전용 callback API를 계속 사용한다.
- 즉 `STREAM`은 raw socket 생성 시 generic handler를 받는 규칙의 예외다.
- `STREAM` callback의 최초 등록은 생성자가 아니라 `attach_*()` 호출로 수행한다.
- 이는 `STREAM`의 byte-stream / LEN32BE packet 특화 semantics를 보존하기 위함이다.
- `zlink_stream_detach()`는 제거한다.
- `zlink_stream_attach_*()`를 다시 호출하면 기존 callback은 새 callback으로 교체된다.
- `NULL` callback은 허용하지 않으며 `EINVAL`을 반환한다.

### 5.3.3 gateway 통합 API

```c
typedef enum zlink_gateway_msg_kind_t {
  ZLINK_GATEWAY_MSG_REQUEST = 1,
  ZLINK_GATEWAY_MSG_REPLY = 2,
  ZLINK_GATEWAY_MSG_CONTROL = 3
} zlink_gateway_msg_kind_t;

typedef void (*zlink_gateway_handler_fn) (
  zlink_gateway_msg_kind_t kind,
  const zlink_routing_id_t *source_rid,
  zlink_msg_t *parts,
  size_t part_count);

void *zlink_gateway_new (void *ctx,
                         void *discovery,
                         const char *service_name,
                         const char *routing_id,
                         zlink_gateway_handler_fn handler);

int zlink_gateway_bind (void *gateway,
                        const char *bind_endpoint);

int zlink_gateway_set_tls_server (void *gateway,
                                  const char *cert,
                                  const char *key);

int zlink_gateway_set_tls_client (void *gateway,
                                  const char *ca_cert,
                                  const char *hostname,
                                  int trust_system);

int zlink_gateway_last_endpoint (void *gateway,
                                 char *endpoint,
                                 size_t *size);

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

int zlink_gateway_peer_info (void *gateway,
                             const zlink_routing_id_t *routing_id,
                             zlink_gateway_peer_info_t *info);

int zlink_gateway_router_peers (void *gateway,
                                zlink_gateway_peer_info_t *peers,
                                size_t *count);

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

int zlink_gateway_update_peer_weight (void *gateway,
                                      const zlink_routing_id_t *routing_id,
                                      uint32_t weight);

int zlink_gateway_set_handler (void *gateway,
                               zlink_gateway_handler_fn handler);
```

의미:

- 통합 `gateway`는 기존 `gateway`와 `receiver`를 모두 대체한다.
- 단일 ROUTER가 client-side outbound request와 server-side inbound request를 모두 담당한다.
- `gateway`는 multi-service handle이 아니라 service-bound handle이다.
- 생성 시 handler 등록을 기본으로 한다.
- callback의 `kind`는 inbound message 성격을 구분한다.
  - `ZLINK_GATEWAY_MSG_REQUEST`: bound service endpoint로 들어온 request
  - `ZLINK_GATEWAY_MSG_REPLY`: outbound request에 대한 provider reply
  - `ZLINK_GATEWAY_MSG_CONTROL`: topology/error/status 같은 control plane event
- `source_rid`는 항상 sender peer routing id다.
- service name은 `zlink_gateway_new()`에서 고정되므로 callback과 send 계열 함수는
  이를 다시 받지 않는다.
- `zlink_gateway_send()` / `zlink_gateway_send_rid()`는 유지하되, callback 안에서 reply 송신에도 사용한다.
- `gateway` 공개 surface는 data-plane/LB handle로 제한하고 별도 public register/unregister API는 두지 않는다.
- 공개 가중치 변경 surface는 `zlink_gateway_update_peer_weight()` 하나로 정리한다.
- `zlink_gateway_peer_info()` / `zlink_gateway_router_peers()`는 `weight`를 포함한
  `zlink_gateway_peer_info_t`를 사용한다.
- `zlink_gateway_update_peer_weight()` 성공 후에는 local snapshot이 먼저 갱신되고,
  이후 discovery/registry runtime을 통해 같은 service를 보는 다른 handle에도 동기화되어야 한다.
- 생성 후 handler 교체가 필요하면 `zlink_gateway_set_handler()`를 다시 호출해 새 callback으로 덮어쓴다.
- 통합 `gateway`는 single handle에서 client-side TLS와 server-side TLS 설정을 모두 가질 수 있다.
- `zlink_gateway_set_tls_client()`와 `zlink_gateway_set_tls_server()`는 서로 배타적이지 않다.
- 실제로 필요한 역할을 시작하기 전 첫 `connect`/`bind` 이전에 관련 TLS 설정을 끝내는 것을 기본 계약으로 둔다.
- `REQUEST` / `REPLY` demux는 unified gateway ROUTER 수신 경로에서 수행한다.
- `CONTROL` demux는 discovery/service-control runtime의 topology/error/status를
  unified gateway callback shape로 정규화하는 경로에서 수행한다.
- gateway monitor는 route/service availability 변화만 공개하고,
  registration result event는 공개하지 않는다.

### 5.3.4 spot facade callback API

```c
typedef enum zlink_spot_role_t {
  ZLINK_SPOT_ROLE_PUB = 1,
  ZLINK_SPOT_ROLE_SUB = 2
} zlink_spot_role_t;

typedef void (*zlink_spot_handler_fn) (
  const zlink_routing_id_t *source_rid,
  const char *topic,
  size_t topic_len,
  zlink_msg_t *parts,
  size_t part_count);

void *zlink_spot_new (void *spot_node,
                      int roles,
                      zlink_spot_handler_fn handler);

int zlink_spot_destroy (void **spot);
int zlink_spot_publish (void *spot,
                        const char *topic_id,
                        zlink_msg_t *parts,
                        size_t part_count,
                        int flags);
int zlink_spot_subscribe (void *spot, const char *topic_id);
int zlink_spot_subscribe_pattern (void *spot, const char *pattern);
int zlink_spot_unsubscribe (void *spot,
                            const char *topic_id_or_pattern);
int zlink_spot_set_handler (void *spot,
                            zlink_spot_handler_fn handler);
int zlink_spot_set_option (void *spot,
                           int role,
                           int option,
                           const void *optval,
                           size_t optvallen);
int zlink_spot_peers (void *spot,
                      int role,
                      zlink_peer_info_t *peers,
                      size_t *count);
void *zlink_spot_monitor_open (void *spot,
                               int role,
                               int events,
                               zlink_service_monitor_handler_fn handler);
```

변경점:

- `spot`은 `spot_node`를 대체하지 않는다.
- `spot`은 지금처럼 내부적으로 `spot_node`에 inproc으로 연결된 facade다.
- `spot`은 기능적으로 `spot_pub` + `spot_sub`를 하나로 합친 unified facade다.
- 이번 재작성은 이 layering을 바꾸지 않고 callback 수신만 추가한다.
- `spot` facade는 publish, subscribe, callback 수신을 계속 제공할 수 있다.
- `spot` public send/recv surface는 `msg_t` 기반으로만 유지하고 `*_bytes` helper는 두지 않는다.
- `zlink_spot_new()`는 `ctx`가 아니라 기존 `spot_node` handle에 attach되는 생성자다.
- 생성 시 `roles` bitmask로 `PUB`, `SUB`, 또는 둘 다를 선언한다.
- `roles`에 `ZLINK_SPOT_ROLE_SUB`가 포함되면 생성 시 non-`NULL` handler가 필수다.
- `roles == ZLINK_SPOT_ROLE_PUB`이면 `handler == NULL`을 허용한다.
- `source_rid`는 해당 SPOT 메시지를 보낸 sender peer routing id다.
- `roles`가 `SUB`를 포함하는데 `handler == NULL`이면 `EINVAL`이다.
- handler 교체가 필요하면 `zlink_spot_set_handler()`를 다시 호출해 새 callback으로 덮어쓴다.
- `role`은 public handle 통합 후에도 pub/sub별 option, peer, monitor 경로를 구분하기 위한 selector다.
- internal inproc bridge와 `spot_node` data plane ownership은 현 구조를 유지한다.
- `spot` facade가 여러 개 필요하면 같은 `spot_node`에 여러 facade를 attach할 수 있다.
- bind/connect/register/discovery/TLS 같은 node-global 동작은 계속 `spot_node_*` API가 담당한다.
- 각 `spot` facade는 자기 자신만의 SUB subscription set을 가진다.
- 따라서 `zlink_spot_set_handler()`는 해당 `spot` facade가 `zlink_spot_subscribe*()`
  로 등록한 topic/pattern에 매칭된 메시지만 전달받는다.

추가로 `spot_node` canonical callback API는 다음으로 정렬한다.

```c
int zlink_spot_node_set_handler (void *spot_node,
                                 zlink_spot_handler_fn handler);
```

의미:

- `zlink_spot_node_recv()`는 삭제하고 `zlink_spot_node_set_handler()`가 canonical recv path가 된다.
- node-owned default sub facade에 callback을 설치하는 의미로 유지한다.
- `handler == NULL`은 허용하지 않으며 callback 제거 API는 제공하지 않는다.
- `zlink_spot_node_set_handler()`는 node-owned default sub facade의 subscription set에
  매칭된 메시지만 전달받는다.
- 즉 `spot_node`에서 `zlink_spot_node_subscribe*()`로 등록하지 않은 topic은
  `zlink_spot_node_set_handler()`로 수신되면 안 된다.
- 반대로 특정 `spot` facade가 `zlink_spot_subscribe*()`로 등록한 topic은
  그 facade의 handler로만 전달되고 `spot_node` handler로 섞여 들어가면 안 된다.

### 5.3.5 monitor callback

```c
typedef void (*zlink_monitor_handler_fn) (const zlink_monitor_event_t *event);

void *zlink_socket_monitor_open (void *socket,
                                 int events,
                                 zlink_monitor_handler_fn handler);

int zlink_monitor_set_handler (void *monitor_socket,
                               zlink_monitor_handler_fn handler);

typedef void (*zlink_service_monitor_handler_fn) (
  const zlink_service_event_t *event);

void *zlink_gateway_monitor_open (void *gateway,
                                  int events,
                                  zlink_service_monitor_handler_fn handler);

void *zlink_spot_monitor_open (void *spot,
                               int role,
                               int events,
                               zlink_service_monitor_handler_fn handler);

int zlink_service_monitor_set_handler (void *monitor,
                                       zlink_service_monitor_handler_fn handler);
```

공통 규칙:

- 모든 `*_set_handler()` / `attach_*()` 계열은 `NULL` callback을 허용하지 않는다.
- callback 제거 API는 제공하지 않는다.
- raw socket과 service는 생성 시 callback 등록을 기본으로 하고,
  생성 후 callback 교체는 같은 setter/attach API를 재호출해 수행한다.
- monitor도 open 시 callback 등록을 기본으로 하고,
  생성 후 callback 교체는 같은 setter를 재호출해 수행한다.
- `userdata`는 어떤 callback API에도 제공하지 않는다.
- "일시 정지 후 나중에 재개"를 위한 handler clear/pause API도 제공하지 않는다.

### 5.3.6 생성 시 callback 등록 세부 규칙

- generic recv-capable raw socket은 `zlink_socket_with_handler()`로 생성하고
  non-`NULL` callback을 요구한다.
- send-only 타입 (`PUB`, `XPUB` 등)은 `zlink_socket()`를 사용한다.
- recv-capable service 생성자도 기본적으로 non-`NULL` callback을 요구한다.
- 단 `spot`은 `PUB` only role일 때만 `handler == NULL`을 허용한다.
- `STREAM`은 예외이며 `zlink_socket()`로 생성하고 `attach_*()`로 최초 등록한다.
- callback 교체는 허용하지만 제거는 허용하지 않는다.
- callback 교체 시 이미 실행 중인 in-flight callback은 기존 handler로 마무리하고,
  이후 새로 도착한 메시지부터 새 handler를 사용한다.
- 생성 시 callback 등록 실패는 object 생성 실패로 처리한다.
- 생성 이후 `set_handler()` 실패는 기존 handler를 유지한 채 오류를 반환한다.

### 5.3.7 서비스별 옵션 표

#### Discovery

| 항목 | API/개념 | 유지/변경 | 비고 |
|---|---|---|---|
| service type scope | `zlink_discovery_new_typed(ctx, service_type)` | 유지 | `GATEWAY` 또는 `SPOT` scope는 생성 시 고정 |
| registry bootstrap endpoint | `zlink_discovery_connect_registry()` | 유지 | registry broadcast/uplink bootstrap |
| routing id override | `zlink_discovery_set_routing_id()` | 유지 | 첫 사용 전 representative identity 고정 |
| routing id 조회 | `zlink_discovery_routing_id()` | 유지 | representative identity 조회 |
| subscription set | `zlink_discovery_subscribe()` / `zlink_discovery_unsubscribe()` | 유지 | service name watch set |
| provider snapshot | `zlink_discovery_get_receivers()` | 유지 | 현재 provider snapshot 조회 |
| availability query | `zlink_discovery_receiver_count()` / `zlink_discovery_service_available()` | 유지 | callback 모델과 무관한 query API |
| monitor | `zlink_discovery_monitor_open()` | 유지 | discovery event 관찰 |
| public service option setter | 없음 | 없음 | `gateway`/`spot`처럼 public `set_option()` surface를 두지 않음 |
| public TLS setter | 없음 | 없음 | discovery 자체 TLS는 bootstrap/control runtime 내부 정책에 귀속 |

Discovery 참고 규칙:

- discovery는 recv callback rewrite의 직접 대상이 아니다.
- discovery는 data plane recv facade가 아니라 topology/control plane snapshot과 watch를 제공한다.
- 따라서 `gateway`/`spot`처럼 callback-driven data receive option surface를 새로 만들지 않는다.
- discovery monitor만 callback/event 모델에 맞춰 정리하면 충분하다.

#### SpotNode (참고용, public canonical type)

| 항목 | API/개념 | 현재 | 이번 문서 처리 |
|---|---|---|---|
| bind endpoint | `zlink_spot_node_bind()` | 유지 | 유지 |
| manual mesh peer | `zlink_spot_node_connect_peer_pub()` / `disconnect_peer_pub()` | 유지 | 유지 |
| registry/discovery attach | `zlink_spot_node_set_discovery()` | 유지 | 유지 |
| registry advertise | `zlink_spot_node_register()` / `unregister()` | 유지 | 유지 |
| TLS server | `zlink_spot_node_set_tls_server()` | 유지 | 유지 |
| TLS client | `zlink_spot_node_set_tls_client()` | 유지 | 유지 |
| default pub option | `zlink_spot_node_set_pub_option()` | 유지 | 유지 |
| default sub option | `zlink_spot_node_set_sub_option()` | 유지 | 유지 |
| node-owned default pub/sub facade | `zlink_spot_node_default_pub()` / `default_sub()` | 유지 | 유지 가능 |
| node-owned recv/callback | `zlink_spot_node_recv()` / `zlink_spot_node_set_handler()` | 유지 | recv 삭제, callback-only로 정렬 |

SpotNode 참고 규칙:

- `spot_node`는 위치투명한 SPOT pub/sub service의 canonical public type으로 유지한다.
- `spot` facade는 이 `spot_node` 위에 inproc으로 연결된 편의 surface일 뿐, 구조를 대체하지 않는다.
- `spot` facade의 역할은 `spot_pub` + `spot_sub` 통합 surface를 제공하는 것이다.
- 이번 재작성의 목적은 `spot_node` 제거가 아니라 recv-side callback 정렬이다.
- `zlink_spot_node_set_handler()`와 `zlink_spot_set_handler()`는 같은 것이 아니다.
- `zlink_spot_node_set_handler()`는 node-owned default sub의 구독 집합만 소비한다.
- 각 `zlink_spot_set_handler()`는 attach된 해당 facade의 구독 집합만 소비한다.
- 두 경로는 같은 `spot_node` data plane 위에 공존할 수 있지만 subscription scope는 섞이지 않는다.

#### Registry

| 항목 | API/개념 | 유지/변경 | 비고 |
|---|---|---|---|
| pub/router endpoints | `zlink_registry_set_endpoints()` | 유지 | registry broadcast / control endpoint |
| registry id | `zlink_registry_set_id()` | 유지 | cluster sync identity |
| peer registries | `zlink_registry_add_peer()` | 유지 | multi-registry sync peer PUB endpoint |
| heartbeat interval/timeout | `zlink_registry_set_heartbeat()` | 유지 | expiry / liveness tuning |
| broadcast interval | `zlink_registry_set_broadcast_interval()` | 유지 | service list publish cadence |
| internal socket option tuning | `zlink_registry_setsockopt()` | 유지 | `PUB` / `ROUTER` / `PEER_SUB` role별 tuning |
| lifecycle start | `zlink_registry_start()` | 유지 | internal thread start |
| topology snapshot/query | `zlink_registry_topology_snapshot()` / `query()` | 유지 | control/query plane |
| external query client | `zlink_registry_query_client_*` | 유지 | remote snapshot client |
| public callback recv surface | 없음 | 없음 | registry는 data receive facade가 아님 |

Registry 참고 규칙:

- registry는 이번 callback recv rewrite의 직접 대상이 아니다.
- registry의 public tuning surface는 그대로 유지하되, 이는 data plane recv option이 아니라
  registry control/broadcast runtime 설정이다.
- `zlink_registry_setsockopt()`는 참고용으로 남기며, `gateway`/`spot`처럼 callback data path와
  결합된 option 표와는 성격이 다르다.

#### Gateway

| 항목 | API | 유지/변경 | 비고 |
|---|---|---|---|
| send HWM | `ZLINK_GATEWAY_OPT_SNDHWM` | 유지 | unified gateway ROUTER send path |
| recv HWM | `ZLINK_GATEWAY_OPT_RCVHWM` | 유지 | unified gateway ROUTER receive path |
| send timeout | `ZLINK_GATEWAY_OPT_SNDTIMEO` | 유지 | send-side only |
| recv timeout | `ZLINK_GATEWAY_OPT_RCVTIMEO` | 삭제 | callback-only recv로 의미 없음 |
| linger | `ZLINK_GATEWAY_OPT_LINGER` | 유지 | close/destroy 시 적용 |
| send buffer | `ZLINK_GATEWAY_OPT_SNDBUF` | 유지 | socket buffer tuning |
| recv buffer | `ZLINK_GATEWAY_OPT_RCVBUF` | 유지 | socket buffer tuning |
| load balancing | `zlink_gateway_set_lb_strategy()` | 유지 | service별 RR/weighted |
| routing id override | `zlink_gateway_set_routing_id()` | 유지 | 첫 bind/connect 전 |
| TLS client | `zlink_gateway_set_tls_client()` | 유지 | outbound provider connect 전 |
| TLS server | `zlink_gateway_set_tls_server()` | 유지 | inbound bind 전 |

Gateway 통합 규칙:

- 과거 `receiver`의 `set_option()`은 unified `gateway` option surface로 흡수한다.
- `receiver` 전용 `RCVTIMEO` 의미는 제거한다.
- request/reply/control callback 모델과 무관한 transport tuning만 option으로 남긴다.

#### Spot

| role | 항목 | API/상수 | 유지/변경 | 비고 |
|---|---|---|---|---|
| `PUB` | send HWM | `ZLINK_SPOT_PUB_OPT_SNDHWM` | 유지 | publish path |
| `PUB` | send timeout | `ZLINK_SPOT_PUB_OPT_SNDTIMEO` | 유지 | publish path |
| `PUB` | linger | `ZLINK_SPOT_PUB_OPT_LINGER` | 유지 | destroy/close |
| `PUB` | nodrop | `ZLINK_SPOT_PUB_OPT_NODROP` | 유지 | slow peer policy |
| `PUB` | mode | `ZLINK_SPOT_PUB_OPT_MODE` | `ENOTSUP` | `spot-proxy-rewrite-spec`에서 의미 제거 |
| `PUB` | queue hwm | `ZLINK_SPOT_PUB_OPT_QUEUE_HWM` | `ENOTSUP` | `spot-proxy-rewrite-spec`에서 의미 제거 |
| `PUB` | queue full policy | `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY` | `ENOTSUP` | `spot-proxy-rewrite-spec`에서 의미 제거 |
| `PUB` | send buffer | `ZLINK_SPOT_PUB_OPT_SNDBUF` | 유지 | socket buffer tuning |
| `PUB` | recv buffer | `ZLINK_SPOT_PUB_OPT_RCVBUF` | 유지 | socket buffer tuning |
| `SUB` | recv HWM | `ZLINK_SPOT_SUB_OPT_RCVHWM` | 유지 | internal fanout / mesh receive path |
| `SUB` | recv timeout | `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | 삭제 | callback-only recv로 의미 없음 |
| `SUB` | linger | `ZLINK_SPOT_SUB_OPT_LINGER` | 유지 | destroy/close |
| `SUB` | queue nodrop | `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | 삭제 | public recv queue 제거 |
| `SUB` | queue full policy | `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | 삭제 | public recv queue 제거 |
| `SUB` | send buffer | `ZLINK_SPOT_SUB_OPT_SNDBUF` | 유지 | socket buffer tuning |
| `SUB` | recv buffer | `ZLINK_SPOT_SUB_OPT_RCVBUF` | 유지 | socket buffer tuning |
| `PUB/SUB` | role selector | `zlink_spot_set_option(..., role, ...)` | 유지 | invalid role/option 조합은 `EINVAL` |
| `PUB/SUB` | attach target | `zlink_spot_new(spot_node, ...)` | 유지 | existing `spot_node`에 facade attach |

Spot 통합 규칙:

- `spot`은 `spot_pub` + `spot_sub`를 하나로 합친 facade다.
- `spot`은 role별 option namespace를 유지하지만 public handle은 하나만 둔다.
- `PUB` only spot은 subscribe/callback API를 호출할 수 없고, 이런 호출은 `ENOTSUP`다.
- `SUB` role이 없는 spot에 `zlink_spot_set_handler()`를 호출하면 `ENOTSUP`다.
- node-global 설정(TLS, mesh wiring, discovery, register/bind)은 계속 `spot_node_*` API가 담당한다.

#### Monitor

| 항목 | API | 유지/변경 | 비고 |
|---|---|---|---|
| events mask | `*_monitor_open(..., events, handler)` | 유지 | open-time filter |
| callback 교체 | `zlink_monitor_set_handler()` / `zlink_service_monitor_set_handler()` | 유지 | remove는 불가 |

Monitor 규칙:

- monitor는 별도 `set_option()` surface를 두지 않는다.
- monitor의 동작 제어는 open-time events mask와 handler 교체만으로 제한한다.

## 5.4 poller 변경 사항

### 5.4.1 유지

- raw fd poll
- timer
- send-side `POLLOUT`
- send-side `POLLOUT`은 poller/조회형 readiness로 유지하고 callback push로 바꾸지 않는다.

이유:

- writable 상태는 message delivery와 달리 application intent 없이는 의미가 약하다.
- send path는 blocking send / nonblocking send / `POLLOUT` poll 조합만으로도 충분히 표현 가능하다.
- recv-side callback 모델을 send readiness callback까지 확장하면 API가 오히려 복잡해진다.

### 5.4.2 제거

- zlink socket/service object의 `POLLIN`
- readiness 후 `recv()` 호출 패턴

### 5.4.3 정리 방안

| API | 조치 |
|---|---|
| `zlink_poller_add(socket, ..., ZLINK_POLLIN)` | 삭제 |
| `zlink_poller_add_spot_sub(..., ZLINK_POLLIN)` | 삭제 |
| `zlink_poller_modify(..., ZLINK_POLLIN)` | 삭제 |
| `zlink_poller_wait(... ZLINK_POLLIN ...)` 관련 사용 예제 | 삭제 |

정책:

- zlink object에 대한 `POLLIN`은 문서/헤더/API에서 삭제한다.
- 외부 사용 패턴은 제거 대상이다.

## 6. callback 계약

### 6.1 ownership

- callback에 전달된 `zlink_msg_t`는 callback이 소비 책임을 가진다.
- callback이 return할 때 각 message는 반드시 정리되어 있어야 한다.
- callback 바깥으로 message pointer/array pointer를 보관하면 안 된다.
- `parts` 배열 메모리 자체는 라이브러리 소유이며 callback return 이후 자동으로 회수된다.
- 비동기 handoff가 필요하면 callback 내부에서 `zlink_msg_move()`로
  사용자 큐로 이동한 뒤 return 해야 한다.

### 6.2 허용 동작

callback 내부에서 다음은 허용 대상이다.

- same socket/service 또는 다른 handle로의 `send`
- 사용자 큐 enqueue
- light-weight 상태 갱신

다음은 구현 시 명시적으로 검증해야 한다.

- handle `close` / `destroy`
- `disconnect`
- `unsubscribe`
- handler 교체

handler 교체 적용 규칙:

- callback 중 동일 handle의 handler 교체 요청은 허용할 수 있다.
- 단 현재 실행 중인 callback frame에는 새 handler를 소급 적용하지 않는다.
- 교체 성공 후 다음 message dispatch부터 새 handler를 사용한다.

권장 정책:

- 같은 callback 대상 handle에 대한 destructive control API는
  즉시 free가 아니라 callback return 이후 teardown을 기본으로 한다.
- 즉 callback 중 `zlink_close()` / `*_destroy()`가 호출되면,
  구현은 실제 객체 해제를 바로 수행하지 않고 `closing_requested` 같은 내부 상태만 세팅한다.
- dispatcher epilogue는 callback return 직후 이 상태를 확인하고
  `recv_dispatch_closed` 경로로 실제 shutdown / teardown / free를 수행한다.
- 필요하면 `in_callback` 또는 dispatch depth 카운터를 두어 현재 dispatch frame 안에서는
  handle 메모리를 해제하지 않도록 강제한다.
- 즉시 free가 꼭 필요한 특수 타입이 생기기 전까지는 별도 self-close fast path를 두지 않는다.

destroy 규칙:

- raw socket과 raw monitor handle은 `zlink_close()`로 종료한다.
- service object(`gateway`, `spot`, `discovery`)는 전용 `*_destroy()`를 사용한다.
- service monitor handle도 기존 규칙을 따라 전용 close/destroy API를 유지한다.

### 6.3 callback blocking

- callback은 I/O thread inline이므로 오래 블로킹하면 해당 connection 수신이 정지한다.
- 이것은 설계상 허용된 backpressure 메커니즘이다.
- 문서에는 "callback은 가볍게 유지하고, 무거운 처리는 사용자 worker queue로 handoff"를 권장 패턴으로 명시한다.

## 7. 목표 내부 아키텍처

## 7.1 핵심 원칙

- recv queue 없음
- callback이 소비 행위 그 자체
- callback return이 implicit continue
- recv-side backpressure는 transport read pause로 표현
- sender pressure는 transport/OS 레벨로 자연 전파

## 7.2 레이어별 책임

### 7.2.1 engine

engine는 다음을 담당한다.

- transport async read
- frame decode / protocol decode
- connection별 frame accumulator를 사용한 완성된 multipart batch 구성
- direct dispatch 호출
- callback 완료 후 next read re-arm
- callback 진행 중 동일 connection 추가 read 금지

engine는 더 이상 다음을 담당하지 않는다.

- recv message를 session pipe에 적재
- `EAGAIN` 기반 inbound queue 재시도 루프 유지

multipart 조립 규칙:

- decoder는 계속 frame 단위 decode와 `MORE` 경계 판별을 담당한다.
- engine는 connection별 accumulator에 frame을 모아 마지막 part(`MORE` 없음)를 본 시점에
  하나의 callback batch로 승격한다.
- protocol-specific frame 경계 해석은 기존 decoder/protocol 계층에 남기고,
  socket family의 `fq/xrecv`가 하던 multipart batch 조립 책임만 engine 쪽으로 올린다.

### 7.2.2 session_base

`session_base_t`는 recv-side에서 축소된다.

유지 책임:

- connection lifecycle
- connect/reconnect
- outbound path bridge
- peer routing id / endpoint metadata

삭제 또는 축소 책임:

- inbound `push_msg()` 적재 path
- recv-side `_pipe` write/flush/restart_input 연쇄
- inbound backpressure를 pipe writable/readable로 모델링하는 로직

### 7.2.3 socket_base / service facade

socket/service는 다음을 담당한다.

- handler install / 교체 상태
- decoded multipart를 callback shape로 변환
- callback 호출
- callback 중 제어 API reentrancy 정책 유지

### 7.2.4 service 통합 구조

- 통합 `gateway`는 내부 ROUTER 하나를 사용한다.
- 이 ROUTER는 bind와 connect를 모두 수행하고 inbound request / outbound reply를 `kind`
  기반으로 demux한다.
- control plane ack/error/status는 discovery/service-control runtime에서 올라온 결과를
  unified `gateway` handler shape로 정규화해 `ZLINK_GATEWAY_MSG_CONTROL`로 전달한다.
- 기존 `receiver` wrapper 때문에 남아 있던 별도 facade 상태와 register result cache는 제거한다.
- `spot_node`는 canonical public SPOT service handle로 유지한다.
- `spot` facade는 필요하면 `spot_node` 위에 얹는 direct facade로 유지한다.
- 내부적으로는 현재 `spot_node` data plane socket / inproc bridge 구조를 유지한다.

### 7.2.5 SPOT data plane

- `SPOT`은 [`spot-proxy-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/spot-proxy-rewrite-spec.ko.md)
  의 data plane worker 구조를 내부 기반으로 유지할 수 있다.
- 즉 `_local_pub_ingress_sub`, `_mesh_xsub`, `_local_fanout_xpub`, `_mesh_pub`
  같은 worker-owned socket topology를 곧바로 버릴 필요는 없다.
- 이번 재작성의 핵심은 public `recv()` facade 제거와 callback-only delivery이지,
  SPOT mesh/proxy 알고리즘 자체를 다시 설계하는 것이 아니다.
- `source_rid`는 PUB/SUB payload frame에 실어 public multipart shape를 바꾸지 않고,
  SPOT 내부 envelope metadata로 운반한다.
- local publish 시 unified `spot`은 publisher routing id를 포함한 internal metadata를
  ingress frame에 붙여 worker로 보낸다.
- mesh publish / mesh receive 경로도 같은 internal SPOT envelope을 forward하고,
  local fanout 직전 또는 callback bridge 직전에 metadata를 벗겨 `source_rid`로 복원한다.
- SPOT 수신 callback은 data plane worker 또는 그 하위 dispatch bridge가
  local fanout 결과를 `spot` handler shape로 변환해 호출하는 방식으로 정리한다.
- `_local_fanout_xpub`와 subscription propagation 경로는 유지 가능하며,
  public `spot_sub` recv API 제거와는 별개다.
- 구현 선택지는 아래 둘 중 하나다.
  - `_local_fanout_xpub` 뒤에 internal SUB/dispatch bridge를 유지하고 callback으로 연결
  - worker가 local fanout 결과를 직접 callback adapter로 전달
- 어느 쪽이든 subscription table과 upstream propagation ownership은 data plane owner thread에 둔다.

### 7.2.6 inproc transport 경로

- 일반 `inproc` transport는 transport-local pipe/ypipe primitive를 유지할 수 있다.
- 단 이 pipe는 더 이상 "나중에 recv()로 꺼내는 public recv queue" 의미를 갖지 않는다.
- inproc 수신은 read activation 시점에 pipe에서 multipart를 drain해 즉시 callback으로 전달한다.
- 즉 일반 network transport는 `decoder -> engine -> callback`,
  일반 inproc transport는 `pipe read activation -> direct dispatch -> callback`으로 정리한다.
- SPOT 내부 inproc bridge도 같은 원칙을 따른다. worker 간 handoff를 위한 inproc hop은 허용하지만,
  public facade recv queue를 부활시키는 용도로 쓰지 않는다.

## 7.3 direct dispatch 결과 모델

내부적으로는 다음 결과 enum을 둔다.

```c++
enum recv_dispatch_result_t {
    recv_dispatch_ok,
    recv_dispatch_paused,
    recv_dispatch_closed
};
```

의미:

- `recv_dispatch_ok`
  - callback이 정상 완료되었다.
  - engine은 다음 read를 바로 arm한다.
- `recv_dispatch_paused`
  - callback이 아직 설치되지 않았거나 내부적으로 읽기 중단이 필요하다.
  - engine은 read를 재arm하지 않고 정지한다.
  - 이후 callback 설치 또는 내부 상태 복구 시점에 read를 다시 시작한다.
- `recv_dispatch_closed`
  - callback 또는 control path에 의해 connection/socket 종료가 요청되었다.
  - engine은 terminate 경로로 진입한다.

중요:

- `paused`는 사용자용 API가 아니다.
- 이것은 내부 state일 뿐이며, 명시적 `resume_recv()`를 공개하지 않는다.

## 7.4 "queue 없음"의 정확한 의미

이번 재작성에서 제거 대상은 다음이다.

- socket-level inbound pipe queue
- service-level recv queue
- recv 전용 facade inproc queue

허용되는 최소 상태는 다음뿐이다.

- 현재 callback에 전달 중인 in-flight multipart 1건
- protocol decoder가 frame 완성을 위해 들고 있는 partial buffer
- callback 부재 시 read stop 상태

즉, "나중에 `recv()`로 꺼내기 위해 쌓아 두는 큐"는 없애되,
transport-local inproc handoff primitive와 protocol decoder partial buffer,
현재 in-flight message까지 금지하는 것은 아니다.

## 8. 타입별 수신 의미

### 8.1 SUB

- 과거 `recv()`가 주던 multipart frame shape를 그대로 callback에 전달한다.
- topic frame 분리는 하지 않는다.
- filtering은 기존 SUB path와 동일하게 적용한다.
- `source_rid`는 payload frame이 아니라 inbound peer session metadata에서 채운다.

### 8.2 DEALER

- multipart frame shape 유지
- direct callback으로 즉시 전달
- 제품 관점에서 cross-peer strict fairness는 이번 재작성의 핵심 계약으로 두지 않는다.
- 우선 목표는 queue 제거와 callback-only API다.
- `source_rid`는 실제 메시지를 보낸 peer routing id다.

### 8.3 ROUTER

- routing id는 `source_rid` 인자로 전달
- payload multipart에서는 routing-id frame 제거
- per-connection direct callback
- payload는 실제 application frame만 전달한다.

### 8.4 PAIR

- direct callback에 가장 자연스럽게 맞는 타입
- 특별한 의미 변화 없음
- `source_rid`는 연결된 단일 peer routing id다.

### 8.5 STREAM

- 기존 stream dispatch 모델을 유지/일반화한다.
- raw mode와 LEN32BE mode의 두 경로를 계속 지원한다.
- 이번 재작성은 `STREAM`을 예외로 두지 않고, 다른 recv-capable 타입도
  비슷한 direct dispatch로 끌어올리는 작업이다.

### 8.6 SPOT

- `spot_node`는 위치투명 pub/sub service의 canonical public type으로 유지한다.
- `spot` facade는 현재처럼 `spot_node`에 inproc으로 연결된 facade로 유지한다.
- `spot_node` / `spot` 모두 callback-only 수신 모델로 정렬한다.
- `spot_node`의 recv/callback은 node-owned default sub가 구독한 topic/pattern 집합에만 적용된다.
- 각 `spot` facade의 recv/callback은 그 facade가 직접 구독한 topic/pattern 집합에만 적용된다.
- 같은 `spot_node`를 공유하더라도 node-owned default sub와 facade sub들의 subscription scope는 독립이다.
- `source_rid` + topic string + payload parts 분리 전달 semantics 유지
- `source_rid`는 SPOT internal envelope metadata에서 복원하며 public payload frame에는 포함하지 않는다.

### 8.7 Gateway

- `gateway`가 기존 `gateway` + `receiver`를 모두 대체한다.
- 생성 시 handler 등록을 기본 수신 모델로 사용한다.
- `set_handler()`는 생성 후 handler 교체 경로다.
- 현재 `recv(..., routing_id_out)` / `recv(..., service_name_out)`가 주던 메타데이터를
  callback 인자로 직접 전달한다.
- request / reply / control은 `kind` enum으로 구분한다.
- out-buffer API는 제거한다.

### 8.8 monitor

- open 후 `*_recv()`가 아니라 handler 등록으로 이벤트 수신
- monitor 자체도 event callback-only handle이 된다.
- monitor callback의 최초 등록은 `*_monitor_open(..., handler)`에서 수행한다.

## 9. 내부 구현 작업 목록

## 9.1 public header / API surface

영향 파일:

- `core/include/zlink.h`
- `core/src/api/zlink.cpp`

작업:

- recv 계열 선언 제거
- 새 callback typedef / setter 선언 추가
- 생성자 시그니처 변경분(`zlink_socket_with_handler`, `zlink_gateway_new`,
  `zlink_spot_new`) 반영
- `POLLIN`/`RCVMORE`/`RCVTIMEO` 관련 public surface 정리
- 문서 주석을 callback ownership 계약으로 전면 수정

## 9.2 socket base

영향 파일:

- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`

작업:

- `recv()` 경로 제거
- `xrecv()` / `xhas_in()` / `has_in()` 기반 수신 readiness 경로 제거
- 공통 direct dispatch entry 추가
- handler install/교체 상태와 internal restart hook 추가

## 9.3 session / engine

영향 파일:

- `core/src/core/session_base.hpp`
- `core/src/core/session_base.cpp`
- `core/src/engine/asio/asio_engine.hpp`
- `core/src/engine/asio/asio_engine.cpp`
- `core/src/transports/ws/asio_ws_engine.hpp`
- `core/src/transports/ws/asio_ws_engine.cpp`

작업:

- inbound `push_msg()` path 제거 또는 outbound-only 축소
- engine decode 후 direct dispatch 호출
- callback 완료 전 동일 connection read 정지
- dispatch target이 아직 활성화되지 않은 내부 상태에서는 internal pause 상태 진입
- handler install 시 input restart 연결

## 9.4 socket family

영향 파일 예시:

- `core/src/sockets/xsub.*`
- `core/src/sockets/router.*`
- `core/src/sockets/dealer.*`
- `core/src/sockets/pair.*`
- `core/src/sockets/stream.*`

작업:

- 기존 dispatch 예외 구현을 공통 모델로 정렬
- 기존 recv용 queue/fq path에서 callback shape로 직접 변환
- frame shape 보존 검증

## 9.5 service layer

영향 파일 예시:

- `core/src/services/spot/spot_pub.*`
- `core/src/services/spot/spot_sub.*`
- `core/src/services/spot/spot_node.*`
- `core/src/services/gateway/receiver.*`
- `core/src/services/gateway/*`
- monitor 관련 구현 파일

작업:

- `recv()` 제거
- `gateway/receiver` -> `gateway` 통합
- `spot_node` callback-only 정렬
- `spot` facade callback 정렬
- standalone `spot_pub` / `spot_sub` 정리 여부 검토
- public child facade 제거
- unified `gateway` single-ROUTER dual-role TLS 적용 경로 정리
- typed callback metadata 직접 전달
- 서비스 문서와 C API 설명 갱신

## 9.6 poller

영향 파일:

- poller C API / docs / tests

작업:

- zlink object `POLLIN` 제거
- send-side `POLLOUT`만 허용
- 관련 예제/문서 교체

## 9.7 bindings

영향 영역:

- C++
- Java
- C#
- Node.js

작업:

- recv loop 기반 wrapper 삭제
- callback -> language async primitive 변환 경로로 통일
- binding은 고정 callback을 사용하고 socket/service handle 기반 registry lookup으로
  사용자 객체/컨텍스트를 찾는다.
- `source_rid`는 sender identity 메타데이터로 바인딩 사용자에게 그대로 노출한다.
- `userdata`는 제공하지 않는다.
- 이것은 제약이 아니라 의도된 설계 결정이다.
- ownership/lifetime 규약을 binding layer에서 안전하게 감싼다
- `source_rid`는 sender identity metadata로 노출하되, binding owner lookup key로 쓰지 않는다.

## 9.8 tests / perf 수정 계획

영향 영역:

- `core/tests/`
- `core/unittests/`
- `core/perf/`

작업:

- `recv()` / `*_recv()` / `monitor_recv()` / `service_monitor_recv()` 기반 테스트를
  callback 기준으로 전환한다.
- zlink object `POLLIN`을 전제로 한 poller 테스트를 제거하거나 send-side `POLLOUT` /
  raw fd poll 중심으로 재작성한다.
- `gateway/receiver` split을 전제로 한 서비스 테스트를 통합 `gateway` 기준으로
  재작성한다.
- `spot_node` / `spot` callback 모델을 기준으로 서비스 테스트를 재작성한다.
- perf benchmark도 동일 원칙으로 callback-only 수신 모델에 맞춰 수정한다.

### 9.8.1 `core/tests`

수정 대상 범주:

- raw socket 수신 테스트
  - `zlink_recv()` / `zlink_msg_recv()` 사용 테스트
  - `ROUTER/DEALER/PAIR/SUB` recv loop 테스트
- service 수신 테스트
  - `gateway` message handler callback 테스트
  - `gateway` peer weight snapshot/update 테스트
- `spot` / `spot_sub` handler callback 테스트
  - `spot_node` default sub handler callback 테스트
- monitor 테스트
  - raw socket monitor handler callback 테스트
  - service monitor handler callback 테스트
- poller 테스트
  - zlink object `POLLIN`을 기대하는 테스트

대표 영향 예시:

- `core/tests/discovery/test_gateway_handover.cpp`
- `core/tests/discovery/test_service_discovery.cpp`
- `core/tests/discovery/test_service_introspection.cpp`
- `core/tests/test_pair_send_blocking_wakeup.cpp`
- `core/tests/test_stream_fastpath.cpp`

수정 방향:

- blocking/nonblocking recv 검증을 callback delivery + send-side/backpressure 검증으로 치환
- monitor drain loop를 event callback 설치 방식으로 교체
- `receiver` 생성/파괴/monitor 시나리오는 통합 `gateway`의 server-side bind/register 흐름으로 치환
- `spot_node` / `spot` readiness/recv 시나리오는 handler + peer readiness/monitor 관찰로 치환

### 9.8.2 `core/unittests`

수정 대상 범주:

- poller unit test
  - `core/unittests/unittest_poller.cpp`

수정 방향:

- zlink object `POLLIN` 보조 API나 helper가 남아 있으면 제거
- raw fd poll / timer / send-side `POLLOUT`만 남는 계약에 맞춰 unit test를 정리
- recv-side readiness helper가 socket core에 남아 있지 않은지 unit 수준에서 검증

### 9.8.3 `core/perf`

수정 대상 범주:

- single benchmark
  - `core/perf/single/src/perf_gateway.cpp`
  - `core/perf/single/src/perf_spot.cpp`
  - `core/perf/single/common/bench_common.hpp`
- multi benchmark
  - `core/perf/multi/src/perf_spot_server.cpp`
  - `core/perf/multi/src/perf_gateway_server.cpp`
  - `core/perf/multi/src/perf_gateway_client.cpp`
- perf 문서
  - `core/perf/README.md`
  - `core/perf/README_KO.md`

수정 방향:

- recv loop 기반 latency/throughput 측정을 callback-based receive completion 측정으로 변경
- `gateway/receiver` benchmark topology를 통합 `gateway` topology로 변경
- `spot_node` + `spot` facade benchmark를 callback 모델 기준으로 갱신
- `zlink_poller_add_receiver`, `zlink_poller_add_spot_sub`, `zlink_poller_add_spot_pub`
  전제를 제거하고 새 public surface 기준으로 재정리
- queue probe / peer stats sampling은 유지하되, recv-side queue probing은 제거하거나 의미를 재정의
- comparison script와 README 예제도 새 API 이름과 callback 모델로 갱신

## 10. 삭제 대상 내부 코드

다음은 재작성 완료 후 삭제 대상이다.

- raw socket `recv()` implementation path
- service `recv()` facade path
- recv/poller 예제 및 문서
- SPOT recv queue 및 handler/recv 상호배타 상태 머신
- direct dispatch를 막기 위해 남겨둔 임시 bridge/inproc queue
- `receiver` wrapper 전용 register result cache / monitor 상태
- standalone `spot_pub` / `spot_sub` public facade 경로

정리 원칙:

- 작업 완료 시점에는 dead code를 남기지 않는다.
- compatibility wrapper는 두지 않는다.
- 레거시 recv path와 새 callback path가 병존하는 중간 상태를 완료로 간주하지 않는다.
- 구현 완료 조건에는 관련 테스트/벤치/문서/바인딩 surface에서
  더 이상 삭제 대상 경로를 참조하지 않는 것까지 포함한다.

## 10.1 작업 완료 조건

다음 조건을 모두 만족해야 이 작업을 완료로 본다.

- public API에서 삭제 대상으로 지정한 recv/pollin/legacy facade가 모두 제거되어 있다.
- 내부 구현에서 삭제 대상으로 지정한 legacy path, 임시 bridge, dead code가 모두 제거되어 있다.
- `core/tests`, `core/unittests`, `core/perf`, guide/doc/api 문서, bindings wrapper에
  삭제된 API/경로 참조가 남아 있지 않다.
- "나중에 정리"를 전제로 한 dormant code path, compatibility flag, legacy wrapper를 남기지 않는다.
- callback-only contract가 실제 production path의 유일한 recv path가 되어 있다.

## 11. 테스트 전략

세부 파일/벤치 수정 범위는 `9.8 tests / perf 수정 계획`을 따른다.

### 11.1 기능 테스트

- 각 recv-capable 타입별 callback 수신 테스트
- recv-capable raw/service 생성 시 callback 누락이 실패하는지 확인
- multipart shape 보존 테스트
- `ROUTER` `source_rid` 전달 및 routing-id frame 제거 테스트
- `SUB` filter semantics 회귀 테스트
- `SPOT` topic/pattern callback semantics 테스트
- 통합 `gateway` request/reply/control demux 테스트
- 통합 `spot` publish/subscribe/handler 동작 테스트

### 11.2 backpressure 테스트

- callback에서 sleep 시 해당 connection read가 정지하는지 확인
- slow receiver 상황에서 sender가 block 또는 policy대로 drop되는지 확인
- `PUB/SUB nodrop` 정책이 기존과 동일하게 동작하는지 확인
- internal recv queue가 증가하지 않는지 확인

### 11.3 lifecycle / reentrancy 테스트

- callback 중 `send`
- callback 중 `close`
- callback 중 handler 교체
- handler 교체 후 in-flight callback은 old handler, subsequent callback은 new handler인지 확인
- disconnect/reconnect 중 callback 수신
- context termination 중 in-flight callback

### 11.4 성능 테스트

- 기존 recv path 대비 end-to-end latency
- high message-rate callback throughput
- slow consumer 시 queue growth 여부
- CPU 사용률 및 context switch 감소 여부

## 12. 구현 방식 및 순서

이번 작업은 단계적 마이그레이션이 아니라 단일 재작성으로 진행한다.

작업 방식은 다음과 같다.

- 중간 호환 단계를 제품 설계의 일부로 두지 않는다.
- legacy recv path와 callback path를 장기간 병존시키지 않는다.
- 다만 구현 자체는 큰 덩어리를 내부 작업 묶음으로 나누고, 각 묶음이 끝날 때마다
  빌드/테스트를 수행해 즉시 회귀와 버그를 수정한다.
- 즉 "compatibility layer를 유지한 채 천천히 이전"이 아니라,
  "단일 재작성 목표를 유지하면서 작업 묶음마다 검증하고 바로 고치는 방식"으로 진행한다.
- 최종 정리(dead code 제거, legacy path 제거, 테스트/문서/바인딩 정리)는
  마지막 후속 작업이 아니라 본 작업의 완료 조건에 포함한다.

구현 순서는 다음처럼 한 번에 밀어붙인다.

1. public contract를 먼저 고정한다.
   - 새 callback API/삭제 API 확정
   - guide/doc/api 문서 스켈레톤 갱신
   - bindings 영향 목록 확정
2. engine direct dispatch와 internal pause 모델을 도입한다.
   - engine -> socket/service direct dispatch 경로 구현
   - per-connection read pause/resume internal state 구현
   - recv queue 없이 callback-only delivery 달성
3. recv-capable raw socket을 전환한다.
   - `SUB`
   - `DEALER`
   - `ROUTER`
   - `PAIR`
   - `STREAM` 정렬
4. service facade를 전환한다.
   - `gateway` 통합
   - `spot` 통합
   - monitor
5. old path를 같은 작업 묶음 안에서 즉시 제거한다.
   - recv facade 제거
   - pollin docs/tests 제거
   - dead code 제거

검증 원칙:

- 각 작업 묶음이 끝날 때마다 해당 범위가 다시 컴파일 가능해야 한다.
- 관련 `core/tests`, `core/unittests`, `core/perf`를 바로 갱신하고 실행해
  새 callback-only 경로 기준으로 회귀를 확인한다.
- 버그 수정은 마지막에 몰아서 하지 않고, 각 작업 묶음 직후 바로 처리한다.
- "일단 크게 다 바꾼 뒤 나중에 테스트하면서 정리" 방식은 지양한다.

## 13. 미해결 사항

다음은 구현 전에 최종 결정을 내려야 한다.

### 13.1 handler clear semantics

결정:

- `SUB`/recv-capable handle에서는 `handler == NULL`을 금지한다.
- 단 `spot`이 `PUB` only role로 생성되는 경우에는 예외적으로 `handler == NULL`을 허용한다.
- callback 제거 API는 두지 않는다.
- raw socket과 service는 생성 시 handler 등록을 기본으로 한다.
- 생성 후 callback 교체는 동일 setter/attach API 재호출로 수행한다.

의미:

- 수신을 일시적으로 멈추고 나중에 재개하는 public API는 제공하지 않는다.
- 수신 중단이 필요하면 handler를 유지한 채 callback 내부에서 사용자 큐 handoff/backpressure로 처리하거나,
  handle 자체를 destroy 후 다시 생성하는 모델을 사용한다.
- 이는 public pause/resume 상태 기계를 추가하지 않기 위한 의도적 단순화다.

### 13.2 callback 중 destructive control API

질문:

- callback 중 handle `close` / `destroy`를 즉시 free까지 허용할지
- callback 중 handle `close` / `destroy`는 요청만 받고
  실제 teardown은 callback return 이후로 미룰지

권장:

- 기본은 deferred handle teardown
- callback 중 `zlink_close()` / `*_destroy()` 호출은 허용하되,
  그 호출은 close 요청만 기록하고 실제 free는 수행하지 않는다.
- callback return 직후 dispatcher epilogue에서 close 요청을 확인하고
  실제 shutdown / teardown / free를 수행한다.
- `zlink_msg_close()`는 이 논의 대상이 아니며, message ownership 정리이므로
  callback 안에서 즉시 수행해야 한다.

### 13.3 `POLLIN` 처리 방식

결정:

- zlink socket/service object에 대한 `POLLIN`은 삭제한다.
- poller는 send-side `POLLOUT`, raw fd poll, timer만 유지한다.
- `POLLOUT` 전용 callback API는 도입하지 않는다.

이유:

- `POLLIN`은 recv callback-only 모델과 충돌하지만,
  `POLLOUT`은 send-side backpressure 관찰 수단으로 여전히 유효하다.
- `POLLOUT`을 callback으로 바꿀 실익보다 계약 복잡도 증가가 더 크다.

## 14. 최종 요약

이번 재작성의 핵심은 다음 한 줄로 요약된다.

`zlink`의 수신은 더 이상 "앱이 큐에서 꺼내는 행위"가 아니라
"라이브러리가 메시지를 완성 즉시 callback으로 넘기고, callback return이 소비 완료가 되는 행위"다.

이를 위해 다음을 수행한다.

- `recv()` 계열 삭제
- `POLLIN` 제거
- recv-side 내부 큐 제거
- `network -> decoder -> callback` direct path 도입
- explicit resume API 없이 callback return 기반 flow control 채택
- send path와 기존 slow peer 정책은 최대한 유지

이 문서의 구현이 완료되면 `zlink`는 외부 API와 내부 data path 모두에서
기존 reactor 흔적을 걷어내고, proactor 기반 callback 수신 라이브러리로 정리된다.

## 15. 추가 인터페이스 재검토

이 장은 별도 문서로 분리한다.

- [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)

분리 이유:

- 본문 스펙은 recv 모델 재작성의 고정 계약과 구현 범위에 집중한다.
- discovery / gateway / spot / socket enum 재설계 논의는 별도 추적 문서로 유지하는 편이
  후속 정리에 더 적합하다.
