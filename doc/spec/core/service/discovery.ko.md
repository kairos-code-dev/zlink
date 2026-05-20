[English](./discovery.md) | [한국어](./discovery.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](./README.ko.md)

# 디스커버리

Discovery는 Registry가 알려 주는 서비스 목록을 받아서, 응용이 가까운 곳에서
바로 조회할 수 있게 정리해 두는 핸들입니다. 응용은 Discovery를 통해 "지금 어떤
서비스가 살아 있는가", "이 이름으로 보낼 수 있는 대상이 있는가"를 확인합니다.
또한 Discovery는 연결된 서비스의 수명 관리 창구 역할도 합니다. SPOT Node와 raw
소켓 패밀리(ROUTER/DEALER/PUB/SUB)는 자신을 Discovery에 붙여 두고 등록,
갱신, 종료를 함께 처리할 수 있습니다.

## SPOT 주소 조회와 캐시

관리형 SPOT 구성에서 Discovery는 `spot_rid` 주소의 **최종 기준**을 직접 들고
있는 주체가 아닙니다. 최종 기준은 Registry가 관리합니다. Discovery는 그 결과를
가까운 곳에 잠시 보관해 두었다가, send 경로에서 빠르게 꺼내 쓰는 역할을 맡습니다.

- Registry가 `spot_rid` 담당 노드를 어떻게 결정하는지:
  [registry.ko.md](./registry.ko.md)
- SPOT send/request/reply 함수 자체의 계약:
  [spot.ko.md](./spot.ko.md)

응용이 Discovery에 기대하는 핵심 역할은 단순합니다. "현재 Discovery가 보고
있는 `channel_name` 안에서 `spot_rid` 하나를 주면, 지금 이 이름을 맡고 있는
`SpotNode`의 `node_rid`를 알려 달라"는 요청에 빠르게 답하는 것입니다. 로컬에
최신 값이 있으면 바로 답하고, 없거나 오래되었으면 Registry 기준으로 다시
확인합니다.

이 문서 버전에서는 그 조회를 위해 아래 공개 API를 사용합니다.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

이 함수는 현재 Discovery의 `channel_name` 범위 안에서 `spot_rid`에 대한 현재
`SpotNode`의 `node_rid`를 구합니다. 성공하면 호출자는 반환된
`owner_node_rid_out`과 원래의 `spot_rid`를 묶어 ROUTER 쪽 direct 함수
(`zlink_router_send_spot()` 또는 `zlink_router_request_spot()`)에 전달할 수
있습니다.

### 캐시 모델

Discovery가 들고 있는 주소 캐시는 전체 주소표를 전부 복제한 것이 아닙니다.

- Discovery는 최근에 조회했거나 자주 쓰는 주소만 들고 있는 부분 캐시로 동작할
  수 있어야 합니다.
- 각 캐시 항목은 `spot_rid -> owner_node_rid` 결과와 함께, 새 정보와 오래된
  정보를 구분할 수 있는 순서 판정 값을 보존해야 합니다.
- 더 새로운 주소 정보가 들어오면 기존 항목을 바로 바꿔야 합니다.
- 더 오래된 정보가 뒤늦게 도착하면 무시해야 합니다.
- 철회되었거나 tombstone 상태로 바뀐 주소는 캐시에서 active owner 를 제거해야
  합니다.

캐시에 값이 없다는 사실만으로 "그 주소가 세상에 없다"라고 판단하면 안 됩니다.
Discovery는 캐시에 없을 때 Registry 기준으로 다시 확인할 수 있어야 합니다.

### 대규모 환경 전제

이 설계는 매우 많은 spot 주소가 동시에 존재하는 환경을 전제로 합니다. 구현은
적어도 "노드 1만 개, 노드당 spot 1만 개" 수준처럼 매우 큰 `spot_rid` 집합을
가정해야 합니다.

- 모든 Discovery 인스턴스가 모든 `spot_rid` 주소 정보를 항상 메모리에 들고
  있다고 가정해서는 안 됩니다.
