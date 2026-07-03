<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: 메시지 흐름 추적과 dispatch 관측](message-flow-tracing.ko.md) | [다음: Location Store — Redis](location-store-redis.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

# Location Runtime

이 문서는 framework의 **분산 위치 관리(location runtime)** 언어 중립 공통 스펙이다. 위치 모델
(peer/spot/actor/route row), owner lease와 generation, store/resolver 계약, 자동 연결 규칙,
watch/polling, 운영 조회 projection의 의미는 이 문서가 소유한다. 다른 spec 문서는 이 문서를
링크하고 세부 모델을 반복하지 않는다. 네이밍은 [framework API](framework-api.ko.md)와
[공통 스펙 README §5.2.1](../README.ko.md#521-네이밍-규칙)의 canonical 규칙을 따른다.

> core discovery/registry는 framework 표면에서 제거됐다. 위치 정보의 기준 저장소는 사용자가
> 등록하는 **location store**(공식 Redis extension 또는 사용자 구현체)이며, framework runtime이
> 그 위에서 lease join, generation guard, 자동 연결을 수행한다.

## 1. 기본 원칙

- **store는 저장만, 정책은 runtime이.** store 구현체는 row 저장/조회/원자적 write만 책임진다.
  owner lease join, generation guard, 자동 연결 diff는 framework runtime의 책임이다.
- **캐시 없음.** resolver와 운영 조회의 모든 읽기는 store에 도달한다. freshness 매개변수나
  cache TTL 같은 개념은 계약에 없다. 위치를 반복 사용하는 쪽(메시징 호출자)이 resolve 결과를
  보관하고, 실패 시 재resolve한다.
- **생존은 owner lease로.** row 자체는 생존을 증명하지 않는다. row owner의 lease가 만료되면
  그 owner의 모든 row는 성공 결과에서 제외된다(stale). 물리 삭제는 background cleanup의
  책임이고 계약 대상이 아니다.
- **시간은 store 기준.** lease 만료 판정에 application node의 wall clock을 쓰지 않는다.
  호출자는 TTL만 전달하고 절대 만료 시각은 store가 자기 시계로 계산한다.
- **fail-static.** store 장애는 즉시 연결 해제로 번역되지 않는다. 마지막으로 성공한 desired
  target set을 유지하고, 이미 수립된 연결의 메시징은 store와 독립적으로 계속 동작한다.

## 2. 위치 모델

네 종류의 location row가 있다. 모든 row는 owner-bound다: `OwnerId`(framework runtime instance
id)와 `Generation`(fencing token)을 가진다.

### 2.1 peer location

자동 연결에 필요한 node endpoint 정보. `AutoConnectType`(route mesh, client/server, dealer
mesh, fanout, spot mesh), `MeshName`, `NodeRid`, `Role`(router/dealer/pub/sub/spot),
`Endpoint`, `Weight`(0..100), `Value`, `Metadata`, `Capabilities`, `OwnerId`, `Generation`,
`UpdatedAt`. node lifecycle과 heartbeat가 자동 갱신한다.

### 2.2 spot location

`spot rid`가 어느 node에 있는지. `MeshName`, `SpotRid`, `SpotType`(선택), `NodeRid`,
`SpotKind`(`ENTRY_SPOT`/`USER_SPOT`), `RouteEndpoint`(선택), `OwnerId`, `Generation`,
`UpdatedAt`. spot lifecycle이 자동 갱신한다.

### 2.3 actor location

actor가 어느 node/spot에 있는지. `ActorType`, `ActorId`, `NodeRid`, `SpotRid`,
`LocationKind`(entry spot actor / user spot actor), `ActorGeneration`, `OwnerId`,
`Generation`, `UpdatedAt`. actor lifecycle이 자동 갱신한다.

### 2.4 route location

actor/spot으로 직접 표현되지 않는 framework route. `RouteKind`는 framework가 정의한 닫힌
집합(`ActorSession`, `SpotName`, `FrameworkRoute`)이며, application이 임의 key-value 저장소로
쓰는 표면이 아니다.

### 2.5 owner lease

runtime instance당 lease row 하나. `OwnerId`, `NodeRid`, `LeaseExpiresAt`(store가 계산),
`RenewedAt`. runtime start 때 만들어지고 heartbeat interval마다 upsert로 갱신된다. heartbeat
write는 runtime instance당 1회이므로 store 부하는 node 수에 비례한다.

## 3. Store 계약

책임별 interface 5종: peer/spot/actor/route store + owner lease store. 한 구현체가 전부 구현할
수 있으며, extension은 통합 계약(`IZLinkLocationStore` — 5종 결합)을 구현한 **인스턴스 하나**를
`AddLocationStore(instance)`로 등록한다(§8). 각 store는 공통으로 다음을 제공한다.

- `Update...(row, intent)` / `Remove...(key, ownerToken)` / `RemoveByOwner(ownerId)`
- 단건 `Resolve...(key)` (peer 제외) / filter 기반 `List...(filter[, page])`
- peer list와 owner lease list는 **pagination 없는 단일 snapshot**이다. reconcile의 desired
  set 계산은 한 시점의 전체 목록을 전제로 하므로 page 간 시점 불일치를 허용하지 않는다.
  mesh당 peer row 수천 상한을 전제로 하며 이를 넘으면 mesh를 분할한다.
- spot/actor/route 목록은 `ZLinkPageRequest`(page size, opaque continuation token)와
  `ZLinkLocationPage<T>`를 사용한다. 기본 page 크기는 `list page size` option을 따른다.

### 3.1 write 결과와 intent

update/remove는 `ZLinkLocationWriteResult`를 반환한다. 성공 시 store가 확정한 `Generation`과
`UpdatedAt`을 함께 돌려준다.

| 결과 | 의미 |
|------|------|
| `Stored` | 저장 또는 교체 성공 |
| `IgnoredStale` | 구세대 owner token의 update/remove라 무시. row 불변 |
| `RejectedConflict` | 살아 있는 row가 있는 key에 대한 new claim 실패(동시 claim 패배) |
| `StoreUnavailable` | store 연결/저장 실패 |

| intent | 성공 조건 | generation |
|--------|-----------|------------|
| `NewClaim` | 현재 row가 없거나 row owner의 lease가 만료된 경우에만 | store가 key별로 원자적으로 증가시킨 새 token 발급 |
| `Renew` | 현재 row와 같은 `OwnerId + Generation` 제시 | 불변 |
| `Takeover` | 살아 있는 row를 새 owner가 명시적으로 교체(framework가 의도한 이동 전용) | 새 token 원자 발급 |

`ZLinkLocationOwnerToken`은 `OwnerId + Generation`이다. read API는 store 장애를 infrastructure
error로 던지고, write API는 예외 대신 `StoreUnavailable`을 반환한다.

### 3.2 owner lease store

`RenewOwnerLease(ownerId, nodeRid, leaseTtl)`는 upsert다. 호출자는 절대 만료 시각이 아니라
TTL을 전달한다. `ListOwnerLeases()`는 lease 목록과 store 기준 현재 시각(`StoreNow`)을 한
snapshot으로 반환한다. 만료 판정은 `LeaseExpiresAt - StoreNow`와 조회 이후의 local monotonic
경과 시간으로 계산한다. `RemoveOwnerLease`는 owner 자신의 shutdown 경로와 운영 복구 도구
전용이다.

### 3.3 optional change stamp / watch

- **change stamp**(`IZLinkLocationChangeStampStore`): kind/mesh별 단조 증가 stamp를 싸게 읽어
  변경 유무를 감지한다. 구현하면 polling tick이 stamp만 읽고 변화가 있을 때만 목록을 읽는다.
- **watch**(`IZLinkLocationWatchStore`): 변경 이벤트 stream으로 reconcile을 깨운다.

둘 다 latency 최적화일 뿐이다. **polling이 correctness 경로**이며, stamp/watch 이벤트가
유실돼도 다음 polling 결과로 같은 상태에 도달해야 한다.

## 4. Owner/Generation 규칙

- generation은 store가 key별로 원자적으로 발급하며, **어떤 경로로도 node 사이에 배포되지
  않는다.** 승자는 자신의 write 응답에 담겨 온 generation을 owner token으로 보관하고 이후
  `Renew`/remove에 제시한다.
- 구 owner는 자기 token이 `IgnoredStale`로 거부되는 순간 소유권 상실만 알게 된다. token
  비교는 항상 store 안에서 일어나므로 generation 배포 protocol 없이 fencing이 성립한다.
- **claim-then-activate**: 위치 claim(`NewClaim`)이 성공한 쪽만 instance를 활성화한다. 패자는
  instance를 만들지 않고 재조회로 승자의 위치를 사용한다.
- 계획된 이동의 기본 경로는 **deactivate-first**(구 instance를 먼저 멈춘 뒤 `Takeover`)다.
  `Takeover`를 구 owner 무응답 fencing 경로로 쓸 때는, 구 owner가 `IgnoredStale`을 관찰할
  때까지 두 instance가 겹칠 수 있음을 감수한다.

## 5. Resolver 계약

resolver는 runtime과 application-facing client의 읽기 표면이다. 캐시가 없다 — 모든 조회가
store에 도달하고 owner lease join으로 유효성을 판정한다.

| resolver | 표면 | 용도 |
|----------|------|------|
| `IZLinkPeerLocationResolver` | `ListPeersAsync(filter)` | 자동 연결의 peer list 조회 |
| `IZLinkSpotLocationResolver` | `ResolveSpotAddressAsync(spotRid)` → `ZLinkSpotAddress?` | 메시징 조회: spot rid → spot full 주소 |
| `IZLinkActorLocationResolver` | `ResolveActorSpotAddressAsync(actorType, actorId)` → `ZLinkSpotAddress?` | 메시징 조회: actor → 그 actor가 위치한 spot의 full 주소 |
| `IZLinkRouteLocationResolver` | `ResolveRouteAsync(key)` | owner-bound route 단건 resolve |

- 메시징 resolver는 **주소**(`ZLinkSpotAddress` = `NodeRid + SpotRid`; mesh는 전송 문맥이
  결정)를 반환한다. 호출자가 주소를 보관하고 전송 실패 시 재resolve한다. 상세는
  [spot 주소 메시징](spot-address-messaging.ko.md)이 정본이다.
- location store 기반 Spot remote address resolver는 spot row의 mesh 이름으로 route mesh
  channel을 고른다. spot mesh 이름과 route mesh channel 이름이 다르면 location option에
  `spot mesh -> route mesh channel` 매핑을 등록해야 한다. 매핑이 없으면 같은 이름을 사용한다.
  이 매핑은 전송 channel 선택만 정하고, store row key나 spot lifecycle mesh 이름을 바꾸지 않는다.
- spot/actor/route resolver는 단건 resolve만 노출한다. 목록 조회는 peer resolver의 peer list,
  운영 조회 표면, store interface에만 둔다.
- generation이 필요한 lifecycle 흐름(재연결/없으면 생성 판단, takeover)은 resolver가 아니라
  store/runtime 경로를 사용한다.

### 5.1 stale row 제외

단건 resolve와 목록 조회의 성공 결과는 stale row를 포함하지 않는다. stale row는 (a) owner
lease가 만료된 owner의 row, (b) 같은 key에 대해 이 runtime이 이미 관찰한 generation보다
오래된 generation의 row(복제 지연 guard)다. 조건 (b)를 위해 runtime은 관찰한 최신 generation을
key별로 기억한다.

## 6. 자동 연결

framework runtime은 store를 등록한 배포에서 자동 연결 가능한 socket(연결형 client socket)의
mesh별 reconcile 루프를 돌린다.

1. **탐지**: change stamp(있으면) 또는 polling interval마다 peer list + owner lease snapshot을
   읽는다.
2. **desired set**: role 허용/target 매칭으로 dial 대상을 계산한다. endpoint가 없는 dial-only
   구성원은 pairwise initiator 순서와 무관하게 항상 dial한다. pairwise initiator 규칙에 따라
   상대가 나를 dial하는 peer는 desired set에 없어도 **mesh 구성원**이다(fail-fast 분류는
   desired set이 아니라 구성원 snapshot 기준).
3. **diff 적용**: 새 target은 connect, desired set에서 빠진 target은 disconnect. 같은 peer
   key의 endpoint 또는 **owner 변경**은 handover다 — 재시작한 peer가 같은 endpoint로 떠도 새
   dial이 필요하므로 disconnect 후 connect한다.
4. **crash 전파**: row write 없이 죽은 node는 owner lease 만료만으로 전파된다. lease join에서
   그 owner의 row가 제외되고 desired set에서 빠져 disconnect된다.

manual endpoint 연결(`EnableClient(endpoint)`)은 auto reconcile이 끊지 않는다.

### 6.1 store 장애와 복구

- 장애 중: fail-static. 기존 연결 유지, diff 계산 중단, `StoreUnavailable` 관측(§9).
  owner lease heartbeat는 backoff로 재시도한다.
- `store failure grace` 초과: 새 outbound connect만 중단하고, 이미 ready인 연결은 transport가
  살아 있는 한 유지한다.
- 복구: 각 노드는 **조회보다 먼저** 자기 owner lease와 local row를 재등록한다. disconnect
  diff는 heartbeat interval 1회 유예 뒤에 적용한다 — 살아 있는 peer들이 재등록을 마치기 전에
  한꺼번에 끊는 것을 막는다.

## 7. 운영 조회 (runtime query)

`IZLinkLocationRuntimeQuery`는 운영 도구와 E2E의 조회 표면이다. 모든 조회가 store를 직접
읽는다.

| API | 반환 |
|-----|------|
| `GetStatusAsync()` | `ZLinkLocationRuntimeStatus` — store health, watch enabled, polling interval, last refresh, last error, owner lease 갱신 상태 |
| `ListPeersAsync(filter)` | 살아 있는 raw peer row (snapshot) |
| `ListSpotsAsync/ListActorsAsync/ListRoutesAsync(filter, page)` | 살아 있는 raw row (paged) |
| `ListTopologyAsync(filter, page)` | `ZLinkLocationTopologyEntry` projection — location row + connection state + lease + generation을 runtime이 합성 |
| `ListServiceSummariesAsync(filter)` | mesh/type/role별 count summary |

topology/summary는 store가 아니라 **관찰 host 자신의 location runtime projection**이다. store가
topology 의미를 결정하지 않는다.

## 8. 등록 API와 option

### 8.1 등록

```csharp
// 개별 등록 (custom 구현체를 역할별로 나눠 제공할 때)
options.AddPeerLocationStore<TStore>();
options.AddSpotLocationStore<TStore>();
options.AddActorLocationStore<TStore>();
options.AddRouteLocationStore<TStore>();
options.AddOwnerLeaseStore<TStore>();

// extension package 통합 등록 (권장) — codec serializer 인스턴스 등록과 같은 형태
options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
    .SetConnectionString("...")
    .SetKeyPrefix("zlink:app")));
```

`AddLocationStore(instance)`는 통합 계약 `IZLinkLocationStore`(store 5종 결합)를 구현한
인스턴스 하나를 등록한다. 같은 인스턴스가 optional 계약(change stamp, watch)도 구현하면
자동으로 인식된다. extension은 전용 등록 함수를 만들지 않는다. 통합 등록과 개별 등록을 섞어
쓰는 것은 검증 오류다.

### 8.2 option

| option | 의미 | 기본값 후보 |
|--------|------|------------|
| heartbeat interval | owner lease 갱신 주기 | 5s |
| owner lease TTL | lease 만료 기준. 만료된 owner의 row는 전부 stale | 15s |
| polling interval | watch/stamp가 없거나 이벤트가 없을 때 store 재조회 주기 | 1s |
| list page size | 목록 조회 기본 page 크기 | 1000 |
| store failure grace | store 장애 중 신규 outbound connect를 허용하는 완충 시간 | 30s |
| spot router channel map | spot mesh 이름과 route mesh channel 이름이 다를 때 쓰는 전송 channel 매핑 | 빈 map |

cache 관련 option은 없다(캐시 제거 결정).

## 9. 관측 (event source)

registry/discovery event source는 location runtime event source로 대체됐다. 각 source의
kind는 닫힌 enum이다. 상세 계약은 [메시지 흐름 추적](message-flow-tracing.ko.md)의 observer
계약과 [비동기 실행 정책](async-execution-policy.ko.md)을 따른다.

| source | kind |
|--------|------|
| `location-runtime` | `StatusChanged`, `TopologyChanged`, `ServiceSummaryChanged`, `StoreUnavailable`, `StoreRecovered` |
| `location-peer` | peer row update/remove, auto-connect desired set 변경 |
| `location-spot` | spot row update/remove, spot resolve miss |
| `location-actor` | actor row update/remove, actor reconnect resolve miss |
| `location-route` | route row update/remove, route resolve miss |

`location-runtime` source는 runtime query 표면의 polling diff로 발행된다. store 장애는 source를
죽이지 않는다 — 조회 실패는 `StoreUnavailable` 이벤트로 강등되고 복구 시 `StoreRecovered`가
발행된다(fail-static).

## 10. 오류 규칙

| 상황 | 결과 |
|------|------|
| store 연결 실패 (read) | infrastructure error |
| store 연결 실패 (write) | `StoreUnavailable`. 기존 connection은 fail-static |
| 위치 없음 | not found |
| stale row만 있음 | not found |
| owner/generation 충돌 | conflict 또는 rejected |
| 잘못된 actor id/spot rid | validation error |

## 11. 구현체 요구와 참조 구현

store 구현체가 지켜야 하는 최소 요구:

- `NewClaim`/`Takeover`의 generation 발급과 lease-만료 판정이 **원자적**이어야 한다(동시
  claim에서 정확히 하나만 `Stored`).
- owner lease와 location row가 같은 물리 저장소를 공유해 "row owner의 lease 만료" 판정이
  원자적으로 가능해야 한다.
- `UpdatedAt`/`LeaseExpiresAt`은 store 기준 시각으로 기록한다.
- continuation token은 opaque하며 filter가 같은 연속 호출에서만 유효하면 된다.

공식 참조 구현은 Redis extension([location-store-redis](location-store-redis.ko.md))이다.
framework 본체는 특정 store 제품에 의존하지 않으며, in-memory 구현은 단일 process 테스트
전용이다. store 제품 자체의 HA/복제는 store 구현체 책임이고 framework가 검증하지 않는다.
