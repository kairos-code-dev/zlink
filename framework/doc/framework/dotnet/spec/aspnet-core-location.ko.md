# ASP.NET Core Location Integration

.NET framework 의 location store 등록·자동 연결·운영 조회 공개 계약이다. 언어 중립
의미(위치 모델, owner lease, generation, 장애 중 마지막 연결 판단 유지, 자동 연결 규칙)는
[공통 location runtime 스펙](../../common/spec/location-runtime.ko.md)이 소유하고, 이
문서는 .NET 표면(등록 API, 타입, DI)만 정의한다. 사용법 예제는
[guide 09-location](../guide/09-location.ko.md)을 본다.

## 1. 등록 표면

| API | 의미 |
|-----|------|
| `IZLinkFrameworkOptions.AddLocationStore(IZLinkLocationStore store)` | 하나의 물리 저장소 인스턴스를 등록한다. 이 인스턴스가 peer, spot, actor, route, owner lease 역할을 모두 맡는다. 같은 인스턴스가 `IZLinkLocationChangeStampStore`/`IZLinkLocationWatchStore` 도 구현하면 자동 인식된다. |
| `IZLinkFrameworkOptions.UseInMemoryLocationStores()` | 단일 프로세스 개발·단위 테스트용 메모리 저장소를 등록한다. 여러 프로세스가 서로 위치를 공유해야 하는 배포에서는 쓰지 않는다. |
| `IZLinkFrameworkOptions.ConfigureLocations()` → `ZLinkLocationOptions` | `HeartbeatInterval`, `OwnerLeaseTtl`, `PollingInterval`, `ListPageSize`, `StoreFailureGrace` |

공식 Redis extension 은 `Zlink.Framework.Locations.Redis` 패키지의
`ZLinkRedisLocationStore` 다. 옵션은 빌더 형식이다:
`new ZLinkRedisLocationStore(redis => redis.SetConnectionString(...).SetKeyPrefix(...))`
(`SetConfiguration(ConfigurationOptions)` 으로 StackExchange.Redis 옵션 직접 전달 가능).

`AddLocationStore(...)` 와 `UseInMemoryLocationStores()` 는 서로 대체 관계다. 둘 다
등록하면 startup 검증 오류다. 역할별 store 를 따로 등록하는 public API 는 없다. owner
lease 와 위치 row 가 같은 물리 저장소에 있어야 오래된 소유자 판정과 위치 갱신을 같은
규칙으로 처리할 수 있기 때문이다.

## 2. DI 로 노출되는 조회 표면

store 를 등록하면 아래 서비스가 DI 에 등록된다. 캐시가 없다 — 모든 조회는 store 에
도달한다.

| 서비스 | 표면 |
|--------|------|
| `IZLinkPeerLocationResolver` | `ListLivePeersAsync(ZLinkPeerLocationFilter)` |
| `IZLinkSpotRefResolver` | `ResolveSpotRefAsync(RoutingId spotRid)` → `SpotRef?` |
| `IZLinkActorAddressResolver` | `ResolveActorSpotRefAsync(string actorId)` → `SpotRef?` |
| route 단건 조회 | public resolver가 아니라 store SPI/운영 조회 경로에서 처리 |
| `IZLinkLocationRuntimeQuery` | `GetStatusAsync`, `ListPeerLocationsAsync`, `ListSpotLocationsAsync`, `ListActorLocationsAsync`, `ListRouteLocationsAsync`, `ListTopologyAsync`, `ListServiceSummariesAsync` |

`SpotRef` 는 `NodeRid + SpotRid` 값 객체다(mesh 는 전송 문맥의 채널이 결정).
호출자가 보관하고 전송 실패 시 재resolve 한다. 실패 분류·재시도 의미는
[공통 spot 주소 메시징 스펙](../../common/spec/spot-address-messaging.ko.md)을 따른다.

## 3. 자동 연결과 채널 표면의 관계

- store 가 등록된 배포에서 `EnableClient()`(endpoint 없음)는 store 기반 자동 연결로
  동작한다. `EnableServer(endpoint)` 는 bind 와 동시에 peer row 자동 등록을 켠다.
- `EnableClient(endpoint)`(수동)는 자동 연결 상태 맞추기 대상이 아니다. 같은 역할에서 두
  방식을 섞으면 startup 오류다.
- 전송 실패의 fail-fast 분류(`RouteNotConnected` 등)는 mesh 구성원 snapshot 기준이다.
  오류 종류는 [session-actor-dispatch](session-actor-dispatch.ko.md)의 오류 매트릭스를
  따른다.

## 4. Monitoring 연동

`AddZLinkMonitoring(monitor => monitor.AddLocationRuntimeEvents(sourceName, interval))` 로
`ZLinkLocationRuntimeEvent`(kind: `StatusChanged`, `TopologyChanged`,
`ServiceSummaryChanged`, `StoreFailure`, `StoreRecovered`) 를 typed handler
(`IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>`) 로 받는다. store 장애는 source
를 죽이지 않고 `StoreFailure` 이벤트로 강등된다. 나머지 location 계열 source
(`location-peer/spot/actor/route`)는 [공통 스펙 §9](../../common/spec/location-runtime.ko.md)를
따른다.

## 5. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `Zlink.Framework.UnitTests` location 계열 | 옵션/등록 검증, resolver가 오래된 row를 제외하는지, 자동 연결 차이 계산 규칙 |
| `Zlink.Framework.ContractTests` store contract | in-memory 와 Redis 구현이 같은 store 계약 시나리오를 통과한다 |
| E2E Config 1 (`LocationMessaging`) | store 기반 자동 연결·failover·scale 시나리오 |
| E2E Config 6 (`StoreFailure`) | store 장애/복구, 장애 중 마지막 연결 판단 유지, owner lease 만료 |
| `RegressionTests.DotNet_Samples_Do_Not_Use_Legacy_Registry_Discovery` | .NET sample 이 제거된 Registry/Discovery API 를 다시 사용하지 않는다 |
