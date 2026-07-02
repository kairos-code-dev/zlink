# Actor Location Registry core/bindings/framework 적용 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, core C API, bindings, framework,
> 회귀 테스트, 문서 반영이 끝난 뒤에만 정식 spec/guide 문서에 나누어 반영한다.
>
> C API 계약 후보는 `core/doc/spec/draft/actor-location-registry.ko.md`를 기준으로 검토한다.

## 1. 목표

actor id로 actor의 현재 위치를 찾고, actor/spot runtime lifecycle이 위치 정보를 자동으로
갱신하는 기능을 core C API, bindings, framework까지 일관되게 제공한다. 수동 갱신 API도 열어 두되,
일반 샘플과 application handler가 직접 호출해야 하는 표면으로 만들지는 않는다.

이 기능이 필요한 이유는 stream client 재연결 때문이다. courier나 customer client가 다시
연결되면 새 actor를 만드는 것이 아니라 기존 actor 위치를 먼저 찾아야 한다. 기존 actor가
있으면 현재 session route만 다시 bind하고, 없을 때만 actor를 만든다. 그래야 actor가 들고
있던 pending offer, subscription, 최근 상태 같은 사용자별 상태가 유지된다.

최종 목표는 framework 사용자가 아래 두 방식을 모두 선택할 수 있게 하는 것이다.

1. Registry/Discovery를 사용하는 경우
   - spot 쪽의 `.UseRegistrySpotResolver()`처럼 actor 쪽도 registry-backed resolver를 켠다.
   - 예: `.UseRegistryActorResolver()`
2. Registry/Discovery를 사용하지 않는 경우
   - 애플리케이션이 actor location resolver를 직접 구현해 등록한다.
   - 예: `.AddActorLocationResolver<TResolver>()`

## 2. 비목표

- actor 업무 상태를 registry에 저장하지 않는다.
- registry가 actor placement policy를 결정하지 않는다.
- actor object를 public API로 노출하지 않는다.
- DeliveryDispatch 코드 구현을 이 계획 문서 작성 단계에서 바로 바꾸지 않는다.
- 구현 전 후보 API를 정식 spec/guide에 공개 계약처럼 반영하지 않는다.

## 3. 설계 요약

### 3.1 core C API

core는 actor location typed API를 제공한다.

- `zlink_discovery_update_actor_location(...)`
- `zlink_discovery_remove_actor_location(...)`
- `zlink_discovery_resolve_actor_location(...)`
- freshness mode를 받도록 변경된 `zlink_discovery_resolve_spot(...)`
- `zlink_discovery_set_route_location_cache_options(...)`
- `zlink_discovery_get_route_location_cache_options(...)`
- `zlink_registry_query_client_actor_location(...)`
- `zlink_registry_query_client_actor_locations(...)`
- in-process registry query 대응 API

상세 시그니처와 타입은 `core/doc/spec/draft/actor-location-registry.ko.md`에 둔다.

### 3.2 binding 라이브러리

각 binding은 core typed API를 언어별 idiom으로 감싼다.

- raw route value bytes를 사용자에게 만들게 하지 않는다.
- actor id, actor type, node rid, spot rid, generation, location kind를 구조화된 타입으로
  제공한다.
- nullable/optional spot rid로 entry spot actor와 spot actor를 구분한다.
- VM 언어는 native struct lifetime을 안전하게 복사한다.

### 3.3 framework

framework는 actor location resolver 추상화를 제공한다.

.NET 기준 후보:

```csharp
public interface IZLinkActorLocationResolver
{
    ValueTask<ZLinkActorLocation?> ResolveActorLocationAsync(
        string actorId,
        string? actorType = null,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationPublisher
{
    ValueTask PublishActorLocationAsync(
        ZLinkActorLocation location,
        CancellationToken cancellationToken = default);

    ValueTask RemoveActorLocationAsync(
        string actorId,
        string? actorType = null,
        CancellationToken cancellationToken = default);
}
```

`ZLinkActorLocation` 후보:

```csharp
public sealed record ZLinkActorLocation(
    ActorRef Actor,
    string? ActorType,
    ZLinkActorLocationKind Kind,
    RoutingId? SpotRid,
    ZLinkSpotKind SpotKind);

public enum ZLinkResolveFreshness
{
    Normal,
    Refresh,
    Direct
}
```

builder 후보:

```csharp
options.AddActorLocationResolver<TResolver>();
options.AddActorLocationStore<TStore>();
options.UseRegistryActorResolver();
options.ConfigureRouteLocationCache(cache =>
{
    cache.ActorCacheEnabled = true;
    cache.SpotCacheEnabled = true;
    cache.MaxEntries = 4096;
    cache.PositiveTtl = TimeSpan.FromSeconds(1);
    cache.NegativeTtl = TimeSpan.Zero;
});
```

SPOT mesh builder 후보:

```csharp
options.AddSpotMesh("delivery-couriers")
    .UseRegistrySpotResolver()
    .UseRegistryActorResolver();
```

자동 갱신과 수동 갱신 책임:

- registry-backed resolver를 켜면 framework runtime이 actor create/join/leave/destroy lifecycle에서
  actor location을 자동 publish/remove한다.
- registry-backed spot resolver를 켜면 framework runtime이 spot create/start/stop/destroy lifecycle에서
  spot location을 자동 publish/remove한다.
- custom resolver를 쓰면 조회만 담당한다. actor lifecycle publish/remove도 필요하면
  `AddActorLocationStore<TStore>()`로 저장소를 별도 등록하거나, 같은 구현체가 resolver와 store
  interface를 모두 구현하도록 등록한다.
- 수동 publish/remove API는 custom runtime, 운영 도구, 테스트를 위해 제공한다. 일반 handler 코드가
  재연결 흐름을 만들기 위해 직접 호출하는 API로 안내하지 않는다.
- actor manager, session bind, actor relay, entry spot actor dispatch는 resolver를 통해 기존
  actor 위치를 먼저 찾고, 없으면 기존 create/get-or-create 경로를 사용한다.
- route location cache 옵션은 actor와 spot cache 사용 여부를 따로 설정할 수 있어야 한다.

## 4. 기존 코드 감사 단계

구현 전에 현재 checkout의 실제 상태를 감사한다.

확인할 항목:

- `core/include/zlink/service/actor.h`
  - `zlink_actor_ref_t`
  - `zlink_actor_route_t`
- `core/include/zlink/service/discovery.h`
  - `ZLINK_ROUTE_KIND_ACTOR`
  - `zlink_discovery_resolve_actor(...)`
  - generic bind/unbind/resolve route API
  - 기존 `zlink_discovery_resolve_spot(...)` cache 동작
- `core/src/api/discovery/`
  - actor route kind가 실제로 구현되어 있는지
  - value serialization 형식이 있는지
- `core/src/runtime/services/discovery/`
  - 기존 spot 전용 cache인 `_summary_store`, `resolve_spot_cache_ttl_ms`,
    `try_resolve_spot_from_cache_locked`, `refresh_spot_owner_cache_locked`
  - `service_update_seq` 기반 spot cache 무효화 방식
- `core/src/api/registry/`
  - route row owner generation, stale cleanup, query client 지원 상태
- `framework/languages/dotnet/src/Zlink.Framework`
  - `UseRegistrySpotResolver()`
  - `AddSpotRemoteAddressResolver<TResolver>()`
  - actor manager `FindAsync` / `GetOrCreateAsync`
  - actor join/entry spot route 흐름

이전 public API를 유지하는 전환 기간은 두지 않는다. 감사 결과와 관계없이 public API는 새 계약으로 한 번에 정리한다.
`zlink_discovery_resolve_actor(...)`와 `zlink_actor_route_t`는 공개 표면에서 제거하거나 내부 전용으로
내린다. `zlink_discovery_resolve_spot(...)`는 이름을 유지하더라도 freshness mode 인자를 받는
새 시그니처로 변경한다.

## 5. Core 구현 계획

### 5.1 public header

변경 대상:

- `core/include/zlink/service/actor.h`
- `core/include/zlink/service/discovery.h`
- `core/include/zlink/service/registry.h`
- 필요하면 `core/include/zlink.h` include 확인

추가/정리:

- `zlink_actor_location_kind_t`
- `zlink_actor_location_flags_t`
- `zlink_actor_location_t`
- `zlink_route_resolve_mode_t`
- `zlink_route_location_cache_options_t`
- `zlink_registry_actor_location_filter_t`
- discovery actor location update/remove/resolve API
- freshness mode를 받는 spot resolve API
- discovery route location cache option API
- registry actor location query API

