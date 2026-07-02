# Actor Location Registry 공개 계약 초안

> 이 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, `core/include/zlink.h`와
> 관련 구현, 회귀 테스트, bindings, framework 반영이 끝나기 전까지 정식 spec 문서에
> 섞어 쓰지 않는다.
>
> 이 문서는 actor id로 actor의 현재 위치를 등록하고 찾는 core C API 계약 후보를 정의한다.

## 1. 목적

SPOT discovery는 `spot rid`로 그 spot을 소유한 node rid를 찾을 수 있다. 그러나
framework의 actor 재연결, stream session 재bind, 사용자별 push 흐름에는
`actor id`로 actor의 현재 위치를 찾는 기능도 필요하다.

현재 framework는 actor 생성 결과로 `ActorRef`를 받을 수 있지만, 이 위치를 registry/discovery
계층에 공통으로 등록하고 조회하는 공개 계약은 충분하지 않다. 샘플이나 애플리케이션이
각자 directory를 만들면 언어별 동작이 달라지고, 재연결 시 기존 actor에 다시 붙는 흐름도
공통으로 설명하기 어렵다.

이 초안의 목표는 아래 기능을 core C API 레벨에서 제공하는 것이다. actor와 spot은
위치를 찾는 방식이 다르지만, discovery가 캐시하고 갱신하는 정책은 같은 규칙을 사용한다.

- actor/spot runtime lifecycle에서 actor 위치와 spot 위치를 자동으로 갱신한다.
- custom runtime, 운영 도구, 테스트가 필요할 때는 actor 위치를 수동으로 갱신할 수 있게 한다.
- `actor id`로 actor 위치를 조회한다.
- actor가 entry spot actor인지, 일반/user spot actor인지 구분한다.
- entry spot actor는 `node rid`만으로 라우팅할 수 있게 한다.
- 일반/user spot actor는 `node rid + spot rid`를 함께 반환한다.
- actor 생성, join, leave, destroy, node/owner 만료 시 stale 위치가 남지 않게 한다.
- actor location과 spot location 조회에 같은 캐시 정책과 freshness 옵션을 적용한다.

## 2. 비목표

- actor 객체를 registry에 저장하지 않는다.
- actor의 업무 상태를 registry에 저장하지 않는다.
- actor 생성 정책이나 placement policy를 registry가 결정하지 않는다.
- string actor id 충돌을 registry가 자동으로 해석하지 않는다. namespace가 필요하면 caller가
  actor id 또는 actor type에 반영한다.
- framework의 typed actor manager API 이름은 이 문서에서 최종 확정하지 않는다.
- 기존 `spot rid -> node rid` discovery 의미를 바꾸지 않는다.

## 3. 현재 상태

현재 public header에는 actor 위치와 관련된 일부 선언이 이미 있다.

- `ZLINK_ROUTE_KIND_ACTOR`
- `zlink_actor_ref_t`
- `zlink_actor_route_t`

하지만 이 선언만으로는 정식 공개 계약이 충분하지 않다.

- actor route value의 직렬화 형식이 명확하지 않다.
- actor location 갱신/제거 전용 typed C API가 없다.
- entry spot actor와 일반 spot actor의 위치 차이가 문서화되어 있지 않다.
- registry query client에서 actor route를 조회하는 계약이 분리되어 있지 않다.
- stale cleanup, generation, owner 교체 규칙이 actor route 관점에서 명확하지 않다.
  켤 수 있는 계약이 없다.

따라서 구현 전 첫 단계는 현재 선언과 실제 구현을 감사하고, 아래 To-Be 계약과 충돌하는
부분을 정리하는 것이다.

## 4. 용어

