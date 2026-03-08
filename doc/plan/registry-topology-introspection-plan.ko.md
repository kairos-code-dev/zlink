# Registry Topology / Introspection 계획

## 1. 목적

이 문서는 discovery와 registry 기반 자동 연결의 현재 상태를
한 곳에서 조회할 수 있게 하기 위한 registry 중심 introspection 기능을 정의한다.

핵심 목표는 다음과 같다.

- 사용자가 `Gateway`, `Receiver`, `SpotPub`, `SpotSub`, `Discovery`를
  각각 따로 조회하지 않고도 전체 자동 연결 상태를 볼 수 있게 한다.
- registry를 전역 요약 상태 저장소로 사용해
  “무엇이 존재하고, 무엇이 준비됐고, 무엇이 문제인지”를 1차로 판단할 수 있게 한다.
- 더 자세한 상태는 각 service의 local monitor로 drill-down 하도록 역할을 분리한다.

service identity는 별도 RID 정책 문서를 따른다.

- `service-routing-id-policy-plan.ko.md`

즉 목표 상태는 다음 한 줄이다.

```text
registry = global summary
local monitor = detailed drill-down
```

추가 전제:

- 이번 설계에서는 API 호환성을 유지할 필요가 없다.
- 필요한 경우 registry protocol과 public API를 함께 정리할 수 있다.

## 2. 왜 registry인가

현재 자동 연결 관련 정보는 여러 곳에 흩어져 있다.

- registry:
  등록된 서비스와 heartbeat 생존 여부를 안다.
- discovery:
  특정 service의 provider snapshot을 안다.
- gateway:
  실제 route ready/down과 connection count를 안다.
- receiver:
  register/unregister 결과와 router peer를 안다.
- spot:
  peer up/down, filter 적용, queue 상태를 안다.

문제는 이 정보가 모두 local view라는 점이다.

- 전체 상태를 보려면 service별 API를 각각 조회해야 한다.
- 운영 관점에서 “어디가 문제인가”를 한눈에 보기 어렵다.
- 자동 연결이 discovery 문제인지, route 문제인지,
  register 문제인지 구분하려면 여러 객체를 조합해야 한다.

registry를 전역 요약 저장소로 두면 다음이 가능해진다.

- 조회를 한 곳에서 끝낼 수 있다.
- cluster 전체 상태를 한 번에 볼 수 있다.
- 로컬 monitor는 정말 필요할 때만 사용하게 된다.

## 3. 이 문서의 기본 결론

이 문서는 다음 결론을 전제로 한다.

- registry에 있는 정보면 1차 진단에는 충분해야 한다.
- raw socket 수준의 세부 상태는 registry에 넣지 않는다.
- 세밀한 원인 분석은 local service monitor가 담당한다.
- registry entry의 대표 식별자는 representative `routing_id`를 사용한다.

즉 사용 흐름은 다음과 같다.

1. registry에서 전체 상태를 조회한다.
2. 이상한 service entry를 찾는다.
3. 필요하면 해당 프로세스의 local monitor로 상세를 본다.

## 4. 범위와 비범위

### 4.1 범위

- registry에 전역 topology/status summary를 저장하는 것
- registry에서 snapshot/query API를 제공하는 것
- 자동 연결 관련 local service가 summary 상태를 registry에 보고하는 것
- query 결과가 운영/디버깅 1차 판단에 충분한 수준이 되게 하는 것

### 4.2 비범위

- raw socket peer 전체를 registry에 복제하는 것
- `POLLIN/POLLOUT` 같은 순간 readiness를 registry에 노출하는 것
- local queue depth의 매우 세밀한 실시간 변화까지 registry에 반영하는 것
- local monitor를 registry로 대체하는 것

## 5. 사용 목적과 기대 효과

### 5.1 대표 사용자 시나리오

- 어떤 service가 registry에는 있는데 왜 gateway에서 아직 ready가 아닌지 보고 싶다.
- receiver register는 성공했는지, heartbeat는 살아있는지 한 번에 보고 싶다.
- spot mesh에서 peer가 몇 개 발견되었고, 어느 쪽이 lost 상태인지 보고 싶다.
- 운영 도구나 admin CLI에서 전체 자동 연결 상태를 한 번에 보고 싶다.

### 5.2 기대 효과

