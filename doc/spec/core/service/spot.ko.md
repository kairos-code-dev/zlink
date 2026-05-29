[English](./spot.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](./README.ko.md)

# SPOT

이 문서는 현재 공개 헤더 `core/include/zlink.h`에 들어 있는 SPOT 계약만 정리한다.
구현 전 설계나 개편 방향은 별도 draft 문서를 본다.

## 개요

SPOT 공개 표면은 두 핸들로 나뉜다.

- `SpotNode`
  SPOT 토폴로지, discovery 기반 peer 연결, 수동 peer 연결, channel 호출용
  `DEALER` 등록, 외부 publish ingress 등록을 관리한다.
- `Spot`
  `SpotNode` 위에 올라가는 데이터 평면 facade이다. 토픽 publish/subscribe,
  routed recv/reply, channel send/request를 제공한다.

`Spot`은 `SpotNode`를 빌려서 만든다. `Spot`을 파괴해도 backing `SpotNode`는
자동으로 파괴되지 않는다.

## 생성과 종료

```c
typedef enum zlink_spot_node_mode_t {
  ZLINK_SPOT_NODE_MODE_PUBSUB = 1,
  ZLINK_SPOT_NODE_MODE_ROUTED = 2,
  ZLINK_SPOT_NODE_MODE_ALL = 3
} zlink_spot_node_mode_t;

typedef struct zlink_spot_node_options_t {
  zlink_spot_node_mode_t mode;
} zlink_spot_node_options_t;

void *zlink_spot_node_new(
  void *ctx,
  const zlink_spot_node_options_t *options);
zlink_close_result_t zlink_spot_node_destroy(void **node_p);

void *zlink_spot_new(void *node);
zlink_close_result_t zlink_spot_destroy(void **spot_p);
```

- `zlink_spot_node_new()`는 새 SPOT node runtime을 만든다.
- `options == NULL`은 안전하다: struct에 접근하지 않으며 `ZLINK_SPOT_NODE_MODE_ALL`을
  묵시적으로 선택한다.
- `options`가 NULL이 아닌 상태에서 `options->mode == 0`이면 0 값을 미설정으로 간주해
  역시 `ZLINK_SPOT_NODE_MODE_ALL`로 동작한다.
- 잘못된 mode 값은 `NULL`과 `errno == EINVAL`로 실패한다.
- `PUBSUB` mode는 topic publish/subscribe만 켠다. routed API는
  `ENOTSUP`으로 실패한다.
- `ROUTED` mode는 routed request/reply와 direct routed send만 켠다.
  topic publish/subscribe API는 `ENOTSUP`으로 실패한다.
- `ALL` mode는 두 기능을 모두 켠다.
- `zlink_spot_new()`는 기존 `SpotNode`를 빌려 unified `Spot` facade를 만든다.
- `Spot`은 backing `SpotNode`의 mode를 그대로 물려받는다. 생성 뒤에는 mode를
  바꿀 수 없다.
- 꺼진 기능을 호출하면 내부 socket을 새로 만들지 않고 실패한다.
- `zlink_spot_destroy()`는 facade만 닫는다.
- `zlink_spot_destroy()`는 routed target lookup을 먼저 제거한 뒤 owned subject를
  닫는다. destroy 시점에 남아 있던 unread routed 메시지는 close 과정에서 버려질
  수 있으며, 호출자는 파괴 전에 unread를 끝까지 모두 읽어 처리해야 할 의무를 지지
  않는다.
- `zlink_spot_node_destroy()`는 node와 내부 runtime 자원을 정리한다.
- discovery에 attach된 node는 보통 `zlink_discovery_destroy()` 흐름에서 함께 정리된다.

### 명시적 routing id 기반 Spot 확보

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_get_or_new(
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_,
  uint32_t *created_out_);
```

- 이 함수는 local `SpotNode` 안에서 `spot_rid_`로 식별되는 logical Spot을 확보한다.
- 같은 `SpotNode`와 같은 `spot_rid_`에 대해 성공한 호출은 모두 같은 logical Spot을
  가리키는 새 owned facade handle을 받는다.
- logical Spot이 없어서 이번 호출이 새로 만들었으면 `created_out_ != NULL`일 때
  `*created_out_ = 1`이다.
- logical Spot이 이미 있으면 새 facade handle만 만들고 `created_out_ != NULL`일 때
  `*created_out_ = 0`이다.
- `created_out_ == NULL`은 허용된다. 생성 여부가 필요 없으면 넘기지 않아도 된다.
- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `spot_rid_ == NULL` 또는 `spot_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로
  실패하고 `errno`는 `EINVAL`이다.
- `spot_rid_`가 비어 있거나 최대 길이를 넘으면 `ZLINK_CONFIG_INVALID_ARGUMENT`로
  실패하고 `errno`는 `EINVAL`이다.
- 실패하면 가능한 경우 `*spot_out_ = NULL`, `*created_out_ = 0`으로 초기화한다.
- 반환된 facade handle은 호출자가 소유하며 `zlink_spot_destroy()`로 닫아야 한다.
- 이 함수는 actor join을 수행하지 않는다. room이나 stage에 actor를 넣는 작업은
  별도 join API로 처리한다.
- remote Spot 생성 또는 확보는 이 함수의 범위가 아니다.

## Entry Spot

`SpotNode`가 생성되면 내부적으로 `Entry Spot`(진입 수신점, 새 Actor가 처음 배정되는 논리적 수신 지점) logical state도 함께 생성된다.
`Entry Spot`은 `SpotNode`가 소유하며, application이 제거할 수 없다. 새로
만들어진 모든 Actor는 생성 직후 반드시 `Entry Spot`에 속한다.

`Entry Spot`은 별도의 `Spot` dispatch context를 가진다. application은 이 context에
dispatch handler(메시지 수신 시 호출되는 콜백)를 등록해 새 Actor의 초기 메시지 처리, 인증, 대상 Spot 선택 같은
작업을 수행한다.

### Entry Spot handle

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_entry_spot(
  void *node_,
  void **spot_out_);
```

- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `spot_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- 실패하면 `*spot_out_ = NULL`이다.
- 성공하면 `*spot_out_`에 새 Entry Spot facade handle을 반환한다.
- 반환된 facade는 `zlink_spot_dispatch_event_handler()` 등 기존 `Spot` API를 지원한다.
- `Entry Spot` logical state는 `SpotNode`가 소유한다. 반환된 facade handle은 application이
  소유하며 `zlink_spot_destroy()`로 닫아야 한다.
- `zlink_spot_destroy()`는 Entry Spot facade만 닫고 logical Entry Spot은 제거하지 않는다.
- 같은 node에서 이 API를 여러 번 호출하면 서로 다른 facade handle이 같은 logical
  Entry Spot을 가리킨다.

### Entry Spot routing id

`Entry Spot`은 일반 `Spot`처럼 routing id를 가진다.
기본값은 `SpotNode` 생성 시 자동 생성되는 random routing id다.
application이 고정 rid를 원하면 `zlink_spot_node_entry_spot()`으로 facade를 얻은 뒤
`zlink_set_routing_id(entry_spot, data, size)`를 호출한다.

Entry Spot rid 설정은 **configuration phase**에서만 허용한다.
configuration phase는 아래 중 **어느 하나라도** 처음 발생하는 시점에 끝난다:
첫 Actor 생성, Discovery attach, SpotNode bind/connect, Spot owner route publish,
Actor active route publish.

> **Spot owner route**: Discovery에 게시되는 레코드로, Spot의 routing id를 소유
> SpotNode에 매핑한다. 원격 노드가 rid 기반으로 해당 Spot에 메시지를 라우팅할 수 있게 해 준다.
>
> **Actor active route**: Discovery에 게시되는 레코드로, Actor id를 현재 해당 Actor를
> 보유한 Spot에 매핑한다. 원격 Actor relay에 사용된다.

- Actor가 하나라도 생성된 뒤 Entry Spot rid를 바꾸려고 하면
  `ZLINK_CONFIG_INVALID_STATE`로 실패하고 `errno`는 `EBUSY`다.
- Entry Spot rid가 Actor active route나 Spot owner route로 publish된 뒤에는 바꿀 수 없다.
- Entry Spot rid는 같은 `SpotNode` 안 다른 user Spot rid와 중복될 수 없다.

### Spot 조회

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup(
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_);
```

- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `spot_rid_ == NULL` 또는 `spot_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로 실패하고
  `errno`는 `EINVAL`이다.
- 같은 rid의 live local Spot이 없으면 `ZLINK_CONFIG_NOT_FOUND`로 실패하고 `errno`는 `ENOENT`이며
  `*spot_out_`은 변경하지 않는다.
- 성공하면 `*spot_out_`에 새 owned Spot facade handle을 반환한다. application이
  `zlink_spot_destroy()`로 닫아야 한다.
- `zlink_spot_node_spot_lookup()`은 현재 routing id index를 기준으로 조회한다.
  `zlink_set_routing_id()`가 성공하면 같은 logical Spot을 가리키는 모든 facade의
  routing id가 함께 바뀌고, lookup index도 old rid에서 new rid로 원자적으로 이동한다.
  routing id 변경 뒤 old rid lookup은 not found가 되고, new rid lookup은 같은 logical
  Spot에 대한 새 facade를 반환한다.
- 같은 logical Spot을 가리키는 여러 facade에서 동시에 routing id 변경을 요청하면
  `SpotNode` event loop에서 직렬화된 순서로 처리한다. 중복 rid나 lifecycle lock에 걸리면
  실패하고, 성공하면 모든 facade와 lookup index가 마지막으로 성공한 rid를 함께 본다.
- 일반 Spot logical state는 마지막 facade가 닫힐 때 제거된다. 단 joined Actor나 pending
  join request가 남아 있으면 마지막 facade close는 `ZLINK_CLOSE_BUSY`로 실패하고 `errno`는
  `EBUSY`다. application은 Spot을 제거하기 전에 해당 Spot의 Actor를 다른 Spot으로 join하거나
  Entry Spot으로 leave해야 한다.