- 주소 갱신과 생존 확인은 spot마다 heartbeat(연결 생존 확인 신호)를 하나씩 보내는 방식이 아니라,
  노드 세션 heartbeat, 여러 건을 묶은 갱신, lease 갱신 같은 집계 방식과 함께
  있어야 합니다.

### 조회 순서

`spot_rid` 하나만 가진 호출자는 현재 Discovery의 `channel_name` 안에서 아래
순서로 실제 전송에 필요한 `dest_node_rid + dest_spot_rid`를 얻습니다.

1. 로컬 Discovery 캐시에서 `spot_rid`를 먼저 찾습니다.
2. 현재 owner 가 있으면 그 값을 `dest_node_rid`로 사용하고, 원래 `spot_rid`와
   묶어 목적지 쌍을 만듭니다.
3. 캐시에 없거나 철회된 상태라면 Registry 기준으로 다시 조회합니다.
4. 다시 조회한 뒤에도 owner 가 없으면 목적지 없음으로 실패합니다.
5. send 이후 route miss, 오래된 owner 사용, handover 가 감지되면 1회에 한해
   다시 조회하고 재시도할 수 있습니다.

이 재시도는 최선 노력입니다. 무한 재시도는 계약이 아닙니다.

Registry 쪽 재조회는 구현에 따라 local shard, remote shard, 또는 그와 같은
기준 서비스로 보낼 수 있습니다.

### 로컬 최적 경로와 handover 중 request

조회 결과의 `owner_node_rid`가 현재 node와 같으면 구현은 로컬 최적 경로를 쓸
수 있습니다. 그래도 외부 계약은 바뀌지 않습니다. 호출자는
`dest_node_rid + dest_spot_rid`를 구한 뒤 일반 routed 경로로 보낸 것과
동일하게 해석합니다.

handover 도중 이미 특정 owner pair로 전달된 request는 그때 정해진 경로를
그대로 따릅니다. handover 이후 새로 조회한 request부터만 새 owner 를 사용합니다.

## 자동 연결 정책

Discovery에 붙은 channel의 자동 연결 범위는 현재 Discovery의 `channel_name`
입니다. 즉 자동 연결은 같은 channel 이름 안에서만 일어나며, 다른 channel 이름으로
넘어가지 않습니다.

### SpotNode Discovery attach

`zlink_spot_node_attach_discovery()`는 `ZLINK_AUTO_CONNECT_SPOT_MESH` Discovery만
받습니다. 이 Discovery가 node의 mesh auto-connect 범위를 결정하는 SPOT channel
view를 제공합니다.

- node에는 active SPOT Discovery view를 하나만 둘 수 있습니다.
- 두 번째 SPOT Discovery attach는 `EBUSY`로 실패합니다.
- attach된 Discovery를 제거하면 그 view가 공급하던 automatic peer set도
  함께 빠집니다.

### SpotNode channel dealer attach

`SpotNode`에서 다른 channel을 호출하려면 호출자가
`zlink_spot_node_attach_channel_dealer()`로 `DEALER`를 attach합니다. 이 함수는
`ZLINK_AUTO_CONNECT_CLIENT_SERVER` Discovery와 `DEALER` socket을 함께 받습니다.
Discovery가 해당 channel의 peer set을 관리합니다.

- Discovery 하나는 하나의 고정 `channel_name`(channel) view를 가집니다.
- 같은 `channel_name`에는 자동 attach와 수동 attach를 합쳐서 `DEALER`
  하나만 등록할 수 있습니다. 중복은 `EBUSY`로 실패합니다.
- 같은 Discovery handle을 둘 이상의 owner에 attach할 수 없습니다.
- attach된 `DEALER`는 `SpotNode` 전용 자원으로 취급합니다. 소유권은 호출자가
  유지하지만, 다른 곳에서 같은 socket을 일반 용도로 함께 써서는 안 됩니다.
