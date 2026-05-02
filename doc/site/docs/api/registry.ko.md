[English](registry.md) | [한국어](registry.ko.md)

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [서비스 공통](README.ko.md)

# 레지스트리

## API 표면

- Registry는 전역 서비스 디렉터리이자 전체 서비스 요약 정보를 보는 기본
  진입점입니다.
- 같은 프로세스 안에서는 `zlink_registry_topology_snapshot()`을 사용합니다.
- 원격 조회는 `zlink_registry_query_client_*()`와
  `zlink_registry_query_snapshot()`을 사용합니다.
- Registry topology는 전체 요약을 볼 때 사용합니다. 개별 서비스의 자세한 상태
  변화는 각 서비스의 monitor API를 사용합니다.

레지스트리는 zlink 서비스 계층의 중앙 서비스 디렉터리입니다. SPOT 노드 및
socket family 서비스로부터 서비스 등록, 등록 해제, 하트비트 요청을 수신하고,
집계된 서비스 목록을 연결된 모든 Discovery 인스턴스에 주기적으로
브로드캐스트합니다.

## SPOT 주소 정보의 최종 기준

Registry는 관리형 SPOT 구성에서 "현재 Discovery가 보고 있는 같은
`service_name` 안에서 `spot_rid`를 지금 어느 `SpotNode`가 맡고 있는가"를
결정하는 **최종 기준**입니다. 이 문서는 `(service_name, spot_rid) ->
owner_node_rid` 매핑이 어떻게 등록되고, 바뀌고, 사라지는지 정의합니다.

- `spot_rid`는 현재 `service_name` 안에서 해석되는 논리 spot 주소 키입니다.
- `owner_node_rid`는 지금 그 이름을 맡고 있는 `SpotNode`를 가리킵니다.
- SPOT direct submit에서 쓰는 `dest_node_rid + dest_spot_rid` 조합은 이 기준
  정보를 바탕으로 만들어집니다.
- Discovery는 이 결과를 가까운 곳에 저장해 둘 수 있지만, 최종 기준이 되지는
  않습니다.

SPOT send/request/reply 함수 계약은 [spot.ko.md](spot.ko.md)에서 정의합니다.
Discovery가 주소를 가까운 곳에 저장하고 다시 조회하는 흐름은
[discovery.ko.md](discovery.ko.md)에서 정의합니다.

응용이 보통 직접 호출하는 조회 함수는 Registry가 아니라 Discovery 쪽의
`zlink_discovery_resolve_spot()`입니다. Registry는 그 함수가 의존하는 기준
데이터를 관리합니다.

## spot 주소 수명 주기

이 절은 Registry가 논리 `spot_rid` 주소 정보를 어떻게 등록, 갱신, 교체,
철회하는지 정의합니다.

### 1. 등록 시점

`Spot`은 자신의 `spot_rid`가 확정되고, 이를 담고 있는 `SpotNode`의 `node_rid`를
알 수 있으며, Registry에 참여 중인 상태가 되면 주소 등록 대상이 됩니다.

- 등록 키는 `spot_rid` 입니다.
- 등록 값은 현재 owner `node_rid` 입니다.
- 같은 `Spot`이 같은 `spot_rid`로 다시 광고되는 경우는 갱신으로 취급할 수
  있습니다.
- 주소 광고가 완료되기 전에는 논리 주소로 보내는 direct submit의 활성 목적지로
  간주되어서는 안 됩니다.

### 2. 주소 기록이 담아야 하는 최소 정보

Registry의 주소 기록은 적어도 아래 의미를 표현할 수 있어야 합니다.

- `service_name`: 이 주소가 속한 논리 서비스 범위
- `spot_rid`: 논리 spot 주소 키
- `owner_node_rid`: 현재 이 이름을 맡고 있는 node
- `ordering token`: 더 새로운 정보와 더 오래된 정보를 비교하는 값
- `state`: active, replaced, withdrawn 같은 주소 상태
- `last_reported_ms`: 가장 최근 등록 또는 갱신 시각

