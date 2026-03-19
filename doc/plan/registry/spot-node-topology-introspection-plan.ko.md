# Service Monitoring Snapshot / Introspection 계획

## 1. 목적

이 문서는 운영 모니터링 관점에서
`spot_node`, `gateway`, `registry` 계열의 snapshot / introspection API를
정의하는 계획 문서다.

현재 공개 surface에는 다음이 이미 있다.

- registry 전역 summary:
  `zlink_registry_topology_snapshot()`,
  `zlink_registry_topology_query()`
- gateway peer summary:
  `zlink_registry_gateway_peers_snapshot()`,
  `zlink_registry_gateway_peers_query()`
- monitor 계열 event/snapshot surface:
  canonical naming은
  `zlink_socket_monitor_open()`,
  `zlink_service_monitor_open()`,
  `zlink_monitor_snapshot()`
  기준으로 재정렬 예정

하지만 사용자 관점에서 다음 질문에 바로 답하는 snapshot API는 없다.

- 이 `spot_node`가 전체적으로 healthy 한가
- configured/active/connected peer 수가 몇 개인가
- 이 `spot_node`가 현재 어떤 peer를 알고 있는가
- 그 peer가 manual인지 discovery 유입인지
- configured 상태인지, active connect 대상인지, 실제 connected인지
- 어떤 `SUB` subject/topic/pattern이 현재 전달 준비 상태인지

핵심 목표:

- 운영 모니터링에서 바로 쓸 수 있는 node-level summary API를 먼저 제공한다.
- `spot_node`에서 직접 읽을 수 있는 로컬 토폴로지 snapshot API를 제공한다.
- registry global summary와 local node detail의 역할을 분리한다.
- 내부 구현 set/map를 그대로 노출하지 않고,
  사용자가 실제로 필요한 개념 모델을 제공한다.
- 저장소의 공개 thread-safe 계약과 충돌하지 않는 조회 API를 정의한다.
- 같은 운영 관점에서 `gateway`와 `registry` 집계 surface의 보완 방향도 함께 정리한다.

연관 문서:

- [`registry-topology-introspection-plan.ko.md`](../direct-callback-recv/registry-topology-introspection-plan.ko.md)
- [`spot-node-direct-facade-plan.ko.md`](../direct-callback-recv/spot-node-direct-facade-plan.ko.md)
- [`service-monitor-readiness-plan.ko.md`](../direct-callback-recv/service-monitor-readiness-plan.ko.md)
- [`monitor-public-surface-reinterface-plan.ko.md`](../direct-callback-recv/re-interface/monitor-public-surface-reinterface-plan.ko.md)

의존 관계:

- monitor public naming과 lifecycle은
  `monitor-public-surface-reinterface-plan.ko.md`를 canonical source로 따른다.
- 이 문서의 monitor 관련 설명은
  `socket monitor` / `service monitor` 구분을 전제로 한다.
- monitor reinterface가 먼저 적용되면,
  이 문서에서 말하는 monitor는 모두 새 canonical public surface를 뜻한다.
- reinterface 이전 컨텍스트에서 구현을 시작하더라도,
  새 API naming과 문서 설명은 reinterface 이후 surface에 맞춰 유지한다.

## 2. 문제 정의

### 2.1 지금 있는 정보

현재 `spot_node_t` 내부에는 공개 API를 구성하기에 충분한 상태가 이미 있다.

- `_manual_peer_endpoints`
- `_discovery_peer_endpoints`
- `_active_peer_endpoints`
- `_connected_peer_endpoints`
- `_bound_endpoint`
- `_advertise_endpoint`
- `_service_name`
- `_discovery_service`
- `_pub_delivery_ready_sources`
- `spot_sub_t`의 `_topics`, `_patterns`, `_ready_peer_endpoints`

또한 `spot_sub_t`는 이미 다음 성격의 local summary를 계산한다.

- raw filter 목록
- subject 목록
- subscription ready event
- delivery ready changed event

즉 문제는 "데이터가 없음"이 아니라
"공개 계약으로 어떤 수준까지 안정화할 것인가"다.

### 2.2 지금 없는 것

현재 사용자는 service monitor event를 직접 소비하거나
registry global summary를 우회해서 다음 상태를 재구성해야 한다.

- local peer graph
- manual/discovery source 구분
- configured/connecting/connected/lost 같은 node-local peer 상태
- node-local subject readiness snapshot

이 방식은 다음 단점이 있다.

- service monitor는 event stream이라 현재 상태를 얻기 어렵다.
- registry는 global summary라 local runtime detail을 대체할 수 없다.
- 내부 set 단위 helper를 여러 개 내놓으면 shallow wrapper가 늘어난다.

또한 운영 관점에서 다음 공백도 있다.

- `gateway`는 route row 없이 먼저 건강 상태를 보는 coarse summary가 없다.
- `registry`는 service별 aggregate 없이 전체 topology row를 직접 다시 집계해야 한다.
- `registry` 프로세스 자체 health를 한 번에 보여주는 status snapshot이 없다.

## 3. 설계 원칙

### 3.1 registry와 spot_node는 관점이 다르다

역할 분리는 아래처럼 고정한다.

```text
registry = global coarse summary
spot_node query = local topology snapshot
service monitor = transition/event detail
```

registry는 프로세스 경계를 넘는 전역 summary를 담당한다.

`spot_node` query는 특정 프로세스 안의 한 node가
현재 무엇을 알고 있고 무엇과 연결돼 있는지를 보여준다.

### 3.2 내부 컬렉션을 그대로 공개하지 않는다

다음 같은 API는 만들지 않는다.

- `manual_peer_endpoints_snapshot`
- `active_peer_endpoints_snapshot`
- `connected_peer_endpoints_snapshot`
- `discovery_peer_endpoints_snapshot`

이런 API는 사용자가 의미 있는 상태를 다시 조합해야 하므로
POSD 관점에서 shallow wrapper다.

대신 사용자 질문에 바로 대응하는 깊은 모델을 제공한다.

- node status summary
- peer entry snapshot
- subject readiness snapshot

### 3.3 query API는 snapshot semantics를 유지한다

새 API는 기존 registry query와 같은 호출 규약을 따른다.

