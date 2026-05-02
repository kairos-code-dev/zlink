# Discovery 자동 연결 타입 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 Discovery 기반 자동 연결 모델을 명확하게 나누기 위한 설계안이다.
정식 spec 문서와 공개 헤더에 반영되기 전까지 응용은 이 동작에 의존하면 안 된다.

## 목적

현재 Discovery 자동 연결은 같은 `service_name` 안에서 참여자의 role을 보고
연결 대상을 고른다. 이 방식은 동작 자체는 가능하지만, 하나의 `SOCKET` 서비스
안에서 ROUTER mesh, DEALER client/server, DEALER mesh, PUB/SUB fanout 의미가
함께 표현된다. 그래서 사용자는 "이 Discovery channel이 어떤 토폴로지를
뜻하는가"를 별도 규칙으로 기억해야 한다.

이 초안은 자동 연결 타입을 Discovery channel의 명시적인 계약으로 올린다.
`ROUTER`, `DEALER`, `PUB`, `SUB`, `SPOT`은 계속 참여자의 role로 남기고,
Discovery channel은 별도의 자동 연결 타입을 가진다.

## Discovery channel의 범위

이 문서의 channel은 zlink 전체에 적용되는 공통 socket 추상화가 아니다.
여기서 channel은 **Discovery channel**을 뜻한다. Discovery channel은
Discovery와 Registry가 같은 이름의 참여자를 묶고, 그 묶음 안에서 자동 연결
타입과 허용 role을 검증하는 논리 범위다.

수동 연결에는 Discovery channel 계약이 없다. 사용자는 endpoint를 직접 알고
`bind`와 `connect`를 호출하며, 라이브러리는 그 연결을 어떤 channel에 속한다고
해석하지 않는다. 응용이 수동 연결을 같은 이름이나 같은 목적에 맞춰 구성할 수는
있지만, 그것은 응용의 구성 규칙이지 zlink가 검증하는 Discovery channel 계약은
아니다.

따라서 이 초안의 개념은 아래처럼 나뉜다.

| 연결 방식 | 이름 범위 | 타입 검증 | 연결 대상 선택 |
|-----------|-----------|-----------|----------------|
| 수동 연결 | 없음 | 없음 | 호출자가 endpoint를 직접 선택 |
| Discovery 자동 연결 | Discovery channel name | Registry와 Discovery가 검증 | 자동 연결 타입이 선택 |

Discovery channel은 자동 연결에만 필요한 개념이다. 자동 연결은 이름으로 peer
집합을 찾고, 그 집합 안에서 연결 방향을 정해야 하므로 이름 붙은 범위가 필요하다.
이 범위를 Discovery channel이라고 부른다.

## 핵심 원칙

1. 같은 Discovery channel name은 하나의 자동 연결 타입만 가진다.
2. 자동 연결 타입은 Discovery channel의 불변 계약이다.
3. 참여자 role은 자동 연결 타입이 허용하는 role이어야 한다.
4. 타입이 다른 참여자는 등록하지 않고 실패한다.
5. 자동 연결 방향은 자동 연결 타입에서 정한다.
6. `DEALER`, `PUB` 같은 socket role은 기존 의미를 유지한다.

## 용어

- **Discovery channel name**: 같은 자동 연결 범위를 공유하는 논리 이름이다.
- **자동 연결 타입**: Discovery channel 안에서 어떤 토폴로지를 만들지 정하는 값이다.
- **참여자 role**: Discovery channel에 들어오는 socket 또는 서비스 노드의
  역할이다.
- **Discovery channel 계약**: channel name, 자동 연결 타입, 허용 role, 연결
  방향을 묶은 불변 계약이다.

## Public API 변경 요약

이 초안은 호환성을 유지하지 않는 변경을 전제로 한다. 따라서 기존
`auto_connect_type` 중심 Discovery 생성 모델과 `DEALER` peer mode 옵션은 새 자동
연결 타입 모델로 대체한다.

| 구분 | 변경 |
|------|------|
| 추가 enum | `zlink_auto_connect_type_t` |
| 변경 API | `zlink_discovery_new()`, `zlink_registry_member_peers()`, `zlink_registry_member_peer_metadata()`, `zlink_spot_node_attach_channel_dealer()` (시그니처 유지, 검증 기준 변경) |
| 유지 API | `zlink_socket_attach_discovery()`, `zlink_spot_node_attach_discovery()`, `zlink_spot_node_attach_channel_dealer_manual()`, `zlink_spot_node_attach_pub_ingress()`, `zlink_discovery_member_peers()`, `zlink_discovery_member_peer_metadata()` |
| 제거 API | `Discovery auto-connect type constructor contract` |
| 제거 enum/인자 | `zlink_discovery_dealer_peer_mode_t`, Discovery/Registry API의 `zlink_auto_connect_type_t` 인자 |
| 추가 option | 없음 |
| 제거 option | `DEALER` peer mode |
| 구조체 변경 | service/member/topology 구조체에서 `service_name` 의미를 `channel_name`으로 정리하고 `auto_connect_type` 추가 |

### 추가되는 enum

새 공개 enum은 Discovery channel의 자동 연결 타입을 표현한다.

```c
typedef enum zlink_auto_connect_type_t {
  ZLINK_AUTO_CONNECT_INVALID = 0,
  ZLINK_AUTO_CONNECT_ROUTE_MESH = 1,
  ZLINK_AUTO_CONNECT_CLIENT_SERVER = 2,
  ZLINK_AUTO_CONNECT_DEALER_MESH = 3,
  ZLINK_AUTO_CONNECT_FANOUT = 4,
  ZLINK_AUTO_CONNECT_SPOT_MESH = 5
} zlink_auto_connect_type_t;
```

