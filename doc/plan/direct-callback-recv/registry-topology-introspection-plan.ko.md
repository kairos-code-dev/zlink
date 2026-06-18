# Registry Topology / Introspection 계획

## 1. 목적

이 문서는 discovery와 registry 기반 자동 연결 상태를
registry 중심 summary로 조회하는 계획을 정의한다.

핵심 목표:

- 사용자가 `Gateway`, `SpotPub`, `SpotSub`, `Discovery` 상태를
  한 곳에서 볼 수 있게 한다.
- registry는 global summary,
  local monitor는 detailed drill-down 역할로 분리한다.
- representative `routing_id`를 registry entry identity로 사용한다.

연관 canonical 문서:

- [`direct-callback-recv-rewrite-spec.ko.md`](direct-callback-recv-rewrite-spec.ko.md)
- [`service-routing-id-policy-plan.ko.md`](service-routing-id-policy-plan.ko.md)
- [`service-monitor-readiness-plan.ko.md`](service-monitor-readiness-plan.ko.md)

## 2. 설계 원칙

### 2.1 registry는 global summary다

registry는 local runtime detail을 그대로 복사한 것이 아니다.

registry에 저장하는 정보:

- 전역적으로 의미 있는 상태
- 운영/진단 1차 판단에 필요한 상태
- 시간이 조금 지나도 의미가 유지되는 상태

### 2.2 local monitor는 상세 분석용이다

registry summary만으로 부족하면
해당 프로세스의 local service monitor를 쓴다.

예:

- gateway route가 왜 ready가 아닌지
- spot sub filter가 실제로 적용됐는지
- registration 실패의 세부 원인이 무엇인지

## 3. public subject와 entry granularity

registry topology entry는 다음 public subject 기준으로 만든다.

- `Gateway`
- `SpotPub`
- `SpotSub`
- `Discovery`

기본 granularity:

```text
representative service socket + logical service subject
```

의미:

- `Gateway`: 인스턴스가 소비하는 service 기준 1 entry
- `SpotPub`: 인스턴스가 advertise하는 service 기준 1 entry
- `SpotSub`: 인스턴스가 watch하는 service 기준 1 entry
- `Discovery`: 인스턴스가 watch하는 service 기준 1 entry

중요:

- `SpotPub`와 `SpotSub`는 registry에서도 별도 entry다.
- `receiver`는 unified `gateway` 모델에 흡수되므로 별도 public subject로 두지 않는다.

## 4. 저장할 정보

### 4.1 공통 entry

```c
typedef struct zlink_registry_topology_entry_t
{
    zlink_routing_id_t routing_id;
    uint16_t service_kind;
    char service_name[256];
    char endpoint[256];
    uint16_t source;
    uint16_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
} zlink_registry_topology_entry_t;
```

필드 의미:

- `routing_id`: representative RID
- `service_kind`: gateway / spot_pub / spot_sub / discovery
- `service_name`: logical service 이름
- `endpoint`: advertise endpoint 또는 대표 endpoint
- `source`: `MANUAL`, `DISCOVERY`, `REGISTRY`
- `state`: 현재 요약 상태
- `desired_count`: 기대 대상 수
- `ready_count`: 실제 ready 대상 수
- `error_code`: 마지막 오류 코드
- `last_reported_ms`: 마지막 report 시각

entry key:

```text
service_kind + routing_id + service_name
```

### 4.2 상태 enum

권장 상태:

- `DISCOVERED`
- `CONNECTING`
- `READY`
- `LOST`
- `ERROR`
- `STOPPED`

## 5. registry에 넣지 않을 정보

다음은 registry에서 제외한다.

- raw socket peer 목록 전체
- `POLLIN/POLLOUT` 같은 순간 readiness
- 세밀한 queue depth 변화
- local filter implementation detail
- per-socket retry/backoff 내부 상태

이 정보는 local monitor가 맡는다.

## 6. 정보 수집 방식

### 6.1 registry가 직접 아는 정보

registry가 기존 control plane으로 직접 알 수 있는 정보:

- 등록된 service와 endpoint
- heartbeat 생존 여부
- provider 집계값 일부

### 6.2 local service report

registry만으로 직접 알 수 없는 정보는
local service가 summary 형태로 보고한다.

예:

- `Gateway`: `desired_count`, `ready_count`, `state`, `last_error`
- `SpotPub` / `SpotSub`: peer count, state, last_error
- `Discovery`: provider summary, local availability state, last_error

원칙:

- report는 summary 수준만 보낸다
- state transition 시점과 heartbeat 주기를 우선 쓴다
- 불필요한 flood를 피한다

### 6.3 reporting channel

1차 권장안:

- registry ROUTER control plane에 `TOPOLOGY_REPORT` message type 추가
- local service는 lightweight reporting client로 registry에 연결

구체 정책:

- `Gateway` topology entry는 gateway가 직접 report
- `SpotPub` / `SpotSub` summary는 discovery uplink 또는 reporting client로 report
- `Discovery`도 topology entry가 필요하면 같은 reporting client를 쓴다

## 7. 조회 API 제안

### 7.1 local-handle query

```c
int zlink_registry_topology_snapshot(void *registry,
                                     zlink_registry_topology_entry_t *entries,
                                     size_t *count);
```

선택 필터:

```c
typedef struct zlink_registry_topology_filter_t
{
    uint16_t service_kind;
    uint16_t state_mask;
    const char *service_name;
    const zlink_routing_id_t *routing_id;
} zlink_registry_topology_filter_t;

int zlink_registry_topology_query(void *registry,
                                  const zlink_registry_topology_filter_t *filter,
                                  zlink_registry_topology_entry_t *entries,
                                  size_t *count);
```

### 7.2 remote query client

```c
void *zlink_registry_query_client_new(void *ctx);
int zlink_registry_query_client_connect(void *client,
                                        const char *registry_endpoint);
int zlink_registry_query_snapshot(void *client,
                                  zlink_registry_topology_entry_t *entries,
                                  size_t *count);
```

## 8. 사용 흐름

권장 사용 흐름:

1. registry에서 전체 topology snapshot을 조회한다.
2. `READY`, `LOST`, `ERROR` entry를 기준으로 이상 대상을 좁힌다.
3. 필요하면 해당 process/service의 local monitor로 상세를 본다.

즉 다음 역할 분리를 유지한다.

```text
registry = coarse global summary
monitor = local detailed cause
```

## 9. 테스트 계획

- `Gateway`, `SpotPub`, `SpotSub`, `Discovery` entry가 snapshot에 나타남
- representative RID 기준 query 가능
- `READY` -> `LOST` -> GC 흐름 검증
- state transition report가 registry summary에 반영됨
- stale entry가 timeout 후 정리됨
- local monitor와 registry summary의 의미가 충돌하지 않음

## 10. Definition of Done

- registry topology 문서에서 public subject가 `Gateway`, `SpotPub`, `SpotSub`, `Discovery`로 고정되어 있다.
- representative RID를 entry identity로 쓴다.
- registry summary와 local monitor의 역할 분리가 명확하다.
- `receiver` 별도 public topology entry 전제가 제거되어 있다.