- Discovery 없이 수동으로 channel dealer를 직접 등록하려면
  `zlink_spot_node_attach_channel_dealer_manual()`을 사용합니다.

### SPOT Node

SPOT Node는 같은 `channel_name`에 속한 다른 SPOT Node endpoint를 자동으로
발견하고, 자기 자신의 endpoint를 제외한 peer endpoint에 연결할 수 있습니다.

- 자동 발견 대상은 같은 `channel_name`의 SPOT Node endpoint입니다.
- 자기 자신의 advertise endpoint에는 자동 연결하지 않습니다.
- 수동 peer connect/disconnect와 Discovery 자동 연결은 섞지 않습니다.

### Raw socket family

raw socket family 자동 연결은 역할별 방향 규칙을 따릅니다. 이 규칙은 단순한
"서로 짝이 되는 역할"이 아니라, 어느 쪽이 outbound connect를 시작하는지를
정하는 규칙입니다.

- `ZLINK_AUTO_CONNECT_ROUTE_MESH`: ROUTER peer가 mesh를 이루며 각 pair에서 한쪽만 connect합니다.
- `ZLINK_AUTO_CONNECT_CLIENT_SERVER`: DEALER client가 같은 channel의 모든 eligible ROUTER endpoint에 connect합니다.
- `ZLINK_AUTO_CONNECT_DEALER_MESH`: DEALER peer가 mesh를 이루며 각 pair에서 한쪽만 connect합니다.
- `ZLINK_AUTO_CONNECT_FANOUT`: SUB가 PUB endpoint에 connect합니다.
- `ZLINK_AUTO_CONNECT_SPOT_MESH`: SpotNode peer가 mesh를 이루며 각 pair에서 한쪽만 connect합니다.

### Pairwise initiator 규칙

`ROUTE_MESH`, `DEALER_MESH`, `SPOT_MESH`에서는 한 번의 connect만으로도 양방향
메시지 경로가 만들어집니다. 양쪽이 동시에 dial하면 중복 연결 경쟁과 handover
churn이 생기므로, 라이브러리는 쌍마다 한쪽만 dial하도록 내부에서 결정합니다.

- 비교 key는 `routing_id`(우선)와 advertise endpoint 문자열(타이브레이크)
  입니다. 두 peer가 같은 입력으로 같은 total order를 계산하므로 쌍마다
  initiator가 정확히 하나만 정해집니다.
- 사용자 입장에서는 "누가 누구에게 connect할지"를 따로 설정할 필요가 없으며,
  결과적으로 한쪽만 dial하는 것으로 보입니다.
- 이 규칙은 Discovery-managed 자동 연결에만 적용됩니다. raw API로 직접
  `zlink_connect()`를 호출하는 수동 연결은 라이브러리가 중재하지 않으며,
  연결 방향 책임은 호출자에게 남습니다.

### 자동 연결 peer 항목과 가중치

Discovery가 노출하는 peer 항목은 peer 가중치를 함께 가집니다.
`zlink_member_peer_entry_t.weight`는 각 peer의 현재 `0..100` 값을 나타냅니다.
DEALER attachment는 가중치가 `0`인 peer를 후보에서 제외하고,
모두 `0`이면 submit이 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패합니다.
로컬 advertised weight를 바꾸는 public handle은 raw ROUTER와 DEALER 소켓입니다.

DEALER 대상 정책을 런타임에 바꾸는 setter는 없습니다.
DEALER-to-ROUTER channel은 Discovery 생성 시 `ZLINK_AUTO_CONNECT_CLIENT_SERVER`를
선택하고, DEALER-to-DEALER mesh channel은 `ZLINK_AUTO_CONNECT_DEALER_MESH`를
선택합니다.

## 스레드 안전성 요약

하나의 Discovery handle은 여러 스레드에서 동시에 사용할 수 있습니다.
다만 모든 호출이 같은 시점 제약을 갖는 것은 아닙니다.