공개 API가 이 필드 이름을 그대로 노출할 필요는 없지만, 관찰 가능한 동작은
이 의미를 보존해야 합니다.

### 3. 같은 이름이 다시 등록될 때와 handover

같은 `spot_rid`로 두 개 이상의 `Spot`이 등록을 시도할 수 있습니다. 이 경우
Registry는 handover 설정에 따라 최종 owner를 결정합니다.

- handover off: 먼저 활성화된 owner를 유지합니다. 이후 duplicate claim 은
  reject 되거나 inactive record 로 남을 수 있지만, 현재 owner는
  바뀌지 않아야 합니다.
- handover on: 더 새로운 ordering token 을 가진 owner 가 현재 owner 가
  됩니다. 이전 owner 는 replaced 상태가 되어야 합니다.
- 더 오래된 update 는 어떤 경우에도 현재 owner 를 되돌려서는 안
  됩니다.

이 규칙은
`ZLINK_OPT_RID_DUPLICATE_POLICY = ZLINK_RID_DUPLICATE_HANDOVER`와 같은
방향으로 해석합니다. 다만 Registry는 연결 순서가 아니라 ordering token
기준으로 더 새로운 정보를 판정해야 합니다. 이 문서 버전에서는 handover
설정을 어디에서 읽어올지는 구현에 맡깁니다. 적절한 구현이라면 owning
service 또는 node에 이미 적용된 routed duplicate policy 설정을 따라도
됩니다.

현재 core 구현은 같은 `(service_name, spot_rid)`에 대해 topology row를
엔드포인트별로 따로 보관한 뒤, owner 조회를 할 때 최종 owner 한 개만 다시
고릅니다. 이때 ordering token의 실제 기준은 아래와 같습니다.

- 1차 기준: 같은 `service_name` 안에서 아직 살아 있는 `SpotNode` provider의
  등록 시각
- 2차 기준: 같은 provider 등록 시각일 때 topology row의 `last_reported_ms`
- 3차 기준: 위 두 값도 같을 때 endpoint 문자열 비교

이 규칙이 필요한 이유는 old owner와 new owner가 잠시 함께 살아 있는 동안,
더 늦게 도착한 오래된 report가 새 owner를 다시 덮어쓰지 않게 하기 위해서입니다.

Registry는 owner를 고를 때 현재 살아 있는 provider 집합에 없는 endpoint를
최종 owner 후보에서 제외해야 합니다. 따라서 예전에 보고된 topology row가
남아 있더라도, 그 endpoint가 더 이상 현재 provider 집합에 없다면
`zlink_discovery_resolve_spot()`의 기준 owner가 되어서는 안 됩니다.

### 4. unregister 와 tombstone

현재 owner가 종료되거나 해당 `Spot`이 destroy 되면 주소 정보는 withdraw
되어야 합니다.

- 정상 종료 경로에서는 unregister 를 즉시 시도해야 합니다.
- 비정상 종료나 네트워크 분리로 unregister 가 오지 않을 수 있으므로 Registry는
  lease, heartbeat, 또는 동등한 만료 규칙으로 오래된 주소 정보를 정리할 수
  있어야 합니다.
- handover 또는 withdraw 직후 오래된 캐시가 다시 살아나는 일을 막기 위해
  Registry는
  짧은 tombstone 또는 withdrawn marker 를 유지할 수 있습니다.

tombstone TTL 의 정확한 값은 구현 정책입니다. 다만 older advertisement 가 새
주소 정보 또는 withdrawn 상태를 오래된 광고가 덮어쓰지 못할 정도의 순서 보존은
제공해야 합니다.

### 5. 대규모 환경 전제

이 주소 관리 모델은 매우 많은 `spot_rid`가 동시에 존재하는 환경을 전제로
해야 합니다. 구현은 적어도 "노드 1만 개, 노드당 spot 1만 개" 수준을 감당할 수
있어야 합니다.

- Registry 주소 저장소는 하나의 인스턴스에 모두 있을 수도 있고,
  여러 shard 로 분산될 수도 있습니다.
- 공개 계약이 요구하는 것은 "어느 노드가 현재 owner인가"에 대한 최종 기준이지,
  단일 프로세스 저장 레이아웃이 아닙니다.