- `count == NULL`이면 `EINVAL`
- `entries == NULL`일 때 필요한 개수를 `count`에 반환
- caller가 버퍼를 제공하면 최대 `*count`개까지 복사
- 실제 반환 개수를 `count`에 기록
- snapshot 시점은 함수 내부에서 단일 논리 시점이어야 한다

버퍼 규약:

- `entries != NULL`인데 `*count == 0`이면 항목을 복사하지 않고 필요한 개수만 반환한다.
- caller 버퍼가 부족해도 실패로 보지 않고 잘린 결과를 복사한 뒤 전체 필요한 개수를 `count`에 기록한다.
- 이 규약은 기존 snapshot 계열 C API와 동일해야 한다.

추가 계약:

- 조회 API는 공개 thread-safe 계약상
  "설정·운영(control-path serialized)" 카테고리에 속한다.
- 같은 handle에 대해 여러 스레드가 동시에 호출해도 안전해야 한다.
- send/publish hot path와 동시 실행돼도 안전해야 한다.
- destroy와 경쟁 시 크래시 대신
  `EBUSY`, `ESHUTDOWN`, `EALREADY` 같은 기존 lifecycle 규칙으로 수렴해야 한다.

적용 범위:

- `spot_node`
- `gateway`
- `registry`

### 3.4 local API는 endpoint 중심 identity를 쓴다

registry 전역 summary는 representative `routing_id` 중심 identity가 맞다.
반면 `spot_node` 로컬 peer 관찰은 endpoint 중심 identity가 더 적합하다.

이유:

- manual connect API가 endpoint를 직접 사용한다.
- discovery가 제공하는 peer 후보도 endpoint 집합으로 관리된다.
- `_active_peer_endpoints`, `_connected_peer_endpoints`가 endpoint 기반이다.

따라서 peer query의 primary identity는 `peer_endpoint`로 둔다.

## 4. 공개 API 범위

본 문서는 세 계층의 API를 제안한다.

1. `spot_node` node-level status summary
2. `spot_node` peer topology snapshot/query
3. `spot_node` subject readiness snapshot/query

또한 같은 운영 관점에서 다음 보완 방향을 함께 제안한다.

4. `gateway` node-level status summary
5. `registry` process-level status summary
6. `registry` service aggregate summary

우선순위는 node summary가 먼저다.

이유:

- 운영자는 먼저 "이 node가 건강한가"를 본다.
- 그 다음 "어느 peer가 문제인가"를 본다.
- 마지막으로 "어떤 subject가 준비되지 않았는가"를 본다.

즉 권장 관찰 순서는 아래와 같다.

```text
node summary -> peer snapshot -> subject snapshot
```

v1 scope 고정:

- `spot_node`: `status`, `peers`, `SUB subjects`
- `gateway`: `status`만 추가
- `registry`: `status`, `service_summary`만 추가
- 기존 row API:
  `registry_topology_snapshot/query`,
  `registry_gateway_peers_snapshot/query`
  는 유지하고 재사용한다

v1에서 의도적으로 제외:

- `PUB subject` snapshot
- 별도 `gateway routes snapshot` 신규 API
- 별도 `registry peer-registry rows` 신규 API

## 5. Node Status Summary API 설계

### 5.1 목적

이 API는 운영 모니터링의 1차 진입점이다.

사용자는 이 API 하나로 다음을 빠르게 판단할 수 있어야 한다.

- 현재 node가 bind/advertise 상태를 가졌는가
- peer 구성이 전혀 없는가, connect 중인가, 일부라도 connected인가
- subject가 하나도 없는가, 일부라도 ready 한가
- 최근 error가 있었는가

이 API는 세부 원인을 모두 담지 않는다.
대신 "이상이 있는지"를 빠르게 판별하는 1-row summary를 제공한다.

### 5.2 enum

```c
typedef enum zlink_spot_node_state_t
{
    ZLINK_SPOT_NODE_STATE_IDLE = 1,
    ZLINK_SPOT_NODE_STATE_CONNECTING = 2,
    ZLINK_SPOT_NODE_STATE_PARTIAL_READY = 3,
    ZLINK_SPOT_NODE_STATE_READY = 4,
    ZLINK_SPOT_NODE_STATE_ERROR = 5
} zlink_spot_node_state_t;
```

의미:

- `IDLE`: bind 또는 peer/subscription 구성이 아직 충분하지 않음
- `CONNECTING`: peer는 있으나 connected peer가 없음
- `PARTIAL_READY`: 일부 peer 또는 일부 subject만 ready
- `READY`: 운영 기준으로 필요한 peer/subject가 준비됨
- `ERROR`: 마지막 오류가 남아 있고 정상 상태로 회복되지 않음

중요:

- 이 enum은 운영 summary용 coarse state다.
- peer별 상세 상태와 1:1 대응하지 않는다.
- service monitor event의 세부 원인을 덮어쓰지 않는다.

### 5.3 entry

```c
typedef struct zlink_spot_node_status_t
{
    char service_name[256];
    char local_endpoint[256];
    zlink_routing_id_t node_routing_id;
    zlink_spot_node_state_t state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_spot_node_status_t;
```

필드 의미:

- `service_name`: 운영 화면에서의 logical service 식별자
- `local_endpoint`: advertise endpoint 우선, 없으면 bound endpoint, 둘 다 없으면 빈 문자열
- `node_routing_id`: 동일 service 내 node instance 식별자
- `state`: node-level coarse state
- `configured_peer_count`: manual/discovery 기준으로 알려진 peer 수
- `active_peer_count`: 현재 active connect 대상 수
- `connected_peer_count`: 현재 실제 connected peer 수
- `subject_count`: 전체 `SUB` subject 수
- `ready_subject_count`: ready peer를 1개 이상 가진 `SUB` subject 수
- `last_error`: 마지막 운영상 의미 있는 오류 코드, 없으면 0
- `last_changed_ms`: summary state가 마지막으로 바뀐 시각

### 5.4 함수

```c
ZLINK_EXPORT int zlink_spot_node_status_snapshot (
  void *node,
  zlink_spot_node_status_t *out);
```

함수 규약:

- `out == NULL`이면 `EINVAL`
- 성공 시 단일 row를 채운다
- 이 API는 monitoring scrape의 1차 진입점으로 권장한다