- Entry Spot rid로 lookup하면 Entry Spot facade를 반환한다. Entry Spot logical state는
  `SpotNode`가 소유하므로 마지막 facade가 닫혀도 제거되지 않는다.
- remote Spot 조회는 Discovery Spot owner resolve가 담당한다. 이 함수는 local `SpotNode`
  안의 Spot만 조회한다.

## SpotNode 계약

SpotNode는 HWM을 `Spot`에서 `SpotNode`로 들어오는 admission control(수신 허가 제어, 새 메시지·연결의 수락 여부를 결정하는 관문)로만 공개한다.
`zlink_set_spot_node_option()` / `zlink_get_spot_node_option()`의 공개 옵션은
다음과 같다:

```c
typedef enum zlink_spot_node_option_t
{
    ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE      = 0x360E,
    ZLINK_SPOT_NODE_OPT_ROUTER_HWM              = 0x360F,
    ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE      = 0x3610,
    ZLINK_SPOT_NODE_OPT_PUBSUB_HWM              = 0x3611,
    ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN    = 0x3612,
    ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX    = 0x3613
} zlink_spot_node_option_t;
```

| 옵션 | 타입 | 설명 |
|------|------|------|
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE` | `int` | routed admission 채널 HWM profile |
| `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | `int` | routed 채널 숫자 HWM override; `0`은 override 해제 |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE` | `int` | pub/sub admission 채널 HWM profile |
| `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | `int` | pub/sub 채널 숫자 HWM override; `0`은 override 해제 |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN` | `int` | dispatch 워커 스레드 최소 수 |
| `ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX` | `int` | dispatch 워커 스레드 최대 수 |

두 admission 채널의 기본 profile은 balanced auto-HWM profile이다. 숫자 override가
없으면 admission 경계(`publish_ingress_queue`, `routed_send_queue`)는 profile별
고정 메시지 수 한도를 사용한다: COMPACT 64, LOW_LATENCY 128, BALANCED 256,
THROUGHPUT 512. 양수 HWM을 직접 설정하면 해당 채널의 자동 값보다 우선한다.
숫자 HWM에 `0`을 설정하면 override를 지우고 자동 값으로 돌아간다. 음수와 알 수
없는 profile은 `EINVAL`로 실패한다.

`Spot` handle은 common `ZLINK_OPT_SNDHWM` 또는 `ZLINK_OPT_RCVHWM` 설정을 받지
않는다. `Spot`은 생성 시점의 SpotNode admission HWM을 캡처하며, 이후 SpotNode HWM
변경은 나중에 생성되는 `Spot`에만 적용된다. SpotNode 내부 relay와 delivery socket은
HWM `0`을 사용한다. 제거된 방향별 SpotNode HWM option과 queue hard limit option은
공개 계약에 없으며, 기존 enum 숫자는 예약 상태다.

SpotNode와 Spot에는 public weight 설정 옵션이 없습니다. peer weight는 raw
ROUTER와 DEALER 소켓에서만 설정합니다. Spot peer snapshot에 남아 있는
`weight` 필드는 discovery나 peer 신호에서 배운 remote peer 상태이며,
Spot/SpotNode의 로컬 옵션이 아닙니다.

SpotNode는 topology와 설정 handle이며 topic publisher가 아니다. SpotNode에
`zlink_publish*()`를 호출하거나 send-ready handler를 설치하면 `ENOTSUP`으로
실패한다. topic publish는 `Spot` facade를 만들어 그 handle에서 수행한다.

### 내부 socket snapshot

```c
typedef struct zlink_spot_node_socket_filter_t {
  zlink_spot_node_socket_owner_t owner;
  zlink_socket_type_t socket_type;
  char socket_name[64];
} zlink_spot_node_socket_filter_t;

typedef struct zlink_spot_node_socket_entry_t {
  zlink_spot_node_socket_owner_t owner;
  uint64_t owner_id;
  char owner_name[64];
  char socket_name[64];
  zlink_socket_type_t socket_type;
  uint32_t auto_hwm_visible;
  zlink_monitor_status_t snapshot;
} zlink_spot_node_socket_entry_t;

zlink_config_result_t zlink_spot_node_internal_sockets(
  void *node,
  const zlink_spot_node_socket_filter_t *filter,
  zlink_spot_node_socket_entry_t *entries,
  size_t *count);
```

- snapshot은 이미 존재하는 내부 socket만 반환한다.
- snapshot 호출은 꺼진 기능이나 lazy socket을 새로 만들지 않는다.
- `entries == NULL`이면 성공하며 필요한 row 수를 `*count`에 쓴다.
- `*count`가 부족하면 `ENOBUFS`로 실패하고 필요한 row 수를 쓴다.
- `owner`는 `ANY`, `NODE`, `SPOT` 중 하나다.
- `socket_type`은 `ZLINK_SOCKET_ANY` 또는 공개 `ZLINK_SOCKET_*` 값만 받는다.
- `socket_name`이 비어 있지 않으면 정확히 같은 내부 socket 이름만 반환한다.
- `auto_hwm_visible == 1`인 row는 기본 Auto-HWM perf 출력 대상이다.
- `snapshot.auto_hwm_profile`, `snapshot.auto_hwm_policy_class`,
  `snapshot.auto_hwm_unit_budget_bytes`, `snapshot.auto_hwm_size_cap`,
  `snapshot.auto_hwm_socket_message_slots`는 진단용 자동 HWM planner 결과다.
- 현재 SPOT topology의 주요 node socket 이름은 `mesh-pub`, `mesh-xsub`,
  `external-router`다. `publish_ingress_queue`, `routed_send_queue`,
  `external_router_ingress_queue`는 socket 없이 런타임 큐로 동작하며 snapshot에
  포함되지 않는다.
- `PUBSUB` mode에서는 routed socket이 생성되지 않고, `ROUTED` mode에서는 topic
  socket이 생성되지 않는다. snapshot 호출은 꺼진 plane을 활성화하지 않는다.

### 토폴로지와 discovery

```c
zlink_config_result_t zlink_spot_node_set_router_bind(
  void *node,
  const char *endpoint);
zlink_config_result_t zlink_spot_node_set_pub_bind(
  void *node,
  const char *endpoint);
zlink_connect_result_t zlink_spot_node_connect_peer(void *node,
                                                    const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer(void *node,
                                                       const char *peer_endpoint);
zlink_connect_result_t zlink_spot_node_disconnect_peer_rid(
  void *node,
  const zlink_routing_id_t *target_node_rid);
zlink_config_result_t zlink_spot_node_attach_discovery(void *node,
                                                       void *discovery);
```

- `zlink_spot_node_set_router_bind()`는 routed ingress에 사용할 router socket
  endpoint를 설정한다. `ROUTED` mode node는 이 호출로 시작한다.
- `zlink_spot_node_set_pub_bind()`는 PUB/SUB mesh endpoint를 설정하고
  PUB/SUB plane을 시작한다. `ALL` mode에서 router와 pub/sub을 함께 쓰는
  node는 `zlink_spot_node_set_router_bind()`를 먼저 호출한 뒤
  `zlink_spot_node_set_pub_bind()`를 호출한다.
- `node == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`과 `EFAULT`로 실패한다.
  `endpoint == NULL` 또는 빈 문자열이면 `ZLINK_CONFIG_INVALID_ARGUMENT`와
  `EINVAL`로 실패한다. 이미 bind가 끝난 plane을 다시 bind하면
  `ZLINK_CONFIG_INVALID_STATE`와 `EBUSY`로 실패한다.
- `zlink_spot_node_connect_peer()`와 `zlink_spot_node_disconnect_peer()`는
  endpoint를 알고 있는 수동 mesh 연결에 쓴다.
- `zlink_spot_node_disconnect_peer_rid()`는 target node routing id를 기준으로
  peer node 연결을 종료한다. target node 아래의 개별 spot routing id는 이
  API의 대상이 아니다.
- discovery가 이미 attach된 node에서 `connect_peer()` 또는
  `disconnect_peer()`를 호출하면 `EBUSY`로 실패한다.
- `Spot` facade에는 별도 peer rid disconnect 함수가 없다. peer 연결은
  `SpotNode` runtime이 소유하기 때문이다.
- `zlink_spot_node_attach_discovery()`는 SPOT channel view를 제공하는
  discovery만 받는다.
- node에는 한 번에 하나의 active SPOT discovery view만 둘 수 있다.

### Router channel peer 연결

```c
zlink_connect_result_t zlink_spot_node_connect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);

zlink_connect_result_t zlink_spot_node_connect_router_channel_peer_rid(
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  const char *endpoint);

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);

zlink_connect_result_t zlink_spot_node_disconnect_router_channel_peer_rid(
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid);

zlink_config_result_t zlink_spot_node_attach_router_channel_discovery(
  void *node,
  const char *channel_name,
  void *discovery);