이 enum은 Discovery 생성, Registry channel 계약, snapshot, topology summary에서
같은 의미로 사용한다.

### 변경되는 Discovery 생성 API

기존 생성 API는 아래 형태였다.

```c
void *zlink_discovery_new(
  void *ctx,
  zlink_auto_connect_type_t auto_connect_type,
  const char *service_name);
```

새 생성 API는 아래 형태로 바뀐다.

```c
void *zlink_discovery_new(
  void *ctx,
  zlink_auto_connect_type_t auto_connect_type,
  const char *channel_name);
```

변경 의미는 다음과 같다.

- `auto_connect_type` 인자는 제거한다.
- `service_name`은 Discovery 자동 연결 범위라는 의미를 분명히 하기 위해
  `channel_name`으로 부른다.
- `auto_connect_type`은 channel 생성 시점에 반드시 정한다.
- 생성 뒤 `auto_connect_type`을 바꾸는 public API는 두지 않는다.
- `auto_connect_type == ZLINK_AUTO_CONNECT_INVALID`이면 실패한다.

이 draft 범위의 public API에서는 `zlink_auto_connect_type_t`를 더 이상 사용하지 않는다.
`ZLINK_AUTO_CONNECT_CLIENT_SERVER`, `ZLINK_AUTO_CONNECT_SPOT_MESH`,
`ZLINK_CHANNEL_TYPE_SOCKET`, `ZLINK_CHANNEL_TYPE_SPOT`은 자동 연결 의미를 고르는
값으로 쓰지 않는다. 다른 정식 spec에서 독립적으로 쓰는 계약이 없다면 공개
헤더에서 제거한다.

### 변경되는 Registry 조회 API

Registry 조회 API도 `auto_connect_type + service_name` 조합 대신 `channel_name`과
자동 연결 타입을 사용한다.

기존 API는 아래 형태였다.

```c
zlink_config_result_t zlink_registry_member_peers(
  void *registry,
  zlink_auto_connect_type_t auto_connect_type,
  const char *service_name,
  zlink_member_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_registry_member_peer_metadata(
  void *registry,
  zlink_auto_connect_type_t auto_connect_type,
  const char *service_name,
  uint16_t service_role,
  const char *endpoint,
  zlink_msg_t *metadata_out);
```

새 API는 아래 형태로 바뀐다.

```c
zlink_config_result_t zlink_registry_member_peers(
  void *registry,
  const char *channel_name,
  zlink_member_peer_entry_t *entries,
  size_t *count);

zlink_config_result_t zlink_registry_member_peer_metadata(
  void *registry,
  const char *channel_name,
  zlink_service_role_t service_role,
  const char *endpoint,
  zlink_msg_t *metadata_out);
```

`zlink_registry_member_peer_metadata()`의 `service_role` 인자는 기존 `uint16_t`에서
공개 enum 타입인 `zlink_service_role_t`로 바꾼다. role 값은 공개 enum으로
표현되므로 호출자와 바인딩이 임의 정수 폭을 해석하지 않게 하기 위한 변경이다.

Discovery handle은 이미 하나의 channel에 묶여 있으므로
`zlink_discovery_member_peers()` 시그니처는 유지한다.

### 유지되는 attach API

attach API의 함수 이름과 기본 형태는 유지한다.

```c
zlink_config_result_t zlink_socket_attach_discovery(
  void *socket,
  void *discovery);

zlink_config_result_t zlink_spot_node_attach_discovery(
  void *node,
  void *discovery);

zlink_config_result_t zlink_spot_node_attach_channel_dealer(
  void *node,
  void *discovery,
  void *dealer);
```

대신 검증 기준이 바뀐다.

- `zlink_socket_attach_discovery()`는 Discovery의 `auto_connect_type`이
  raw socket role을 허용해야 성공한다.
- `zlink_spot_node_attach_discovery()`는 Discovery의 `auto_connect_type`이
  `ZLINK_AUTO_CONNECT_SPOT_MESH`일 때만 성공한다.
- `zlink_spot_node_attach_channel_dealer()`는 Discovery의 `auto_connect_type`이
  `DEALER` role을 허용해야 성공한다. 즉 `CLIENT_SERVER` 또는 `DEALER_MESH`
  Discovery만 받을 수 있다.
- attach API는 자동 연결 타입을 인자로 받지 않는다. 자동 연결 타입은 Discovery
  handle의 계약이기 때문이다.

`zlink_spot_node_attach_channel_dealer_manual()`은 Discovery를 받지 않으므로
Registry channel 계약을 검증하지 않는다. 이 API는 호출자가 직접 관리하는
manual channel 이름을 사용한다. `zlink_spot_node_attach_pub_ingress()`도 Discovery
자동 연결 타입의 영향을 받지 않는다.

### Discovery 조회 API 검증

Discovery handle을 받는 조회 API는 handle의 자동 연결 타입을 기준으로 의미를
검증한다.

```c
zlink_config_result_t zlink_discovery_resolve_spot(
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

`zlink_discovery_resolve_spot()`은 `SPOT_MESH` Discovery에서만 의미가 있다.
다른 자동 연결 타입의 Discovery handle에 호출하면 `ENOTSUP`으로 실패한다.

`zlink_discovery_member_peers()`와 `zlink_discovery_member_peer_metadata()`는
모든 자동 연결 타입에서 사용할 수 있다. 반환 row에는 해당 Discovery handle의
`auto_connect_type`이 들어간다.

### 제거되는 API와 enum

아래 API와 enum은 새 모델에서는 제거한다.

```c
typedef enum zlink_discovery_dealer_peer_mode_t {
  ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER = 1,
  ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER = 2
} zlink_discovery_dealer_peer_mode_t;