- 만료 판정은 spot 개수에 선형 비례하는 per-spot heartbeat 를 강제해서는 안
  됩니다.

집계 방식의 생존 확인과 Discovery 쪽 캐시 확장 규칙은
[discovery.ko.md](discovery.ko.md)를 참조하세요.

## 상수

### Registry 상태

```c
typedef enum zlink_registry_state_t
{
    ZLINK_REGISTRY_STATE_IDLE     = 1,
    ZLINK_REGISTRY_STATE_ACTIVE   = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR    = 4
} zlink_registry_state_t;
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_REGISTRY_STATE_IDLE` | 1 | Registry가 생성되었으나 아직 시작되지 않음 |
| `ZLINK_REGISTRY_STATE_ACTIVE` | 2 | Registry가 정상적으로 실행 중 |
| `ZLINK_REGISTRY_STATE_DEGRADED` | 3 | Registry가 연결 저하 상태로 실행 중 |
| `ZLINK_REGISTRY_STATE_ERROR` | 4 | Registry가 에러 상태 |

## 함수

### zlink_registry_new

서비스 레지스트리를 생성합니다.

```c
void *zlink_registry_new(void *ctx);
```

새 Registry 인스턴스를 할당하고 초기화합니다. Registry는 브로드캐스트 및
등록 수신을 위한 내부 PUB 및 ROUTER 소켓을 관리합니다. 컨텍스트 핸들은
Registry의 수명 동안 유효해야 합니다.

**반환값:** 성공 시 Registry 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_registry_bind`, `zlink_registry_destroy`

---

### zlink_registry_bind

Registry의 PUB 및 ROUTER 엔드포인트를 바인딩하고 Registry를 시작합니다.

```c
zlink_bind_result_t zlink_registry_bind(void *registry,
                                        const char *pub_endpoint,
                                        const char *router_endpoint);
```

Registry의 PUB 및 ROUTER 엔드포인트를 바인딩하고, 바인드 성공을 확인한 뒤,
내부 control task를 시작하며 서비스 등록 수신과 브로드캐스트를 시작합니다.
PUB 엔드포인트는 Discovery 인스턴스에 서비스 목록을 브로드캐스트하는 데
사용됩니다. ROUTER 엔드포인트는 SPOT 노드 및 socket family 서비스로부터
등록, 등록 해제, 하트비트 메시지를 수신하는 데 사용됩니다.

**반환값:** `zlink_bind_result_t` 값을 반환합니다. 상세 errno 는 진단용으로
`zlink_errno()`에서 계속 조회할 수 있습니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe). 다만 이 호출은 lifecycle 제약상 Registry당 한 번만
호출해야 합니다.

**참고:** `zlink_registry_new`, `zlink_registry_destroy`

---

### zlink_registry_set_id

레지스트리 고유 ID를 설정합니다.

```c
zlink_config_result_t zlink_registry_set_id(void *registry, uint32_t registry_id);
```

이 Registry 인스턴스에 고유 식별자를 할당합니다. ID는 여러 레지스트리가
피어 연결을 통해 서로 동기화하는 클러스터 구성에 사용됩니다.
`zlink_registry_bind` 전후 모두 호출할 수 있으며, 변경된 ID는 다음 runtime
tick에서 반영됩니다. bind 이후에 ID를 바꾸면 이미 송출된 broadcast가
직전 값으로 나갈 수 있으므로 초기 설정 단계에서 지정하는 것을 권장합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe).

**참고:** `zlink_registry_add_peer`

---

### zlink_registry_add_peer

클러스터 동기화를 위한 피어 레지스트리 PUB 엔드포인트를 추가합니다.

```c
zlink_config_result_t zlink_registry_add_peer(void *registry,
                                              const char *peer_pub_endpoint);