### 5.5 상태 계산 규칙

권장 계산 순서:

1. fatal 또는 sticky error가 있으면 `ERROR`
2. configured peer == 0 이고 subject == 0 이면 `IDLE`
3. active peer > 0 이고 connected peer == 0 이면 `CONNECTING`
4. connected peer > 0 이고 ready subject < subject 이면 `PARTIAL_READY`
5. connected peer > 0 이고 ready subject == subject 이면 `READY`
6. 그 외 나머지는 `IDLE` 또는 `PARTIAL_READY`로 수렴

중요:

- `READY`는 "모든 시스템이 완벽하다"가 아니라 운영용 coarse readiness를 뜻한다.
- subject가 하나도 없을 때 `READY`로 올리지 않는다.
- 이 상태 계산은 문서화된 정책이어야 하며, 구현이 임의로 drift 하면 안 된다.

## 6. Peer Topology API 설계

### 6.1 enum

```c
typedef enum zlink_spot_peer_source_t
{
    ZLINK_SPOT_PEER_SOURCE_MANUAL = 1,
    ZLINK_SPOT_PEER_SOURCE_DISCOVERY = 2,
    ZLINK_SPOT_PEER_SOURCE_MIXED = 3
} zlink_spot_peer_source_t;

typedef enum zlink_spot_peer_state_t
{
    ZLINK_SPOT_PEER_STATE_CONFIGURED = 1,
    ZLINK_SPOT_PEER_STATE_CONNECTING = 2,
    ZLINK_SPOT_PEER_STATE_CONNECTED = 3
} zlink_spot_peer_state_t;
```

의미:

- `MANUAL`: manual set에만 존재
- `DISCOVERY`: discovery set에만 존재
- `MIXED`: manual과 discovery 양쪽에서 동시에 참조

state 의미:

- `CONFIGURED`: peer 후보로 알고 있으나 현재 active connect 대상은 아님
- `CONNECTING`: active set에는 있으나 connected set에는 없음
- `CONNECTED`: active set과 connected set에 모두 존재

중요:

- v1 snapshot은 현재 상태만 표현한다.
- 과거 상태인 `LOST`는 snapshot enum에 넣지 않는다.
- peer down 이력은 monitor event가 담당한다.
- 추후 이력 기반 read model이 필요하면 별도 enum/entry 확장으로 다룬다.

### 6.2 entry

```c
typedef struct zlink_spot_node_peer_entry_t
{
    char service_name[256];
    char local_endpoint[256];
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
} zlink_spot_node_peer_entry_t;
```

필드 의미:

- `service_name`: node의 public service name
- `local_endpoint`: advertise endpoint 우선, 없으면 bound endpoint, 둘 다 없으면 빈 문자열
- `peer_endpoint`: peer PUB endpoint
- `source`: manual/discovery/mixed
- `state`: 현재 node-local connection state
- `connected_since_ms`: connected 상태 진입 시각, 모르면 0
- `last_changed_ms`: source/state가 마지막으로 바뀐 시각

### 6.3 filter

```c
typedef struct zlink_spot_node_peer_filter_t
{
    char peer_endpoint[256];
    zlink_spot_peer_source_t source;
    zlink_spot_peer_state_t state;
} zlink_spot_node_peer_filter_t;
```

필터 규칙:

- 빈 문자열 endpoint는 wildcard
- `source == 0`이면 wildcard
- `state == 0`이면 wildcard

### 6.4 함수

```c
ZLINK_EXPORT int zlink_spot_node_peers_snapshot (
  void *node,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);

ZLINK_EXPORT int zlink_spot_node_peers_query (
  void *node,
  const zlink_spot_node_peer_filter_t *filter,
  zlink_spot_node_peer_entry_t *entries,
  size_t *count);
```

### 6.5 상태 계산 규칙

입력 집합:

- manual set = `_manual_peer_endpoints`
- discovery set = `_discovery_peer_endpoints`
- active set = `_active_peer_endpoints`
- connected set = `_connected_peer_endpoints`

계산 대상 peer universe:

```text
manual U discovery
```

source 계산:

- manual only -> `MANUAL`
- discovery only -> `DISCOVERY`
- both -> `MIXED`

불변식:

- v1 구현은 `source`를 현재 membership만으로 결정할 수 있는 entry만 반환한다.
- 즉 peer universe는 `manual U discovery`를 기본으로 한다.
- `active`와 `connected`는 상태 계산에만 사용한다.
- 만약 `active`나 `connected`에만 존재하는 endpoint가 관측되면
  이는 공개 API에서 추정 복구하지 않고 내부 일관성 버그로 본다.
- 구현은 debug assert 또는 내부 로그를 남기고,
  release 동작에서는 entry를 노출하지 않는다.

state 계산:

- connected set에 있으면 `CONNECTED`
- connected는 아니고 active set에 있으면 `CONNECTING`
- active는 아니지만 manual/discovery set에는 있으면 `CONFIGURED`

### 6.6 왜 endpoint 중심인가

이 API는 다음 사용자 흐름에 맞춰야 한다.

```c
zlink_spot_node_connect_peer_pub(node, "tcp://10.0.0.10:7001");
zlink_spot_node_peers_snapshot(node, ...);
```

사용자는 동일 endpoint가 현재

- manual source인지
- discovery에서도 유지 중인지
- 실제 connected인지

를 바로 확인하고 싶다.

이 문제는 routing id보다 endpoint가 훨씬 직접적이다.

## 7. Subject Readiness API 설계

### 7.1 목표

peer query가 "누구와 연결됐는가"를 보여준다면,
subject query는 "무엇이 전달 준비 상태인가"를 보여준다.

이 API는 다음 질문에 답해야 한다.

- 이 node의 sub는 어떤 topic/pattern을 가지고 있는가
- 각 subject가 몇 개 peer에 대해 ready인가

v1 범위에서는 위 질문을 `SUB` subject에 한정한다.

### 7.2 enum

기존 `zlink_spot_role_t`와
`zlink_service_event_subject_kind_t`를 재사용한다.

새 enum은 추가하지 않는다.

### 7.3 entry

```c
typedef struct zlink_spot_node_subject_entry_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
} zlink_spot_node_subject_entry_t;
```

필드 의미:

- `role`: v1에서는 항상 `SUB`
- `subject`: topic 또는 pattern 문자열
- `subject_kind`: topic / pattern
- `ready_peer_count`: 현재 준비된 peer 수
- `active_peer_count`: node가 알고 있는 active peer 수
- `last_changed_ms`: readiness가 마지막으로 바뀐 시각

### 7.4 filter

```c
typedef struct zlink_spot_node_subject_filter_t
{
    zlink_spot_role_t role;
    char subject[256];
    uint32_t subject_kind;
} zlink_spot_node_subject_filter_t;
```

### 7.5 함수

```c
ZLINK_EXPORT int zlink_spot_node_subjects_snapshot (
  void *node,
  const zlink_spot_node_subject_filter_t *filter,
  zlink_spot_node_subject_entry_t *entries,
  size_t *count);
```

v1에서는 `snapshot` 하나만 두고,
별도 `query` 함수는 filter 인자를 포함한 단일 함수로 단순화할 수 있다.

이유:

- peer query보다 사용 빈도가 낮다.
- 초기 설계에서 API 수를 늘릴 필요가 없다.
- filter가 optional이면 snapshot/query를 한 함수로 합칠 수 있다.

### 7.6 v1 범위

v1 subject query는 `SUB` 중심으로 시작하는 것이 안전하다.

노출 대상:

- `spot_sub_t::append_all_subjects()` 기반 subject 목록
- 각 subject의 ready peer count
- node active peer count

중요:

- v1의 `zlink_spot_node_subjects_snapshot()`는 `SUB` 역할만 지원한다.
- `filter == NULL`이면 전체 `SUB` subject snapshot으로 해석한다.
- filter에서 `role == ZLINK_SPOT_ROLE_PUB`를 요청하면 `ENOTSUP`로 실패한다.
- 문서와 구현 모두 이 제한을 명시해야 한다.

`PUB` readiness는 `_pub_delivery_ready_sources` 의미가
"어떤 peer가 어떤 subject에 ack를 보냈는가"에 묶여 있으므로
subject-level 공개 contract를 더 신중히 정리해야 한다.

따라서 단계 계획은 아래가 권장된다.

1. v1: `SUB` subject snapshot만 공개
2. v2: `PUB` subject delivery-ready snapshot 추가

## 8. 데이터 모델과 내부 매핑

### 8.1 peer query 구현에 필요한 최소 내부 확장

정확한 `connected_since_ms`와 `last_changed_ms`를 주려면
현재 set만으로는 부족하다.

최소 확장 구조:

```c++
struct peer_observation_t
{
    uint64_t last_changed_ms;
    uint64_t connected_since_ms;
    bool was_connected;
};

std::map<std::string, peer_observation_t> _peer_observations;
```

업데이트 시점:

- manual connect/disconnect
- discovery refresh로 active set 변경
- connected set refresh 변경

v1 단순 구현에서는 아래도 허용 가능하다.

- `connected_since_ms = 0`
- `last_changed_ms = 0`

하지만 설계 문서 기준 권장안은 timestamp cache를 두는 것이다.

thread-safe 조건:

- `_peer_observations` 갱신은 항상 `_sync` 아래에서 수행한다.
- 조회 함수는 `_sync` 아래에서 snapshot용 복사본을 만들고 lock을 해제한 뒤
  외부 버퍼에 복사한다.
- 외부 버퍼 복사 중에는 node 내부 lock을 오래 잡지 않는다.

### 8.2 subject query 구현에 필요한 최소 내부 확장

`spot_sub_t`는 subject 목록과 ready peer set을 이미 관리한다.
다만 `last_changed_ms`를 내보내려면 subject별 시각 캐시가 필요하다.

최소 확장 구조 예:

```c++
std::map<std::string, uint64_t> _subject_last_changed_ms;
```

key는 다음 normalized 문자열을 사용한다.

```text
<subject_kind>:<subject>
```

이미 `spot_sub.cpp`에 유사 helper 패턴이 있으므로
같은 canonical key를 재사용하는 편이 좋다.

중요한 제약:

- `spot_sub_t`는 내부 child handle이므로 외부 공개 thread-safe 계약의 직접 주체가 아니다.
- 따라서 subject snapshot 구현은 child handle에 새 public C API를 늘리기보다
  `spot_node_t`가 내부 helper를 통해 요약을 수집하는 방향이어야 한다.
- 공개 계약은 끝까지 `spot_node` handle 기준으로 유지한다.

### 8.3 node summary 구현에 필요한 최소 내부 확장

운영용 summary를 제대로 만들려면 다음 캐시가 필요하다.

```c++
int _last_summary_error;
uint64_t _summary_last_changed_ms;
```

권장 업데이트 시점:

- bind 성공/실패
- manual connect/disconnect
- discovery attach 및 refresh
- connected peer set 변화
- subscription add/remove
- node fault mark

v1 단순 구현에서는 `last_error = 0`,
`last_changed_ms = 0`으로 시작할 수 있다.
하지만 운영 API 목적상 가능한 빠르게 실제 값을 채우는 편이 좋다.

### 8.4 gateway status 구현에 필요한 최소 내부 확장

`gateway_status_snapshot()`은 단순히 기존 row를 다시 포장하는 API가 아니다.
운영 summary에 필요한 coarse aggregate를 안정적으로 계산해야 한다.

최소 구현 출처:

- `service_name`: gateway가 소비 중인 logical service 이름
- `gateway_routing_id`: gateway 대표 routing id
- `ready_provider_count`: 현재 send snapshot 또는 monitor 기준 ready provider 수
- `active_route_count`: 현재 control snapshot 또는 send snapshot 기준 route 수
- `send_ready`: 현재 gateway send-ready 여부
- `last_error`: gateway가 마지막으로 기록한 운영상 의미 있는 오류

권장 보완:

```c++
int _last_summary_error;
uint64_t _summary_last_changed_ms;
```

주의:

- `desired_provider_count`는 현재 구현에서 항상 자명하지 않을 수 있다.
- discovery 기반 gateway에서는 "이상적으로 몇 개 provider가 있어야 하는가"보다
  "현재 몇 개를 알고 있고 몇 개가 ready 인가"가 더 명확하다.
