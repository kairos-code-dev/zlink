# Framework Location Store 포팅 inventory (초안)

> 이 문서는 .NET 첫 구현이 확정한 location resolver/store 계약을 Java, Kotlin, Node.js, C++
> framework로 포팅하기 위한 작업 목록이다. 계약 기준은
> `framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`이고,
> 검증 기준은 `framework/doc/framework/common/e2e/`의 갱신된 config 문서다.
> .NET public API 이름은 각 언어 관용에 맞춰 옮기되 의미는 같아야 한다.

## 1. 포팅 대상 공통 표면

.NET 기준 구현이 확정한 표면. 각 언어는 같은 의미의 public contract를 제공해야 한다.

| 영역 | 표면 | draft 절 |
|------|------|----------|
| 모델 | peer/spot/actor/route location, owner lease(+snapshot: StoreNow 포함), key 4종, filter 4종, page request/page | 6절 |
| 값 집합 | auto-connect type 5종, role 5종 — key 직렬화는 canonical 소문자 문자열로 언어 간 동일 | 6.5 |
| store | peer/spot/actor/route store(write intent, owner guard remove, RemoveByOwner, list), owner lease store(TTL 전달, snapshot), optional watch/change stamp | 7절 |
| write 계약 | write 결과 4종, intent 3종(NewClaim/Renew/Takeover), owner token, read=예외·write=StoreUnavailable | 7.6 |
| resolver | peer list resolver(freshness), spot/actor/route 단건 resolver(freshness Normal/Refresh) | 8절 |
| 운영 조회 | runtime query(freshness 없음, status/topology/summary/paged list) | 8.2 |
| 등록 | store별 등록 + in-memory opt-in + location options + 일괄 등록 hook(AddLocationStores 상당) | 20.2 |
| event | location-runtime source: StatusChanged/TopologyChanged/ServiceSummaryChanged | 20.5 |
| runtime 정책 | owner lease heartbeat(runtime당 1회), 소유권 상실 시 deactivate, claim-then-activate, fail-static과 복구 순서, 자동연결 planner/reconciler/pairwise initiator, change stamp polling | 9, 14~17절 |

## 2. .NET 기준 아티팩트 (참조용)

- 계약: `framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/`
- runtime 정책: `framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/`
  (in-memory store, resolver, owner lease tracker, planner/reconciler/loop, lifecycle, runtime, query)
- Redis extension: `framework/languages/dotnet/src/Zlink.Framework.Locations.Redis/`
- 회귀 테스트: `tests/Zlink.Framework.UnitTests/Runtime/*Location*`, `tests/Zlink.Framework.Locations.Redis.Tests/`

Redis key schema와 Lua script는 언어 중립 계약이다. 다른 언어의 Redis extension은
`ZLinkRedisLocationScripts`의 Lua와 key schema(`{prefix}:row/gen/keys/own/lease/leases/stamp`)를
그대로 재사용해야 같은 store를 공유하는 혼합 언어 cluster가 성립한다.

## 3. 언어별 상태

| 언어 | framework 계약 | in-memory | runtime 정책 | Redis extension | E2E 전환 |
|------|----------------|-----------|--------------|-----------------|----------|
| .NET | 완료 | 완료 | 완료 | 완료 (StackExchange.Redis) | 진행 중 |
| Java | 미착수 | 미착수 | 미착수 | 미착수 (권장 client: Lettuce) | 미착수 |
| Kotlin | 미착수 | 미착수 | 미착수 | 미착수 (Java와 공유 검토) | 미착수 |
| Node.js | 미착수 | 미착수 | 미착수 | 미착수 (권장 client: ioredis) | 미착수 |
| C++ | 미착수 | 미착수 | 미착수 | 미착수 (권장 client: hiredis) | 미착수 |

포팅 순서는 draft 21절의 포팅 순서를 따른다: feature map 갱신 → 언어별 store/resolver interface +
Redis extension 등록 방식 추가 → 기존 registry/discovery E2E를 location store E2E로 교체.

## 4. 계약 gap과 주의점 (모든 언어 공통)