zlink_config_result_t Discovery auto-connect type constructor contract(
  void *discovery,
  zlink_discovery_dealer_peer_mode_t mode);
```

대체 규칙은 다음과 같다.

| 기존 의미 | 새 자동 연결 타입 |
|-----------|-------------------|
| `DEALER -> ROUTER` | `ZLINK_AUTO_CONNECT_CLIENT_SERVER` |
| `DEALER -> DEALER` | `ZLINK_AUTO_CONNECT_DEALER_MESH` |

새 모델에서는 `DEALER` 자동 연결 대상이 mutable option이 아니다. Discovery
channel을 만들 때 `CLIENT_SERVER` 또는 `DEALER_MESH` 중 하나를 선택한다.

### service/member/topology 구조체 변경

Discovery member snapshot, Registry service summary, Registry topology summary는
자동 연결 타입을 직접 확인할 수 있어야 한다.

구조체는 아래 형태로 바꾼다. 기존 `service_name` 필드는 `channel_name`으로
이름을 바꾼다.

```c
typedef struct zlink_member_peer_entry_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_service_role_t service_role;
  char channel_name[256];
  char endpoint[256];
  uint32_t weight;
  zlink_routing_id_t routing_id;
  int64_t value;
} zlink_member_peer_entry_t;

typedef struct zlink_registry_service_summary_filter_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_service_role_t service_role;
  char channel_name[256];
} zlink_registry_service_summary_filter_t;

typedef struct zlink_registry_service_summary_entry_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_service_role_t service_role;
  char channel_name[256];
  uint32_t total_count;
  uint32_t connecting_count;
  uint32_t ready_count;
  uint32_t error_count;
  uint32_t stopped_count;
  uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;

typedef struct zlink_registry_topology_entry_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_routing_id_t routing_id;
  zlink_service_kind_t service_kind;
  zlink_service_role_t service_role;
  char channel_name[256];
  char endpoint[256];
  zlink_topology_source_t source;
  zlink_topology_state_t state;
  uint32_t desired_count;
  uint32_t ready_count;
  uint32_t error_code;
  uint64_t last_reported_ms;
} zlink_registry_topology_entry_t;
```

`zlink_registry_topology_filter_t`도 아래 형태로 바꾼다.

```c
typedef struct zlink_registry_topology_filter_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_service_kind_t service_kind;
  zlink_service_role_t service_role;
  char channel_name[256];
  zlink_routing_id_t routing_id;
  zlink_topology_state_t state;
  zlink_topology_source_t source;
} zlink_registry_topology_filter_t;
```

위 두 필터 구조체에 공통으로 적용되는 영값 규칙은 다음과 같다.
`auto_connect_type == ZLINK_AUTO_CONNECT_INVALID`이면 모든 자동 연결 타입을 대상으로
조회한다. `service_role == ZLINK_SERVICE_ROLE_INVALID`이면 모든 role을 대상으로
조회한다. `channel_name[0] == '\0'`이면 모든 Discovery channel을 대상으로 조회한다.

### public option 변경

새 public option은 추가하지 않는다.

- 자동 연결 타입은 Discovery 생성 인자이며, runtime option이 아니다.
- `DEALER` peer mode option은 제거한다.
- socket role을 강제로 지정하는 option은 추가하지 않는다. role은 socket 타입에서
  파생한다.
- SpotNode role을 지정하는 option은 추가하지 않는다. SpotNode는 항상 `SPOT`
  role로 참여한다.

## enum 이름 선택 근거

공개 API에서는 축약형보다 의미가 바로 보이는 이름을 사용한다. enum 값 자체는
위의 "추가되는 enum" 절에 정의한다.

`RouteType`, `CSType`, `FanoutType`, `SpotType` 같은 짧은 이름은 내부 설계
메모에서는 쓸 수 있지만, 공개 API에서는 아래 이유로 피한다.

- `RouteType`은 routing 기능 전체와 ROUTER mesh를 혼동할 수 있다.
- `CSType`는 client/server 의미를 처음 읽는 사람이 바로 알기 어렵다.
- `SpotType`은 SPOT 자체의 데이터 모델과 SPOT 자동 연결 타입을 혼동할 수 있다.

## Discovery 생성 계약

새 계약에서는 Discovery가 생성 시점부터 자동 연결 타입을 가진다.

```c
void *zlink_discovery_new(
  void *ctx,
  zlink_auto_connect_type_t auto_connect_type,
  const char *channel_name);