사용자 관점:

- 전체 상태를 보려는 경우 service별 API를 각각 호출할 필요가 없다.
- “registry에는 있음 / local route는 아직 아님 / error가 있음” 같은 상태를 바로 볼 수 있다.

운영 관점:

- cluster 전체 service entry health를 한 번에 볼 수 있다.
- 장애 triage의 1차 진입점이 단순해진다.

문서/가이드 관점:

- “전역 상태는 registry, 상세 상태는 monitor”로 설명이 단순해진다.

## 6. 설계 원칙

### 6.1 registry는 전역 요약 저장소다

registry는 모든 local runtime detail의 완전한 복사본이 아니다.

registry에 저장하는 정보는 다음 수준으로 제한한다.

- 전역적으로 의미 있는 상태
- 운영/진단 1차 판단에 필요한 상태
- 시간이 조금 지나도 의미가 유지되는 상태

### 6.2 local monitor는 정밀 상세용이다

registry가 알려주는 정보만으로 부족할 때는
해당 service의 local monitor를 사용한다.

예:

- gateway route가 왜 ready가 안 됐는지
- spot sub filter가 실제로 적용됐는지
- receiver register 실패의 세부 원인이 무엇인지

### 6.3 registry 정보만으로 충분해야 하는 범위

registry에서 바로 알 수 있어야 하는 것:

- 어떤 service entry가 존재하는가
- 마지막 heartbeat/report는 언제였는가
- 상태가 `READY`, `LOST`, `ERROR` 중 무엇인가
- 현재 연결/peer/provider 집계값은 얼마인가
- 마지막 오류가 있었는가

registry에서 굳이 알 필요 없는 것:

- raw socket별 peer detail
- local poll readiness
- transient retry 내부 상태

중요한 사용 계약:

- registry summary는 eventually consistent한 global summary다
- 운영/진단/1차 coarse readiness 판단에는 적합하다
- 하지만 timing-sensitive setup이나 benchmark의 final strict start gate로는 쓰지 않는다
- 그런 경우 최종 readiness는 local service monitor가 담당한다

## 7. registry에 저장할 정보

### 7.1 entry granularity

기본 granularity는 다음으로 잡는다.

```text
representative service socket + logical service subject
```

의미:

- `Receiver`:
  인스턴스가 제공하는 service 기준 1 entry
- `Gateway`:
  인스턴스가 소비하는 service 기준 1 entry
- `SpotPub`:
  인스턴스가 advertise하는 service 기준 1 entry
- `SpotSub`:
  인스턴스가 watch하는 service 기준 1 entry
- `Discovery`:
  인스턴스가 watch하는 service 기준 1 entry

중요한 점:

- `SpotPub`와 `SpotSub`는 registry에서도 별도 entry다.
- 같은 runtime이 여러 topology entry를 가질 수 있다.
- 이 문서는 runtime grouping용 `instance_id`를 1차 key로 쓰지 않는다.

### 7.2 공통 필드

초안:

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

- `routing_id`:
  service-facing socket의 representative RID
- `service_kind`:
  gateway / receiver / spot_pub / spot_sub / discovery
- `service_name`:
  자동 연결의 대상 service 이름
- `endpoint`:
  advertise endpoint 또는 대표 endpoint
- `source`:
  topology relation의 기원을 뜻한다.
  `MANUAL`, `DISCOVERY`, `REGISTRY`를 사용한다.
  report가 어떤 채널로 registry에 도착했는지를 뜻하지는 않는다.
  예:
  manual connect로 생긴 관계는 `MANUAL`,
  discovery/registry를 통해 자동 형성된 관계는 `DISCOVERY` 또는 `REGISTRY`
- `state`:
  현재 요약 상태
- `desired_count`:
  registry/discovery 관점에서 기대되는 대상 수
- `ready_count`:
  local runtime 관점에서 실제 ready로 본 대상 수
- `error_code`:
  마지막 오류 코드
- `last_reported_ms`:
  마지막 heartbeat/report 시각

entry key는 다음 조합으로 본다.

```text
service_kind + routing_id + service_name
```

이유:

- representative RID는 service socket identity다.
- 하나의 `Gateway`가 여러 `service_name`을 소비할 수 있다.
- `routing_id`만 단독 key로 쓰면 gateway multi-service를 표현하기 어렵다.