```

이 Registry를 피어 Registry의 PUB 엔드포인트에 연결하여 클러스터 전체에서
서비스 목록을 동기화할 수 있도록 합니다. 여러 피어를 추가할 수 있습니다.
`zlink_registry_bind` 전후 모두 호출할 수 있으며, runtime tick이 다음
주기에 새 peer endpoint의 PUB에 dial합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe).

**참고:** `zlink_registry_set_id`

---

### zlink_registry_set_heartbeat

하트비트 간격 및 타임아웃을 설정합니다.

```c
zlink_config_result_t zlink_registry_set_heartbeat(void *registry,
                                                   uint32_t interval_ms,
                                                   uint32_t timeout_ms);
```

Registry가 등록된 서비스로부터 하트비트 메시지를 기대하는 빈도와 서비스를
만료로 간주하는 시점을 구성합니다. 서비스가 `timeout_ms` 밀리초 이내에
하트비트를 보내지 않으면 Registry는 해당 서비스를 서비스 목록에서 제거합니다.
`zlink_registry_bind` 전후 모두 호출할 수 있으며, runtime tick이 다음 주기에
새 값을 반영합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe).

**참고:** `zlink_registry_set_broadcast_interval`

---

### zlink_registry_set_broadcast_interval

서비스 목록 브로드캐스트 간격을 설정합니다.

```c
zlink_config_result_t zlink_registry_set_broadcast_interval(void *registry,
                                                            uint32_t interval_ms);
```

Registry가 PUB 소켓을 통해 전체 서비스 목록을 게시하는 빈도를 제어합니다.
PUB 엔드포인트를 구독하는 Discovery 인스턴스는 이 간격으로 업데이트를
수신합니다. `zlink_registry_bind` 전후 모두 호출할 수 있으며, runtime tick이
다음 주기에 새 간격을 반영합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe).

**참고:** `zlink_registry_set_heartbeat`

---

### zlink_registry_destroy

Registry를 파괴하고 모든 리소스를 해제합니다.

```c
zlink_close_result_t zlink_registry_destroy(void **registry_p);
```

내부 스레드를 중지하고, 모든 소켓을 닫고, Registry를 해제합니다. 파괴 후
`*registry_p`의 포인터는 `NULL`로 설정됩니다. Registry가 시작된 경우
이 함수는 내부 스레드가 종료될 때까지 블록합니다.

**반환값:** `zlink_close_result_t` 값을 반환합니다.

**스레드 안전성:** 하나의 Registry handle을 여러 스레드에서 동시에 사용할 수
있습니다 (thread-safe). 다만 `zlink_registry_destroy()`는 더 보수적이며, 같은
handle에서 다른 스레드가 운영 API를 실행 중이면 `errno=EBUSY`로
실패합니다. destroy가 성공한 경우에만 `*registry_p`가 `NULL`로 정리됩니다.

**참고:** `zlink_registry_new`, `zlink_registry_bind`

---

## 스냅샷 / 인트로스펙션

Registry의 프로세스 수준 건강 요약과 서비스 수준 집계 뷰를 제공하는
API입니다.

### Registry Status Snapshot

```c
zlink_config_result_t zlink_registry_status_snapshot(void *registry,
                                                     zlink_registry_status_t *out);