- 따라서 구현 단계에서는 `desired_provider_count`를 유지할지,
  `observed_provider_count`로 바꿀지 별도 결정이 필요하다.

### 8.5 registry status / service summary 구현에 필요한 최소 내부 확장

`registry_status_snapshot()`과
`registry_service_summary_snapshot()`은 현재 registry 내부 row store에서
계산 가능해야 한다.

필드별 권장 계산 출처:

- `topology_entry_count`: `_topology.size()`
- `gateway_peer_entry_count`: `_gateway_peers.size()`
- `peer_registry_count`: peer registry 메타 store 크기
- `connected_peer_registry_count`: peer sync 소켓이 현재 연결된 peer registry 수
- `list_seq`: `_list_seq`
- `service summary`: `service_kind + service_name` 기준으로 `_topology` row group-by

권장 보완:

```c++
int _last_summary_error;
uint64_t _summary_last_changed_ms;
```

중요:

- 문서에 나온 모든 status/summary 필드는
  "어디서 계산되는가"가 구현 전에 설명 가능해야 한다.
- 계산 출처가 불명확한 필드는 v1에서 빼거나 이름을 더 정확히 바꿔야 한다.

### 8.6 파일별 구현 매핑

새 컨텍스트에서 바로 구현을 시작할 때 기준이 되는 파일 매핑은 아래와 같다.

- public C 선언:
  `core/include/zlink.h`
- public C 엔트리포인트:
  `core/src/api/zlink.cpp`
- `spot_node` 구현:
  `core/src/services/spot/spot_node.hpp`
  `core/src/services/spot/spot_node.cpp`
- `gateway` 구현:
  `core/src/services/gateway/gateway.hpp`
  `core/src/services/gateway/gateway.cpp`
- `registry` 구현:
  `core/src/services/discovery/registry.hpp`
  `core/src/services/discovery/registry.cpp`
- 테스트 추가 위치:
  `core/tests/unittest/`
  `core/tests/integration/`
  필요 시 `core/tests/e2e/`

권장 구현 순서:

1. `core/include/zlink.h`에 struct/function 선언 추가
2. `core/src/api/zlink.cpp`에 admission + null check + handle dispatch 추가
3. 각 서비스 클래스에 internal snapshot helper 추가
4. 단위 테스트 추가
5. 통합 테스트 추가

## 9. monitor와의 관계

이 절은
[`monitor-public-surface-reinterface-plan.ko.md`](../direct-callback-recv/re-interface/monitor-public-surface-reinterface-plan.ko.md)
기준의 canonical monitor public surface를 전제로 한다.

즉 본 문서에서 말하는 monitor는 다음 둘을 뜻한다.

- `socket monitor`
- `service monitor`

새 snapshot API는 monitor를 대체하지 않는다.

service monitor가 계속 맡아야 할 것:

- `PEER_UP`, `PEER_DOWN`
- `SUB_FILTER_APPLIED`
- `SUBSCRIPTION_READY`
- `PUB_DELIVERY_READY_CHANGED`
- `ERROR`

snapshot API가 맡을 것:

- 현재 node-level coarse health
- 현재 peer 목록
- 현재 `SUB` subject 목록
- 현재 ready count

즉 역할 분리는 아래와 같다.

```text
service monitor event stream = 무엇이 언제 바뀌었는가
snapshot = 지금 상태가 무엇인가
```

추가로:

- socket-level bind/connect/disconnect/handshake 진단은 `socket monitor`가 맡는다.
- service-level readiness/provider/peer/subscription 상태 변화는
  `service monitor`가 맡는다.
- 이 문서의 `status/peers/subjects` snapshot API는
  service monitor의 현재 상태 재구성 부담을 줄이는 read model이다.

운영 권장 사용 순서:

1. `zlink_spot_node_status_snapshot()`으로 이상 node 탐지
2. `zlink_spot_node_peers_snapshot()`으로 peer drill-down
3. `zlink_spot_node_subjects_snapshot()`으로 subject drill-down

## 10. registry와의 관계

새 API는 registry topology를 대체하지 않는다.

registry에 남겨야 할 것:

- service-level global summary
- process 밖에서도 볼 수 있는 정보
- cluster 운영자의 1차 진단 정보

spot_node local query에 남겨야 할 것:

- 현재 process 안의 node-level health summary
- 현재 process 안의 peer wiring 상태
- local subject readiness
- internal attachment aggregate

중요:

- registry는 cluster-wide summary다.
- `zlink_spot_node_status_snapshot()`은 process-local operational summary다.
- 둘은 경쟁 관계가 아니라 상보 관계다.

중요:

- `spot_node` 자체를 registry public subject로 추가하지 않는다.
- registry에는 계속 `SpotPub` / `SpotSub` 단위 summary가 올라간다.
- node query는 local facade/attachments를 묶어 보여주는 read model일 뿐이다.

### 10.1 registry/gateway 운영 관점 보완점

현재 `zlink_registry_topology_snapshot()`과
`zlink_registry_gateway_peers_snapshot()`은 row drill-down에는 유용하다.
하지만 운영 대시보드의 1차 진입점으로 쓰기에는 coarse aggregate가 부족하다.

부족한 점:

- registry 프로세스 자체가 healthy 한지 한 번에 보기 어렵다.
- 특정 service가 cluster-wide로 healthy 한지 바로 판단하기 어렵다.
- gateway 로컬 상태를 peer row 없이 먼저 판단하기 어렵다.
- `zlink_monitor_snapshot()`은 monitor handle 기준의
  low-level queue/peer 수 중심이라
  운영 summary와 직접 대응되지 않는다.

권장 보완 방향:

- `spot_node`에는 local `status -> peers -> subjects`
- `gateway`에는 local `status -> routes`
- `registry`에는 global `status -> service_summary -> topology/peer_rows`

즉 운영 관찰 흐름을 계층화해야 한다.

### 10.2 gateway status summary 제안

운영자가 gateway에 대해 먼저 알고 싶은 것은 다음이다.

- 이 gateway가 어떤 service를 보고 있는가
- provider가 전혀 없는가, 일부라도 ready 인가
- route가 몇 개인가
- send 가능한 상태인가
- 최근 error가 있는가

권장 API:

```c
typedef enum zlink_gateway_state_t
{
    ZLINK_GATEWAY_STATE_IDLE = 1,
    ZLINK_GATEWAY_STATE_CONNECTING = 2,
    ZLINK_GATEWAY_STATE_PARTIAL_READY = 3,
    ZLINK_GATEWAY_STATE_READY = 4,
    ZLINK_GATEWAY_STATE_ERROR = 5
} zlink_gateway_state_t;

typedef struct zlink_gateway_status_t
{
    char service_name[256];
    char bind_endpoint[256];
    zlink_routing_id_t gateway_routing_id;
    zlink_gateway_state_t state;
    uint32_t observed_provider_count;
    uint32_t ready_provider_count;
    uint32_t active_route_count;
    uint32_t send_ready;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_gateway_status_t;

ZLINK_EXPORT int zlink_gateway_status_snapshot (
  void *gateway,
  zlink_gateway_status_t *out);
```

기존 API와의 관계:

- `zlink_service_monitor_open()`은 service-level event stream 유지
- `zlink_monitor_snapshot()`은 low-level socket/queue snapshot 유지
- `zlink_gateway_status_snapshot()`은 운영용 coarse state 제공

필드 해석:

- `observed_provider_count`: discovery 또는 manual route 기준 현재 관측된 provider 수
- `ready_provider_count`: 그중 실제 send-ready 로 간주 가능한 provider 수

중요:

- 운영용 1차 판단에는 `observed_provider_count`와 `ready_provider_count`가 더 중요하다.

운영 사용 순서:

1. `zlink_gateway_status_snapshot()`
2. 필요하면 `zlink_registry_gateway_peers_snapshot()` 또는 service monitor

### 10.3 registry process status summary 제안

운영자가 registry 프로세스에 대해 먼저 알고 싶은 것은 다음이다.

- peer sync가 살아 있는가
- topology row 총량이 어느 정도인가
- gateway peer row 총량이 어느 정도인가
- peer registry 수가 몇 개인가
- 최근 stale/GC/error 징후가 있는가

권장 API:

```c
typedef enum zlink_registry_state_t
{
    ZLINK_REGISTRY_STATE_IDLE = 1,
    ZLINK_REGISTRY_STATE_ACTIVE = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR = 4
} zlink_registry_state_t;

typedef struct zlink_registry_status_t
{
    uint32_t registry_id;
    char bind_endpoint[256];
    zlink_registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t gateway_peer_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_registry_status_t;

ZLINK_EXPORT int zlink_registry_status_snapshot (
  void *registry,
  zlink_registry_status_t *out);
```

이 API는 "registry 프로세스가 건강한가"를 빠르게 보여주는 용도다.
기존 topology row를 대체하지 않는다.

### 10.4 registry service aggregate summary 제안

운영 대시보드에서 가장 자주 필요한 것은
"서비스별로 몇 개 인스턴스가 ready 인가"다.

현재 `zlink_registry_topology_snapshot()`만으로도 계산은 가능하지만,
호출자가 매번 전체 row를 다시 집계해야 한다.
이건 운영 사용성 관점에서 비용이 크다.

권장 API:

```c
typedef struct zlink_registry_service_summary_entry_t
{
    zlink_service_kind_t service_kind;
    char service_name[256];
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;

typedef struct zlink_registry_service_summary_filter_t
{
    zlink_service_kind_t service_kind;
    char service_name[256];
} zlink_registry_service_summary_filter_t;

ZLINK_EXPORT int zlink_registry_service_summary_snapshot (
  void *registry,
  const zlink_registry_service_summary_filter_t *filter,
  zlink_registry_service_summary_entry_t *entries,
  size_t *count);
```

집계 기준:

- source별 row를 그대로 노출하지 않고 service 단위로 합산
- `service_kind + service_name` 기준 group-by
- topology entry state를 count로 합산

운영 사용 순서:

1. `zlink_registry_status_snapshot()`
2. `zlink_registry_service_summary_snapshot()`
3. `zlink_registry_topology_snapshot()` / `zlink_registry_gateway_peers_snapshot()`

### 10.5 운영 관점에서 기존 함수 개선 포인트

기존 함수 자체도 다음 개선이 있으면 더 쓸 만해진다.

- `zlink_registry_topology_entry_t`에 optional instance label 또는 node-local id를 넣을지 검토
- `zlink_registry_gateway_peer_entry_t`에 `last_error` 또는 route health reason을 넣을지 검토
- `zlink_registry_query_client_*`에도 `status/service_summary` 원격 조회를 같이 제공
- 문서에서 각 함수의 권장 사용 순서를 명시

중요:

- coarse summary API를 추가하더라도 기존 row API는 유지해야 한다.
- summary만 남기고 row drill-down을 제거하면 운영 진단 깊이가 떨어진다.

### 10.6 gateway / registry v1 구현 경계

문서 범위가 넓어졌지만 v1 구현 경계는 명확해야 한다.

`gateway`:

- 새로 추가하는 것은 `zlink_gateway_status_snapshot()` 하나다.
- route별 drill-down은 기존 `registry_gateway_peers_snapshot()`과
  service monitor를 우선 사용한다.

`registry`:

- 새로 추가하는 것은
  `zlink_registry_status_snapshot()`,
  `zlink_registry_service_summary_snapshot()` 두 개다.
- topology row와 gateway-peer row는 기존 API를 그대로 유지한다.

이 경계를 넘는 신규 row API 제안은 별도 문서로 분리한다.

## 11. API naming 판단

권장 naming:

- `zlink_spot_node_status_snapshot`
- `zlink_spot_node_peers_snapshot`
- `zlink_spot_node_peers_query`
- `zlink_spot_node_subjects_snapshot`
- `zlink_gateway_status_snapshot`
- `zlink_registry_status_snapshot`
- `zlink_registry_service_summary_snapshot`

피해야 할 naming:

- `zlink_spot_topology_snapshot`
- `zlink_spotnode_topology_query`
- `zlink_spot_node_mesh_snapshot`

이유:

- `spot_node` handle에 귀속된 API임이 명확해야 한다.
- `peers`, `subjects`처럼 조회 대상이 이름에서 바로 드러나야 한다.
- `topology` 하나로 뭉개면 peer와 subject가 섞여 shallow해진다.