```

- `ctx == NULL`이면 실패한다.
- `channel_name == NULL` 또는 빈 문자열이면 실패한다.
- `auto_connect_type == ZLINK_AUTO_CONNECT_INVALID`이면 실패한다.
- Discovery handle 하나는 하나의 `channel_name`과 하나의
  `auto_connect_type`만 가진다.
- 생성 후 `auto_connect_type`을 바꾸는 API는 두지 않는다.

기존 `auto_connect_type` 기반 생성자는 새 모델에서 사용하지 않는다.

## 자동 연결 타입 목록

| 자동 연결 타입 | 의미 | 허용 role | 연결 방향 |
|----------------|------|-----------|-----------|
| `ROUTE_MESH` | ROUTER 간 route mesh | `ROUTER` | 쌍마다 한쪽 `ROUTER`가 다른 `ROUTER`에 connect |
| `CLIENT_SERVER` | DEALER client와 ROUTER server | `DEALER`, `ROUTER` | `DEALER`가 `ROUTER`에 connect |
| `DEALER_MESH` | DEALER 간 peer mesh | `DEALER` | `DEALER`가 다른 `DEALER`에 connect |
| `FANOUT` | PUB/SUB fanout | `PUB`, `SUB` | `SUB`가 `PUB`에 connect |
| `SPOT_MESH` | SpotNode 간 mesh | `SPOT` | SpotNode가 다른 SpotNode에 connect |

## Role 파생

raw socket을 Discovery에 attach하면 socket 타입에서 참여자 role을 파생한다.

| socket 타입 | 참여자 role |
|-------------|-------------|
| `ZLINK_SOCKET_ROUTER` | `ROUTER` |
| `ZLINK_SOCKET_DEALER` | `DEALER` |
| `ZLINK_SOCKET_PUB` | `PUB` |
| `ZLINK_SOCKET_SUB` | `SUB` |

SpotNode를 Discovery에 attach하면 참여자 role은 `SPOT`이다.

그 밖의 socket 타입은 이 초안의 자동 연결 채널에 참여할 수 없다.

## Endpoint와 참여 상태

Discovery attach와 Registry provider registration은 같은 의미가 아니다.

- **attached participant**는 Discovery handle에 붙은 local socket 또는 SpotNode다.
- **advertised provider**는 public endpoint를 bind했고 Registry에 endpoint를
  등록한 participant다.

Endpoint가 없는 participant는 Registry member snapshot에 나타나지 않는다.
그래도 자동 연결 consumer로 동작할 수 있다. 예를 들어 `CLIENT_SERVER`의 DEALER나
`FANOUT`의 SUB는 자기 endpoint를 advertise하지 않아도 Registry에서 ROUTER 또는
PUB provider 목록을 받아 outbound connect를 시작할 수 있다.

반대로 다른 participant의 connect 대상이 되려면 endpoint를 advertise해야 한다.
`ROUTE_MESH`, `DEALER_MESH`, `SPOT_MESH`의 완전한 mesh 참여자는 public endpoint가
필요하다.

## Pairwise initiator 규칙

`ROUTE_MESH`, `DEALER_MESH`, `SPOT_MESH`는 peer mesh 타입이다. 이 세 타입은
같은 두 advertised provider 사이에서 한쪽만 outbound connect를 시작한다. 양쪽이
동시에 connect하면 같은 peer pair에 중복 pipe가 생길 수 있으므로, Discovery
자동 연결은 pairwise single initiator 규칙을 사용한다.

initiator는 아래 connect key로 정한다.

1. 두 provider 모두 routing id가 있으면 routing id 바이트를 비교한다.
2. routing id 비교 결과가 같거나, 둘 중 하나라도 routing id가 없으면 endpoint
   문자열을 비교한다.
3. connect key가 더 작은 provider가 더 큰 provider의 endpoint로 outbound connect를
   시작한다.
4. routing id와 endpoint가 모두 같으면 같은 provider로 간주하고 connect하지 않는다.

routing id는 정상 provider에서는 고유해야 한다. 그래도 구현은 잘못된 설정,
이전 버전 peer, 테스트 fixture, Registry projection 지연 때문에 routing id가
없거나 같은 경우를 방어해야 한다. endpoint 타이브레이크는 그런 경우에도 모든
참여자가 같은 initiator를 계산하게 하기 위한 규칙이다.

## 타입별 세부 계약

### ROUTE_MESH

`ROUTE_MESH`는 ROUTER socket끼리 route mesh를 만든다.

- `ROUTER` role만 attach할 수 있다.
- 각 ROUTER는 자신의 endpoint를 advertise해야 완전한 참여자가 된다.
- 자기 자신의 endpoint에는 connect하지 않는다.
- 같은 두 ROUTER 사이에서는 pairwise initiator 규칙에 따라 한쪽만 outbound
  connect를 시작한다.

이 타입에 `DEALER`, `PUB`, `SUB`, `SPOT`을 attach하면 실패한다.

### CLIENT_SERVER

`CLIENT_SERVER`는 DEALER client가 ROUTER server로 접속하는 채널이다.

- `ROUTER`와 `DEALER` role만 attach할 수 있다.
- `ROUTER`는 server participant다.
- `DEALER`는 client participant다.
- `DEALER`는 같은 채널 안의 eligible `ROUTER` endpoint에 connect한다.
- `ROUTER`는 discovery 때문에 `DEALER` endpoint로 outbound connect하지 않는다.
- 여러 ROUTER가 있으면 DEALER는 eligible ROUTER endpoint 전부에 connect한다.
- ROUTER endpoint가 사라지면 DEALER는 해당 discovery-managed endpoint를
  disconnect한다.

DEALER가 반드시 public endpoint를 advertise해야 하는 것은 아니다. client-only
DEALER는 outbound connect만 수행할 수 있다. 다만 DEALER가 endpoint를 advertise한
경우에도 `CLIENT_SERVER` 채널에서는 ROUTER의 자동 connect 대상이 되지 않는다.

이 타입에 `PUB`, `SUB`, `SPOT`을 attach하면 실패한다.

### DEALER_MESH

`DEALER_MESH`는 DEALER socket끼리 peer mesh를 만든다.

- `DEALER` role만 attach할 수 있다.
- 각 DEALER는 pairwise initiator 규칙에 따라 일부 DEALER endpoint를 자동 연결
  대상으로 본다.
- 자기 자신의 endpoint에는 connect하지 않는다.
- endpoint를 advertise하지 않는 DEALER는 다른 참여자의 connect 대상이 될 수 없다.
- 완전한 mesh 참여자가 되려면 DEALER가 public endpoint를 advertise해야 한다.

이 타입에 `ROUTER`, `PUB`, `SUB`, `SPOT`을 attach하면 실패한다.

### FANOUT

`FANOUT`은 PUB/SUB fanout 채널이다.

- `PUB`와 `SUB` role만 attach할 수 있다.
- `SUB`는 같은 채널 안의 `PUB` endpoint에 connect한다.
- `PUB`는 discovery 때문에 `SUB` endpoint로 outbound connect하지 않는다.
- PUB endpoint가 사라지면 SUB는 해당 discovery-managed endpoint를 disconnect한다.
- SUB endpoint advertise는 필수가 아니다.

이 타입에 `ROUTER`, `DEALER`, `SPOT`을 attach하면 실패한다.

### SPOT_MESH

`SPOT_MESH`는 SpotNode끼리 mesh를 만든다.

- `SPOT` role만 attach할 수 있다.
- SpotNode는 pairwise initiator 규칙에 따라 같은 채널 안의 일부 SpotNode
  endpoint에 connect한다.
- 자기 자신의 endpoint에는 connect하지 않는다.
- 완전한 mesh 참여자가 되려면 SpotNode가 public endpoint를 advertise해야 한다.

이 타입에 raw socket을 attach하면 실패한다.

## 같은 채널에서 타입이 다른 경우

같은 `channel_name`에 서로 다른 `auto_connect_type`이 들어오면 나중에 관측한
쪽을 실패시킨다. 여기서 "나중"은 로컬 프로세스의 시간 순서가 아니라 Registry가
채널 계약을 원자적으로 확정한 뒤 그 계약과 다른 선언을 관측한 순서를 뜻한다.

### 규칙

- Registry는 `channel_name`마다 하나의 채널 계약을 가진다.
- Registry에 해당 채널 계약이 없으면 첫 번째 유효 선언이 계약을 만든다.
- 이미 계약이 있으면 새 선언의 `auto_connect_type`이 기존 계약과 같아야 한다.
- 타입이 다르면 새 선언은 실패한다.
- 실패한 참여자는 Registry service list, member list, topology summary에
  등록되지 않는다.
- 실패한 참여자는 discovery-managed connect를 시작하지 않는다.

## Registry 등록 시점

Discovery handle 생성은 로컬 객체 생성만 수행한다. Registry channel 계약은
`zlink_discovery_connect_registry()`에서 선언하고 검증한다.

`zlink_discovery_connect_registry()`는 Registry bootstrap 요청에 아래 값을 포함한다.

```c
typedef struct zlink_discovery_bootstrap_request_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_routing_id_t discovery_routing_id;
  char channel_name[256];
} zlink_discovery_bootstrap_request_t;
```

Registry는 이 요청을 받으면 아래 순서로 처리한다.

1. `channel_name`이 비어 있거나 `auto_connect_type`이
   `ZLINK_AUTO_CONNECT_INVALID`이면 `EINVAL`로 거부한다.
2. 같은 `channel_name`의 계약이 없으면 새 channel 계약을 만든다.
3. 같은 `channel_name`의 계약이 이미 있으면 `auto_connect_type`이 같은지 비교한다.
4. 타입이 같으면 bootstrap을 계속 진행한다.
5. 타입이 다르면 `EEXIST`로 거부한다.

Discovery가 provider endpoint를 등록할 때도 같은 `channel_name`과
`auto_connect_type`을 함께 보낸다. Registry는 bootstrap에서 검증했더라도
provider registration마다 channel 계약과 role을 다시 검증한다. 이 재검증은
오래된 Discovery, 재연결, Registry failover, 잘못된 클라이언트 구현을 막기 위한
방어 규칙이다.

Discovery가 Registry에 연결되지 않은 상태에서도 local attach 검증은 수행한다.
이때는 Registry channel 계약을 알 수 없으므로 local Discovery의
`auto_connect_type`과 참여자 role만 검증한다. Registry 연결 뒤 타입 충돌이
확인되면 Registry 연결 또는 provider registration이 실패하고, 해당 Discovery는
자동 연결을 시작하지 않는다.

`zlink_discovery_connect_registry()`가 `EEXIST`로 실패하면 Discovery handle은
살아 있지만 Registry에 연결되지 않은 상태로 남는다. 이미 attach된 local
participant는 계속 Discovery-managed 상태이므로 manual lifecycle API 제한을
유지한다. 호출자는 같은 handle로 같은 Registry에 다시 연결해도 같은 충돌을 받는다.
복구하려면 Discovery를 destroy하고, 기존 계약의 `auto_connect_type`에 맞춰 새
Discovery를 만들어야 한다.

Provider registration 실패는 public bind/attach 호출 관점에서 원자적으로 보여야
한다. 이미 bind된 endpoint를 attach하는 중 registration이 실패하면 attach 상태를
되돌리고 실패를 반환한다. attach된 뒤 새 endpoint를 bind하는 중 registration이
실패하면 구현은 방금 만든 physical bind를 unbind한 뒤 실패를 반환한다. 따라서
실패한 provider registration 뒤에는 Registry에도 provider row가 없고, local
socket 또는 SpotNode에도 discovery-advertised endpoint가 남지 않는다.

### 동시 등록

두 Discovery가 동시에 같은 `channel_name`에 다른 타입을 선언할 수 있다.
Registry는 이 상황을 원자적으로 처리해야 한다.

1. 둘 중 하나의 선언만 채널 계약 생성에 성공한다.
2. 다른 선언은 기존 계약과 타입이 다르므로 실패한다.
3. 실패한 쪽은 자신이 채널에 참여했다고 보고하면 안 된다.

즉 결과는 "둘 다 부분적으로 성공"이 아니라 "하나는 성공, 하나는 타입 충돌 실패"다.

### 에러

타입 충돌과 검증 실패는 아래 errno를 사용한다.

| 상황 | errno |
|------|-------|
| 알 수 없는 자동 연결 타입 | `EINVAL` |
| 자동 연결 타입이 허용하지 않는 role attach | `ENOTSUP` |
| 같은 handle에 중복 attach | `EBUSY` |
| 같은 채널 이름의 자동 연결 타입 충돌 | `EEXIST` |
| Registry와 통신할 수 없음 | 기존 Registry 연결 실패 errno |

## Registry 채널 계약

Registry는 각 채널 이름에 대해 아래 정보를 보존한다.

| 필드 | 의미 |
|------|------|
| `channel_name` | 논리 채널 이름 |
| `auto_connect_type` | 채널의 불변 자동 연결 타입 |
| `allowed_roles` | 이 타입에서 허용되는 참여자 role 집합 |
| `created_at` | 진단용 생성 시각 |
| `owner_registry_id` | 이 계약을 만든 Registry 식별자 |

Registry channel 계약은 provider 수명보다 오래 산다. 어떤 Discovery나 provider가
destroy되더라도 Registry는 해당 `channel_name`의 계약을 즉시 삭제하지 않는다.
같은 Registry 프로세스가 살아 있는 동안 같은 `channel_name`은 같은
`auto_connect_type`만 받을 수 있다.

운영자가 channel의 자동 연결 타입을 바꾸려면 기존 Registry 프로세스를 종료하고
새 Registry 상태로 시작하거나, 다른 `channel_name`을 사용해야 한다. 이 draft는
런타임 channel type 변경 API를 제공하지 않는다.

Registry 간 동기화가 있는 환경에서는 같은 `channel_name`에 서로 다른 계약이
전파될 수 있다. 이 경우 Registry는 deterministic winner를 골라야 한다.

winner는 아래 순서로 정한다.

1. `owner_registry_id`가 더 작은 계약이 이긴다.
2. `owner_registry_id`가 같으면 `created_at`이 더 작은 계약이 이긴다.
3. 두 값이 모두 같으면 `auto_connect_type` 숫자가 더 작은 계약이 이긴다.

winner 계약을 관측한 Registry는 해당 `channel_name`의 로컬 계약을 winner로
교체한다. 교체 전 loser 계약으로 등록된 provider는 제거하고 다음 projection
broadcast에서 내보내지 않는다. 정상 winner 계약의 provider를 다른 타입으로
재해석하면 안 된다.

winner가 아닌 계약은 `EEXIST`로 거부한다. 이후 같은 Registry에서 해당
`channel_name`을 계속 쓰려면 winner의 자동 연결 타입으로 Discovery를 다시
만들어야 한다. 서로 다른 topology가 필요하면 서로 다른 channel name을 사용한다.

## Discovery snapshot 계약

Discovery가 노출하는 peer snapshot과 member snapshot에는 참여자의
`auto_connect_type`과 Discovery channel 계약을 확인할 수 있는 정보가 있어야 한다.

각 row는 advertised provider만 나타낸다. Discovery에 attach되었지만 endpoint를
등록하지 않은 local consumer는 member snapshot에 나타나지 않는다.

```c
typedef struct zlink_member_peer_entry_t {
  zlink_auto_connect_type_t auto_connect_type;
  zlink_service_role_t service_role;
  char channel_name[256];
  char endpoint[256];
  uint32_t weight;
  zlink_routing_id_t routing_id;
  int64_t value;
} zlink_member_peer_entry_t;
```

이 값은 디버깅과 운영 확인을 위한 정보다. 자동 연결 결정은 호출자가 snapshot을
해석해서 수행하지 않고, Discovery와 attach runtime이 채널 계약에 따라 수행한다.

## Topology summary 계약

Topology summary도 같은 채널 계약을 반영해야 한다.

- `service_kind`나 기존 role 값만으로 자동 연결 타입을 추론하게 만들지 않는다.
- summary row에는 `auto_connect_type`을 포함한다.
- `service_kind`는 topology row의 보고 주체를 설명하는 진단 필드로 유지한다.
  raw socket provider는 `ZLINK_SERVICE_KIND_SOCKET`을 사용한다. Discovery가
  Registry에서 관측한 channel 상태를 보고하는 row는
  `ZLINK_SERVICE_KIND_DISCOVERY`를 사용한다. 기존 SPOT pub/sub 세부 summary는
  필요할 때 `ZLINK_SERVICE_KIND_SPOT_PUB`와 `ZLINK_SERVICE_KIND_SPOT_SUB`를
  계속 사용할 수 있다.
- 자동 연결 판단은 `auto_connect_type`과 `service_role`만 사용한다.
  `service_kind`는 자동 연결 대상 선택에 쓰지 않는다.
- 타입 충돌로 실패한 참여자는 READY, STOPPED, DEGRADED 같은 topology row를
  만들지 않는다.
- 타입 충돌 진단은 실패 API의 `errno == EEXIST`와 Registry 로그로 제공한다.
  이 draft에서는 별도의 error topology row를 만들지 않는다.

## Attach 검증 순서

raw socket attach는 아래 순서로 검증한다.

1. Discovery handle이 유효한지 확인한다.
2. Discovery의 `auto_connect_type`을 읽는다.
3. socket 타입에서 참여자 role을 파생한다.
4. 자동 연결 타입이 해당 role을 허용하는지 확인한다.
5. socket이 이미 manual connect나 active pipe를 가진 상태인지 확인한다.
6. attach를 기록한다.
7. endpoint가 이미 bind되어 있으면 provider registration을 시도한다.
8. Registry 채널 계약과 충돌하면 실패하고 attach 상태를 되돌린다.

SpotNode attach는 아래 순서로 검증한다.

1. Discovery handle이 유효한지 확인한다.
2. Discovery의 `auto_connect_type`이 `SPOT_MESH`인지 확인한다.
3. node가 이미 discovery 자동 peer 연결이나 수동 peer 연결을 갖고 있는지 확인한다.
4. attach를 기록한다.
5. endpoint가 이미 bind되어 있으면 provider registration을 시도한다.
6. Registry 채널 계약과 충돌하면 실패하고 attach 상태를 되돌린다.

SpotNode channel dealer attach는 아래 순서로 검증한다.

1. Discovery handle과 dealer socket handle이 유효한지 확인한다.
2. Discovery의 `auto_connect_type`이 `CLIENT_SERVER` 또는 `DEALER_MESH`인지
   확인한다.
3. dealer socket 타입이 `ZLINK_SOCKET_DEALER`인지 확인한다.
4. 같은 node에 같은 channel name의 channel dealer가 이미 attach되어 있지 않은지
   확인한다.
5. dealer socket이 이미 다른 SpotNode attachment에 쓰이고 있지 않은지 확인한다.
6. attach를 기록하고 Discovery observer로 등록한다.
7. Registry channel 계약과 충돌하면 실패하고 attach 상태를 되돌린다.

## Public lifecycle

Discovery가 채널 자동 연결을 소유하는 동안 수동 lifecycle API는 제한한다.

- Discovery-managed raw socket은 public `connect`, `disconnect`, `unbind`,
  `disconnect_rid`, `close`를 거부한다.
- Discovery-managed SpotNode는 수동 peer connect/disconnect를 거부한다.
- Discovery destroy는 해당 Discovery가 소유한 자동 연결 endpoint를 정리한다.

이 규칙은 자동 연결 타입과 무관하게 같다. 자동 연결 타입은 어떤 peer를 고를지
정하고, lifecycle ownership은 Discovery attach 여부가 정한다.

## 기존 auto-connect type 제거

이 초안에서는 `DEALER -> ROUTER`와 `DEALER -> DEALER`를 하나의 Discovery 옵션으로
바꾸지 않는다. 대신 서로 다른 자동 연결 타입으로 나눈다.

- 기존 `ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER` 의미는
  `ZLINK_AUTO_CONNECT_CLIENT_SERVER`로 표현한다.
- 기존 `ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER` 의미는
  `ZLINK_AUTO_CONNECT_DEALER_MESH`로 표현한다.
- `Discovery auto-connect type constructor contract`는 새 모델에서 제거한다.

## 같은 기능에 여러 토폴로지가 필요한 경우

하나의 논리 기능이 request/reply와 event fanout을 모두 필요로 할 수 있다.
이 경우 같은 채널 이름에 여러 자동 연결 타입을 섞지 않는다.
서로 다른 채널 이름을 사용한다.

예:

| 목적 | 채널 이름 | 자동 연결 타입 |
|------|-----------|----------------|
| 게임 명령 request/reply | `game.play.rpc` | `CLIENT_SERVER` |
| 게임 상태 broadcast | `game.play.events` | `FANOUT` |
| 게임 서버 간 route mesh | `game.play.routes` | `ROUTE_MESH` |
| worker 간 peer mesh | `game.play.workers` | `DEALER_MESH` |
| SPOT node mesh | `game.play.spots` | `SPOT_MESH` |

채널 이름을 나누면 각 채널의 실패, 모니터링, capacity, 권한 정책을 따로 볼 수 있다.

## 예시

### ROUTE_MESH

```c
void *discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_ROUTE_MESH, "route.global");