```

이 API들은 `SpotNode`의 routed router를 router capability가 있는 channel의
`ROUTER` peer에 연결한다. 연결된 router channel은
`zlink_router_send_spot_part()` 또는 `zlink_router_request_spot_part()`로
target node routing id와 target spot routing id를 지정해 local `Spot`으로
메시지를 보낼 수 있다.

- `channel_name`은 router channel peer 집합을 구분하는 이름이다. `NULL`이나
  빈 문자열은 `EINVAL`이다.
- `endpoint`는 router channel의 공개 `ROUTER` endpoint다. 호출자는 내부 endpoint
  파생 규칙을 알 필요가 없고, 파생 endpoint를 넘겨서는 안 된다.
- routed mode가 없는 node에서는 `ENOTSUP`으로 실패한다.
- 같은 `(channel_name, endpoint)` 수동 connect는 성공 no-op이다.
- discovery가 붙은 같은 channel에 수동 peer를 추가하려 하면 `EBUSY`다.
- 없는 수동 endpoint를 disconnect하면 `ENOENT`다.
- `zlink_spot_node_attach_router_channel_discovery()`는 route mesh 또는
  client/server router channel view를 제공하는 discovery만 받는다. channel 이름이
  discovery의 channel view와 다르면 `EINVAL`이다.
- 같은 channel에서 수동 peer와 discovery peer source는 섞을 수 없다.
- `zlink_spot_node_connect_router_channel_peer_rid()`는 routing id를 명시적으로
  지정해 연결하는 형태다. 결과 peer entry가 알려진 routing id에 즉시 묶이므로
  첫 reply가 관측되기 전이라도 snapshot 조회와 rid 기반 disconnect가
  매끄럽게 동작한다.
- `zlink_spot_node_disconnect_router_channel_peer_rid()`는 router channel peer의
  routing id로 연결을 끊는다. SPOT mesh peer와 router channel peer는 별도 peer
  종류로 조회된다.

이 계약은 target SpotNode 쪽 ingress 연결만 정의한다. framework 의 handler group,
DI client, local egress channel 선택은 core 서비스 계약이 아니다. 상위 계층이
egress client 를 제공하더라도, core에는 router channel peer와 routed Spot
send/request 오류 의미만 반영한다.

### Channel 호출용 socket 등록

```c
zlink_config_result_t zlink_spot_node_attach_channel_dealer(
  void *node,
  void *discovery,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual(
  void *node,
  const char *channel_name,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_pub_ingress(
  void *node,
  void *pub);
```

- `attach_channel_dealer()`는 discovery 기반 channel 대상 집합에 붙어 있는
  `DEALER`를 node에 등록한다.
- `attach_channel_dealer_manual()`은 호출자가 직접 connect를 끝낸 `DEALER`를
  지정한 `channel_name` 아래에 등록한다.
- 같은 channel 이름에는 자동 attach와 수동 attach를 합쳐서 `DEALER` 하나만
  등록할 수 있다. 중복 등록은 `EBUSY`다.
- attach 함수는 socket 생성이나 connect를 대신하지 않는다.
- attach된 `DEALER`는 `SpotNode` 전용 자원이다. 소유권은 호출자가 유지하지만,
  attach 뒤에는 다른 owner가 같은 socket을 일반 용도로 함께 써서는 안 된다.
- `zlink_spot_node_attach_pub_ingress()`는 외부 일반 `PUB`를 `Spot` 입력 경로에
  연결한다.
- ingress `PUB`는 node당 하나만 등록할 수 있다. 두 번째 등록은 `EBUSY`다.
- ingress `PUB`도 `SpotNode` 전용 자원으로 취급한다.

## Socket Channel Name Metadata

attach된 `DEALER`에 논리 channel name을 미리 기록해 두는 편의 API다.

```c
ZLINK_EXPORT zlink_config_result_t zlink_socket_set_channel_name (
  void *socket_,
  const char *channel_name_);

ZLINK_EXPORT zlink_config_result_t zlink_socket_get_channel_name (
  void *socket_,
  char *channel_name_buf_,
  size_t channel_name_capacity_,
  size_t *channel_name_len_out_);
```

- setter는 socket에 fixed logical channel name metadata를 기록한다.
- 이 metadata는 transport connect, bind, routing, discovery를 자동으로 바꾸지 않는다.
- getter는 현재 기록된 channel name을 돌려준다.
- channel name이 설정되지 않은 socket이면 `ENOENT`로 실패한다.
- 비어 있거나 잘못된 `channel_name`은 `EINVAL`이다.
- attach나 discovery가 귀속을 확정한 뒤에는 setter 변경을 `EBUSY` 또는 `EINVAL`로
  거부한다.

attach와의 관계는 아래처럼 고정한다.

- discovery attach 시 socket metadata가 비어 있으면 discovery channel 이름을 채운다.
- discovery attach 시 기록된 값과 discovery channel이 같으면 허용한다.
- discovery attach 시 기록된 값과 discovery channel이 다르면 `EINVAL`이다.
- manual attach 시 socket metadata가 비어 있으면 attach 인자의 `channel_name`을 채운다.
- manual attach 시 기록된 값과 attach 인자가 같으면 허용한다.
- manual attach 시 기록된 값과 attach 인자가 다르면 `EINVAL`이다.

`CHANNEL_REPLY_READABLE` callback에서 `subject_kind == CHANNEL_DEALER`일 때
`zlink_socket_get_channel_name(subject, ...)`를 호출해 해당 dealer가 어느 channel에
속하는지 읽을 수 있다.

## Spot 데이터 평면 계약

### Channel send/request

```c
zlink_submit_result_t zlink_spot_send_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

- channel 호출은 attach된 `DEALER` 경로로만 나간다.
- lookup 키는 `channel_name`이다.
- 같은 `channel_name`에 attach된 `DEALER`가 없으면 send/request는 실패한다.
- `Spot`에서는 direct `rid`로 `ROUTER`를 지정해 ordinary one-way send를 하지 않는다.
  direct routed request 시작은 아래 별도 섹션을 참조한다.

channel request의 transport owner와 delivery owner는 다르다.

- **transport owner**: 실제 request를 내보내고 network reply를 받는 attached `DEALER`
- **delivery owner**: 최종 user callback을 실행하는 `Spot` dispatch stream

즉 network reply는 선택된 `DEALER` 경로로 돌아오지만, 최종 callback 실행은 request를
시작한 `Spot`의 dispatch stream이 맡는다. reply는
`zlink_spot_request_channel_part()` 호출 시 등록한 `zlink_reply_handler_fn`을
통해 전달된다. `CHANNEL_REPLY_READABLE` dispatch event는 attached dealer에
진행이 발생했음을 알리는 readiness 신호이며, 실제 완료 전달은 core가 내부적으로
수행한다. 사용자가 별도로 public drain API를 호출할 필요는 없다.

### Topic publish/subscribe

```c
zlink_submit_result_t zlink_spot_publish(
  void *spot,
  const char *service_name,
  const char *topic_id,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_recv_result_t zlink_spot_subscribe(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);

zlink_recv_result_t zlink_spot_subscription_event(
  void *spot,
  zlink_routing_id_t *source_rid_out,
  int *subscribed_out,
  char *service_name_out,
  size_t *service_name_len_out,
  char *topic_id_out,
  size_t *topic_id_len_out,
  zlink_recv_flags_t flags);
```

- 토픽 평면의 공개 인자 이름은 아직 `service_name`을 쓴다.
- 이 이름은 topic plane namespace를 구분하는 현재 계약 이름이다.
- `zlink_spot_node_attach_pub_ingress()`로 연결한 외부 `PUB`는 이 토픽 입력
  경로로 합류한다.

### Routed recv/reply

```c
zlink_recv_result_t zlink_spot_recv(
  void *spot,
  const zlink_routing_id_t **source_rid_out,
  const zlink_routing_id_t **spot_rid_out,
  uint64_t *request_seq_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_reply_spot(
  void *spot,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);

zlink_submit_result_t zlink_spot_reply_router(
  void *spot,
  const zlink_routing_id_t *peer_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

- `zlink_spot_recv()`는 routed receive plane을 읽는다.
- `zlink_spot_recv()`는 현재 `Spot`에 귀속된 routed ingress만 읽는다.
- 첫 `zlink_spot_recv()`가 routed target registration이나 hidden queue open을
  수행하지 않는다.
- `ZLINK_DONTWAIT`에서 `EAGAIN`이 나오면, 그 시점에는 해당 `Spot`의 routed ingress에
  읽을 데이터가 없다는 뜻이다.
- 수신한 요청이 SPOT origin이면 `zlink_spot_reply_spot()`으로 reply한다.
- 수신한 요청이 ROUTER origin이면 `zlink_spot_reply_router()`로 reply한다.
- reply 경로는 수신 이벤트가 알려준 concrete source address를 그대로 사용해야 한다.
- routed recv metadata인 `source_rid`, `spot_rid`, `request_seq`는 local forward가
  끼어도 application recv 표면까지 그대로 유지되어야 한다.

### Handler

#### Dispatch event 타입

```c
typedef enum zlink_spot_dispatch_event_t {
  ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE    = 1,
  ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE       = 2,
  ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE        = 3,
  ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE = 4,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE        = 5,
  ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE   = 6
} zlink_spot_dispatch_event_t;

typedef enum zlink_spot_dispatch_subject_kind_t {
  ZLINK_SPOT_DISPATCH_SUBJECT_SPOT           = 1,
  ZLINK_SPOT_DISPATCH_SUBJECT_TIMER          = 2,
  ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3,
  ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR          = 4
} zlink_spot_dispatch_subject_kind_t;

typedef struct zlink_spot_dispatch_info_t {
  zlink_spot_dispatch_event_t        event;
  zlink_spot_dispatch_subject_kind_t subject_kind;
  void                              *subject;
} zlink_spot_dispatch_info_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_);
```

- `event`는 어떤 종류의 work가 준비됐는지 나타낸다.
- `subject_kind`는 `subject` 포인터를 어떤 타입으로 해석해야 하는지를 나타낸다.
- `subject`는 실제 drain(큐에 쌓인 메시지를 꺼내 소비하는 행위) 대상 인스턴스다.

각 조합의 의미는 아래와 같다.

| event | subject_kind | subject | drain 방법 |
|-------|-------------|---------|------------|
| `SUBSCRIBE_READABLE` | `SPOT` | `spot_` (또는 NULL) | `zlink_spot_subscribe()` |
| `ROUTED_READABLE` | `SPOT` | `spot_` (또는 NULL) | `zlink_spot_recv()` |
| `TIMER_READABLE` | `TIMER` | timer handle | `zlink_timer_recv()` |
| `CHANNEL_REPLY_READABLE` | `CHANNEL_DEALER` | attached dealer handle | 없음 — 요청 시 등록한 `zlink_reply_handler_fn`이 자동으로 호출됨 |
| `ACTOR_READABLE` | `ACTOR` | callback lifetime의 `const zlink_actor_ref_t *` | `zlink_spot_node_actor_recv_part()` |
| `ACTOR_JOIN_READABLE` | `SPOT` | `spot_` | `zlink_spot_actor_join_recv()` |

dispatch 우선순위는 아래 순서로 고정한다.

1. `SUBSCRIBE_READABLE`
2. `ROUTED_READABLE`
3. `CHANNEL_REPLY_READABLE`
4. `TIMER_READABLE`
5. `ACTOR_JOIN_READABLE`
6. `ACTOR_READABLE`

`CHANNEL_REPLY_READABLE` 이벤트가 뜻하는 것은 "해당 attached dealer를 통해 시작한
channel request 중 user callback을 실행할 준비가 끝난 completion이 하나 이상 있다"는
것이다. raw dealer frame 수신 여부가 아니라, request completion 준비 상태를 알린다.

`SUBSCRIBE_READABLE`과 `ROUTED_READABLE`은 메시지 개수 이벤트가 아니라
readiness 이벤트다.

- callback 1회가 메시지 1개를 뜻하지 않는다.
- 이미 readable인 동안 같은 plane으로 메시지가 더 들어오더라도, dispatch callback
  개수와 메시지 개수는 1:1로 대응하지 않을 수 있다.
- 호출자는 해당 plane에서 `EAGAIN`이 나올 때까지 모두 읽어 처리하는 방식으로 처리해야 한다.
- `SUBSCRIBE_READABLE`은 node-wide broad fan-out이 아니라, 해당 `Spot`이 실제로
  subscribe recv를 할 수 있을 때만 올라와야 한다.
- `ACTOR_READABLE`은 특정 Actor의 unread part가 준비됐다는 뜻이다. callback의
  `subject`는 callback lifetime 동안만 유효한 `const zlink_actor_ref_t *`이며,
  호출자는 값을 복사한 뒤 `zlink_spot_node_actor_recv_part()`에 넘겨 모두 읽어 처리한다.
- `ACTOR_JOIN_READABLE`은 Spot에 처리할 Actor join request가 있다는 뜻이다.
  `zlink_spot_actor_join_recv()`가 `ZLINK_RECV_NO_DATA`를 반환할 때까지 모두 읽어 처리한다.

#### Channel reply 진행

- `CHANNEL_REPLY_READABLE`은 attached dealer에 진행이 발생했음을 알리는
  readiness 이벤트일 뿐, 별도 public drain API는 없다.
- 실제 reply 전달은 `zlink_spot_request_channel_part()` 호출 시 등록한
  `zlink_reply_handler_fn`을 통해 자동으로 이뤄지며, core 내부의 completion
  driver가 이를 수행한다.
- callback의 `subject`(dealer handle)는 어떤 attached dealer에서 진행이
  발생했는지 알려주는 진단 정보이며, 호출자가 직접 recv해야 한다는 뜻은
  아니다.

#### Handler 등록

```c
zlink_handler_result_t zlink_spot_dispatch_event_handler(
  void *spot,
  zlink_spot_dispatch_event_handler_fn handler,
  void *userdata);
```

`zlink_spot_dispatch_event_handler()`는 **통합 readiness notification**이다. 모든
이벤트 종류(subscribe, routed, channel reply, timer, Actor join, Actor readable,
Actor lifecycle)를 readiness 형태로 알린다. callback은 "읽을 것이 있다"는 신호이며,
실제 데이터는 각 plane의 drain API(`zlink_spot_recv_part()`,
`zlink_spot_subscribe_part()`, `zlink_spot_recv_actor_lifecycle()` 등)로 읽는다.

SPOT routed receive와 Actor lifecycle event는 **직접 callback 모드가 없다**.
dispatch readiness를 받은 뒤 명시적 drain API로 소비한다.

### Poller와의 관계

현재 public poller 계약은 바뀌지 않았다.

- `zlink_poller_event_t`는 owner spot / event kind / subject를 함께 표현하지 않는다.
- 따라서 현재 공개 계약에서 `Spot` 직접 등록으로 dispatch와 같은 의미를 받는
  poller 표면은 아직 없다.
- `Spot`의 subscribe/routed/timer/channel-reply readiness를 한 callback으로 다루려면
  `zlink_spot_dispatch_event_handler()`를 사용해야 한다.

## Actor 계약

Actor는 `SpotNode`가 소유하는 routing target이다. Actor는 public `void *` handle이
아니라 `zlink_actor_ref_t`로 식별한다. Actor는 socket, inproc endpoint, transport
endpoint를 소유하지 않는다. Actor별 HWM 설정은 공개 계약에 없다.

Actor는 항상 정확히 하나의 `Spot`에 속한다. 생성 직후에는 Entry Spot에 속하고,
`join`으로 user Spot으로 이동하며, `leave`로 Entry Spot으로 돌아온다. Actor가
dispatch context 없이 남는 구간은 없다. Actor로 들어오는 session relay message는
항상 current Spot dispatch event에서 `zlink_spot_node_actor_recv_part()`로 읽는다.

Actor 전용 dispatch context나 Actor 전용 callback은 제공하지 않는다. 이렇게 해야
join 전후 처리의 동기화 모델이 같은 Spot dispatch context 안에서 유지된다.

### Actor ref 타입

```c
#define ZLINK_ACTOR_ID_MAX 256

typedef struct zlink_actor_ref_t {
  zlink_routing_id_t node_rid;
  char actor_id[ZLINK_ACTOR_ID_MAX];
  uint64_t generation;
} zlink_actor_ref_t;

typedef struct zlink_actor_recv_info_t {
  zlink_actor_ref_t actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_session_rid;
  uint32_t flags;
} zlink_actor_recv_info_t;

typedef struct zlink_actor_join_info_t {
  zlink_actor_ref_t source_actor;
  zlink_actor_ref_t target_actor;
  zlink_routing_id_t source_node_rid;
  zlink_routing_id_t source_spot_rid;
  zlink_routing_id_t target_node_rid;
  zlink_routing_id_t target_spot_rid;
  uint64_t join_epoch;
  void *request;
  uint32_t flags;
} zlink_actor_join_info_t;

typedef struct zlink_actor_route_t {
  zlink_actor_ref_t actor;
  zlink_routing_id_t current_spot_rid;
  zlink_spot_kind_t current_spot_kind;
} zlink_actor_route_t;

typedef struct zlink_actor_join_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  zlink_routing_id_t joined_spot_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_actor_join_result_t;

typedef struct zlink_actor_join_entry_spot_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  zlink_routing_id_t target_node_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_actor_join_entry_spot_result_t;

typedef struct zlink_actor_lookup_result_t {
  zlink_request_result_t result;
  zlink_actor_ref_t actor;
  uint32_t flags;
} zlink_actor_lookup_result_t;

typedef struct zlink_spot_actor_lifecycle_info_t {
  zlink_actor_ref_t previous_actor;
  zlink_actor_ref_t current_actor;
  zlink_routing_id_t previous_spot_rid;
  zlink_routing_id_t current_spot_rid;
  uint64_t join_epoch;
  uint32_t flags;
} zlink_spot_actor_lifecycle_info_t;

typedef void (*zlink_actor_join_spot_handler_fn)(
  const zlink_actor_join_result_t *result,
  zlink_msg_t *parts,
  size_t part_count,
  void *userdata);

typedef void (*zlink_actor_join_entry_spot_handler_fn)(
  const zlink_actor_join_entry_spot_result_t *result,
  void *userdata);

typedef void (*zlink_actor_lookup_handler_fn)(
  const zlink_actor_lookup_result_t *result,
  void *userdata);

```

`zlink_actor_route_t`는 `zlink_discovery_resolve_actor()`의 출력 타입입니다.
`actor.node_rid`는 현재 Actor slot을 소유한 node이고,
`current_spot_rid`는 Actor의 현재 Spot이다. `current_spot_kind`는
Entry Spot이면 `ZLINK_SPOT_KIND_ENTRY`, user Spot이면 `ZLINK_SPOT_KIND_USER`다.

`zlink_actor_join_result_t`는 join completion handler에 전달된다.
`result`는 join operation의 최종 결과이고, 성공이면 `actor`는 최종 Actor ref(remote
join이면 target node의 ref), `joined_spot_rid`는 Actor가 속한 current Spot rid다.
성공 시 Actor가 속한 current node rid는 `actor.node_rid`를 기준으로 읽는다.
`join_epoch`는 commit된 위치 변경을 식별하는 진단값으로, 해당 Actor slot을 소유하는
SpotNode 안에서 0이 아닌 값으로 증가한다. `flags`는 현재 예약 필드이며 0이다.
실패 시 `actor`와 `joined_spot_rid`는 사용하지 않는다. `result` pointer는 callback
호출 중에만 유효하므로 필요한 값은 callback 안에서 복사한다.

`zlink_actor_join_entry_spot_result_t`는 Entry Spot join completion handler에
전달된다. 성공하면 `actor`는 이동이 끝난 뒤의 최종 Actor ref이고,
`target_node_rid`는 호출자가 지정한 target SpotNode rid다. Entry Spot join은
application join payload와 reply payload가 없으므로 callback은 message part를 받지
않는다. idempotent success도 성공 result를 반환하지만 위치가 바뀌지 않았으므로
joined/left lifecycle event을 다시 발생시키지 않는다.

`zlink_actor_lookup_result_t`는 remote Actor lookup completion handler에 전달된다.
`result`는 lookup operation의 최종 결과이고, 성공이면 `actor`는 target node에 존재하는
checked Actor ref다. `flags`는 현재 예약 필드이며 0이다.

`zlink_spot_actor_lifecycle_info_t`는 Spot lifecycle receive API에 전달된다.
`previous_actor`는 이동 전 Actor ref(생성 이벤트면 zero-value ref), `current_actor`는
이동 후 Actor ref(destroy 이벤트면 zero-value ref)다. 이동 전 node rid는
`previous_actor.node_rid`, 이동 후 node rid는 `current_actor.node_rid`로 읽는다.
`previous_spot_rid`/`current_spot_rid`는 위치 변경의 source/target Spot rid다.
zero-value ref는 `node_rid.size == 0`, `actor_id[0] == '\0'`, `generation == 0`이며,
이전 또는 이후 Actor가 없음을 뜻한다. `join_epoch`는 위치 변경의 commit epoch이고,
`flags`는 현재 예약 필드이며 0이다. `info` pointer는 callback 호출 중에만 유효하다.

`join_epoch`는 commit된 위치 변경을 식별하는 진단값이다. 서로 다른 Actor 또는 서로
다른 SpotNode 사이의 epoch 값은 비교하지 않는다. timeout, reject, validation
실패처럼 위치가 바뀌지 않은 작업은 epoch를 증가시키지 않는다. remote join에서는
source `on_leave`, target `on_join`, join completion이 서로 다른 SpotNode의 epoch
값을 가질 수 있다.

Actor id는 NUL 종료 UTF-8 바이트열이며, 유효 최대 길이는 `ZLINK_ACTOR_ID_MAX - 1`
(255바이트)다. 빈 id, NULL id, 255바이트를 넘는 id는 `EINVAL` 계열 실패다.
같은 `SpotNode` 안에서 live Actor id는 유일하다. 서로 다른 `SpotNode`에는 같은
Actor id가 동시에 존재할 수 있다.

`zlink_actor_ref_t.generation == 0`은 unchecked ref다. unchecked ref는 잘못된 값이
아니며, target node의 현재 같은 Actor id를 대상으로 해석한다. `generation != 0`인
checked ref는 target Actor의 현재 generation과 일치해야 한다. 일치하지 않으면
stale 또는 conflict 계열 실패로 끝난다.

### 생성과 조회

```c
zlink_config_result_t zlink_spot_node_actor_new(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *actor_out);

zlink_config_result_t zlink_spot_node_actor_lookup(
  void *node,
  const char *actor_id,
  zlink_actor_ref_t *out);

zlink_submit_result_t zlink_remote_actor_get_ref(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_actor_lookup_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

- `zlink_spot_node_actor_new()`는 local Actor를 Entry Spot에 만들고 checked ref를
  `actor_out`에 반환한다. Actor 생성은 Entry Spot dispatch handler나 join request
  handler를 거치지 않는다. 생성은 active route를 공개하지 않는다. 생성은 Entry
  Spot의 `joined lifecycle event을 scheduling한다.
- 같은 node에 같은 live Actor id가 이미 있으면 생성은 `EBUSY` 계열로 실패한다.
- `node_ == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
- `actor_id_ == NULL` 또는 `actor_out_ == NULL`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`로
  실패하고 `errno`는 `EINVAL`이다.
- `zlink_spot_node_actor_lookup()`은 caller node가 소유한 live local Actor를 조회하고
  checked ref를 반환한다. 같은 Actor id의 live Actor가 없으면 not-found 계열 실패다.
- `zlink_remote_actor_get_ref()`는 remote node에 Actor가 존재하는지 확인해서 checked
  ref를 반환하는 async lookup API다. `node`는 lookup request를 제출하는 request owner
  `SpotNode`다. target node에 해당 Actor가 있으면 checked ref를 completion의
  `result->actor`에 반환하고, 없으면 not-found 계열 completion으로 실패한다. lookup
  대상은 commit된 live Actor뿐이며, remote join 준비 중인 pending target Actor는 결과로
  노출하지 않는다. target node와의 control path가 없으면 not-connected 계열 completion으로
  실패한다. `target_node_rid`가 request owner node 자신이면 local lookup과 같은 의미로
  처리한다. `handler == NULL`이면 invalid argument 계열 submit 실패다. `timeout_ms > 0`이면
  submit 뒤 completion까지의 operation timeout이고, `timeout_ms == 0`이면 timeout을
  설치하지 않는다. `result` pointer는 callback 호출 중에만 유효하다. 이 함수는 Actor를
  생성하지 않고, Actor 위치를 바꾸지 않고, active route를 갱신하지 않는다.
- `zlink_remote_actor_get_ref()`와 `zlink_discovery_resolve_actor()`는 목적이 다르다.
  전자는 caller가 target node rid와 actor id를 이미 알고 있을 때 해당 node에 직접 물어
  checked ref를 얻는 API이고, 후자는 Registry에 공개된 active route를 조회해 현재
  공개 위치를 얻는 API다.

### Remote Actor 생성 모델

remote Actor 생성 API는 제공하지 않는다. remote node의 lobby에서 새로 시작해야 하는
Actor는 application이 해당 SpotNode에서 `zlink_spot_node_actor_new()`로 직접
생성한다. 이미 존재하는 Actor를 다른 SpotNode의 Entry Spot 위치로 옮길 때는
`zlink_spot_node_actor_join_entry_spot()`을 사용한다. 원격 배치는 아래 흐름으로
표현한다.

1. caller가 local Actor를 생성한다.
2. 필요하면 `zlink_spot_node_actor_join_spot()`으로 원하는 SpotNode의 user Spot에
   이동한다.
3. user Spot에서 나오면 같은 node의 Entry Spot은
   `zlink_spot_node_actor_leave_spot()`으로, 다른 SpotNode의 Entry Spot은
   `zlink_spot_node_actor_join_entry_spot()`으로 이동한다.
4. join completion이 반환한 최종 Actor ref를 후속 Actor API에 사용한다. 기존 logical
   session binding은 join 성공 뒤 별도 reattach 없이 새 위치를 따른다.

remote Actor destroy는 `zlink_spot_node_actor_destroy()`로 ref 기반 요청을 보낸다.
target node에 도달할 수 없거나 checked ref generation이 맞지 않으면 Actor slot을
제거하지 않는 실패다.

### Spot join

```c
zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_actor_join_spot_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_spot_node_actor_join_entry_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  zlink_actor_join_entry_spot_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *parts,
  size_t part_count);
```

`join`은 Actor를 현재 Spot에서 target user Spot으로 이동하는 요청이다. `join`이
성공하면 Actor의 current Spot이 target으로 바뀐다.

`zlink_spot_node_actor_join_spot()` 계약:

- `node`는 join request를 제출하는 request owner `SpotNode`다. session owner, backend
  service node, source Actor owner node 모두 request owner가 될 수 있다.
- `dest_node_rid`가 Actor owner node와 같으면 local join으로, 다르면 remote join
  handoff로 처리한다.
- `dest_spot_rid`는 target node의 user Spot이어야 한다. Entry Spot은 이 API의 target이
  아니다. target이 Entry Spot이면 invalid argument 계열 실패다.
- Actor는 bound STREAM session이 없어도 user Spot으로 join할 수 있다. Actor 위치
  이동과 session attach는 서로 다른 상태 전이이며 session attach는 위치 이동의
  유효성 조건이 아니다.
- 이미 대상 Spot에 있으면 join request handler를 거치지 않고 비동기 idempotent success
  completion으로 완료한다. idempotent join도 route가 없거나 stale이면 현재 위치로
  갱신한다.
- join request가 pending 중인 Actor에 새 join, leave, destroy를 요청하면 busy 또는
  invalid-state 계열 실패다.
- target Spot에 dispatch handler가 없으면 user Spot join request는 자동 accept되지
  않는다. `timeout_ms > 0`이면 timeout까지 pending 상태로 남고, `timeout_ms == 0`이면
  handler가 등록되어 처리하거나 Spot/SpotNode가 종료될 때까지 pending 상태로 남는다.
- return이 `ZLINK_SUBMIT_OK`이면 join operation이 접수된 것이지 accept가 된 것은 아니다.
  accept/reject 결과는 `zlink_actor_join_spot_handler_fn` completion으로 전달한다. completion은
  최종 Actor ref(remote join이면 target node의 ref)와 joined Spot rid를 포함한다.
- `handler == NULL`이면 invalid argument 계열 submit 실패다. join 성공 뒤 caller가
  최종 Actor ref를 받아야 후속 Actor API나 위치 이동을 정확히 수행할 수 있기 때문이다.
- `timeout_ms`는 submit 성공 뒤 join reply와 remote handoff가 완료되기까지의 operation
  timeout이다. `timeout_ms == 0`이면 operation timeout을 설치하지 않는다. 이는 submit
  nonblocking 지시가 아니다. submit 단계의 즉시 실패 여부는 `flags`의 `ZLINK_DONTWAIT`가
  결정한다.
- submit 성공 시 `parts` 소유권은 라이브러리로 이전된다. local validation 또는
  submit 전 실패가 발생하면 소유권은 caller에게 남는다.
- join request는 `zlink_msg_t` part 배열로 이루어진 multipart payload를 싣는다.
  target Spot은 이 payload를 읽고 accept 또는 reject를 결정한다.
- accept commit 뒤 active route를 target user Spot 위치로 갱신한다.

`zlink_spot_node_actor_join_entry_spot()` 계약:

- `node`는 Entry Spot 이동 요청을 제출하는 request owner `SpotNode`다.
- `dest_node_rid`는 Entry Spot rid가 아니라 target SpotNode rid다. Entry Spot은
  SpotNode마다 하나이므로 별도 target Spot rid를 받지 않는다.
- 이 API는 Actor를 target SpotNode의 Entry Spot 위치로 이동한다. local target이면
  target Entry Spot state를 사용하고, remote target이면 기존 remote Actor 이동 규칙에
  따라 target actor placeholder, route, bound session relay 위치를 갱신한다.
- application join queue에 요청을 넣지 않는다.
  `zlink_spot_actor_join_recv()`로 읽을 message가 생기지 않으며
  `zlink_spot_actor_join_reply()`도 사용하지 않는다.
- payload와 reply parts가 없다. completion callback은
  `zlink_actor_join_entry_spot_handler_fn`으로 result만 받는다.
- 성공 completion의 `actor`는 이동 이후 최종 Actor ref이고,
  `target_node_rid`는 호출자가 넘긴 target SpotNode rid다.
- 이미 같은 target SpotNode의 Entry Spot에 있는 Actor는 idempotent success로
  완료한다. 이 경우 joined/left lifecycle event을 다시 발생시키지 않는다.
- 실제 위치가 바뀌면 이전 user Spot에는 leave lifecycle event이, target Entry
  Spot에는 joined lifecycle event이 발생한다.
- target node에 도달할 수 없으면 not-connected 계열 completion으로 끝난다.
- invalid actor ref, checked ref generation mismatch, pending join 중복은 기존
  Actor API 정책과 같은 invalid-argument 또는 invalid-state 계열 실패로 끝난다.
- `handler == NULL`이면 invalid argument 계열 submit 실패다. caller는 completion에서
  최종 Actor ref를 받아 후속 Actor API와 session bind에 사용해야 한다.

`zlink_spot_actor_join_recv()` 계약:

- `ACTOR_JOIN_READABLE` dispatch event가 뜨면 해당 Spot에서 이 API로 모두 읽어 처리한다.
- 성공 시 join message 소유권은 호출자에게 이전된다.
- `zlink_actor_join_info_t.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE`가 0이 아니면
  remote handoff join이다. payload에서 Actor state를 복원할 수 없으면 reject해야 한다.
- `flags`의 현재 공개 bit는 `ZLINK_ACTOR_JOIN_INFO_REMOTE`뿐이다. 알 수 없는 bit는
  무시하지 말고 invalid protocol로 처리한다. 버전 협상 없이 새 public bit를 추가하지
  않는다. 새 bit가 필요하면 새 recv/reply 계약 또는 versioned info 구조체를 정의한다.
- target Spot join handler가 Actor 입장 승인을 결정한다. Entry Spot은 생성과 leave의
  lobby이며 별도 admission 대상이 아니다.
- remote join에서 target node에 같은 actor id의 live Actor가 이미 있으면 source
  Actor를 target node로 이동해 새 current Actor를 만들 수 없으므로 conflict 계열
  실패다.
- remote join에서 checked ref의 generation이 target node의 existing Actor와 충돌하면
  stale 또는 conflict 계열 실패다.
- `zlink_actor_join_info_t.request`는 opaque one-shot handle이다. application은 이
  값을 역참조하거나 직접 저장하지 않는다. reply 호출에서 `info` 구조체를 그대로 넘긴다.

`zlink_spot_actor_join_reply()` 계약:

- `accepted`는 반드시 `0`(reject) 또는 `1`(accept)이어야 한다. 다른 값은
  `ZLINK_SUBMIT_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `info == NULL`이면 `ZLINK_SUBMIT_INVALID_ARGUMENT`로 실패하고 `errno`는 `EINVAL`이다.
- `message == NULL`이면 payload 없는 completion이다.
- submit 성공 시 reply message 소유권은 라이브러리로 이전된다. validation 실패 또는
  duplicate reply 실패 시 소유권은 caller에게 남는다.
- 같은 `info.request`에 두 번 reply하면 두 번째는 `ZLINK_SUBMIT_INVALID_STATE`로 실패하고
  `errno`는 `EALREADY` 또는 `EINVAL`이다.
- join timeout, target Spot destroy, SpotNode shutdown 뒤 늦게 도착한 reply는
  `ZLINK_SUBMIT_INVALID_STATE`로 실패한다.
- `info.request`의 lifetime은 join request가 reply, timeout, reject cleanup, Spot
  destroy, node shutdown으로 끝날 때까지다.

join 원자성:

- accept 전까지 source Spot이 current Spot이다.
- target Spot이 reject하거나 timeout되면 Actor는 source Spot에 그대로 남는다.
- remote join에서 source Actor는 session Actor list compare-and-swap이 성공하고
  target Actor activate와 active route 갱신이 끝난 뒤 source Spot에서 제거된다.
- join 전후에 도착한 Actor queue message의 순서는 Actor queue 도착 순서로 보존한다.
- Spot destroy는 joined Actor나 pending join request가 남아 있으면 busy로 실패한다.

### Spot leave

```c
zlink_submit_result_t zlink_spot_node_actor_leave_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *current_spot_rid,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

`leave`는 Actor를 현재 Spot에서 같은 node의 Entry Spot으로 돌려보내는 async submit
API다. local Actor와 remote Actor 모두 같은 submit + completion 경로를 사용한다.

- `node`는 leave request owner다. 실제 leave는 `actor.node_rid`가 가리키는 Actor
  owner node에서 수행한다. request owner와 Actor owner가 다르면 내부 control path로
  요청을 전달한다.
- `current_spot_rid`는 Actor owner node 안에서 Actor가 현재 속한 Spot rid와 일치해야
  한다. 일치하지 않으면 stale leave로 보고 invalid-state 계열 실패다.
- Actor가 이미 Entry Spot에 있고 `current_spot_rid`도 그 Entry Spot rid와 일치하면
  leave는 idempotent success다. 이 경우 `on_leave`와 `on_join`은 호출하지 않는다.
  route가 이미 존재하지만 stale이면 Entry Spot 위치로 갱신하고, route가 없으면 새
  route를 만들지 않는다.
- Actor가 이미 Entry Spot에 있는데 caller가 user Spot rid를 `current_spot_rid`로
  넘기면 stale leave로 보고 invalid-state 계열 실패다.
- Actor에 join request가 pending이면 busy 또는 invalid-state 계열로 실패한다. leave는
  pending join을 취소하지 않는다.
- user Spot에서 Entry Spot으로 실제 위치가 바뀐 leave 성공은 source Spot `on_leave`와
  Entry Spot `joined lifecycle event을 scheduling하고, active route를 Entry Spot
  위치로 갱신한다.
- leave 성공 뒤 Actor message는 Entry Spot dispatch event로 올라간다. leave는 Actor
  queue를 비우지 않는다. leave 전후 message 순서는 보존한다.
- leave 최종 결과는 `zlink_reply_handler_fn` completion으로 전달한다. completion
  payload는 없으므로 `parts == NULL`, `part_count == 0`이다.
- `handler == NULL`이면 invalid argument 계열 submit 실패다. submit 단계에서 실패한
  작업은 completion을 호출하지 않는다.
- `node == NULL`이면 invalid handle 계열 submit 실패다.
- `actor == NULL` 또는 `current_spot_rid == NULL`이면 invalid argument 계열 submit
  실패다.
- `timeout_ms > 0`이면 submit 뒤 completion까지의 operation timeout이고, `timeout_ms == 0`
  이면 timeout을 설치하지 않는다.

### 종료

```c
zlink_submit_result_t zlink_spot_node_actor_destroy(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);
```

- Actor destroy는 Actor가 Entry Spot에 있을 때만 성공한다.
- Actor가 user Spot에 있으면 invalid-state 계열로 실패한다. application은 먼저 `leave`로
  Entry Spot에 돌려보낸 뒤 Actor를 제거한다.
- join request가 pending이면 busy 또는 invalid-state 계열로 실패한다. destroy는
  pending join을 취소하지 않는다.
- destroy 성공 시 Entry Spot에서 Actor를 제거하고, active route가 이 Actor ref를
  가리키면 route를 제거한다. active route가 다른 generation의 Actor를 가리키면
  destroy는 그 route를 제거하지 않는다.
- destroy 성공은 current Spot의 `left lifecycle event을 scheduling한다.
- destroy 성공 뒤 해당 Actor ref는 stale이 된다. local Actor와 remote Actor 모두
  같은 submit + completion 경로를 사용한다.
- destroy 최종 결과는 `zlink_reply_handler_fn` completion으로 전달한다. completion
  payload는 없으므로 `parts == NULL`, `part_count == 0`이다.
- `handler == NULL`이면 invalid argument 계열 submit 실패다.
- `node == NULL`이면 invalid handle 계열 submit 실패다.
- `actor == NULL`이면 invalid argument 계열 submit 실패다.
- `timeout_ms > 0`이면 submit 뒤 completion까지의 operation timeout이다.

### Message recv

```c
zlink_recv_result_t zlink_spot_node_actor_recv_part(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_actor_recv_info_t *info_out,
  zlink_msg_t *part_out,
  zlink_part_flag_t *has_more_out,
  zlink_recv_flags_t flags);
```

- current Spot dispatch context에서 `ACTOR_READABLE` event를 받은 뒤 호출한다.
- `node`는 Actor owner `SpotNode`여야 한다. remote route는 수행하지 않는다.
- `node == NULL`이거나 `node`가 Actor owner가 아니면 `ZLINK_RECV_INVALID_HANDLE`로
  실패하고 `errno`는 `EFAULT`다.
- `actor == NULL`, `info_out == NULL`, `part_out == NULL`, `has_more_out == NULL`이면
  `ZLINK_RECV_INVALID_HANDLE`로 실패하고 `errno`는 `EFAULT`다.
  (`zlink_recv_result_t`에는 invalid-argument bucket이 없으므로 NULL output pointer도
  recv 계열 invalid-handle failure로 통일한다.)
- 읽을 part가 없고 `ZLINK_DONTWAIT`이면 `ZLINK_RECV_NO_DATA`다.
- 성공 시 `part_out` 소유권은 호출자에게 이전된다.
- `has_more_out`은 `ZLINK_PART_MORE` 또는 `ZLINK_PART_FINAL`을 반환한다.
- `info_out->actor`는 drain 대상 Actor ref이고, `source_node_rid`와
  `source_session_rid`는 message를 보낸 STREAM session을 식별한다.
- `info_out->flags`는 현재 `0`이다. 알 수 없는 bit는 invalid protocol로 처리한다.
  버전 협상 없이 새 public bit를 추가하지 않는다.

### STREAM session 연결

```c
zlink_config_result_t zlink_stream_attach_actor_gateway(
  void *stream,
  void *node);

zlink_submit_result_t zlink_stream_bind_actor(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_unbind_actor(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_reply_handler_fn handler,
  void *userdata,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_send_bound_actor_part(
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_msg_t *part,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag);

zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg(
  void *node,
  const zlink_actor_ref_t *actor,
  zlink_msg_t *message,
  zlink_send_flags_t flags);

zlink_config_result_t zlink_stream_bound_actors(
  void *stream,
  const zlink_routing_id_t *session_rid,
  zlink_actor_ref_t *entries,
  size_t *count);

zlink_request_result_t zlink_spot_node_actor_close_bound_session(
  void *node,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);
```

`zlink_stream_attach_actor_gateway()`는 STREAM Actor relay에 사용할 session owner
`SpotNode`를 정한다. `node`는 routed-capable `SpotNode`여야 한다. 같은 stream을 같은
node에 다시 attach하면 성공한다. 같은 stream을 다른 node에 attach하면 invalid-state 계열
config 실패다.

session attach는 Actor 위치와 독립이다. session attach 성공/실패는 Actor의 current
Spot을 바꾸지 않고, Actor 위치 이동(join/leave)은 session mapping을 바꾸지 않는다.

session owner node와 Actor owner node는 같을 수도 다를 수도 있다.

- **local Actor**: session owner node와 Actor owner node가 같다. bind, relay,
  Actor-to-session send가 같은 node 안에서 끝난다.
- **remote Actor**: session owner node와 Actor owner node가 다르다. bind control
  request, session-to-Actor relay frame, Actor-to-session frame이 node 사이를 지난다.
- session owner는 Actor의 joined Spot 상태를 저장하지 않는다.
- Actor owner는 STREAM session의 application state를 저장하지 않는다.
- session attach 성공은 active route를 만들거나 갱신하지 않는다. session detach
  성공도 active route를 제거하지 않는다. active route 갱신 시점은
  [Discovery active route](#discovery-active-route) 절을 본다.

`zlink_stream_bind_actor()` / `zlink_stream_unbind_actor()` 계약:

- bind와 unbind는 nonblocking submit API다. remote Actor owner의 응답이 필요해도 호출
  thread를 blocking하지 않는다.
- 최종 결과는 `zlink_reply_handler_fn` completion으로 전달한다. completion payload는
  없으므로 `parts == NULL`, `part_count == 0`이다.
- `handler == NULL`이면 invalid argument 계열 submit 실패다. submit 단계에서 실패한
  작업은 completion을 호출하지 않는다.
- local Actor bind/unbind도 같은 completion 경로를 사용한다.
- `timeout_ms > 0`이면 submit 뒤 completion까지의 operation timeout이고, `timeout_ms == 0`
  이면 timeout을 설치하지 않는다.
- `stream`은 STREAM session Actor mapping을 소유한다. `stream`은 session owner
  `SpotNode`와 연결되어 있어야 한다. raw 또는 connector STREAM은
  `zlink_stream_attach_actor_gateway()`로 연결하고, SpotNode가 내부에서 소유한 stream만
  구조적으로 owner를 알 수 있다. remote Actor owner와의 control path와 relay는 이 owner
  `SpotNode`가 수행한다.
- ActorGateway에 attach되지 않은 raw 또는 connector STREAM에서 bind를 호출하면
  invalid-state 계열 completion으로 실패한다. bind 대상 Actor의 `node_rid`를 session
  owner fallback으로 사용하지 않는다.
- bind 대상 Actor의 owner node는 `actor->node_rid`로 찾는다. Actor가 remote node에
  있으면 stream owner `SpotNode`가 `actor->node_rid`의 `SpotNode`로 bind 정보를
  전달한다.
- `stream` 하나만으로는 client session을 식별하지 않는다. 하나의 raw STREAM socket은
  여러 client session을 multiplex할 수 있으므로 `session_rid`가 있어야 특정 client
  session을 고를 수 있다. `stream`, `session_rid` 조합이 하나의 STREAM session binding
  key다.
- unbind는 `stream`, `session_rid`, `actor_id`로 session mapping을 찾고, 그 mapping에
  저장된 Actor ref를 기준으로 Actor owner에 detach를 전달한다.
- 하나의 session은 여러 Actor ref를 attach할 수 있다. 같은 session에 같은 Actor ref를
  다시 attach하면 idempotent success다.
- 같은 session에 같은 actor id로 다른 generation을 attach하면 해당 actor id 항목을
  새 Actor ref로 교체한다. 이때 이전 Actor의 bound session ref도 함께 제거한다.
- 이미 다른 session에 attach된 Actor를 새 session에 attach하면 busy 계열 실패다.
  Actor당 단일 session 제약을 유지한다. 새 session에 attach하려면 기존 attach를 먼저
  해제해야 한다.

`zlink_stream_send_bound_actor_part()` 계약:

- session-bound relay는 별도 `SpotNode` 인자를 받지 않는다. `stream`과 `session_rid`
  로 session mapping을 찾고 `actor_id`에 저장된 Actor ref의 owner로 message part를
  relay한다.
- fire-and-forget submit이며 completion handler가 없다.
- 내부 queue나 runtime lock을 즉시 확보할 수 없으면 호출 thread를 기다리게 하지 않고
  backpressured 계열 submit 실패로 돌려보낸다.
- submit이 `ZLINK_SUBMIT_OK`이면 message 소유권은 라이브러리로 이전된다. submit 실패면
  소유권은 caller에게 남고 implementation은 `part`를 close하거나 reinit하지 않는다.

`zlink_spot_node_actor_send_bound_session_msg()` 계약:

- Actor에서 bound session으로 message를 돌려보내는 반대 방향 fire-and-forget relay다.
  completion handler가 없으며 return 값은 command 접수 여부만 나타낸다.
- request owner `node`는 호출을 제출하는 SpotNode이고, 실제 session mapping은
  `actor->node_rid`가 가리키는 Actor owner에서 확인한다. Actor가 remote node에 있으면
  request owner는 Actor owner로 relay request를 전달하고, Actor owner는 저장된 bound
  session ref를 기준으로 stream owner에 message를 전달한다.
- 내부 queue나 runtime lock을 즉시 확보할 수 없으면 호출 thread를 기다리게 하지 않고
  backpressured 계열 submit 실패로 돌려보낸다.
- submit 성공 시 `message` 소유권은 라이브러리로 이전된다. submit 실패면 소유권은
  caller에게 남는다.

remote join이 성공하면 기존 session mapping은 같은 Actor id/generation 조건 아래에서 target
Actor 위치로 갱신된다. session relay를 유지하려고 application이 join completion의 최종
Actor ref로 다시 attach할 필요는 없다. reject와 timeout은 기존 session mapping을 바꾸지
않는다.

`zlink_stream_bound_actors()` 계약:

- 특정 STREAM session에 attach된 Actor ref 목록을 반환하는 snapshot API다.
- 일반 snapshot API와 같은 2-pass 규칙을 따른다. `entries == NULL`이면 필요한 개수를
  `*count`에 반환하고, `entries != NULL`이면 최대 `*count`개를 채운 뒤 실제로 채운
  개수를 `*count`에 반환한다.
- session mapping이 없으면 성공하면서 `*count == 0`이다.
- 이 함수는 `stream`의 local session mapping만 읽고, remote Actor owner에 존재 확인
  요청을 보내지 않는다. 반환된 ref가 stale일 수 있으므로 현재 존재 여부까지 확인해야
  하면 ref 기반 API나 remote lookup을 별도로 호출한다.

`zlink_spot_node_actor_close_bound_session()` 계약:

- Actor의 bound STREAM session을 닫고 session Actor list 항목과 Actor의 bound session
  ref를 제거한다.
- bound STREAM session이 없으면 `ZLINK_REQUEST_NOT_FOUND` 계열로 실패한다.
- close 성공은 Actor의 current Spot을 바꾸지 않고 active route를 제거하지 않는다.
- close 성공 뒤 Actor queue에 unread message가 남아 있으면 current Spot dispatch
  handler에 `ACTOR_READABLE` event를 올린다.
- `timeout_ms == 0`은 nonblocking request다.

### Spot lifecycle receive

```c
typedef enum zlink_spot_actor_lifecycle_event_kind_t {
  ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1,
  ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2
} zlink_spot_actor_lifecycle_event_kind_t;

typedef struct zlink_spot_actor_lifecycle_event_t {
  zlink_spot_actor_lifecycle_event_kind_t kind;
  zlink_spot_actor_lifecycle_info_t info;
} zlink_spot_actor_lifecycle_event_t;

zlink_recv_result_t zlink_spot_recv_actor_lifecycle(
  void *spot,
  zlink_spot_actor_lifecycle_event_t *event_out,
  zlink_recv_flags_t flags);
```

`zlink_spot_recv_actor_lifecycle()`는 특정 Spot의 Actor lifecycle event를 하나씩
drain한다. lifecycle readiness는 `ACTOR_LIFECYCLE_READABLE` dispatch event로 알리고,
payload는 직접 callback으로 전달하지 않는다.

- `kind`는 Actor가 Spot에 들어오면 `JOINED`, Spot을 떠나면 `LEFT`다.
- `kind`는 `info`가 담고 있는 상태 전이를 읽기 쉽게 분류한 값이며, 둘은 항상 같은
  전이를 가리켜야 한다.
- `spot == NULL` 또는 `event_out == NULL`이면 invalid handle 계열 실패다.
- `ZLINK_DONTWAIT`이고 queue가 비어 있으면 `ZLINK_RECV_NO_DATA`와 `EAGAIN`을 반환한다.
- lifecycle event는 dispatch handler가 이미 등록된 Spot에만 쌓인다. 등록 전 과거
  전이는 나중에 replay하지 않는다.

`on_join` 발생 조건:

- Actor 생성 직후 Entry Spot에 속하는 초기 배치(생성으로 호출되는 `on_join`은
  admission을 거치지 않는다).
- Actor가 join operation으로 해당 Spot에 들어온 경우(target Spot에서 호출).
- 명시적 leave API가 Actor를 user Spot에서 Entry Spot으로 이동시킨 경우(Entry Spot에서
  호출).

`on_leave` 발생 조건:

- Actor가 join operation으로 해당 Spot을 떠난 경우(source Spot에서 호출).
- 명시적 leave API가 Actor를 user Spot에서 Entry Spot으로 이동시킨 경우(source Spot
  에서 호출).
- Actor destroy 성공으로 Actor가 current Spot에서 제거된 경우.

`info` 내용:

- Actor 생성으로 호출되는 `on_join`에서 `previous_actor`는 zero-value ref,
  `previous_spot_rid.size == 0`이다.
- Actor destroy 성공으로 호출되는 `on_leave`에서 `current_actor`는 zero-value ref,
  `current_spot_rid.size == 0`이다.
- local join에서는 `previous_actor`와 `current_actor`가 같고, remote join에서는 다를
  수 있다.
- 명시적 leave 성공에서는 `previous_actor`와 `current_actor`가 같은 Actor ref이고,
  `previous_spot_rid`는 user Spot, `current_spot_rid`는 같은 node의 Entry Spot이다.
- `join_epoch`는 `on_join`에서 `current_actor`가 가리키는 Actor slot의 commit epoch,
  `on_leave`에서 `previous_actor`가 가리키는 Actor slot의 commit epoch다.

전달 규칙:

- lifecycle event은 선택 사항이다. 해당 Spot에 lifecycle receive API가 등록된 경우에만
  호출한다.
- handler가 없을 때 발생한 생성, join, leave, destroy callback은 나중에 재전달하지
  않는다.
- lifecycle event은 Actor queue payload가 아니므로 `zlink_spot_node_actor_recv_part()`
  로 읽지 않는다.
- callback은 해당 Spot의 dispatch worker context에서 호출한다. 같은 Spot의 dispatch
  callback과 lifecycle event은 동시에 실행되지 않는다.
- 서로 다른 Spot의 lifecycle event 사이에는 실행 순서 보장이 없다. 하나의 join
  operation에서 source `on_leave`와 target `on_join`은 모두 commit 뒤 scheduling되지만
  두 callback이 실제로 어느 순서로 실행되는지는 공개 계약으로 보장하지 않는다.
- join completion handler는 state commit과 active route 갱신이 끝난 뒤 호출된다.
  lifecycle event이 이미 실행되었는지는 보장하지 않는다.
- application state machine이 join 완료 순서를 결정할 때는 join completion handler와
  반환된 최종 Actor ref를 기준으로 삼아야 한다. lifecycle event은 관측용이다.
- `info` pointer는 callback 호출 중에만 유효하므로 필요한 값은 callback 안에서
  복사한다.
- lifecycle event 안에서 같은 Actor에 대해 join, leave, destroy를 재진입 호출하는
  것은 지원하지 않는다.

### Discovery active route

`zlink_actor_route_t`는 Actor의 현재 dispatch 위치를 나타낸다.
이 route는 `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`가 켜진 Discovery와 Registry가
있을 때 외부 조회 결과로 관측된다. 옵션이 꺼져 있거나 Registry가 연결되지 않은
환경에서는 local Actor 위치가 바뀌어도 `zlink_discovery_resolve_actor()`가 실패할 수
있다.

- `actor`는 active route가 가리키는 최종 Actor ref다.
- `current_spot_rid`는 Actor의 current Spot이다.
- `current_spot_kind`는 Entry Spot 또는 user Spot 여부를 나타낸다.
- route는 join commit 뒤 공개된다.
- route는 session bind 여부를 나타내지 않는다.

route 갱신 시점:

| 이벤트 | route 동작 |
|--------|------------|
| local Actor 생성 | 공개하지 않음 |
| local user Spot join 성공 | user Spot 위치로 공개 또는 갱신 |
| remote user Spot join 성공 | target node user Spot 위치로 공개 또는 갱신 |
| user Spot에서 explicit leave 성공 | Entry Spot 위치로 공개 또는 갱신 |
| join reject | 변경 없음 |
| join timeout | 변경 없음 |
| session bind 성공 | 변경 없음 |
| session unbind 성공 | 변경 없음 |
| matching Actor destroy | route 제거 |
| stale Actor destroy | 변경 없음 |

위 route 갱신은 join 또는 leave commit 이후 Actor를 소유하는 current `SpotNode`의
Discovery에서 `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`가 켜져 있고 Registry와 통신할
수 있을 때 Registry visible 상태가 된다.

## Spot routed request 시작

`Spot`은 routed request와 one-way direct send를 직접 시작할 수 있다.
아래 경로들을 공개한다.

### core helper substrate

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

### C API wrapper

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);
```

- `zlink_spot_request_spot()`은 `zlink_spot_reply_spot(_part)`와 reply 짝을 이룬다.
- `zlink_spot_request_router()`는 `zlink_router_reply_spot(_part)`와 reply 짝을 이룬다.
- submit이 `ZLINK_SUBMIT_OK`이면 `handler_`가 정확히 한 번 호출된다.
- 그 외 반환값이면 handler는 등록되지 않는다.
- 반환 코드 의미는 [errno-map.ko.md](../errno-map.ko.md)의
  `zlink_submit_result_t`와 `zlink_request_result_t` 절을 참조한다.
- `zlink_spot_send_spot()`은 대상 `Spot`으로 one-way routed send를 수행한다. reply를 기다리지 않으며 handler가 없다.
- `zlink_spot_send_spot()`은 `ZLINK_SUBMIT_OK`이면 메시지가 전송 경로에 올라간 것이다.

## Router와 SPOT 직접 주소 지정

ROUTER는 concrete destination을 지정해 SPOT으로 one-way send 및 request를 보낼 수 있다.

```c
zlink_submit_result_t zlink_router_send_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_router_request_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_router_reply_spot(
  void *router,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  uint64_t request_seq,
  zlink_msg_t *parts,
  size_t part_count);
```

이 경로는 라우터 쪽 저수준 direct addressing 계약이다.

## 관찰과 스냅샷

```c
zlink_config_result_t zlink_spot_node_status(
  void *node,
  zlink_spot_node_status_t *out);

zlink_config_result_t zlink_spot_node_peers(
  void *node,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_peers(
  void *node,
  const zlink_spot_node_peer_filter_t *filter,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_subjects(
  void *node,
  const zlink_spot_node_subject_filter_t *filter,
  zlink_spot_node_subject_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_spots(
  void *node,
  zlink_spot_node_spot_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_node_actors(
  void *node,
  zlink_spot_node_actor_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_spot_actors(
  void *spot,
  zlink_actor_ref_t *entries,
  size_t *count);
```

- SPOT node monitor 전용 별도 recv API는 현재 공개 계약에 없다.
- snapshot/query API를 사용해 상태를 관찰한다.
- `entries == NULL`이면 snapshot 함수는 필요한 row 수를 `*count`에 쓴다.
- `*count`가 부족하면 `ENOBUFS`로 실패하고 필요한 row 수를 쓴다.
- `zlink_spot_node_status_t`와 peer/subject entry 구조체의 `service_name`
  필드는 현재 공개 이름을 유지한다.
- `zlink_spot_node_status_t.disconnected_sub_target_count`와
  `zlink_spot_node_status_t.disconnected_routed_target_count`는 ABI 호환을 위해 남아
  있다. 현재 HWM 정책은 delivery queue가 깊어졌다는 이유로 local subscribe 또는
  routed target을 끊지 않는다.
- `zlink_spot_node_spots()`은 local Spot 목록을 반환한다. Entry Spot도 이 목록에 포함된다.
  `spot_kind`는 Entry Spot과 user Spot을 구분한다. `joined_actor_count`,
  `pending_actor_join_count`, `route_synced`, `last_changed_ms`는 진단용 값이다.
- `zlink_spot_node_actors()`은 live local Actor row를 반환한다. 각 row에는
  Actor ref, current Spot rid, current Spot kind, route sync 여부, unread message count,
  `last_changed_ms`가 들어간다.
- `zlink_spot_actors()`은 특정 Spot에 join된 Actor ref 목록을 반환한다.
- snapshot 값은 flow control 계약으로 쓰지 않는다.

## 제약 요약

- `SpotNode` mesh peer 자동 연결 대상은 SPOT discovery peer뿐이다.
- 일반 socket service provider는 `SpotNode` mesh peer로 섞이지 않는다.
- channel 호출은 attach된 `DEALER`로만 처리한다.
- `SpotNode.router`를 channel 호출 경로로 우회해서 쓰지 않는다.
- discovery attach와 수동 peer connect를 같은 peer 관계에 동시에 섞지 않는다.
- attach 함수는 socket 생성과 connect를 대신하지 않는다.
- attach된 `DEALER`를 app이 raw `zlink_recv()`로 직접 읽거나 별도 poller에
  등록하면 `Spot` progress와 경합할 수 있다. attach 이후 그 socket은 SpotNode
  runtime 전용으로 취급한다.
- channel request의 transport owner는 attach된 `DEALER`지만, callback delivery
  owner는 request를 시작한 `Spot`의 dispatch stream이다.
- `CHANNEL_REPLY_READABLE` callback에서 `subject`를 일반 dealer처럼 raw recv하지
  않는다. 별도 public drain API는 없으며, reply는
  `zlink_spot_request_channel_part()`에 등록한 `zlink_reply_handler_fn`을 통해
  core가 내부적으로 전달한다.