- `zlink_discovery_connect_registry()`, `zlink_discovery_resolve_spot()`, monitor,
  조회성 API는 실행 중에도 호출할 수
  있습니다.
- `zlink_set_routing_id()`는 첫 subscribe/query/connect 전에만 의미가 있는 초기
  설정용 API입니다.
- `zlink_discovery_destroy()`는 종료 진입을 엄격하게 막는 규칙을 사용합니다.
  다른 스레드가 같은 handle에서 callback이나 허용된 API를 실행 중이면 `EBUSY`
  로 실패하고, destroy가 받아들여진 뒤에는 새 API 진입이 `ESHUTDOWN`으로
  실패합니다.

## API 표면

- Discovery 자체의 이름은 `zlink_set_routing_id(discovery, data, size)` /
  `zlink_get_routing_id(discovery, &out)`로 다룹니다.
- Discovery registry 링크의 TLS 설정은
  `zlink_set_tls_client(discovery, ca_cert, hostname, trust_system)`로
  구성합니다.
- `zlink_discovery_connect_registry()` 하나만 Registry에 처음 붙는 연결로
  사용하고, 브로드캐스트/uplink 경로는 내부에서 자동 구성합니다.
- 논리 `spot_rid`에서 현재 목적지 `node_rid`를 얻으려면
  `zlink_discovery_resolve_spot()`를 사용합니다.
  이 조회에 필요한 SPOT owner row는 publish하는 Discovery에서
  `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`를 켰을 때만 Registry에 저장됩니다.
- `ZLINK_DISCOVERY_SERVICE_UP`,
  `ZLINK_DISCOVERY_PROVIDERS_CHANGED` 같은 상태 전이는
  `zlink_discovery_member_peers()`로 현재 peer view를 읽습니다. 서비스 수준
  상태 변화를 추적하려면 이 조회 결과를 주기적으로 비교합니다.
- Actor active route 조회에는 `zlink_discovery_resolve_actor()`를 사용합니다.
  이 조회는 `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`가 켜진 Actor owner
  `SpotNode`에서 bind가 성공한 뒤에만 성공할 수 있습니다.
- framework adapter 처럼 route key/value 를 직접 관리해야 하는 계층은
  `zlink_discovery_bind_route()`, `zlink_discovery_unbind_route()`,
  `zlink_discovery_resolve_route()`를 사용합니다. 이 API 는 Registry row 를
  임의 key-value 저장소처럼 노출하는 것이 아니라 현재 Discovery service
  registration 이 소유한 owner-bound route 를 다룹니다.
- 전역 요약 상태는 registry topology snapshot/query API로 조회합니다.
- Discovery는 `zlink_set_option(discovery, ZLINK_OPT_*, ...)`을 지원하며,
  내부 관리 소켓 세트 전체에 같은 값이 퍼져 적용됩니다.
  Discovery 전용 옵션인 `ZLINK_OPT_ROUTE_VALUE_MAX_SIZE`,
  `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`,
  `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`는 fan-out이 아니라 Discovery handle
  자체의 값을 읽고 씁니다. `zlink_get_option()`은 이 Discovery 전용
  옵션에만 제공됩니다.

## 상수

### 자동 연결 타입

```c
typedef enum zlink_auto_connect_type_t
{
    ZLINK_AUTO_CONNECT_INVALID = 0,
    ZLINK_AUTO_CONNECT_ROUTE_MESH = 1,
    ZLINK_AUTO_CONNECT_CLIENT_SERVER = 2,
    ZLINK_AUTO_CONNECT_DEALER_MESH = 3,
    ZLINK_AUTO_CONNECT_FANOUT = 4,
    ZLINK_AUTO_CONNECT_SPOT_MESH = 5
} zlink_auto_connect_type_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_AUTO_CONNECT_ROUTE_MESH` | ROUTER mesh channel |
| `ZLINK_AUTO_CONNECT_CLIENT_SERVER` | DEALER client와 ROUTER server channel |
| `ZLINK_AUTO_CONNECT_DEALER_MESH` | DEALER mesh channel |
| `ZLINK_AUTO_CONNECT_FANOUT` | PUB/SUB fanout channel |
| `ZLINK_AUTO_CONNECT_SPOT_MESH` | SpotNode mesh channel |