```

Registry의 단일 행 프로세스 수준 건강 요약을 반환합니다.

#### zlink_registry_status_t

```c
typedef struct zlink_registry_status_t
{
    uint32_t registry_id;
    char bind_endpoint[256];
    zlink_registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_registry_status_t;
```

| 필드 | 설명 |
|------|------|
| `registry_id` | `zlink_registry_set_id()`로 할당된 고유 ID. |
| `bind_endpoint` | null 종료 바인드 엔드포인트. |
| `state` | `IDLE`, `ACTIVE`, `DEGRADED`, 또는 `ERROR`. |
| `topology_entry_count` | topology 테이블의 총 항목 수. |
| `peer_registry_count` | 구성된 피어 레지스트리 수. |
| `connected_peer_registry_count` | 현재 연결된 피어 레지스트리 수. |
| `list_seq` | 최신 브로드캐스트의 단조 증가 시퀀스 번호. |
| `last_error` | 마지막 기록된 에러 코드, 또는 0. |
| `last_changed_ms` | 마지막 상태 변경 시점 (에포크 ms). |

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### Registry Service Summary Snapshot

```c
zlink_config_result_t zlink_registry_service_summary_snapshot(
  void *registry,
  const zlink_registry_service_summary_filter_t *filter,
  zlink_registry_service_summary_entry_t *entries,
  size_t *count);
```

서비스 수준 집계 정보를 반환합니다. 각 항목은 주어진 (service_kind,
service_name) 쌍에 대해 상태별 인스턴스 수를 요약합니다.

**버퍼 규약:** `entries = NULL`을 전달하면 필요한 개수만 반환합니다. 다음
호출에서 호출자가 할당한 버퍼를 제공합니다. 버퍼가 부족하면 `-1`을 반환하고
`errno = ENOBUFS`, `*count`에 필요한 용량을 설정합니다.

결과는 (`service_kind`, `service_name`) 오름차순으로 정렬됩니다.

#### zlink_registry_service_summary_entry_t

```c
typedef struct zlink_registry_service_summary_entry_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;
```

| 필드 | 설명 |
|------|------|
| `service_kind` | `ZLINK_SERVICE_KIND_*` 상수 중 하나. |
| `service_role` | `ZLINK_SERVICE_ROLE_*` 상수 중 하나. |
| `service_name` | null 종료 서비스 이름. |
| `total_count` | 이 서비스의 총 등록된 인스턴스 수. |
| `connecting_count` | 현재 연결 중인 인스턴스 수. |
| `ready_count` | 현재 ready 상태인 인스턴스 수. |
| `error_count` | 에러 상태의 인스턴스 수. |
| `stopped_count` | 중지된 인스턴스 수. |
| `last_reported_ms` | 모든 인스턴스 중 최신 하트비트 시점 (에포크 ms). |

#### zlink_registry_service_summary_filter_t

```c
typedef struct zlink_registry_service_summary_filter_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
} zlink_registry_service_summary_filter_t;
```

0이 아닌 값으로 설정된 필드를 기준으로 필터링합니다. 0인 필드는
와일드카드입니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

## Topology & Query API

Registry가 관리하는 전역 서비스 토폴로지를 조회하는 API입니다.

### Topology 상수

```c
#define ZLINK_TOPOLOGY_SOURCE_MANUAL    1
#define ZLINK_TOPOLOGY_SOURCE_DISCOVERY 2
#define ZLINK_TOPOLOGY_SOURCE_REGISTRY  3

#define ZLINK_TOPOLOGY_STATE_DISCOVERED 1
#define ZLINK_TOPOLOGY_STATE_CONNECTING 2
#define ZLINK_TOPOLOGY_STATE_READY      3
#define ZLINK_TOPOLOGY_STATE_LOST       4
#define ZLINK_TOPOLOGY_STATE_ERROR      5
#define ZLINK_TOPOLOGY_STATE_STOPPED    6
```

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_TOPOLOGY_SOURCE_MANUAL` | 1 | 수동 연결로 추가된 항목 |
| `ZLINK_TOPOLOGY_SOURCE_DISCOVERY` | 2 | Discovery를 통해 발견된 항목 |
| `ZLINK_TOPOLOGY_SOURCE_REGISTRY` | 3 | Registry를 통해 등록된 항목 |
| `ZLINK_TOPOLOGY_STATE_DISCOVERED` | 1 | 발견되었지만 아직 연결되지 않음 |
| `ZLINK_TOPOLOGY_STATE_CONNECTING` | 2 | 연결 진행 중 |
| `ZLINK_TOPOLOGY_STATE_READY` | 3 | 연결 완료 및 준비됨 |
| `ZLINK_TOPOLOGY_STATE_LOST` | 4 | 연결 손실 |
| `ZLINK_TOPOLOGY_STATE_ERROR` | 5 | 에러 상태 |
| `ZLINK_TOPOLOGY_STATE_STOPPED` | 6 | 중지됨 |

### Topology 타입

#### zlink_registry_topology_entry_t

```c
typedef struct zlink_registry_topology_entry_t
{
    zlink_routing_id_t routing_id;
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    char endpoint[256];
    zlink_topology_source_t source;
    zlink_topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
} zlink_registry_topology_entry_t;
```