### 7.3 상태 enum

권장 상태:

- `DISCOVERED`
- `CONNECTING`
- `READY`
- `LOST`
- `ERROR`
- `STOPPED`

설명:

- `DISCOVERED`:
  registry/discovery에는 보이지만 local transport ready는 아님
- `CONNECTING`:
  local runtime이 ready를 만들기 위해 진행 중
- `READY`:
  service-level 의미에서 사용 가능
- `LOST`:
  이전에 ready였지만 현재는 상실
- `ERROR`:
  오류가 보고됨
- `STOPPED`:
  인스턴스 종료 또는 명시적 정지

### 7.4 entry 수명과 정리 정책

registry entry는 다음 규칙으로 유지/정리한다.

- heartbeat/report가 정상적으로 들어오면 `last_reported_ms`를 갱신한다.
- report timeout을 넘기면 state를 `LOST`로 전이한다.
- `LOST` 상태에서 추가 grace window를 넘기면 stale entry를 제거한다.
- service가 명시적으로 `STOPPED`를 보고하면 짧은 grace 후 제거한다.

권장 파라미터:

- `report_timeout_ms`:
  `READY`/`CONNECTING` 상태를 `LOST`로 내리는 기준
- `stale_gc_timeout_ms`:
  `LOST` 상태 stale entry를 실제로 제거하는 기준
- `stopped_gc_timeout_ms`:
  `STOPPED` 보고 후 제거까지의 짧은 grace window

즉 crash처럼 `STOPPED`를 못 보내는 경우에도
entry가 영구적으로 남지 않게 한다.

## 8. registry에 넣지 않을 정보

다음 정보는 registry에서 제외한다.

- raw socket peer 목록 전체
- `POLLIN/POLLOUT` 같은 순간 readiness
- 세밀한 queue depth 변화
- local filter implementation detail
- per-socket retry/backoff 내부 상태

이유:

- 전역 조회 API가 과도하게 무거워진다.
- stale 정보가 많아진다.
- local monitor의 역할과 중복된다.

## 9. 정보 수집 방식

### 9.1 registry가 직접 아는 정보

registry가 기존 protocol만으로 직접 알 수 있는 정보:

- receiver / spot_pub / spot_sub의 등록 상태
- advertise endpoint
- heartbeat 생존 여부
- provider 집계값

### 9.2 local service가 report해야 하는 정보

registry만으로는 직접 알 수 없는 정보는 local service가 summary 형태로 보고해야 한다.

예:

- gateway:
  service별 `desired_count`, `ready_count`, `state`, `last_error`
- receiver:
  register ok/failed, unregister, router peer count summary
- spot_pub / spot_sub:
  peer count, filter applied summary, state, queue/backpressure summary
- discovery:
  watch 중인 service의 provider summary, local availability state, last_error

원칙:

- report는 summary 수준만 보낸다.
- report 주기는 heartbeat와 묶거나 state transition 시점에만 보낸다.
- state가 바뀌지 않으면 불필요한 flood를 피한다.

### 9.3 reporting channel

1차 권장안:

- registry ROUTER control plane에 `TOPOLOGY_REPORT` message type 추가
- local service는 lightweight reporting client로 registry ROUTER에 연결한다

이유:

- 기존 registry가 이미 registration/heartbeat를 받는 경로와 맞닿아 있다.
- 별도 외부 저장소 없이 같은 신뢰 경계 안에서 처리할 수 있다.
- `Gateway`도 같은 경로를 재사용할 수 있다.

구체 정책:

- `Receiver`와 `SpotPub/SpotSub`은 기존 register/heartbeat와 함께 summary report를 보낼 수 있다.
- `Gateway`는 discovery로 registry endpoint를 알게 된 뒤
  lightweight reporting client를 registry ROUTER에 붙인다.
- `Discovery`도 topology entry를 유지하려면 같은 lightweight reporting client를 사용한다.
- registry는 `TOPOLOGY_REPORT`를 기존 control-plane trust boundary 안에서 처리한다.

## 10. 조회 API 제안

### 10.1 registry local-handle query

registry를 직접 소유한 프로세스에서는 handle 기반 query가 필요하다.

```c
int zlink_registry_topology_snapshot(void *registry,
                                     zlink_registry_topology_entry_t *entries,
                                     size_t *count);
```