### 서비스 역할

```c
typedef enum zlink_service_role_t
{
    ZLINK_SERVICE_ROLE_INVALID = 0,
    ZLINK_SERVICE_ROLE_SPOT    = 2,
    ZLINK_SERVICE_ROLE_ROUTER  = 3,
    ZLINK_SERVICE_ROLE_DEALER  = 4,
    ZLINK_SERVICE_ROLE_PUB     = 5,
    ZLINK_SERVICE_ROLE_SUB     = 6
} zlink_service_role_t;
```

| 상수 | 설명 |
|------|------|
| `ZLINK_SERVICE_ROLE_SPOT` | SPOT 역할 |
| `ZLINK_SERVICE_ROLE_ROUTER` | 소켓 패밀리: ROUTER 소켓 |
| `ZLINK_SERVICE_ROLE_DEALER` | 소켓 패밀리: DEALER 소켓 |
| `ZLINK_SERVICE_ROLE_PUB` | 소켓 패밀리: PUB 소켓 |
| `ZLINK_SERVICE_ROLE_SUB` | 소켓 패밀리: SUB 소켓 |

SPOT은 고정 역할을 가집니다. 소켓 패밀리 서비스는 attach된 소켓 타입에서 role을
얻습니다. 자동 연결은 Discovery 생성 시 정한 `zlink_auto_connect_type_t` 계약을
따릅니다.

### route kind

```c
typedef uint32_t zlink_route_kind_t;

#define ZLINK_ROUTE_KIND_INVALID       0u
#define ZLINK_ROUTE_KIND_ACTOR         1u
#define ZLINK_ROUTE_KIND_SPOT_NAME     2u
#define ZLINK_ROUTE_KIND_ACTOR_SESSION 3u
```

| 상수 | 설명 |
|------|------|
| `ZLINK_ROUTE_KIND_ACTOR` | actor active route sync 가 사용하는 actor route |
| `ZLINK_ROUTE_KIND_SPOT_NAME` | framework adapter 가 Spot name 을 Spot RID 로 찾기 위한 route |
| `ZLINK_ROUTE_KIND_ACTOR_SESSION` | framework adapter 가 actor-session binding 을 찾기 위한 route |

`ZLINK_ROUTE_KIND_INVALID`는 bind, unbind, resolve 에 사용할 수 없습니다.

## 함수

### zlink_discovery_new

고정 channel view로 Discovery 인스턴스를 생성합니다.

```c
void *zlink_discovery_new (void *ctx,
                           zlink_auto_connect_type_t auto_connect_type,
                           const char *channel_name);
```

지정된 자동 연결 타입과 논리 channel 이름으로 범위가 지정된 새 Discovery
인스턴스를 할당하고 초기화합니다. 두 값 모두 생성 시 고정되며 변경할 수
없습니다. 모든 조회는 해당 논리 channel view 안에서
동작합니다.

**매개변수:**
- `ctx` -- Context 핸들.
- `auto_connect_type` -- 이 핸들의 자동 연결 topology 계약.
- `channel_name` -- 이 핸들의 고정 논리 channel 이름.

**반환값:** 성공 시 Discovery 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Registry bootstrap/control 엔드포인트에 연결합니다.

```c
zlink_connect_result_t zlink_discovery_connect_registry (void *discovery,
                                                         const char *registry_endpoint);
```

이 Discovery 인스턴스를 Registry control plane에 bootstrap(초기 연결 설정) 연결합니다.
Registry 응답에서 내부 broadcast/uplink 엔드포인트를 학습하고, Discovery가
그 소켓들을 자동으로 구성한 뒤 주기적인 서비스 목록 브로드캐스트를 수신합니다.