void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
zlink_socket_attach_discovery(router, discovery);
zlink_bind(router, "tcp://127.0.0.1:7001");
```

같은 `route.global` 채널에 다른 ROUTER가 들어오면 두 ROUTER 중 한쪽만 outbound
connect를 시작한다.

### CLIENT_SERVER

```c
void *server_discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "match.rpc");
void *client_discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "match.rpc");

void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

zlink_socket_attach_discovery(router, server_discovery);
zlink_socket_attach_discovery(dealer, client_discovery);

zlink_bind(router, "tcp://127.0.0.1:7101");
```

DEALER는 같은 채널의 ROUTER endpoint에 connect한다. ROUTER는 DEALER endpoint로
자동 connect하지 않는다. ROUTER가 여러 개 있으면 DEALER는 발견한 eligible ROUTER
endpoint 전부에 connect한다.

### DEALER_MESH

```c
void *discovery_a = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_DEALER_MESH, "peer.workers");
void *discovery_b = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_DEALER_MESH, "peer.workers");

void *dealer_a = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
void *dealer_b = zlink_socket(ctx, ZLINK_SOCKET_DEALER);

zlink_socket_attach_discovery(dealer_a, discovery_a);
zlink_socket_attach_discovery(dealer_b, discovery_b);

zlink_bind(dealer_a, "tcp://127.0.0.1:7251");
zlink_bind(dealer_b, "tcp://127.0.0.1:7252");
```

두 DEALER 사이에서는 pairwise initiator 규칙에 따라 한쪽만 outbound connect를
시작한다.

### FANOUT

```c
void *pub_discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_FANOUT, "match.events");
void *sub_discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_FANOUT, "match.events");