메모리 모델:

- `entries == NULL`이면 필요한 entry 수를 `*count`에 반환한다.
- caller buffer가 작으면 required count를 `*count`에 돌려주고
  `-1`과 `ENOBUFS`를 반환한다.
- partial copy는 하지 않는다.

선택 필터 API:

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

### 10.2 remote query client

registry가 독립 프로세스로 뜨는 운영 환경을 고려하면
remote query client는 실용적인 1차 surface에 가깝다.

권장 API:

```c
void *zlink_registry_query_client_new(void *ctx);
int zlink_registry_query_client_connect(void *client,
                                        const char *registry_endpoint);
int zlink_registry_query_snapshot(void *client,
                                  zlink_registry_topology_entry_t *entries,
                                  size_t *count);
```

이 문서의 기본 판단:

- local-handle query는 embedded registry/test 용도로 유지한다.
- remote query client는 standalone registry 운영을 위해
  늦어도 Phase 1에는 포함하는 쪽이 좋다.

## 11. 사용 예시

### 예시 1: registry에서 전체 상태 조회

```c
size_t count = 0;

if (zlink_registry_topology_snapshot(registry, NULL, &count) == 0 &&
    count > 0) {
    zlink_registry_topology_entry_t *entries =
        malloc(count * sizeof(*entries));

    if (entries != NULL &&
        zlink_registry_topology_snapshot(registry, entries, &count) == 0) {
        for (size_t i = 0; i < count; ++i) {
            /* routing_id, service_name, state, ready_count 출력 */
        }
    }

    free(entries);
}
```

### 예시 1-1: remote query client로 조회

```c
size_t count = 0;

if (zlink_registry_query_snapshot(client, NULL, &count) == 0 &&
    count > 0) {
    zlink_registry_topology_entry_t *entries =
        malloc(count * sizeof(*entries));

    if (entries != NULL &&
        zlink_registry_query_snapshot(client, entries, &count) == 0) {
        for (size_t i = 0; i < count; ++i) {
            /* routing_id, service_name, state, ready_count 출력 */
        }
    }

    free(entries);
}
```

### 예시 2: registry에서 문제 항목을 찾고 local monitor로 drill-down

```text
1. registry snapshot에서 gateway svc-a state=ERROR 확인
2. 해당 프로세스의 gateway monitor에서 ROUTE_DOWN / ERROR event 확인
3. root cause는 local monitor에서 분석
```

핵심:

- registry는 1차 triage 용도다.
- 세부 root cause는 local monitor가 담당한다.

## 12. monitor/readiness와의 관계

이 문서와 `service-monitor-readiness-plan.ko.md`의 관계는 다음과 같다.

- registry topology:
  전역 aggregate 조회
- local monitor:
  개별 service의 상세 상태 변화

즉 둘은 경쟁 관계가 아니라 2층 구조다.

```text
registry snapshot -> 이상 항목 발견 -> local monitor drill-down
```

### 12.1 local service state transition -> registry report state 대응

대표적인 매핑 예시는 다음과 같다.

| local monitor event | registry state 전이 |
|---|---|
| `ZLINK_DISCOVERY_SERVICE_UP` | `DISCOVERED` 유지 또는 `DISCOVERED -> CONNECTING` |
| `ZLINK_GATEWAY_ROUTE_UP` | `CONNECTING -> READY` |
| `ZLINK_GATEWAY_SERVICE_LOST` | `READY -> LOST` |
| `ZLINK_RECEIVER_REGISTER_OK` | `DISCOVERED -> READY` |
| `ZLINK_RECEIVER_REGISTER_FAILED` | `DISCOVERED -> ERROR` |
| `ZLINK_SPOT_SUB_PEER_UP` | `DISCOVERED -> CONNECTING` |
| `ZLINK_SPOT_SUB_FILTER_APPLIED` | `CONNECTING -> READY` |
| `ZLINK_SPOT_SUB_PEER_DOWN` | `READY -> LOST` 또는 `CONNECTING` 유지 |

즉 local service가 자기 상태 전이를 해석해 report state를 결정하고,
registry는 그 report를 저장하는 global summary 저장소로 이해하면 된다.

## 13. 구현 우선순위

### Phase 0: registry data model