**반환값:** `zlink_connect_result_t` 값을 반환합니다. 상세 errno 는 진단용으로
`zlink_errno()`에서 계속 조회할 수 있습니다.

**스레드 안전성:** 하나의 Discovery handle은 여러 스레드에서 동시에 사용할 수
있습니다. 이 호출도 일반 수명 주기 제약을 지키는 한 다른 Discovery 작업과
병행할 수 있습니다.

**참고:** `zlink_discovery_destroy`

---

### zlink_discovery_resolve_spot

논리 `spot_rid`가 현재 어느 `SpotNode`에 연결되어 있는지 조회합니다.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

이 함수는 현재 Discovery가 보고 있는 `channel_name` 범위 안에서 `spot_rid`를
받아, 지금 그 이름을 맡고 있는 `SpotNode`의 `node_rid`를 구합니다. Discovery는
먼저 로컬 캐시를 확인할 수 있고, 필요하면 Registry 기준으로 다시 확인할 수
있습니다.
현재 core 구현은 캐시를 아무 때나 오래 믿지 않습니다. Discovery는 cached owner
row가 현재 channel view와 같은 갱신 순번에서 검증된 값인지 확인하고, 그렇지
않으면 아주 짧은 로컬 TTL 안에 있는 값만 잠시 재사용합니다. channel view가
바뀌었거나 TTL이 지난 값이면 Registry에 다시 질의해서 owner를 새로 확인합니다.

SPOT owner row publish는 기본값으로 꺼져 있습니다. SpotNode를 Discovery에
붙이는 것만으로는 Registry에 `spot_rid -> owner node` row가 저장되지 않습니다.
SPOT owner 조회를 Registry 기준으로 사용하려면 owner를 publish하는 Discovery
handle에서 `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`를 `int` 값 `1`로 설정해야
합니다. 이 옵션을 켜지 않은 Discovery가 publish한 SpotNode는
`zlink_discovery_resolve_spot()`의 Registry refresh 결과에 나타나지 않으며,
조회는 `ENOENT`로 실패할 수 있습니다.

성공하면 `owner_node_rid_out`에 현재 owner node의 routing id가 채워집니다.
호출자는 이 값을 원래 `spot_rid`와 함께 ROUTER 쪽 direct 함수
(`zlink_router_send_spot()` 또는 `zlink_router_request_spot()`)에 전달하면
됩니다.

이 함수는 send/request 시작 경로를 위한 조회 API입니다. reply 경로에는 쓰지
않습니다. reply는 request 수신 시 함께 전달된 source 주소를 그대로 사용해야
합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다. owner를 찾지 못했거나,
Discovery가 Registry 기준 정보에 접근할 수 없으면 실패할 수 있습니다. 상세
errno 는 `zlink_errno()`에서 조회할 수 있습니다.

**스레드 안전성:** 하나의 Discovery handle은 여러 스레드에서 동시에 사용할 수
있습니다. 일반 조회 API처럼 다른 Discovery 작업과 함께 호출할 수 있습니다.

**참고:** `zlink_router_send_spot`, `zlink_router_request_spot`

---

### zlink_discovery_bind_route

현재 Discovery service registration 이 소유하는 route row 를 Registry 에 등록합니다.

```c
zlink_config_result_t zlink_discovery_bind_route (
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size,
  const void *value,
  size_t value_size);
```