| 용어 | 의미 |
|------|------|
| actor id | 애플리케이션이 정한 string actor 식별자 |
| actor type | 같은 actor id를 다른 actor 종류와 구분하기 위한 선택적 string 분류 |
| actor ref | actor의 concrete 참조. `node rid`, `actor id`, `generation`을 포함한다 |
| actor location | actor ref에 더해 현재 actor가 붙어 있는 위치 종류와 spot rid를 포함한 조회 결과 |
| entry spot actor | entry spot 아래에 있고 node rid만으로 entry spot actor route를 보낼 수 있는 actor |
| spot actor | 특정 user spot에 join해 있고 node rid와 spot rid가 모두 필요한 actor |
| owner | actor location route row를 registry에 등록한 discovery/service participant |
| route location | registry가 관리하는 actor 또는 spot의 현재 라우팅 위치 |
| positive cache | actor 또는 spot 위치를 찾은 성공 결과를 저장한 캐시 |
| negative cache | actor 또는 spot이 없다는 조회 실패 결과를 짧게 저장한 캐시 |
| freshness | 조회가 local cache를 허용하는지, registry를 다시 확인해야 하는지 나타내는 요구 |

## 5. 위치 모델

actor 위치는 두 종류를 구분한다.

| 종류 | 필요한 주소 | 설명 |
|------|-------------|------|
| entry spot actor | `node_rid` | entry spot은 node마다 하나인 actor 진입점이므로 node rid만 있으면 actor route를 보낼 수 있다. |
| spot actor | `node_rid + spot_rid` | actor가 user spot에 join해 있으므로 spot rid까지 알아야 정확한 spot route로 보낼 수 있다. |

`actor_ref.node_rid`는 actor가 현재 존재하는 concrete node를 가리킨다.
`spot_rid`는 actor가 일반/user spot에 join했을 때만 필수다. entry spot actor이면
`spot_rid`는 empty routing id일 수 있다.

## 6. actor/spot 공통 캐시 정책

actor location과 spot location은 모두 registry를 authoritative source로 둔다. Discovery의
local cache는 성능 최적화일 뿐이며, registry의 결과와 다른 값을 영구적으로 보장하지 않는다.

캐시 key는 아래 정보를 포함한다.

| 구성 요소 | 설명 |
|-----------|------|
| channel | discovery가 바라보는 logical channel |
| route kind | actor location 또는 spot location |
| route id | actor이면 `(actor_type, actor_id)`, spot이면 `spot rid` |

캐시 value는 조회 결과와 함께 최소한 아래 정보를 저장한다.

| 구성 요소 | 설명 |
|-----------|------|
| location | actor 또는 spot의 현재 라우팅 위치 |
| positive/negative 여부 | 위치를 찾았는지, 없다는 결과인지 구분한다 |
| validated time | registry 또는 local authoritative source로 검증한 시각 |
| owner/generation 정보 | registry row가 제공하는 owner 또는 generation 정보가 있으면 보존한다 |

캐시는 반드시 bounded cache여야 한다. TTL만으로는 actor별 spot처럼 route id 개수가 많은 사용
패턴에서 메모리 증가를 막을 수 없다. 구현은 max entry 수를 두고 LRU 또는 clock 방식 같은
bounded eviction을 사용해야 한다.

기본 TTL 후보:

| 항목 | 기본값 후보 | 설명 |
|------|-------------|------|
| positive cache TTL | 1000 ms | 일반 actor/spot 조회의 기본값이다. |
| stable spot TTL | 5000 ms | 장기 service spot처럼 이동이 드문 spot에 사용할 수 있다. |
| actor-owned spot TTL | 250-1000 ms | actor와 1:1로 매칭되는 spot처럼 생성과 삭제가 잦은 대상에 사용한다. |
| negative cache TTL | 0-100 ms | 기본은 0 ms로 두고, registry 부하가 확인될 때만 짧게 켠다. |

negative cache는 “없음” 결과를 저장한다. actor 재연결이나 “없으면 생성” 판단에서는 방금 생성된
actor를 놓칠 수 있으므로 negative cache를 사용하지 않거나, registry 강제 조회 모드를 사용해야 한다.

### 6.1 freshness 모드

TTL을 길게 둘 수 있으려면 조회마다 freshness 요구를 전달할 수 있어야 한다.

```c
typedef enum zlink_route_resolve_mode_t
{
    ZLINK_ROUTE_RESOLVE_NORMAL = 0,
    ZLINK_ROUTE_RESOLVE_REFRESH = 1,
    ZLINK_ROUTE_RESOLVE_DIRECT = 2
} zlink_route_resolve_mode_t;
```