- `zlink_registry_topology_entry_t`
- state/source enum 정의
- registry internal storage 추가
- local-handle snapshot API

완료 기준:

- registry가 직접 아는 정보만으로 snapshot 1회 반환 가능
- local-handle query의 2-pass 메모리 모델이 고정됨

### Phase 1: receiver / spot / discovery report + remote query

- register, unregister, heartbeat 기반 상태를 topology entry에 반영
- spot register/heartbeat 기반 상태 반영
- discovery topology summary report 반영
- remote query client 추가

완료 기준:

- provider/service 존재 여부와 생존 상태를 registry snapshot에서 확인 가능
- standalone registry 환경에서도 remote query로 snapshot 조회 가능

### Phase 2: gateway summary reporting

- gateway가 service별 summary 상태를 registry에 report
- `desired_count`, `ready_count`, `last_error` 반영

완료 기준:

- gateway readiness 문제를 registry snapshot에서 1차로 식별 가능

### Phase 3: query filtering / tooling

- filter query
- admin/diagnostic tool sample

완료 기준:

- 운영 도구에서 service/routing_id/state 기준 조회 가능

## 14. 테스트 계획

### 14.1 core 테스트

- receiver register 후 registry snapshot에 entry 생성
- heartbeat timeout 후 state가 `LOST` 또는 equivalent 상태로 전이
- unregister 후 entry 제거 또는 `STOPPED` 전이
- gateway report 반영 후 `desired_count`/`ready_count` 갱신
- spot peer summary report 반영
- stale `LOST` entry가 `stale_gc_timeout_ms` 이후 제거되는지 검증

### 14.2 consistency 테스트

- 예: registry state가 `READY`인데 local monitor에서 이미
  `GATEWAY_SERVICE_LOST`를 본 경우, report timeout 이후
  registry가 `LOST` 또는 equivalent 상태로 수렴하는지 검증
- stale report timeout 동작 검증

### 14.3 운영 시나리오 테스트

- registry snapshot만으로 1차 triage가 가능한지 검증
- 문제 항목을 local monitor로 drill-down 하는 흐름 검증

## 15. 리스크와 완화

| 리스크 | 설명 | 완화 |
|---|---|---|
| stale state | registry summary가 local runtime보다 늦을 수 있음 | `last_reported_ms`, timeout, state expiry 도입 |
| 정보 과다 | local detail을 너무 많이 넣으면 registry가 무거워짐 | summary 수준만 저장 |
| reporting flood | state report가 너무 자주 발생 | state transition 기반 보고 + heartbeat 결합 |
| gateway reporting 추가 복잡도 | gateway는 현재 registry에 직접 연결되지 않음 | lightweight reporting client + `TOPOLOGY_REPORT`로 고정 |
| 의미 혼동 | registry state와 local monitor event를 같은 것으로 오해 | 문서에서 global summary / local detail 구분 고정 |
| runtime grouping 정보 부재 | `instance_id` 없이 per-runtime 묶음이 어려울 수 있음 | 1차는 `service_kind + routing_id + service_name` entry 중심으로 정의하고, 필요 시 후속 grouping key 검토 |

## 16. Definition of Done

- registry가 topology/status summary를 저장할 수 있다
- `zlink_registry_topology_snapshot` 또는 동등 API가 존재한다
- local-handle과 remote query 양쪽에서 snapshot을 조회할 수 있다
- receiver/spot/gateway summary가 registry snapshot에 반영된다
- registry 정보만으로 1차 진단이 가능하다
- 더 자세한 상태는 local monitor로 drill-down 한다는 역할 분리가 문서에 명시된다

## 17. 자기 리뷰

- registry를 단일 조회 지점으로 두되,
  local monitor를 대체하지 않는 방향으로 정리했다.
- registry에는 전역 요약 정보만 넣고,
  raw socket/local transient 상태는 제외했다.
- gateway처럼 registry가 직접 모르는 정보는
  local runtime reporting으로 채우되,
  reporting channel은 registry ROUTER + lightweight client로 고정했다.
- 1차 triage와 2차 drill-down 흐름이 문서만 읽어도 보이게 구성했다.
- `instance_id` 대신 representative RID를 entry identity로 사용하도록 정리했다.
- `SpotPub`와 `SpotSub`를 registry에서도 별도 entry로 다루도록 고정했다.