void *pub = zlink_socket(ctx, ZLINK_SOCKET_PUB);
void *sub = zlink_socket(ctx, ZLINK_SOCKET_SUB);

zlink_socket_attach_discovery(pub, pub_discovery);
zlink_socket_attach_discovery(sub, sub_discovery);

zlink_bind(pub, "tcp://127.0.0.1:7201");
```

SUB는 같은 채널의 PUB endpoint에 connect한다.

### SPOT_MESH

```c
void *discovery = zlink_discovery_new(
  ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot.cluster");

void *node = zlink_spot_node_new(ctx, NULL);
zlink_spot_node_attach_discovery(node, discovery);
zlink_spot_node_bind(node, "tcp://127.0.0.1:7301");
```

SpotNode는 pairwise initiator 규칙에 따라 같은 `spot.cluster` 채널의 일부
SpotNode endpoint에 connect한다.

## 구현 순서

구현은 아래 순서로 진행한다.

1. 공개 enum과 public API 시그니처를 먼저 바꾼다.
2. Discovery 내부 상태에서 `auto_connect_type`을 제거하고 `auto_connect_type`과
   `channel_name`을 기준으로 저장한다.
3. Registry channel 계약 저장소를 추가한다.
4. Registry channel 계약을 Registry 프로세스 수명 동안 유지한다.
5. Registry bootstrap과 provider registration 프로토콜에
   `auto_connect_type`과 `channel_name`을 넣는다.
6. raw socket attach 검증을 자동 연결 타입별 허용 role 기준으로 바꾼다.
7. raw socket peer refresh에서 자동 연결 타입별 연결 방향을 적용한다.
8. SpotNode attach 검증을 `SPOT_MESH` 기준으로 바꾼다.
9. SpotNode channel dealer attach 검증을 `CLIENT_SERVER`와 `DEALER_MESH`
   기준으로 바꾼다.
10. member snapshot, service summary, topology summary 구조체와 query filter를
   새 필드 기준으로 바꾼다.
11. 기존 `DEALER` peer mode API와 내부 상태를 제거한다.
12. `zlink_discovery_resolve_spot()`을 `SPOT_MESH` 전용 조회로 검증한다.
13. 바인딩 공개 API와 바인딩 spec을 새 enum과 새 Discovery 생성자에 맞춘다.
14. 문서의 테스트 요구사항을 통과시킨 뒤 정식 spec 문서로 나누어 반영한다.

## 테스트 요구사항

정식 구현 시 최소한 아래 테스트가 필요하다.

1. 같은 channel name에 같은 자동 연결 타입을 가진 여러 Discovery가 성공한다.
2. 같은 channel name에 다른 자동 연결 타입을 가진 두 Discovery 중 하나만 성공한다.
3. 타입 충돌로 실패한 참여자가 member snapshot에 나타나지 않는다.
4. 마지막 provider가 unregister되어도 Registry channel 계약은 남고, 같은
   channel name의 다른 자동 연결 타입 bootstrap은 `EEXIST`로 실패한다.
5. `ROUTE_MESH`에서 ROUTER가 아닌 socket attach가 실패한다.
6. `ROUTE_MESH`에서 ROUTER 쌍마다 initiator가 하나만 생긴다.
7. `CLIENT_SERVER`에서 DEALER는 ROUTER에 connect하고 ROUTER는 DEALER에 connect하지 않는다.
8. `CLIENT_SERVER`에서 DEALER는 eligible ROUTER endpoint 전부에 connect한다.
9. `DEALER_MESH`에서 DEALER 쌍마다 initiator가 하나만 생긴다.
10. `FANOUT`에서 SUB는 PUB에 connect하고 PUB는 SUB에 connect하지 않는다.
11. `SPOT_MESH`에서 SpotNode 쌍마다 initiator가 하나만 생긴다.
12. endpoint 없는 `CLIENT_SERVER` DEALER가 member snapshot에 나타나지 않으면서
    ROUTER provider에는 connect한다.
13. endpoint 없는 `FANOUT` SUB가 member snapshot에 나타나지 않으면서 PUB provider에는
    connect한다.
14. Registry member peer 조회 API가 `channel_name`만으로 조회된다.
15. service summary와 topology query가 `auto_connect_type` 필터를 적용한다.
16. `Discovery auto-connect type constructor contract`와
    `zlink_discovery_dealer_peer_mode_t`가 공개 헤더에서 제거된다.
17. `zlink_spot_node_attach_channel_dealer()`가 `CLIENT_SERVER`와 `DEALER_MESH`
    Discovery만 받고, `ROUTE_MESH`, `FANOUT`, `SPOT_MESH` Discovery를 거부한다.
18. `zlink_discovery_resolve_spot()`이 `SPOT_MESH` Discovery에서는 동작하고,
    다른 자동 연결 타입에서는 `ENOTSUP`으로 실패한다.
19. Discovery destroy가 discovery-managed endpoint를 정리한다.
20. Registry peer sync를 켠 환경에서 서로 다른 타입의 같은 channel 계약이
    전파되면 deterministic winner만 projection에 남는다.

## 구현 확정 사항

이 문서는 아래 결정을 확정한다.

- 타입 충돌 errno는 `EEXIST`다.
- 타입 충돌 전용 zlink 에러는 추가하지 않는다.
- Registry cluster 충돌 winner는 `owner_registry_id`, `created_at`,
  `auto_connect_type` 순서로 결정한다.
- Registry channel 계약은 Registry 프로세스가 살아 있는 동안 유지된다.
- endpoint 없는 participant는 member snapshot에 나타나지 않는다.
- service summary, member snapshot, topology summary, topology filter에는
  `auto_connect_type`을 포함한다.