route 는 `kind + key` 로 식별되고, `value` 는 호출 계층이 해석하는 byte payload 입니다.
Registry 는 payload 를 해석하지 않습니다. owner 는 이 Discovery handle 에 attach 되어
Registry 에 등록된 service participant 입니다. owner registration 이 사라지거나 새
generation 으로 교체되면 그 owner 가 등록한 route row 도 정리됩니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK` 를 반환합니다. invalid kind, key 크기 초과,
Registry 연결 오류, live owner registration 부재는 실패할 수 있습니다.

---

### zlink_discovery_unbind_route

현재 Discovery service registration 이 소유한 route row 를 제거합니다.

```c
zlink_config_result_t zlink_discovery_unbind_route (
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size);
```

unbind 는 같은 owner generation 이 claim 한 row 에만 적용됩니다. 오래된 owner generation
또는 다른 owner 가 같은 `kind + key` 로 만든 route 를 제거하지 않습니다.

---

### zlink_discovery_resolve_route

Registry 가 materialize 한 현재 route winner 를 조회합니다.

```c
zlink_config_result_t zlink_discovery_resolve_route (
  void *discovery,
  zlink_route_kind_t kind,
  const void *key,
  size_t key_size,
  zlink_routing_id_t *owner_rid_out,
  zlink_msg_t *value_out);
```

성공하면 `owner_rid_out`에는 route owner 의 routing id 가, `value_out`에는 route value 가
채워집니다. 호출자는 `value_out`을 다 쓴 뒤 `zlink_msg_close()`로 닫아야 합니다.

route 를 찾지 못하면 실패하고 errno 로 원인을 조회할 수 있습니다. payload 형식은 core
계약이 아니므로, 상위 framework adapter 는 자신이 정한 versioned payload 를 decode 한 뒤
맞지 않는 row 를 not found 로 처리할 수 있습니다.

---

### 제거된 DEALER 대상 setter

`zlink_discovery_set_dealer_peer_mode()` API는 없습니다.
DEALER-to-ROUTER channel은 `ZLINK_AUTO_CONNECT_CLIENT_SERVER`, DEALER-to-DEALER
channel은 `ZLINK_AUTO_CONNECT_DEALER_MESH`로 Discovery를 생성합니다.

**참고:** `zlink_discovery_connect_registry`,
`zlink_socket_attach_discovery`

---

### zlink_set_tls_client

Discovery registry 연결에 TLS 설정을 구성합니다.

```c
zlink_config_result_t zlink_set_tls_client (void *discovery,
                                            const char *ca_cert,
                                            const char *hostname,
                                            int trust_system);
```

Discovery 서비스가 내부적으로 관리하는 registry bootstrap 및 uplink 연결에
TLS 클라이언트 설정을 적용합니다. `zlink_discovery_connect_registry()` 전에
호출해야 합니다.

**매개변수:**
- `ca_cert` -- PEM 인코딩된 CA 인증서 번들 경로.
- `hostname` -- TLS SNI 및 인증서 검증에 사용할 호스트명.
- `trust_system` -- 0이 아니면 시스템 CA 인증서 저장소를 신뢰합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**참고:** `zlink_discovery_connect_registry`

---

### zlink_set_routing_id

첫 subscribe/query/connect 전에 대표 routing id를 재정의합니다.

```c
zlink_config_result_t zlink_set_routing_id (void *discovery,
                                            const void *data,
                                            size_t size);
```

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**참고:** `zlink_get_routing_id`

---

### zlink_get_routing_id

이 Discovery의 대표 routing id를 반환합니다.

```c
zlink_config_result_t zlink_get_routing_id (void *discovery,
                                            zlink_routing_id_t *out);