### 5.2 registry storage

registry에 actor route row 저장소를 추가한다.

키:

- actor type
- actor id

값:

- actor ref
- location kind
- spot rid
- spot kind
- owner routing id
- owner generation
- updated timestamp

정책:

- owner heartbeat가 끊기면 row를 stale/remove 처리한다.
- 최신 owner generation만 같은 actor key를 갱신할 수 있다.
- query 기본값은 stale row를 반환하지 않는다.

### 5.3 actor/spot 공통 route location cache

기존 spot 전용 cache는 제거하고 actor와 spot이 같은 route location cache를 사용하게 한다.
현재 spot cache는 `_summary_store`와 `resolve_spot_cache_ttl_ms` 중심으로 동작하므로, actor와
spot을 1:1로 매칭하는 high-cardinality 사용 패턴에서는 메모리 증가와 정책 불일치가 생길 수 있다.

제거/정리 대상:

- `resolve_spot_cache_ttl_ms` 같은 spot 전용 TTL 상수
- `try_resolve_spot_from_cache_locked(...)`
- `refresh_spot_owner_cache_locked(...)`
- spot owner 조회 결과를 `_summary_store`에 장기 누적하는 코드
- spot 전용 cache 무효화를 전제로 한 테스트 기대값

새 구현 방향:

- `route_location_cache_t` 같은 discovery 내부 공통 cache를 만든다.
- cache key는 `channel + route kind + route id`로 둔다.
- actor route id는 `(actor_type, actor_id)`이다.
- spot route id는 `spot rid`이다.
- cache value는 positive/negative 여부, route location, owner/generation 정보, validated time을
  함께 보관한다.
- actor cache와 spot cache는 각각 켜고 끌 수 있어야 한다.
- cache는 반드시 max entry 수를 가진다.
- eviction은 LRU 또는 clock 방식으로 구현한다.
- TTL은 cache hit 허용 시간이며, entry 제거 정책은 max entry eviction이 담당한다.

기본 정책 후보:

| 항목 | 기본값 후보 | 설명 |
|------|-------------|------|
| actor cache enabled | true | actor location cache 사용 여부다. |
| spot cache enabled | true | spot location cache 사용 여부다. |
| max entries | 4096 | actor/spot location entry를 합친 기본 상한이다. |
| positive TTL | 1000 ms | 일반 actor/spot 조회의 기본 TTL이다. |
| stable spot TTL | 5000 ms | 이동이 드문 service spot에 사용할 수 있다. |
| actor-owned spot TTL | 1000 ms | actor와 1:1로 매칭되는 spot에 사용한다. |
| negative TTL | 0 ms | 기본은 not-found cache 비활성이다. 필요하면 50-100 ms로 켠다. |

freshness mode:

| mode | 동작 |
|------|------|
| Normal | cache hit 허용. miss 또는 TTL 만료 시 registry 조회 후 cache 저장. |
| Refresh | cache가 있어도 registry 조회. 조회 결과로 cache 갱신. |
| Direct | registry 조회. cache를 읽지도 쓰지도 않음. |

`zlink_discovery_resolve_spot(...)`는 freshness mode 인자를 받는 새 시그니처로 변경한다.
`zlink_discovery_resolve_actor_location(...)`은 처음부터 freshness mode를 받는다.

재연결과 “없으면 생성” 판단은 반드시 `Refresh`를 사용한다. 일반 send/request 경로는 `Normal`을
사용한다. 운영 진단이나 cache 오염을 피해야 하는 검증은 `Direct`를 사용한다.

registry 연결이 없거나 authoritative 조회가 실패하면 명확한 error를 반환한다. 단순히 오래된
cache를 성공처럼 반환하는 fallback은 기본 동작으로 두지 않는다. stale fallback이 필요하면 별도
설계 초안에서 caller가 허용한 경우로 분리한다.

cache 사용 여부 조합:

| actor cache | spot cache | 사용 예 |
|-------------|------------|---------|
| on | on | 기본값. 일반적인 registry-backed actor/spot lookup에 사용한다. |
| off | on | actor 재연결 정확성을 우선하고 spot 수가 적은 환경에 사용한다. actor resolve는 항상 registry를 본다. |
| on | off | actor마다 spot을 1:1로 만들고 spot churn이 큰 환경에 사용한다. spot resolve는 항상 registry를 본다. |
| off | off | cache를 전혀 쓰지 않는 진단, 테스트, 강한 최신성 요구 환경에 사용한다. |