| 필드 | 설명 |
|------|------|
| `routing_id` | 서비스 인스턴스의 라우팅 아이덴티티. |
| `service_kind` | `ZLINK_SERVICE_KIND_*` 상수 중 하나. |
| `service_role` | `ZLINK_SERVICE_ROLE_*` 상수 중 하나. |
| `service_name` | null 종료 서비스 이름. |
| `endpoint` | null 종료 광고 엔드포인트. |
| `source` | 항목 추가 방식 (`ZLINK_TOPOLOGY_SOURCE_*`). |
| `state` | 현재 상태 (`ZLINK_TOPOLOGY_STATE_*`). |
| `desired_count` | 예상 인스턴스 수. |
| `ready_count` | 현재 준비된 인스턴스 수. |
| `error_code` | 상태가 `ERROR`인 경우 에러 코드. |
| `last_reported_ms` | 마지막 하트비트 또는 업데이트 타임스탬프 (에포크 ms). |

#### zlink_registry_topology_filter_t

```c
typedef struct zlink_registry_topology_filter_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    zlink_routing_id_t routing_id;
    zlink_topology_state_t state;
    zlink_topology_source_t source;
} zlink_registry_topology_filter_t;
```

0이 아닌 값으로 설정된 필드를 기준으로 필터링합니다. 0인 필드는
와일드카드(모두 일치)로 처리됩니다.

---

### zlink_registry_topology_snapshot

로컬 Registry 인스턴스에서 전체 토폴로지 스냅샷을 가져옵니다.

```c
zlink_config_result_t zlink_registry_topology_snapshot(void *registry,
                                                       zlink_registry_topology_entry_t *entries,
                                                       size_t *count);
```

`entries`에 토폴로지의 모든 등록된 서비스를 채웁니다. 입력 시 `*count`는
배열 용량이고, 출력 시 실제 개수입니다. 필요한 개수를 먼저 조회하려면
`entries = NULL`을 전달합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_registry_topology_query`

---

### zlink_registry_topology_query

필터를 사용하여 로컬 토폴로지를 조회합니다.

```c
zlink_config_result_t zlink_registry_topology_query(void *registry,
                                                    const zlink_registry_topology_filter_t *filter,
                                                    zlink_registry_topology_entry_t *entries,
                                                    size_t *count);
```

`zlink_registry_topology_snapshot`와 동일하지만 `filter` 조건과 일치하는
항목만 반환합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_registry_topology_snapshot`

---

### zlink_registry_query_client_new

원격 토폴로지 조회 클라이언트를 생성합니다.

```c
void *zlink_registry_query_client_new(void *ctx);
```

원격 Registry에 연결하여 토폴로지를 조회할 수 있는 클라이언트를 생성합니다.
Registry가 다른 프로세스에서 실행 중일 때 사용합니다.

**반환값:** 성공 시 쿼리 클라이언트 핸들, 실패 시 `NULL`.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

**참고:** `zlink_registry_query_client_connect`, `zlink_registry_query_destroy`

---

### zlink_registry_query_client_connect

쿼리 클라이언트를 원격 Registry에 연결합니다.

```c
zlink_connect_result_t zlink_registry_query_client_connect(void *client,
                                                           const char *endpoint);
```

토폴로지 조회를 위해 Registry의 ROUTER 엔드포인트에 연결합니다.

**반환값:** `zlink_connect_result_t` 값을 반환합니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_registry_query_snapshot`

---

### zlink_registry_query_snapshot

원격 Registry 토폴로지를 조회합니다.

```c
zlink_config_result_t zlink_registry_query_snapshot(void *client,
                                                    const zlink_registry_topology_filter_t *filter,
                                                    zlink_registry_topology_entry_t *entries,
                                                    size_t *count);
```

연결된 원격 Registry에 토폴로지 조회를 전송하고 일치하는 결과를 `entries`에
채웁니다. 입력 시 `*count`는 배열 용량이고, 출력 시 실제 개수입니다.
필터 없이 전체 스냅샷을 가져오려면 `filter = NULL`을 전달합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_registry_query_client_connect`