의미:

| mode | 의미 |
|------|------|
| `NORMAL` | cache hit를 허용한다. cache가 없거나 만료되면 registry를 조회하고 결과를 cache에 저장한다. |
| `REFRESH` | cache가 있어도 registry를 조회한다. 조회 결과는 cache에 반영한다. |
| `DIRECT` | registry를 조회하지만 cache를 읽지도 쓰지도 않는다. 진단 또는 cache 오염을 피해야 하는 경로에서 사용한다. |

일반 send/request 경로는 `NORMAL`을 사용할 수 있다. actor 재연결, “없으면 생성” 판단, 이동 직후
검증은 `REFRESH`를 사용한다. 운영 진단처럼 cache 영향을 완전히 배제해야 하는 조회는 `DIRECT`를
사용한다.

### 6.2 캐시 설정 API 후보

구현체마다 route 수와 churn이 다르므로 cache limit과 TTL은 discovery 단위로 조정할 수 있어야 한다.

```c
typedef struct zlink_route_location_cache_options_t
{
    uint32_t actor_cache_enabled;
    uint32_t spot_cache_enabled;
    uint32_t max_entries;
    uint32_t positive_ttl_ms;
    uint32_t stable_spot_ttl_ms;
    uint32_t actor_owned_spot_ttl_ms;
    uint32_t negative_ttl_ms;
} zlink_route_location_cache_options_t;

    void *discovery,
    const zlink_route_location_cache_options_t *options);

    void *discovery,
    zlink_route_location_cache_options_t *options_out);
```

의미:

| 필드 | 의미 |
|------|------|
| `actor_cache_enabled` | 0이면 actor location positive/negative cache를 사용하지 않는다. |
| `spot_cache_enabled` | 0이면 spot location positive/negative cache를 사용하지 않는다. |
| `max_entries` | positive/negative entry를 합친 최대 개수다. 0은 cache 비활성화를 뜻한다. |
| `positive_ttl_ms` | 일반 actor/spot 성공 조회 결과의 기본 TTL이다. |
| `stable_spot_ttl_ms` | 이동이 드문 service spot에 사용할 수 있는 TTL이다. 0이면 `positive_ttl_ms`를 사용한다. |
| `actor_owned_spot_ttl_ms` | actor와 1:1로 매칭되는 spot에 사용할 수 있는 TTL이다. 0이면 `positive_ttl_ms`를 사용한다. |
| `negative_ttl_ms` | not found 결과를 저장하는 시간이다. 0이면 negative cache를 사용하지 않는다. |

API 기본값은 구현 단계에서 확정하되, draft 기준 권장값은 `actor_cache_enabled=1`,
`spot_cache_enabled=1`, `max_entries=4096`, `positive_ttl_ms=1000`,
`stable_spot_ttl_ms=5000`, `actor_owned_spot_ttl_ms=1000`, `negative_ttl_ms=0`이다.

actor와 spot cache는 서로 독립적으로 끌 수 있어야 한다. 예를 들어 actor 재연결 정확성을 우선하는
환경은 actor cache를 끄고 spot cache만 사용할 수 있다. 반대로 spot을 actor마다 1:1로 만들고
spot 위치 churn이 큰 환경은 spot cache를 끄고 actor location만 cache할 수 있다.

### 6.3 spot resolver와의 관계

spot 조회도 actor와 같은 route location cache를 사용한다. 과도기 API를 별도로 두지 않고,

```c
    void *discovery,
    const zlink_routing_id_t *spot_rid,
    zlink_route_resolve_mode_t mode,
    zlink_spot_route_t *route_out);
```

caller는 일반 송신에는 `ZLINK_ROUTE_RESOLVE_NORMAL`을 사용하고, 재연결이나 최신 상태 확인에는
`ZLINK_ROUTE_RESOLVE_REFRESH` 또는 `ZLINK_ROUTE_RESOLVE_DIRECT`를 사용한다.