```

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**참고:** `zlink_set_routing_id`

---

### zlink_socket_attach_discovery

raw ROUTER/DEALER/PUB/SUB 소켓을 discovery 서비스 뷰에 연결합니다.

```c
zlink_config_result_t zlink_socket_attach_discovery (void *socket, void *discovery);
```

소켓을 지정된 Discovery 인스턴스에 연결합니다. Discovery 자동 연결 타입은
`ZLINK_AUTO_CONNECT_CLIENT_SERVER`이어야 하며, 소켓 타입은 ROUTER, DEALER, PUB,
SUB 중 하나여야 합니다. 서비스 역할은 소켓 타입에서 자동으로 파생됩니다.

연결되면 소켓은 프로바이더 등록, 피어 갱신, 종료를 Discovery 인스턴스에
위임합니다. 연결된 소켓에서 `connect`, `disconnect`, `unbind`, `close`
수동 호출은 실패합니다. 연결된 소켓의 lifecycle을 종료하려면 Discovery
인스턴스를 파괴하십시오.

**매개변수:**
- `socket` -- 소켓 핸들 (ROUTER, DEALER, PUB, SUB 중 하나).
- `discovery` -- `ZLINK_AUTO_CONNECT_CLIENT_SERVER`으로 생성된 Discovery 핸들.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**오류:**
- `EINVAL` -- 잘못된 소켓 또는 discovery 핸들.
- `ENOTSUP` -- 지원하지 않는 소켓 타입 (ROUTER, DEALER, PUB, SUB만 가능).
- `EBUSY` -- 소켓이 이미 discovery 인스턴스에 연결되어 있거나, 기존
  connect 엔드포인트 또는 attached pipe가 있음.

**스레드 안전성:** 동일 핸들 호출 시 thread-safe.

**참고:** `zlink_discovery_new`, `zlink_discovery_destroy`

---

### zlink_discovery_set_value

이 Discovery 인스턴스의 숫자 라우팅 속성을 설정합니다.

```c
zlink_config_result_t zlink_discovery_set_value (void *discovery, int64_t value);
```

서비스 등록과 함께 게시되는 `value` 필드를 설정합니다. 원격 소비자는
`zlink_member_peer_entry_t.value`에서 이 값을 확인할 수 있습니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_discovery_get_value`

---

### zlink_discovery_get_value

현재 숫자 라우팅 속성을 가져옵니다.

```c
zlink_config_result_t zlink_discovery_get_value (void *discovery,
                                                 int64_t *value_out);
```

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**참고:** `zlink_discovery_set_value`

---

### zlink_discovery_resolve_actor

Actor active route를 조회합니다.

```c
zlink_config_result_t zlink_discovery_resolve_actor(void *discovery,
                                                    const char *actor_id,
                                                    zlink_actor_route_t *route_out);
```

`actor_id`는 비어 있지 않은 NUL 종료 문자열이어야 하며,
`ZLINK_ACTOR_ID_MAX - 1` byte를 넘을 수 없습니다. 성공하면 `route_out->actor`에
현재 active Actor ref가 기록됩니다. Actor가 현재 Spot에 join되어 있으면
`route_out->joined != 0`이고 `route_out->joined_spot_rid`가 유효합니다.

Actor active route는 Actor 생성이나 join만으로 만들어지지 않습니다.
Actor owner `SpotNode`에 연결된 Discovery에서
`ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`가 켜져 있고, `zlink_stream_bind_actor()`가
성공했을 때 publish됩니다.

**오류:**
- `EINVAL` -- handle, actor id, 출력 포인터가 올바르지 않습니다.
- `ENOENT` -- active route가 없습니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 같은 handle에 대한 호출은 thread-safe입니다.

---

### zlink_discovery_destroy

Discovery 인스턴스를 파괴하고 모든 리소스를 해제합니다.

```c
zlink_close_result_t zlink_discovery_destroy (void **discovery_p);
```

내부 SUB 소켓을 닫고, 모든 캐시된 데이터를 해제하며, Discovery 인스턴스를
해제합니다. Discovery를 파괴하면 이 서비스 뷰에 lifecycle 소유권을 위임한
모든 참여자(SPOT Node, 소켓)도 함께 종료됩니다. 파괴 후
`*discovery_p`의 포인터는 `NULL`로 설정됩니다.

**반환값:** `zlink_close_result_t` 값을 반환합니다.

**스레드 안전성:** Discovery destroy는 lifecycle gate를 사용합니다. 같은
handle에서 다른 스레드가 콜백 또는 admitted API를 실행 중이면 `errno=EBUSY`로
실패합니다. destroy가 accepted된 뒤 새 API 진입은 `errno=ESHUTDOWN`입니다.
destroy가 성공한 경우에만 `*discovery_p`가 `NULL`로 정리됩니다.

**참고:** `zlink_discovery_new`