---

### zlink_registry_query_destroy

쿼리 클라이언트를 파괴하고 리소스를 해제합니다.

```c
zlink_close_result_t zlink_registry_query_destroy(void **client_p);
```

클라이언트 연결을 닫고 `*client_p`를 `NULL`로 설정합니다.

**반환값:** `zlink_close_result_t` 값을 반환합니다.

**스레드 안전성:** 스레드 안전하지 않음.

**참고:** `zlink_registry_query_client_new`

---

## Member Peers API

Registry 및 Discovery가 관리하는 서비스의 멤버 피어 상태를 조회하는
API입니다.

### Member Peers 타입

#### zlink_member_peer_entry_t

```c
typedef struct zlink_member_peer_entry_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_role_t service_role;
    char channel_name[256];
    char endpoint[256];
    uint32_t weight;
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;
```

| 필드 | 설명 |
|------|------|
| `auto_connect_type` | 자동 연결 channel 타입 (`ZLINK_AUTO_CONNECT_*`). |
| `service_role` | 서비스 인스턴스의 역할. |
| `channel_name` | null 종료 channel 이름. |
| `endpoint` | null 종료 엔드포인트. |
| `routing_id` | 피어의 라우팅 아이덴티티. |
| `weight` | 현재 peer 가중치 (`0..100`). `0`은 provider를 새 outbound 후보에서 제외함을 뜻하고, 양수 값은 후보에 남되 값 비율에 맞게 더 자주 또는 덜 자주 선택됨을 뜻한다. |
| `value` | 서비스별 숫자 값. |

---

### zlink_registry_member_peers

로컬 Registry에서 서비스의 멤버 피어 항목을 가져옵니다.

```c
zlink_config_result_t zlink_registry_member_peers(void *registry,
                                                  const char *channel_name,
                                                  zlink_member_peer_entry_t *entries,
                                                  size_t *count);
```

주어진 channel 이름에 일치하는 멤버 피어 항목을 `entries`에 채웁니다. 입력 시
`*count`는 배열 용량이고, 출력 시 실제 개수입니다. 필요한 개수를 먼저
조회하려면 `entries = NULL`을 전달합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### zlink_registry_member_peer_metadata

로컬 Registry에서 특정 멤버 피어의 메타데이터를 가져옵니다.

```c
zlink_config_result_t zlink_registry_member_peer_metadata(void *registry,
                                                          const char *channel_name,
                                                          zlink_service_role_t service_role,
                                                          const char *endpoint,
                                                          zlink_msg_t *metadata_out);
```

channel 이름, 역할, 엔드포인트로 식별되는 멤버 피어의 메타데이터를 조회합니다.
메타데이터는 `metadata_out`에 기록됩니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### zlink_discovery_member_peers

로컬 Discovery 인스턴스에서 멤버 피어 항목을 가져옵니다.

```c
zlink_config_result_t zlink_discovery_member_peers(void *discovery,
                                                   zlink_member_peer_entry_t *entries,
                                                   size_t *count);
```

Discovery 인스턴스가 알고 있는 모든 멤버 피어 항목을 `entries`에 채웁니다.
입력 시 `*count`는 배열 용량이고, 출력 시 실제 개수입니다. 필요한 개수를
먼저 조회하려면 `entries = NULL`을 전달합니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.

---

### zlink_discovery_member_peer_metadata

로컬 Discovery 인스턴스에서 특정 멤버 피어의 메타데이터를 가져옵니다.

```c
zlink_config_result_t zlink_discovery_member_peer_metadata(void *discovery,
                                                           uint16_t service_role,
                                                           const char *endpoint,
                                                           zlink_msg_t *metadata_out);
```

역할과 엔드포인트로 식별되는 멤버 피어의 메타데이터를 조회합니다.
메타데이터는 `metadata_out`에 기록됩니다.

**반환값:** `zlink_config_result_t` 값을 반환합니다.

**스레드 안전성:** 모든 스레드에서 호출할 수 있습니다.