## 7. 공개 C 타입 초안

아래 타입 이름은 구현 전 초안이다. 이미 존재하는 타입은 의미를 명확히 보강하고, 없는 타입은
새로 추가한다.

```c
typedef enum zlink_actor_location_kind_t
{
    ZLINK_ACTOR_LOCATION_KIND_INVALID = 0,
    ZLINK_ACTOR_LOCATION_KIND_ENTRY_SPOT = 1,
    ZLINK_ACTOR_LOCATION_KIND_SPOT = 2
} zlink_actor_location_kind_t;
```

`zlink_actor_location_kind_t`는 actor route가 어떤 주소 형태를 갖는지 나타낸다.

```c
typedef uint32_t zlink_actor_location_flags_t;

typedef enum zlink_actor_location_flag_values_t
{
    ZLINK_ACTOR_LOCATION_FLAG_NONE = 0,
    ZLINK_ACTOR_LOCATION_FLAG_HAS_SPOT_RID = 1,
    ZLINK_ACTOR_LOCATION_FLAG_REMOTE = 2,
    ZLINK_ACTOR_LOCATION_FLAG_STALE = 4
} zlink_actor_location_flag_values_t;
```

`STALE`은 query 결과에 stale row를 반환하는 모드가 생길 때만 사용한다. 기본 resolve 함수는
stale row를 성공으로 반환하지 않는다.

```c
enum
{
    ZLINK_ACTOR_TYPE_MAX = 128
};
```

actor type은 선택 값이다. 비어 있으면 actor id만으로 조회한다. framework는 typed actor를
사용할 때 actor type을 넣어 충돌을 줄일 수 있다.

```c
typedef struct zlink_actor_location_t
{
    zlink_actor_ref_t actor;
    char actor_type[ZLINK_ACTOR_TYPE_MAX];
    zlink_actor_location_kind_t location_kind;
    zlink_routing_id_t spot_rid;
    zlink_spot_kind_t spot_kind;
    zlink_actor_location_flags_t flags;
    uint64_t updated_ms;
} zlink_actor_location_t;
```

필드 의미:

| 필드 | 의미 |
|------|------|
| `actor` | concrete actor ref. `actor.node_rid`, `actor.actor_id`, `actor.generation`을 포함한다. |
| `actor_type` | 선택적 actor type. 비어 있으면 actor id만 사용한다. |
| `location_kind` | entry spot actor인지 일반/user spot actor인지 나타낸다. |
| `spot_rid` | `location_kind == SPOT`이면 필수. entry spot actor이면 empty일 수 있다. |
| `spot_kind` | `spot_rid`가 있을 때 그 spot의 종류를 나타낸다. |
| `flags` | 보조 상태. 기본 등록/조회에서는 `NONE` 또는 `HAS_SPOT_RID`를 사용한다. |
| `updated_ms` | registry가 관측한 마지막 갱신 시각. caller가 직접 신뢰성 판단에 쓰기보다 진단용으로 사용한다. |

`zlink_actor_route_t`는 이미 public header에 존재하지만 새 계약에서는 표준 반환 타입으로
사용하지 않는다. 이번 변경에서는 이전 API 유지를 목표로 하지 않으므로, 구현 단계에서 `zlink_actor_route_t`와
계약은 `zlink_actor_location_t` 하나로 정리한다.

## 8. Discovery actor location API 초안

Discovery는 registry와 연결된 service participant 관점에서 actor location row를 갱신하고
조회한다. 기본 경로에서는 actor/spot runtime lifecycle이 이 API를 호출한다. 애플리케이션의
일반 handler나 샘플 흐름이 actor location을 직접 등록하지 않아도 된다.

수동 갱신 API는 계속 제공한다. 이 API는 custom actor runtime, registry를 직접 다루는 운영 도구,
회귀 테스트처럼 runtime 자동 갱신 밖에서 location row를 관리해야 하는 경우에 사용한다.

### 8.1 수동 갱신

```c
    void *discovery,
    const zlink_actor_location_t *location);
```

의미:

- 일반 framework 사용자는 이 함수를 직접 호출하지 않는다. registry-backed actor runtime이
  actor lifecycle에 맞춰 자동 호출한다.
- `location->actor.actor_id`가 비어 있으면 실패한다.
- `location->actor.node_rid`가 비어 있으면 실패한다.
- `location_kind == ZLINK_ACTOR_LOCATION_KIND_ENTRY_SPOT`이면 `spot_rid`는 비어 있어도 된다.
- `location_kind == ZLINK_ACTOR_LOCATION_KIND_SPOT`이면 `spot_rid`가 비어 있으면 실패한다.
- 같은 `(actor_type, actor_id)` row가 있으면 같은 owner generation에서 최신 값으로 교체한다.
- 다른 owner generation이 이미 같은 actor key를 소유하면 registry의 owner 정책에 따라
  더 최신 generation만 이긴다.

### 8.2 수동 제거

```c
    void *discovery,
    const char *actor_type,
    const char *actor_id);
```

의미:

- 일반 framework 사용자는 이 함수를 직접 호출하지 않는다. actor destroy, leave, owner shutdown 같은
  runtime lifecycle이 자동 호출한다.
- 현재 discovery owner가 등록한 actor location row를 제거한다.
- 같은 actor key를 더 최신 owner generation이 소유 중이면 제거하지 않는다.
- `actor_type == NULL`은 empty actor type과 같다.

### 8.3 조회

```c
    void *discovery,
    const char *actor_type,
    const char *actor_id,
    zlink_route_resolve_mode_t mode,
    zlink_actor_location_t *location_out);
```

의미:

- 성공하면 `location_out`에 현재 actor location을 채운다.
- actor key가 없거나 stale이면 `ZLINK_CONFIG_ERROR`를 반환하고 `zlink_errno()`에
  `ENOENT`에 해당하는 값을 설정한다.
- `actor_type == NULL`은 empty actor type과 같다.
- `mode`는 actor/spot 공통 route location cache의 freshness 요구를 따른다.


## 9. Registry query client API 초안

remote query client는 현재 topology 중심이다. actor location을 운영/진단 및 framework resolver가
직접 조회할 수 있도록 typed query를 추가한다.

### 9.1 단건 조회

```c
    void *client,
    const char *actor_type,
    const char *actor_id,
    zlink_actor_location_t *location_out);
```

의미:

- registry query endpoint에 단건 actor location 조회를 요청한다.
- stale row는 기본 성공으로 반환하지 않는다.
- `actor_type == NULL`은 empty actor type과 같다.

### 9.2 목록 조회

```c
{
    char actor_type[ZLINK_ACTOR_TYPE_MAX];
    char actor_id[ZLINK_ACTOR_ID_MAX];
    zlink_routing_id_t node_rid;
    zlink_actor_location_kind_t location_kind;

    void *client,
    zlink_actor_location_t *entries,
    size_t *count);
```

의미:

- `entries == NULL`이면 필요한 개수를 `count`에 채운다.
- `entries != NULL`이면 최대 `*count`개를 채우고 실제 개수를 다시 쓴다.
- filter의 빈 문자열/empty routing id/invalid enum은 wildcard로 처리한다.

registry in-process query도 같은 의미의 API를 제공한다.

```c
    void *registry,
    const char *actor_type,
    const char *actor_id,
    zlink_actor_location_t *location_out);

    void *registry,
    zlink_actor_location_t *entries,
    size_t *count);
```

## 10. Generic route API와의 관계

다만 actor location과 spot location의 공개 표준은 typed API다.

typed API가 필요한 이유:

- actor key 검증을 core가 수행할 수 있다.
- route value serialization을 caller가 직접 만들지 않아도 된다.
- bindings가 raw byte value를 반복 구현하지 않는다.
- framework가 actor location kind, spot rid 존재 여부, generation을 같은 방식으로 해석한다.

generic route API는 caller가 actor/spot location value를 직접 만들기 위한 공개 우회 경로로
사용하지 않는다. guide와 framework 문서에서는 actor/spot location typed API만 사용한다.