### 5.4 typed API와 generic route API

typed API는 generic route API 위에 구현할 수 있다. 다만 public 표준은 typed API다.

- generic route value serialization은 core 내부 detail로 둔다.
- bindings/framework는 generic route value bytes를 직접 만들지 않는다.
- `ZLINK_ROUTE_KIND_ACTOR`는 typed actor location API가 사용하는 route kind로 유지한다.

## 6. Core C API 회귀 테스트

core 테스트는 framework 없이 C API만 검증한다.

| 테스트 | 기대 결과 |
|--------|-----------|
| manual actor location update/resolve entry spot | 수동 갱신 API로 entry spot actor location을 갱신한 후 actor id로 조회하면 node rid, actor id, generation, kind가 맞다. spot rid는 empty 허용이다. |
| manual actor location update/resolve spot | 수동 갱신 API로 spot actor location을 갱신한 후 조회하면 node rid와 spot rid가 모두 맞다. |
| automatic actor create publish | actor 생성 시 runtime이 actor location을 자동 등록한다. |
| automatic actor join update | actor가 user spot에 join하면 runtime이 actor location의 spot rid를 자동 갱신한다. |
| automatic actor leave update | actor가 user spot에서 leave하면 runtime이 entry spot actor location으로 자동 갱신한다. |
| automatic actor destroy remove | actor destroy 시 runtime이 actor location을 자동 제거한다. |
| automatic spot create publish | spot 생성/start 시 runtime이 spot location을 자동 등록한다. |
| automatic spot destroy remove | spot stop/destroy 시 runtime이 spot location을 자동 제거한다. |
| missing actor | 없는 actor id 조회는 실패하고 `ENOENT` 계열 errno를 설정한다. |
| invalid input | NULL handle, empty actor id, empty node rid, SPOT kind의 empty spot rid는 실패한다. |
| overwrite same owner | 같은 owner generation이 같은 actor id를 다시 update하면 최신 location으로 교체된다. |
| stale owner cleanup | owner heartbeat timeout 후 resolve는 stale row를 성공으로 반환하지 않는다. |
| remove owner guard | 오래된 owner가 최신 owner row를 지우지 못한다. |
| query client single lookup | remote registry query client로 단건 actor location을 조회할 수 있다. |
| query client list lookup | filter별 목록 조회가 count-probe 패턴을 지킨다. |
| actor cache disabled | actor cache를 끄면 반복 조회가 local cache를 사용하지 않고 registry 조회 경로를 탄다. |
| spot cache disabled | spot cache를 끄면 `resolve_spot`도 공통 cache를 사용하지 않고 registry 조회 경로를 탄다. |
| cache max entries | max entries를 넘으면 오래된 actor/spot location entry가 eviction된다. |
| freshness refresh | cache hit 가능한 entry가 있어도 `Refresh` 조회는 registry 결과로 cache를 갱신한다. |
| freshness direct | `Direct` 조회는 기존 cache를 사용하지 않고, 조회 결과도 이후 `Normal` cache hit로 사용되지 않는다. |
| negative cache disabled | negative TTL 0이면 not-found 결과가 cache되지 않는다. |
| actor-owned spot churn | actor별 spot을 많이 생성/삭제해도 discovery cache entry 수가 max entries를 넘지 않는다. |

실행:

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

프로젝트의 실제 test runner 이름이 다르면 구현 단계에서 현재 CMake target을 확인해 기록한다.

## 7. bindings 적용 계획

core 구현이 끝나면 local core runtime을 bindings에 동기화한다.

```bash
cmake --build core/build
bindings/dev_sync_local_core_libs.sh
```

주의:

- `bindings/dev_sync_local_core_libs.sh`는 개발 검증용 배포 단계다.
- 복사된 native library 산출물은 release artifact이므로 커밋하지 않는다.
- header sync 뒤 각 binding의 native declarations를 갱신한다.

언어별 작업:

| binding | 작업 | 회귀 테스트 |
|---------|------|-------------|
| C | header와 C sample/test에서 actor location typed API 사용 예 추가 | C binding build/test |
| C++ | `actor_location` value type과 discovery/registry query wrapper 추가 | C++ binding build/test |
| .NET | P/Invoke 선언, SafeHandle/struct marshal, `ActorLocation` managed type 추가 | bindings/dotnet test |
| Java | native mapping/JNI 또는 Panama mapping에 actor location API 추가 | Java binding test |
| Kotlin | Java binding 위 typed extension 또는 data class 추가 | Kotlin sample/build test |
| Node | native addon에 actor location update/remove/resolve/query 추가, JS object mapping 추가 | Node binding test |
| Python | cffi/extension mapping 추가 | Python binding test |
| Go | cgo wrapper와 struct mapping 추가 | Go binding test |
| Rust | FFI binding과 safe wrapper 추가 | Rust binding test |

binding 공통 테스트:

- entry spot actor location roundtrip
- spot actor location roundtrip
- missing actor
- too-long actor id/type
- empty optional spot rid mapping
- generation 값 보존

## 8. framework 적용 계획

### 8.1 공통 framework 계약

공통 계약:

- actor location resolver interface
- registry-backed actor resolver
- custom actor resolver 등록
- actor location publisher interface
- actor lifecycle 자동 publish/remove

framework 사용자는 spot resolver와 비슷한 방식으로 actor resolver를 켠다.

```csharp
options.AddSpotMesh("delivery-couriers")
    .UseRegistrySpotResolver()
    .UseRegistryActorResolver();
```

Discovery/Registry를 사용하지 않는 샘플이나 앱은 custom resolver를 등록한다.

```csharp
options.AddActorLocationResolver<MyActorLocationResolver>();
options.AddActorLocationStore<MyActorLocationStore>();
```

custom resolver 요구:

- actor id/type으로 actor location을 반환한다.
- actor lifecycle publish/remove를 지원하려면 store를 함께 등록한다.
- read-only resolver만 등록하면 framework가 publish 비활성 상태임을 startup validation으로
  확인한다.
- resolver가 stale location을 반환하면 actor request가 명확한 error를 낸다.

### 8.2 .NET framework 우선 적용

.NET을 기준 구현으로 둔다.

작업:

- `IZLinkActorLocationResolver`
- `IZLinkActorLocationPublisher`
- `ZLinkActorLocation`
- `ZLinkActorLocationKind`
- `UseRegistryActorResolver()`
- `AddActorLocationResolver<TResolver>()`
- `AddActorLocationStore<TStore>()`
- registry-backed resolver 구현
- custom store publish/remove 연결
- actor lifecycle publish/remove 연결
- spot lifecycle publish/remove 연결
- session bind와 actor request 경로에서 resolver 사용

회귀 테스트:

- registry-backed actor resolver를 켜면 actor create 시 location row가 등록된다.
- actor가 user spot에 join하면 spot rid가 포함된 location으로 갱신된다.
- actor가 entry spot 상태이면 spot rid 없이 node rid만 반환된다.
- registry-backed spot resolver를 켜면 spot create/start/stop/destroy 시 spot location row가 자동 갱신된다.
- client 재연결 시 기존 actor location을 찾아 session만 재bind한다.
- resolver 미등록 상태에서 remote actor lookup이 필요한 API를 호출하면 명확한 설정 오류가 난다.
- custom resolver를 등록하면 registry 없이 actor location lookup이 동작한다.
- stale actor location이면 request timeout이 아니라 route-not-found 성격의 오류가 난다.

### 8.3 다른 framework 언어 적용

.NET 기준 구현과 테스트가 끝난 뒤 Java/Kotlin/Node/C++ 순서로 반영한다.

순서:

1. public contract/type 추가
2. registry-backed resolver 연결
3. custom resolver 등록 API
4. actor lifecycle publish/remove
5. sample/e2e 업데이트
6. 언어별 guide/spec 업데이트

공통 검증:

- DeliveryDispatch 또는 SupportChat에서 재연결 actor lookup 흐름을 검증한다.
- registry를 쓰는 config와 custom resolver config를 최소 하나씩 둔다.
- public API 이름은 언어 관용을 따르되 의미는 .NET 기준과 같게 유지한다.

## 9. 문서 반영 계획

### 9.1 구현 전

유지할 문서:

- `core/doc/spec/draft/actor-location-registry.ko.md`
- `framework/doc/plan/framework-actor-location-registry-plan.ko.md`

정식 spec/guide에는 아직 반영하지 않는다.

### 9.2 core/doc 구현 후 반영