## 12. 스레드 안정성과 일관성

### 12.1 공통 공개 API 가드

새 status/query API는 기존 공개 핸들 계약과 동일하게
`service_public_api_scope_t` admission을 사용한다.

이유:

- destroy와 query 동시 실행을 막아야 한다.
- snapshot 중 `_runtime`와 attachment 집합이 사라지면 안 된다.
- 저장소의 three-tier contract 상
  조회 API가 control path serialized 규칙을 따르도록 해야 한다.

적용 대상:

- `zlink_spot_node_status_snapshot()`
- `zlink_spot_node_peers_snapshot()`
- `zlink_spot_node_peers_query()`
- `zlink_spot_node_subjects_snapshot()`
- `zlink_gateway_status_snapshot()`
- `zlink_registry_status_snapshot()`
- `zlink_registry_service_summary_snapshot()`

반환 규칙:

- destroy가 이미 수락된 뒤 새 query가 진입하면 `ESHUTDOWN`
- destroy와 경합 중 close admission이 먼저 성공하면 query는 `ESHUTDOWN`
- query가 먼저 admitted된 상태에서 destroy가 진입하면 destroy는 `EBUSY`

### 12.2 공통 lock / fail-fast 원칙

기본 원칙:

- summary/query 구현은 internal state copy만 수행한다.
- blocking wait, retry loop, sleep 기반 일관성 보정은 금지한다.
- lock은 짧게 잡고 snapshot용 복사본을 만든 뒤 해제한다.
- 외부 버퍼 복사나 row 정렬 같은 후처리는 lock 밖에서 수행한다.
- destroy와 경합 시 fail-fast 해야 한다.

### 12.3 monitor surface와의 정합성

이 문서의 snapshot/status API는
monitor reinterface 이후 public surface와 충돌하지 않아야 한다.

정합성 규칙:

- monitor는 `socket monitor` / `service monitor` 두 축으로 설명한다.
- `spot_node`, `gateway`, `registry`의 운영 event stream은
  `zlink_service_monitor_open()` 기준으로 문서화한다.
- low-level queue/socket 계측은 계속 `zlink_monitor_snapshot()`과
  `socket monitor` 영역으로 둔다.
- 새 summary API가 monitor event 이름이나 lifecycle을 다시 복제하지 않는다.
- 구현 문서와 주석에서 legacy open 함수 이름을 새로운 canonical surface보다
  우선 개념으로 쓰지 않는다.

### 12.4 subject별 구현 메모

`spot_node`:

- `spot_node`의 `_sync`를 짧게 잡고 필요한 상태를 로컬 복사본으로 뽑는다.
- 이후 `spot_sub_t`별 상태 조회는 각 handle lock에서 수행한다.
- node lock을 잡은 채 child handle lock을 오래 잡지 않는다.
- child handle helper는 외부 콜백, send, monitor open 같은 public path를 호출하지 않는다.

`gateway`:

- route/control/send snapshot이 분리돼 있으면
  운영 summary는 각 snapshot의 읽기 전용 집계를 조합해 계산한다.
- hot path send 경로를 block 하는 broad lock을 두지 않는다.

`registry`:

- `_topology`, `_gateway_peers`, peer-registry 메타 store를
  읽기 전용으로 스냅샷한다.
- service summary group-by는 row 복사본 위에서 lock 밖에서 수행한다.

### 12.5 snapshot atomicity

peer snapshot은 하나의 `_sync` 구간에서
manual/discovery/active/connected 집합을 모두 복사하면 충분하다.

subject snapshot은 완전한 global atomicity를 보장하기 어렵다.
하지만 다음 정도면 충분하다.

- node가 관리 중인 `sub` handle 목록을 복사
- 각 `sub`에서 현재 subject/ready 상태를 읽음
- 함수 전체를 "best effort coherent local snapshot"으로 규정

이는 monitor/event detail이 아닌
운영용 조회 API라는 목적과 부합한다.

단, thread-safe 문서와 맞추기 위해 표현을 아래처럼 제한한다.

- "best effort"는 결과 의미를 약화하는 표현이 아니라
  cross-child 완전 선형화를 강제하지 않는다는 뜻이다.
- 각 child에서 읽은 개별 state는 lock으로 보호된 실제 값이어야 한다.
- 함수는 fail-fast 해야 하며 sleep/poll/retry로 snapshot 일관성을 맞추지 않는다.

## 13. 단계별 구현 계획

### 13.1 Phase 1

- `zlink.h`에 node status entry/function 추가
- `api/zlink.cpp`에 public C API 추가
- `spot_node_t`에 status snapshot helper 추가
- 최소 동작 구현:
  `service_name`, `local_endpoint`, `configured_peer_count`,
  `active_peer_count`, `connected_peer_count`, `subject_count`,
  `ready_subject_count`, `state`
- thread-safe 계약 문서와 API 주석 추가

이 단계가 운영 모니터링 기준의 첫 릴리즈 단위다.

### 13.2 Phase 2

- `zlink.h`에 gateway status entry/function 추가
- `api/zlink.cpp`에 public C API 추가
- `gateway_t`에 status snapshot helper 추가
- 최소 동작 구현:
  `service_name`, `observed_provider_count`, `ready_provider_count`,
  `active_route_count`, `send_ready`, `state`

### 13.3 Phase 3

- `zlink.h`에 registry status/service summary entry/function 추가
- `api/zlink.cpp`에 public C API 추가
- `registry_t`에 aggregate helper 추가
- 최소 동작 구현:
  `topology_entry_count`, `gateway_peer_entry_count`,
  `service_kind + service_name` group-by summary

### 13.4 Phase 4

- `zlink.h`에 peer entry/filter/function 추가
- `api/zlink.cpp`에 public C API 추가
- `spot_node_t`에 peer snapshot helper 추가
- 최소 동작 구현:
  `service_name`, `local_endpoint`, `peer_endpoint`, `source`, `state`
- thread-safe 계약 문서와 API 주석 추가

이 단계에서는 timestamp 필드를 `0`으로 둘 수 있다.

### 13.5 Fresh-Start Checklist

새 컨텍스트에서 이 작업을 시작할 때 최소 확인 항목:

1. `core/include/zlink.h`의 기존 snapshot/query 스타일 확인
2. `core/src/api/zlink.cpp`에서 유사 admission 패턴 확인
3. `spot_node`, `gateway`, `registry` 각 클래스의 public_api_guard 사용 위치 확인
4. `gateway`의 send/control snapshot 구조 확인
5. `registry`의 `_topology`, `_gateway_peers`, `_list_seq` store 확인
6. monitor public naming은
   `doc/plan/direct-callback-recv/re-interface/monitor-public-surface-reinterface-plan.ko.md`
   를 canonical source로 확인
7. 테스트 추가 시 `core/build/`만 사용한다는 저장소 규칙 확인

새 컨텍스트에서 바로 이해해야 할 선행 결정:

- monitor public surface는 `socket monitor` / `service monitor` 두 축으로 재정렬된다
- 이 문서의 monitor 관련 서술은 새 canonical naming을 기준으로 유지한다
- legacy `spot`/`gateway` 전용 monitor open 이름이 코드에 남아 있어도
  새 설계 설명의 기준은 reinterface 문서다

권장 기본 명령:

- configure:
  `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
- build:
  `cmake --build core/build`
- unit tests:
  `ctest --test-dir core/build --output-on-failure -L unittest -j$(nproc)`
- integration tests:
  `ctest --test-dir core/build --output-on-failure -L integration -j1`

구현 전에 이 문서에서 다시 확인할 핵심 결정:

- v1에서는 `PUB subject` snapshot을 넣지 않는다
- `gateway`는 status만 추가한다
- `registry`는 status/service_summary만 추가한다
- 모호한 필드는 v1에서 제거하거나 0으로 시작하지 않고, 가능하면 이름을 좁힌다

### 13.6 Phase 5

- peer observation timestamp cache 추가
- `connected_since_ms`, `last_changed_ms` 채우기
- peer transition unit/integration test 보강
- destroy/query 경쟁 테스트 추가

### 13.7 Phase 6

- `SUB` subject snapshot API 추가
- `spot_sub_t`에 summary helper 추가
- ready count snapshot 테스트 추가
- concurrent subscribe/query, disconnect/query 테스트 추가

### 13.8 Phase 7

- 필요하면 `PUB` subject readiness까지 확장
- `_pub_delivery_ready_sources`의 공개 의미를 문서화
- 과도하게 복잡하면 `PUB`는 별도 문서로 분리

## 14. 테스트 계획

### 14.1 node summary

- bind 전 상태에서 `IDLE` 검증
- peer 구성 후 connected peer가 없으면 `CONNECTING` 검증
- connected peer가 생기고 ready subject 일부만 있으면 `PARTIAL_READY` 검증
- ready subject가 전체와 같아지면 `READY` 검증
- node fault 후 `ERROR` 검증
- send/publish hot path와 status snapshot 동시 호출 검증
- destroy/status query 경쟁에서 `EBUSY`/`ESHUTDOWN` 규칙 검증

### 14.2 gateway status summary

- provider가 없을 때 `IDLE` 또는 `CONNECTING` 정책 검증
- ready provider가 일부만 있을 때 `PARTIAL_READY` 검증
- send-ready 변화가 summary state에 반영되는지 검증
- monitor snapshot과 status summary 의미 충돌이 없는지 검증

### 14.3 registry status / service summary

- topology row 증가/감소가 registry status count에 반영됨
- gateway peer row 증가/감소가 registry status count에 반영됨
- service summary가 `service_kind + service_name` 기준으로 올바르게 집계됨
- topology row drill-down과 service summary aggregate가 의미적으로 일치함

### 14.4 peer snapshot

- manual peer connect 후 snapshot에 `MANUAL + CONNECTING/CONNECTED`가 나타남
- discovery attach 후 peer가 `DISCOVERY` source로 나타남
- manual과 discovery가 같은 endpoint를 참조하면 `MIXED`
- disconnect 후 active set에서 제거되면 snapshot에서 state가 갱신됨
- `entries == NULL` count probe 동작 검증
- filter by endpoint/source/state 검증
- send/publish hot path와 peer query 동시 호출 검증
- destroy/query 경쟁에서 `EBUSY`/`ESHUTDOWN` 규칙 검증

### 14.5 subject snapshot

- topic subscribe 후 subject snapshot에 topic entry가 나타남
- pattern subscribe 후 `pattern` kind로 나타남
- active peer 없을 때 `ready_peer_count == 0`
- peer 연결 및 subscription replay 후 `ready_peer_count` 증가
- unsubscribe 후 subject snapshot에서 제거됨
- `role == PUB` 요청 시 `ENOTSUP` 검증
- subscribe/unsubscribe와 query 동시 호출 검증

### 14.6 lifecycle / fail-fast

- destroy 중 query는 안전하게 실패하거나 admission에서 차단됨
- null handle, bad count, undersized buffer 처리 검증
- retry/sleep 의존 없는 deterministic test 작성
- query 구현에 sleep 기반 일관성 보정이 없음을 코드 리뷰 체크리스트에 포함

## 15. Definition of Done

- `spot_node` local topology query의 역할이 registry global summary와 구분되어 문서화되어 있다.
- 운영용 1차 API로 `zlink_spot_node_status_snapshot()`이 정의되어 있다.
- 운영용 1차 API로 `zlink_gateway_status_snapshot()`이 정의되어 있다.
- 운영용 1차 API로 `zlink_registry_status_snapshot()`이 정의되어 있다.
- service aggregate 용도로 `zlink_registry_service_summary_snapshot()`이 정의되어 있다.
- peer snapshot API의 공개 계약이 `zlink.h` 수준으로 정의되어 있다.
- 내부 set 노출형 API 대신 peer/subject read model이 정의되어 있다.
- peer identity가 endpoint 중심이라는 이유가 설명되어 있다.
- subject readiness는 peer query보다 뒤의 drill-down read model로 정리되어 있다.
- monitoring 사용 순서가 `status -> peers -> subjects`로 정리되어 있다.
- registry/gateway도 monitoring 사용 순서가 `status -> summary -> rows`로 정리되어 있다.
- 단계별 구현 순서와 테스트 계획이 명시되어 있다.
- 공개 thread-safe 계약과 lifecycle 에러 규칙이 문서에 명시되어 있다.