## 11. 자동 갱신 주체와 lifecycle

actor location row와 spot location row는 해당 객체를 소유한 runtime이 자동으로 등록, 갱신, 해제해야
한다. 수동 API는 이 자동 경로를 대체하는 일반 사용법이 아니라, custom runtime과 운영 도구를 위한
고급 표면이다.

| 이벤트 | 동작 |
|--------|------|
| spot created | spot location을 등록한다. `spot rid`로 현재 owner node를 찾을 수 있어야 한다. |
| spot stopped or destroyed | spot location을 remove하거나 stale 처리한다. |
| actor created in entry spot | entry spot actor location을 등록한다. `location_kind=ENTRY_SPOT`, `node_rid=actor.node_rid`, `spot_rid=empty`. |
| actor joined user spot | spot actor location으로 갱신한다. `location_kind=SPOT`, `node_rid=actor.node_rid`, `spot_rid=current spot rid`. |
| actor left user spot and returned to entry spot | entry spot actor location으로 갱신한다. |
| actor moved to another node | 새 node owner가 actor location을 갱신한다. 이전 owner row는 generation으로 밀려난다. |
| actor destroyed | actor location을 remove한다. |
| owner heartbeat timeout | registry가 owner에 속한 actor location row를 stale 또는 removed로 전환한다. 기본 resolve는 반환하지 않는다. |

framework는 이 lifecycle을 수동 handler 코드에 맡기지 않는다. actor manager/entry spot/user spot,
spot node runtime이 registry-backed locator를 켰을 때 자동으로 등록과 해제를 수행한다.

## 12. 오류 규칙

| 상황 | 결과 |
|------|------|
| discovery/client/registry handle이 NULL | 실패, `EINVAL` |
| actor id가 NULL 또는 empty | 실패, `EINVAL` |
| actor id가 `ZLINK_ACTOR_ID_MAX` 이상 | 실패, `ENAMETOOLONG` 또는 `EINVAL` |
| actor type이 너무 김 | 실패, `ENAMETOOLONG` 또는 `EINVAL` |
| node rid가 empty인 location 등록 | 실패, `EINVAL` |
| `location_kind == SPOT`인데 spot rid가 empty | 실패, `EINVAL` |
| 조회 결과 없음 | 실패, `ENOENT` |
| registry 연결 없음 | 실패, 연결 상태에 맞는 errno |
| stale owner row만 있음 | 실패, `ENOENT` |
| 출력 버퍼가 NULL | 실패, `EINVAL` |

## 13. 문서 반영 위치

구현 완료 전에는 이 문서만 유지한다. 구현이 끝나면 아래 문서에 나누어 반영한다.

### core/doc

- `core/doc/spec/core/registry.ko.md` 또는 대응 registry spec
  - actor location row, owner generation, stale cleanup 계약
- `core/doc/spec/core/discovery.ko.md` 또는 대응 discovery spec
  - actor/spot 공통 route location cache, freshness mode, actor location update/remove/resolve C API 계약
- `core/doc/spec/core/spot.ko.md`
  - spot resolver가 공통 route location cache를 사용하는 방식과 기존 spot-only cache 제거 사실
- `core/doc/guide/07-1-discovery.ko.md`
  - registry-backed actor lookup 사용 예
- `core/doc/guide/07-4-actor.ko.md`
  - actor 재연결과 actor location 의미
- `core/doc/internals/services-internals.ko.md`
  - registry row 저장, owner heartbeat cleanup, discovery cache 구조

### framework/doc

- `framework/doc/framework/common/sample/deliverydispatch/README.ko.md`
  - DeliveryDispatch의 actor 재연결 흐름을 registry-backed actor locator 기준으로 정리
- `framework/doc/framework/dotnet/` 및 각 언어 framework spec/guide
  - `UseRegistryActorResolver()`와 custom actor location resolver 사용법
- 공통 framework spec/guide
  - actor resolver contract, registry-backed resolver, custom resolver 차이

정식 문서에는 구현된 API만 반영한다. 구현 전 계획 문구나 후보 API는 정식 spec에 넣지 않는다.