구현과 core 회귀 테스트가 끝나면 아래 문서를 갱신한다.

- `core/doc/spec/core/registry.ko.md`
  - actor location row, owner generation, stale cleanup
- `core/doc/spec/core/discovery.ko.md`
  - actor/spot 공통 route location cache
  - route resolve freshness mode
  - route location cache option API
  - actor location update/remove/resolve C API
- `core/doc/spec/core/spot.ko.md`
  - 기존 spot 전용 cache 제거 후 공통 route location cache를 사용하는 정책
  - freshness mode 인자를 받는 새 `zlink_discovery_resolve_spot(...)` 계약
  - entry spot actor와 spot actor location 차이
- `core/doc/guide/07-1-discovery.ko.md`
  - actor location resolver 사용 예
- `core/doc/guide/07-4-actor.ko.md`
  - actor 재연결과 actor location
- `core/doc/internals/services-internals.ko.md`
  - registry/discovery 내부 저장과 cleanup
  - 기존 `_summary_store` 기반 spot cache 제거와 새 `route_location_cache_t` 구조

### 9.3 framework/doc 구현 후 반영

- 공통 framework spec/guide
  - actor resolver contract
  - registry-backed actor resolver
  - custom resolver
- `framework/doc/framework/dotnet/`
  - .NET builder API와 guide
- `framework/doc/framework/java/`, `framework/doc/framework/kotlin/`, `framework/doc/framework/node/`,
  `framework/doc/framework/cpp/`
  - 언어별 API와 예제
- `framework/doc/framework/common/sample/deliverydispatch/README.ko.md`
  - DeliveryDispatch의 actor lookup/rebind 흐름을 구현된 public API 기준으로 갱신

정식 문서에는 구현된 이름과 실제 signature만 쓴다.

## 10. DeliveryDispatch 적용 계획

문서와 framework 구현이 준비되면 DeliveryDispatch 샘플을 갱신한다.

목표 흐름:

1. courier client connect
2. `CourierSession`이 actor resolver로 `courier-a` actor location 조회
3. 있으면 기존 actor에 session route rebind
4. 없으면 placement policy로 target node를 정하고 actor 생성
5. spot lifecycle과 actor lifecycle이 location을 registry/custom resolver store에 자동 publish
6. DispatchWorker는 `courier-a` actor location을 찾아 offer를 보냄

customer도 같은 원칙을 따른다.

1. customer client subscribe/connect
2. `CustomerGateway`가 customer actor location 조회
3. 있으면 기존 actor에 session rebind
4. 없으면 actor 생성
5. Tracking은 customer actor location으로 status notify route

샘플 회귀:

- courier 재연결 후 같은 actor generation에 session route만 바뀌는지 확인
- customer 재연결 후 기존 subscription 상태가 유지되는지 확인
- actor location resolver가 없을 때 샘플 startup이 실패하는지 확인
- registry-backed resolver와 custom in-memory resolver 중 최소 하나를 runner에서 검증

## 11. 전체 실행 순서

1. 현재 core actor/discovery/registry route 선언과 구현 감사
2. `core/doc/spec/draft/actor-location-registry.ko.md` 확정 리뷰
3. core C API 구현
4. core 회귀 테스트 작성/통과
5. `cmake --build core/build`
6. `bindings/dev_sync_local_core_libs.sh`
7. bindings native declarations/wrappers 추가
8. binding별 회귀 테스트 통과
9. .NET framework resolver/publisher 구현
10. .NET framework 회귀 테스트와 DeliveryDispatch focused test
11. Java/Kotlin/Node/C++ framework 순차 반영
12. core/doc 정식 spec/guide/internals 반영
13. framework/doc 정식 spec/guide/sample 문서 반영
14. 전체 sample/e2e 회귀 실행

## 12. 완료 기준

- core C API actor location tests 통과
- 모든 bindings가 새 core header와 빌드됨
- .NET framework에서 `UseRegistryActorResolver()`와 custom resolver가 모두 동작함
- 적어도 DeliveryDispatch에서 actor 재연결이 기존 actor location을 사용함
- registry owner timeout 시 stale actor location이 성공 조회되지 않음
- core/doc 정식 문서와 framework/doc 정식 문서가 실제 구현과 일치함
- dev sync로 생긴 native library 산출물이 커밋에 포함되지 않음