.NET 구현 중 확인된, 계약이 아직 답하지 않았거나 의도적으로 좁힌 지점. 포팅 시 같은 제약을
유지하고, 확장이 필요하면 먼저 draft를 고친다.

1. **원격 runtime query client 없음** — 계약은 process 내 `IZLinkLocationRuntimeQuery`만 정의한다.
   E2E와 운영 도구는 각 역할 서버가 노출하는 HTTP endpoint로 조회한다(구 registry topology
   query client의 대체). 원격 client가 필요하면 draft 신규 절로 설계한다.
2. **claim 충돌 시 원격 actor 사용(17절 2·6단계)** — 현재 .NET create 경로는 local actor를
   반환해야 하므로, 다른 node가 이미 만든 actor는 위치를 resolve해 오류에 담아 알릴 뿐 투명하게
   route-back하지 않는다. 원격 create/route-back protocol은 별도 설계 후보다.
3. **actor 재조회의 key 구성** — 공개 lookup 표면(`FindAsync(actorId)` 계열)이 actor type을 받지
   않아 재연결 Refresh resolve는 claim 충돌 지점에만 연결되어 있다. lookup에 type을 추가하는 것은
   public contract parity 결정이 필요하다(AGENTS.md parity 정책).
4. **native entry-spot join의 소유권** — framework runtime이 없는 대상 node로의 native 이동은
   대상이 claim할 주체가 없어, 원 소유자가 row의 NodeRid만 Renew한다. 원 process가 죽으면 lease와
   함께 row가 만료된다는 한계를 계약 문서에 명시된 대로 유지한다.
5. **registry clustering 계열 검증 삭제** — 구 DR 시나리오의 registry 병합 뷰/embedded registry/
   query client 동등성 검증은 단일 authoritative store 계약에서 성립하지 않아 삭제되었고(config-6
   재작성), store 장애/복구 검증(SF-*)이 그 축을 대체한다.
6. **Redis cluster** — 공식 extension의 Lua는 row/gen/index key가 한 slot에 있어야 한다.
   cluster 배포는 hash-tagged key prefix가 전제라는 점을 각 언어 extension 문서에 명시한다.
7. **Redis 호출은 반드시 논블로킹** — `location-store-redis.ko.md`가 "Redis 응답 지연/실패가
   framework runtime을 블록하면 안 된다"를 명문화한다. .NET(`StackExchange.Redis` async API,
   `ValueTask`/`ConfigureAwait(false)` 전 구간)과 Node.js(`redis` client의 Promise 기반
   `sendCommand`, 이벤트 루프 논블로킹 소켓)는 이미 이 계약을 지킨다. 나머지 언어는 포팅 시
   같은 수준을 실측으로 증명해야 한다.
   - **Kotlin**: 코루틴 기반이므로 Redis client가 진짜 suspend 논블로킹이어야 한다(예: Lettuce
     코루틴 확장). Jedis 같은 블로킹 전용 client는 쓰지 않는다. 부득이하게 블로킹 client를
     감싸면 반드시 `Dispatchers.IO`로 격리하고, 코루틴 연산에 쓰이는 `Dispatchers.Default`
     공유 스레드풀과 절대 공유하지 않는다.
   - **C++**: 이 Redis I/O가 core 메시징 엔진의 고정 I/O 스레드(`PERF_IO_THREADS=4`,
     spec-fixed — 늘리지 말 것)와 스레드를 공유하면 안 된다. 논블로킹 client(예:
     `redis-plus-plus` async 인터페이스, 또는 hiredis + libevent/libuv)를 전용 이벤트
     루프/스레드에서 돌려 core I/O 경로와 격리한다.
   - 공통 검증: Redis를 인위적으로 느리게 만든 상태에서 무관한 동시 spot/actor 처리(코루틴/
     이벤트 루프 task)의 p99 latency가 영향받지 않는지 부하 테스트로 확인한다.

## 5. 다음 갱신 시점

.NET E2E/sample 전환과 신규 bindings 통합이 끝나면 이 문서의 3절 상태 표와
`framework/doc/framework/common/spec/` 반영 계획(draft 23절)을 갱신한다.
