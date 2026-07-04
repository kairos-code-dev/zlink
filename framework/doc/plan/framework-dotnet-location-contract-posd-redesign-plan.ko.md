# .NET location contract POSD 기반 재설계 계획

## 문서 목적

이 문서는 `/framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations` 아래의 공개 location
계약을 POSD 관점에서 다시 설계하기 위한 작업 계획이며, 재설계 후 계약 전문을 파일 단위로 명시하는
정본이다. 현재 코드는 동작 가능한 초안에 가깝지만 다음 문제가 겹쳐 있다.

- 저장소 행 모델, 메시징 주소 조회, 운영 조회, 직렬화 형식이 같은 공개 표면에 섞여 있다.
- 같은 시그니처의 메서드가 여러 인터페이스에 중복 선언되어 합성 인터페이스에서 호출이 모호하다.
- 예상된 경합 결과와 인프라 장애가 하나의 상태 enum에 섞여 오류 처리 규칙이 두 갈래다.
- 닫힌 값 집합, 행 identity 규칙 같은 지식이 여러 곳에 반복 서술된다.
- 샘플이 보여 주듯 실제 사용자가 location row, spot address, actor ref 변환을 직접 조립해야 한다.

이 상태로 다른 언어에 확산하면 public contract 자체가 얕아지고, 이후 저장소 형식이나 actor
lifecycle을 바꿀 때 변경 범위가 커진다.

이 작업은 .NET 구현만 먼저 바꾸는 작업이 아니다. location 계약은 framework 공통 기능이므로 먼저 공통
draft를 갱신하고, 그 계약에 맞춰 .NET 구현과 테스트를 고친 뒤 다른 언어 porting 문서의 기준도 함께
갱신한다. Node/Java/C++는 현행 계약 형태로 이식이 진행 중이므로, 변경 확정분은 언어별 porting draft에
차분으로 반영한다. Kotlin은 Java 계약 공유 여부를 먼저 확인해 Java draft에 포함하거나 별도 Kotlin
차분 문서로 분리한다.

## 기준 문서와 대상 파일

- POSD 기준: `doc/principal/software-design-principles.md`
- 현재 공통 draft: `framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`
- 언어별 porting draft:
  - `framework/doc/framework/common/draft/framework-location-resolver-store-porting-node.ko.md`
  - `framework/doc/framework/common/draft/framework-location-resolver-store-porting-java.ko.md`
  - `framework/doc/framework/common/draft/framework-location-resolver-store-porting-cpp.ko.md`
  - Kotlin은 Java framework 계약을 공유하는지 먼저 확인한다. 별도 Kotlin public surface나 테스트가 있으면
    위 Java draft에 Kotlin 적용 항목을 추가하거나 별도 Kotlin 차분 문서를 만든다.
- 공통 샘플 문서(사용성 근거): `framework/doc/framework/common/sample/{deliverydispatch,supportchat,bingo}/`
- .NET 계약 파일:
  - `framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Models.cs`
  - `framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Stores.cs`
  - `framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Resolvers.cs`
  - `framework/languages/dotnet/src/Zlink.Framework/Contracts/Locations/Options.cs`
- .NET 구현 파일:
  - `framework/languages/dotnet/src/Zlink.Framework/Runtime/Locations/`
  - `framework/languages/dotnet/src/Zlink.Framework.Locations.Redis/`
- 사용성 근거 샘플 코드:
  - `framework/languages/dotnet/samples/DeliveryDispatch/Server/Tracking/Handlers.cs` (resolver + SendToSpot 직접 조합)
  - `framework/languages/dotnet/samples/DeliveryDispatch/Server/Dispatch/DispatchServerHostFactory.cs` (topology row 스캔 health check)
  - Bingo, SupportChat의 `ActorRefSnapshot` 중복 DTO
- 검증 테스트:
  - `framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Locations/LocationContracts.cs`
  - `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/*Location*Tests.cs`
  - `framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/*`

## 표면과 대상 독자

재설계의 기준 축은 "누가 이 인터페이스를 읽는가"다. 계층마다 독자가 다르고, 독자가 다르면 표면을
섞지 않는다.

| 표면 | 독자 | 반환 모델 | 파일 |
|------|------|-----------|------|
| 사용자 편의 API (actor directory/client) | 애플리케이션 개발자 | `ActorRef` 등 사용자가 실제로 쓰는 값 | `Contracts/Actors`, `Contracts/Routes` (location 밖) |
| 사용자 편의 API (readiness) | 애플리케이션 개발자 | `bool` | `Contracts/Locations` — mesh 이름·`ZLinkLocationRole` 등 location 도메인 타입에 의존하는 질의라 location에 남는다 |
| messaging resolver | framework 내부 send 경로 + 고급 사용자 | `ZLinkSpotAddress`, live peer 목록 | `Contracts/Locations/Resolvers.cs` |
| 운영 조회 (runtime query) | 운영 도구, self-check | 원시 location row, topology | `Contracts/Locations/RuntimeQuery.cs` |
| store 계약 | store backend 구현자 | row + write result | `Contracts/Locations/Stores.cs` |

애플리케이션 코드는 첫 줄만 쓰고, location store 표면은 아래로 내려갈수록 사용자에게서 멀어진다.
현재 샘플이 두 번째·세 번째 줄을 업무 로직에 쓰고 있는 것이 사용성 문제의 뿌리다.

## 현재 설계의 POSD 문제

### 계약 결함 (동작·컴파일 수준의 문제)

| 항목 | 현재 상태 | POSD 문제 | 영향 |
|------|----------|-----------|------|
| `RemoveByOwnerAsync` 4중 선언 | 4개 kind store 인터페이스가 동일 시그니처를 각각 선언하고 `IZLinkLocationStore`가 넷을 모두 상속한다. | 다이아몬드 모호성. 합성 인터페이스로 호출하면 컴파일 오류라서 구현체는 전부 명시적 인터페이스 구현으로 도피했다. | 유일한 호출자(`ZLinkLocationRuntime` shutdown 경로)는 kind별 필드 4개로 순차 호출한다. 유스케이스는 "owner의 모든 행 제거" 하나인데 계약이 4조각이고, 정리가 비원자적이다. |
| 오류 전략 비대칭 | 읽기는 store 장애 시 예외, 쓰기는 `ZLinkLocationWriteStatus.StoreUnavailable` 반환. | 예상된 경합 결과(stale/conflict)와 인프라 장애가 한 enum에 섞였다. | 쓰기 호출자는 예외도 잡고 상태도 검사하는 이중 처리를 강요받는다. |
| `ZLinkActorLocation.SpotMeshName` | positional record 뒤에 `{ get; init; } = ""`로 덧붙어 있다. | 필수 필드가 생성 시 누락 가능한 형태로 노출된다. | 빈 mesh name 행이 조용히 store에 들어갈 수 있다. |
| `LocationKind`/`SpotKind` 중복 | actor row에 `ZLinkSpotKind` 타입 필드가 두 개 있고, lifecycle은 둘을 항상 같은 값으로 채운다. | 같은 지식의 이중 표현. | 두 필드가 어긋난 행이 표현 가능하고, 읽는 쪽은 어느 쪽이 정본인지 알 수 없다. |
| lease renew 반환형 | `RenewOwnerLeaseAsync`가 `ZLinkLocationWriteResult`를 반환한다. | lease에는 generation 개념이 없는데 write result를 재사용해 `Generation = 0`이 관례로 들어간다. | 호출자가 의미 없는 필드를 해석해야 하고, lease 만료 시각 같은 유용한 정보는 못 받는다. |

### 정보 노출 (직렬화·내부 형식이 public으로 새는 문제)

| 항목 | 현재 상태 | POSD 문제 | 영향 |
|------|----------|-----------|------|
| actor ref 표현 | `ZLinkActorLocation.ActorRef`가 `string`이다. | 저장소 직렬화 형식이 공개 모델로 새어 나온다. | 호출자가 actor ref 문자열 포맷을 알아야 하며, Redis row JSON 형식을 바꾸기 어렵다. |
| actor claim lifecycle | claim 직후 actor ref가 없어서 빈 문자열 row를 만든 뒤 나중에 채운다. | 시간적 분해가 public row 상태로 노출된다. | "actor ref가 없는 actor row"라는 불완전 상태를 호출자가 해석해야 한다. |
| canonical names | `ZLinkLocationCanonicalNames`가 public static helper로 노출되고, 닫힌 값 집합이 `ToCanonicalString`/`TryParse`/`IsKnown` 세 곳에 반복 서술된다. | store key 직렬화 지식이 공개 surface에 걸쳐 있고, 같은 지식이 세 곳에 있다. | 값 하나를 추가하면 세 곳을 고쳐야 하고 하나만 빠뜨리면 조용히 어긋난다. cross-language store 형식 변경이 사용자 코드까지 영향을 준다. |
| watch change key | `ZLinkLocationChanged.LocationKey`가 string이다. | key 직렬화 형식이 event 계약에 새어 나온다. | event 소비자가 key 문자열을 파싱하는 방향으로 흐른다. |
| peer row identity | `ZLinkPeerLocationKey`의 `NodeRid?`/`Endpoint?`가 nullable인데 identity 규칙이 어디에도 없다. | 무엇이 같으면 같은 행인지가 암묵 지식이다. | store 구현마다 upsert 판정이 어긋날 수 있다. |
| route row value | `ZLinkRouteLocation.Value`가 `ReadOnlyMemory<byte>`로 공개된다. | protocol payload 형식 결정이 공개 행 모델에 드러난다. | route store가 임의 key-value 저장소처럼 오해될 수 있다. |

### 표면 혼합·명명 (인지 부하 문제)

| 항목 | 현재 상태 | POSD 문제 | 영향 |
|------|----------|-----------|------|
| `ListPeersAsync` 3중 노출 | store / `IZLinkPeerLocationResolver` / `IZLinkLocationRuntimeQuery` 세 표면에 같은 이름으로 있다. store 판은 원시 행 스냅샷, resolver 판은 owner lease liveness 조인 결과다. | 같은 이름이 계층마다 다른 결과를 준다. | 호출자가 어느 표면의 결과를 신뢰해야 하는지 이름만으로 알 수 없다. |
| resolver 파일 | `Resolvers.cs` 안에 messaging resolver와 운영 조회 `IZLinkLocationRuntimeQuery`가 같이 있다. | 서로 다른 추상화가 같은 표면에 섞인다. | resolver가 row page를 반환하는 것처럼 보여 인지 부하가 크다. |
| runtime query 이름 | `ListSpotsAsync`, `ListActorsAsync`가 location row를 반환한다. | 이름이 반환 모델의 의미를 숨긴다. | 사용자는 spot/actor 업무 모델 목록으로 오해할 수 있다. |
| store update 모델 | `UpdateActorAsync(ZLinkActorLocation actor, intent)`가 모든 행 필드를 호출자에게 요구한다. | 호출자가 lifecycle 내부 결정까지 알아야 한다. | owner id, generation, spot kind 전환 같은 책임이 밖으로 밀린다. |
| Models.cs god file | 행 record, key/filter, 쓰기 결과, watch, topology 진단, canonical 매핑까지 6개 관심사 30여 타입이 한 파일이다. | together/apart 판단 없이 전부 together다. | 파일 하나가 모든 변경의 교차점이 된다. |
| options | `ListPageSize`가 runtime query와 store paging의 기본값을 동시에 설명한다. | 하나의 옵션이 여러 계층 의미를 가진다. | store 구현 정책과 운영 조회 정책이 섞인다. |
| enum 규약 불일치 | `ZLinkLocationKind`만 `Invalid = 0`이 없고, `ZLinkLocationRole`은 `ushort` 기반에 값 1이 결번이다. | 규약이 타입마다 다르고 결번의 이유가 기록되어 있지 않다. | 이식자가 임의로 "고치다" 값이 어긋날 수 있다. |
| duplicate comments | `Stores.cs`의 `IZLinkLocationStore`에 `<summary>`가 두 개 붙어 있고 첫 번째는 peer store 설명이다. | 문서 노이즈. | 공개 계약 설명이 흐려진다. |

### 샘플 사용성 (사용자가 낮은 수준을 조립하는 문제)

| 항목 | 현재 상태 | POSD 문제 | 영향 |
|------|----------|-----------|------|
| actor 대상 전송 | DeliveryDispatch Tracking이 `IZLinkActorLocationResolver.ResolveActorSpotAddressAsync(...)` 뒤 `routes.SendToSpot(...)`을 직접 조합한다. | 사용자가 원하는 작업("customer actor에게 알려라")보다 낮은 수준을 알아야 한다. | 애플리케이션마다 resolve → null 처리 → send 조합이 반복된다. |
| actor 확보 | Bingo, SupportChat, DeliveryDispatch가 `FindActorReq`/`EnsureActorReq` route 요청과 응답 DTO를 각자 조립한다. | find-or-create라는 한 가지 의도가 여러 저수준 호출로 흩어진다. | actor 생성 위치, route mesh 선택, publish 시점 지식이 샘플로 샌다. |
| actor ref wire 표현 | 샘플마다 `ActorRefSnapshot` DTO를 정의하고 `NodeRid` 문자열/바이트 변환을 반복한다. | actor ref wire 표현 지식이 샘플마다 반복된다. | 사용자가 샘플을 따라 하면 변환 정책이 제각각이 된다. |
| readiness | Dispatch host health check가 `ListTopologyAsync(...)`로 ready row를 직접 스캔한다. | 운영 진단 표면이 일반 readiness 코드에 쓰인다. | "mesh가 준비됐는가"라는 boolean 질문에 row 스캔 코드가 필요하다. |
| 세션 바인딩 | `context.Actors.Find(...) ?? await context.Actors.BindAsync(...)` 관용구가 반복된다. | 흔한 의도(bind-or-get)에 이름이 없다. | 샘플마다 같은 두 줄이 복사된다. |

## 재설계 원칙

1. 공개 계약은 저장소 직렬화 형식을 노출하지 않는다.
2. messaging lookup은 주소나 actor ref처럼 호출자가 실제로 사용할 값만 반환한다.
3. 운영 조회는 row 조회임을 이름과 파일 위치로 분명히 드러낸다.
4. lifecycle의 실행 순서는 public row의 불완전 상태로 드러내지 않고 runtime 내부에서 흡수한다.
5. store 구현자는 원자적 저장 규칙을 구현하되, framework 사용자는 owner token, generation, row update
   순서를 직접 다루지 않는다.
6. 오류 규칙은 하나다: 예상된 경합(stale/conflict)은 상태값, 인프라 장애는 읽기/쓰기 모두 예외.
   fail-static 같은 장애 정책은 framework 계층(`StoreFailureGrace`)이 예외를 잡아 적용한다.
7. 같은 지식은 한 곳에만 둔다: 닫힌 값 집합은 단일 매핑 테이블, owner 정리는 단일 메서드, actor 위치
   kind는 단일 필드.
8. cross-language Redis 형식은 extension 내부 codec 또는 공통 store codec 문서에서만 다룬다.
9. 기존 public contract 변경은 먼저 공통 draft에 반영하고, .NET 구현은 그 draft를 따라간다.
10. 샘플 코드는 사용자가 따라 할 public contract 예시이므로, 샘플에서 보이는 반복 사용 흐름을
    framework API가 흡수한다. 사용자는 actor를 찾거나 만들고, 세션에 묶고, actor에게 메시지를 보내는
    작업을 말해야 하며, store row, owner token, spot address resolve 순서를 직접 조립하지 않아야 한다.

비목표: kind별 store 인터페이스 4개를 제너릭 `IStore<TRow, TKey, TFilter>`로 통합하지 않는다.
표면이 비슷해 보여도 kind마다 반환 형태(peer는 스냅샷, 나머지는 page)와 의미가 다르고, 제너릭 계약은
java/cpp 이식에서 타입 곡예가 된다. per-type API를 유지한 채 진짜 중복(owner 정리)만 위로 올린다.

## 목표 계약 전문

`Contracts/Locations`를 관심사별 파일로 재구성한다. 아래 코드가 재설계 후 public 계약의 전문이며,
XML doc 주석은 계약 문구의 요지만 남겼다(구현 시 현행 주석 수준으로 채운다).

| 파일 | 내용 |
|------|------|
| `Values.cs` | 닫힌 값 집합 enum 4종 |
| `Rows.cs` | location row record 4종 + owner lease + spot address |
| `Keys.cs` | row key 4종 + typed key union + filter 4종 + paging |
| `Writes.cs` | write intent/status/result + owner token + lease renewal |
| `Watch.cs` | watch filter + change event + change stamp scope |
| `Diagnostics.cs` | runtime status + topology + service summary |
| `Options.cs` | `ZLinkLocationOptions` |
| `Resolvers.cs` | live peer 조회 + address resolver 2종 (spot/actor) |
| `RuntimeQuery.cs` | `IZLinkLocationRuntimeQuery` |
| `Stores.cs` | store 계약 5+2종 |

`ZLinkLocationCanonicalNames`는 public 계약에서 제거하고 runtime 내부 codec으로 이동한다(후술).
사용자 편의 표면(`IZLinkActorDirectory` 등)은 location 계약이 아니라 actor/route 계약에 두며 별도
절에서 설명한다. readiness만 location 도메인 질의라 `Contracts/Locations`에 남는다.

### Values.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkLocationAutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

/// <summary>core discovery의 uint16_t service_role 값과 일치한다(ushort 유지).
/// 값 1은 제거된 gateway role의 자리라 예약 결번으로 유지한다. 숫자 값은
/// core wire와 Redis row JSON에 실제 직렬화되므로 바꾸지 않는다(P0 조사-1).
/// store key에는 canonical 문자열이 쓰인다.</summary>
public enum ZLinkLocationRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum ZLinkRouteKind
{
    Invalid = 0,
    ActorSession = 1,
    SpotName = 2,
    FrameworkRoute = 3
}

public enum ZLinkLocationKind
{
    Invalid = 0,
    Peer = 1,
    Spot = 2,
    Actor = 3,
    Route = 4
}
```

### Rows.cs

```csharp
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkPeerLocation(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    RoutingId? NodeRid,
    ZLinkLocationRole Role,
    string Endpoint,
    uint Weight,
    long Value,
    IReadOnlyDictionary<string, string>? Metadata,
    IReadOnlyList<string>? Capabilities,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    string? SpotType,
    RoutingId NodeRid,
    ZLinkSpotKind SpotKind,
    string? RouteEndpoint,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

/// <summary>
/// actor 한 개의 현재 위치. <see cref="LocationKind"/>가 actor가 사는 spot의
/// 종류를 단독으로 말한다(별도 SpotKind 필드는 두지 않는다).
/// <see cref="ActorRef"/>는 claim 직후 publish 전까지만 null이며, 그 상태의
/// 행은 runtime이 공개 조회 결과에 노출하지 않는다.
/// </summary>
public sealed record ZLinkActorLocation(
    string ActorId,
    string? ActorType,
    ActorRef? ActorRef,
    RoutingId NodeRid,
    ZLinkSpotKind LocationKind,
    string SpotMeshName,
    RoutingId? SpotRid,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

/// <summary>
/// framework 내부 route 항목. <see cref="Value"/>는 framework route payload이며
/// application key-value 저장 용도가 아니다.
/// </summary>
public sealed record ZLinkRouteLocation(
    ZLinkRouteKind RouteKind,
    string RouteKey,
    RoutingId OwnerNodeRid,
    string OwnerId,
    long Generation,
    ReadOnlyMemory<byte> Value,
    DateTimeOffset UpdatedAt);

/// <summary>
/// framework runtime 인스턴스당 하나. location row는 자체 lease를 갖지 않으며,
/// owner의 lease가 살아 있는 동안만 살아 있는 행으로 취급된다.
/// </summary>
public sealed record ZLinkOwnerLease(
    string OwnerId,
    RoutingId NodeRid,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset UpdatedAt);

/// <summary>
/// lease 목록과 store의 현재 시각. 만료 판정은 <see cref="StoreNow"/>와 로컬
/// monotonic 경과 시간으로만 하고, store 타임스탬프를 애플리케이션 벽시계와
/// 비교하지 않는다.
/// </summary>
public sealed record ZLinkOwnerLeaseSnapshot(
    IReadOnlyList<ZLinkOwnerLease> Leases,
    DateTimeOffset StoreNow);

/// <summary>
/// mesh 안에서 spot 하나의 논리 메시징 주소. resolver로 한 번 resolve해서
/// 호출자가 들고 있다가 실패 시 재-resolve한다. send 경로는 resolve하지
/// 않는다. entry spot의 주소는 NodeRid == SpotRid다.
/// </summary>
public readonly record struct ZLinkSpotAddress(
    RoutingId NodeRid,
    RoutingId SpotRid);
```

### Keys.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// peer 행의 identity는 이 다섯 구성요소 전체다. null인 구성요소도 "값 없음"
/// 그대로 키의 일부로 비교하며, store 구현은 이 규칙 외의 upsert 판정을
/// 만들지 않는다.
/// </summary>
public readonly record struct ZLinkPeerLocationKey(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    ZLinkLocationRole Role,
    RoutingId? NodeRid,
    string? Endpoint);

public readonly record struct ZLinkSpotLocationKey(
    string MeshName,
    RoutingId SpotRid);

/// <summary>
/// actor id는 framework 전체에서 unique하다. actor location identity는 actor id
/// 하나로만 정해지며, actor type은 생성/handler 등록/진단 정보일 뿐 key에
/// 참여하지 않는다.
/// </summary>
public readonly record struct ZLinkActorLocationKey(
    string ActorId);

public readonly record struct ZLinkRouteLocationKey(
    ZLinkRouteKind RouteKind,
    string RouteKey);

/// <summary>
/// kind별 typed key의 닫힌 합(union). watch event가 문자열 key 대신 이 모델을
/// 나른다. 문자열 인코딩은 store codec 내부 지식이다. java는 sealed
/// interface, cpp는 variant로 대응한다.
/// </summary>
public abstract record ZLinkLocationKey
{
    private ZLinkLocationKey() { }

    public sealed record Peer(ZLinkPeerLocationKey Key) : ZLinkLocationKey;
    public sealed record Spot(ZLinkSpotLocationKey Key) : ZLinkLocationKey;
    public sealed record Actor(ZLinkActorLocationKey Key) : ZLinkLocationKey;
    public sealed record Route(ZLinkRouteLocationKey Key) : ZLinkLocationKey;
}

public sealed record ZLinkPeerLocationFilter(
    ZLinkLocationAutoConnectType? AutoConnectType = null,
    string? MeshName = null,
    ZLinkLocationRole? Role = null,
    RoutingId? NodeRid = null,
    string? Endpoint = null);

public sealed record ZLinkSpotLocationFilter(
    string? MeshName = null,
    string? SpotType = null,
    RoutingId? NodeRid = null,
    ZLinkSpotKind? SpotKind = null);

public sealed record ZLinkActorLocationFilter(
    string? ActorType = null,
    RoutingId? NodeRid = null,
    RoutingId? SpotRid = null,
    ZLinkSpotKind? LocationKind = null);

public sealed record ZLinkRouteLocationFilter(
    ZLinkRouteKind? RouteKind = null,
    RoutingId? OwnerNodeRid = null,
    string? OwnerId = null);

/// <summary>
/// spot/actor/route list 조회의 page 요청. 기본값은 "설정된 기본 page 크기로
/// 첫 페이지부터"를 뜻한다. store와 운영 조회가 공유하는 모델이다.
/// </summary>
public readonly record struct ZLinkPageRequest(
    int PageSize = 0,
    string? ContinuationToken = null);

public sealed record ZLinkLocationPage<T>(
    IReadOnlyList<T> Items,
    string? ContinuationToken);
```

### Writes.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkLocationWriteIntent
{
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

/// <summary>
/// 쓰기의 예상된 경합 결과만 표현한다. store 장애는 읽기와 동일하게 예외로
/// 보고한다(StoreUnavailable 상태는 존재하지 않는다).
/// </summary>
public enum ZLinkLocationWriteStatus
{
    Stored = 1,
    IgnoredStale = 2,
    RejectedConflict = 3
}

/// <summary>
/// store 쓰기 결과. Stored일 때 store가 발급한 generation과 기록한 갱신
/// 시각을 돌려주며, 호출자는 이를 owner token으로 보관한다. generation은
/// 다른 어떤 경로로도 노드 간에 배포되지 않는다.
/// </summary>
public sealed record ZLinkLocationWriteResult(
    ZLinkLocationWriteStatus Status,
    long Generation,
    DateTimeOffset UpdatedAt)
{
    public static ZLinkLocationWriteResult IgnoredStale { get; } =
        new(ZLinkLocationWriteStatus.IgnoredStale, 0, default);

    public static ZLinkLocationWriteResult RejectedConflict { get; } =
        new(ZLinkLocationWriteStatus.RejectedConflict, 0, default);

    public static ZLinkLocationWriteResult Stored(long generation, DateTimeOffset updatedAt) =>
        new(ZLinkLocationWriteStatus.Stored, generation, updatedAt);
}

public readonly record struct ZLinkLocationOwnerToken(
    string OwnerId,
    long Generation);

/// <summary>
/// owner lease 갱신 결과. lease에는 generation 개념이 없으므로
/// <see cref="ZLinkLocationWriteResult"/>를 재사용하지 않는다.
/// <see cref="StoreNow"/>는 store 시계 기준의 현재 시각으로, 시계 편차 진단과
/// 로컬 만료 추정의 기준점이 된다.
/// </summary>
public sealed record ZLinkOwnerLeaseRenewal(
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);
```

### Watch.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkLocationWatchFilter(
    ZLinkLocationKind Kind,
    string? MeshName = null,
    ZLinkRouteKind? RouteKind = null);

public enum ZLinkLocationChangeType
{
    Upserted = 1,
    Removed = 2,
    Expired = 3
}

/// <summary>
/// 변경 event는 typed key를 나른다. backend가 문자열 key로 event를 전파하는
/// 경우 디코딩은 store 구현(codec) 내부에서 끝내고, 소비자에게 문자열 파싱을
/// 요구하지 않는다.
/// </summary>
public sealed record ZLinkLocationChanged(
    ZLinkLocationKind Kind,
    ZLinkLocationKey Key,
    ZLinkLocationChangeType ChangeType,
    long Generation,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationKind Kind,
    string? MeshName);
```

### Diagnostics.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkLocationRuntimeStatus(
    bool StoreHealthy,
    bool WatchEnabled,
    TimeSpan PollingInterval,
    DateTimeOffset? LastRefreshAt,
    string? LastError,
    bool OwnerLeaseHealthy,
    DateTimeOffset? OwnerLeaseRenewedAt);

public enum ZLinkLocationTopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}

public sealed record ZLinkLocationTopologyFilter(
    ZLinkLocationKind? Kind = null,
    string? MeshName = null,
    ZLinkLocationRole? Role = null,
    RoutingId? NodeRid = null,
    ZLinkLocationTopologyState? State = null);

public sealed record ZLinkLocationTopologyEntry(
    ZLinkLocationKind Kind,
    string? MeshName,
    ZLinkLocationRole? Role,
    RoutingId? NodeRid,
    RoutingId? SpotRid,
    string? ActorId,
    string? Endpoint,
    ZLinkLocationTopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    int ErrorCode,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkLocationServiceSummaryFilter(
    string? MeshName = null,
    ZLinkLocationAutoConnectType? AutoConnectType = null,
    ZLinkLocationRole? Role = null);

public sealed record ZLinkLocationServiceSummary(
    string MeshName,
    ZLinkLocationAutoConnectType AutoConnectType,
    ZLinkLocationRole Role,
    uint TotalCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    DateTimeOffset LastUpdatedAt);
```

### Options.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// location runtime 정책 옵션. store 구현은 이 옵션을 읽지 않는다 — heartbeat,
/// polling, 장애 grace는 framework가 등록된 store 위에서 적용하는 정책이다.
/// resolver에는 캐시가 없다: 모든 resolve가 store를 읽고, resolve된 spot
/// 주소는 호출자가 보관한다(spot-address messaging draft).
/// </summary>
public sealed class ZLinkLocationOptions
{
    /// <summary>owner lease 갱신 주기. runtime 인스턴스당 interval마다 한 번의
    /// 쓰기이며, heartbeat가 location row를 쓰는 일은 없다.</summary>
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(5);

    /// <summary>owner lease 수명. 만료된 owner의 행은 어디서나 stale로 취급된다.</summary>
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(15);

    /// <summary>watch 미지원 store의 재조회 주기. 로컬 owner lease snapshot의
    /// staleness 상한이기도 하다.</summary>
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);

    /// <summary>기본 <see cref="ZLinkPageRequest"/>로 들어온 list 조회에 적용할
    /// page 크기. runtime이 store 호출 시 채워 넣는 값이며 store 자체 정책이
    /// 아니다.</summary>
    public int ListPageSize { get; set; } = 1000;

    /// <summary>store 장애 중 auto connect가 마지막 desired target 집합을
    /// 유지하는 시간(fail-static).</summary>
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);
}
```

### Resolvers.cs (messaging 전용)

```csharp
using Zlink.Framework.Contracts.Actors;

namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// auto connect용 peer 목록 읽기. 모든 읽기는 store에 닿고 owner liveness를
/// 조인한다 — 이름 그대로 "살아 있는 peer"만 반환한다. 원시 행 조회는
/// <see cref="IZLinkLocationRuntimeQuery"/>가 담당한다.
/// </summary>
public interface IZLinkPeerLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// messaging lookup: spot rid → 주소. 한 번 resolve해서 spot 수명 동안 들고
/// 있다가 실패 시 재-resolve한다. generation이 필요한 lifecycle 흐름은 store
/// 또는 runtime query 표면을 사용한다.
/// </summary>
public interface IZLinkSpotAddressResolver
{
    /// <summary>살아 있는 행이 없으면(미지의 spot 또는 owner lease 만료) null.</summary>
    ValueTask<ZLinkSpotAddress?> ResolveSpotAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// messaging lookup: actor id → actor가 사는 spot의 주소(ENTRY_SPOT actor는
/// entry spot 주소, USER_SPOT actor는 user spot 주소). 애플리케이션 코드는
/// 이 표면 대신 actor-level client(사용자 편의 표면 절 참고)를 쓴다.
/// actor id는 framework 전체에서 unique하므로 lookup은 actor id만 받는다.
/// </summary>
public interface IZLinkActorAddressResolver
{
    ValueTask<ZLinkSpotAddress?> ResolveActorSpotAddressAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}
```

actor ref 조회의 public 표면은 `IZLinkActorDirectory.FindAsync` 하나다. `(actorId) → ActorRef?`라는
같은 질문에 답하는 `IZLinkActorRefResolver`류를 병설하지 않는다 — 이 문서가 `ListPeersAsync` 3중
노출을 결함으로 진단해 놓고 같은 중복을 새로 만들 수는 없다. framework 내부가 ref 조회를 필요로
하면 internal 표면을 쓴다. Resolvers.cs의 파일 첫 줄 `using Zlink.Framework.Contracts.Actors;`도
이에 따라 불필요해진다.

`IZLinkRouteLocationResolver`는 이 파일에 두지 않는다. route location은 actor/spot으로 표현되지 않는
framework 내부 route row이며, 사용자 메시징 lookup이 아니다. route row 단건 조회가 필요하면
`IZLinkRouteLocationStore.ResolveRouteAsync(...)` 또는 운영 조회 표면에서만 다룬다. 일반 application
코드는 route row를 직접 resolve하지 않고, actor/spot/readiness용 상위 API를 사용한다.

### RuntimeQuery.cs (운영 조회 전용)

```csharp
namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// 운영 도구와 self-check를 위한 운영 조회 표면. 모든 쿼리가 캐시 없이 store를 직접
/// 읽으므로 freshness 파라미터가 없다. List 계열은 owner liveness를 조인한
/// live row만 반환한다(만료 owner의 행 제외). stale 관측은
/// ListTopologyAsync(만료 owner peer를 Lost로 노출)와
/// ListServiceSummariesAsync(Stopped 집계)가 담당하고, 원시 row가 필요한
/// 진단은 store SPI의 몫이다.
/// </summary>
public interface IZLinkLocationRuntimeQuery
{
    ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeerLocationsAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        CancellationToken cancellationToken = default);
}
```

liveness 의미론은 P0 조사-2로 확정됐다(P0 결정 기록 참고): 현행 구현이 전 메서드 live-only이고
e2e(RM-A4/RM-B2/SF-A2)가 그 의미론에 의존하므로 계약 문구를 live-only로 고정했다. 이에 따라 세
계층 구분은 "store=원시 row / resolver·runtime query=live / stale 관측=topology·summary"다 —
resolver의 `ListLivePeersAsync`와 query의 `ListPeerLocationsAsync`는 둘 다 live이며, 이름 차이는
liveness가 아니라 표면의 독자(재조정 스냅샷 vs 운영 페이지 조회)를 구분한다.

### Stores.cs

```csharp
namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// 필요한 store role 전부를 제공하는 하나의 물리 location store. 다섯 role은
/// 한 backend에서 all-or-nothing이며, 선택 계약(change stamp, watch)은 같은
/// 인스턴스가 구현했을 때 인식된다. AddLocationStore로 등록한다 — framework
/// 표면은 구체 backend 이름을 언급하지 않는다.
///
/// 오류 규약: 예상된 경합(stale/conflict)은 상태값으로, store 장애는
/// 읽기/쓰기 모두 예외로 보고한다.
/// </summary>
public interface IZLinkLocationStore :
    IZLinkPeerLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkRouteLocationStore,
    IZLinkOwnerLeaseStore
{
    /// <summary>
    /// owner가 남긴 모든 location row를 kind 구분 없이 제거한다. runtime
    /// shutdown/takeover 정리 경로 전용이며, 구현이 가능하면 한 번의 원자적
    /// 연산으로 수행한다. 반환값은 제거된 행 수.
    /// </summary>
    ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkPeerLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// filter에 걸리는 peer row를 단일 스냅샷으로 반환한다. peer 목록은 계약상
    /// mesh당 수천 행으로 유계라 페이지네이션하지 않는다 — reconcile은 한
    /// 시점의 목록 하나가 필요하다.
    /// </summary>
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRouteLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// framework runtime 인스턴스당 lease row 하나를 관리한다. NewClaim이 "row
/// owner의 lease 만료"를 원자적으로 판정할 수 있도록 location store와 같은
/// 물리 저장소를 공유해야 한다.
/// </summary>
public interface IZLinkOwnerLeaseStore
{
    /// <summary>
    /// upsert: lease row가 없으면 만들고 있으면 연장한다. runtime 인스턴스당
    /// heartbeat interval마다 한 번 호출된다. 호출자는 TTL만 넘기고 절대 만료
    /// 시각은 store가 자기 시계로 계산한다 — 호출자는 절대 시각을 만들지
    /// 않는다.
    /// </summary>
    ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// lease를 제거한다. owner 자신의 shutdown 경로와 운영 복구 도구 전용.
    /// 반환값은 제거 여부(이미 없으면 false).
    /// </summary>
    ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// 모든 lease와 store 현재 시각을 한 스냅샷으로 반환한다. runtime 인스턴스
    /// 수로 유계라 페이지네이션하지 않는다.
    /// </summary>
    ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default);
}

/// <summary>
/// 선택 계약. store 구현이 함께 구현하면 framework가 change event로 reconcile을
/// 깨운다. event 유실은 허용된다: polling이 correctness 경로다.
/// </summary>
public interface IZLinkLocationWatchStore
{
    IAsyncEnumerable<ZLinkLocationChanged> WatchAsync(
        ZLinkLocationWatchFilter filter,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// 선택 계약. scope 내 행 변경마다 증가하는 counter를 반환해, stamp가 그대로인
/// polling tick이 전체 list 조회를 건너뛸 수 있게 한다.
/// </summary>
public interface IZLinkLocationChangeStampStore
{
    ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}
```

통합 store 등록은 유지한다. POSD 관점에서 하나의 physical store가 lease와 row를 함께 관리한다는
결정은 중요하며, 이를 여러 registration option으로 흩뜨리면 원자성 판단이 어려워진다.

store 표면의 `ListPeersAsync`/`ListSpotsAsync`/`ListActorsAsync`/`ListRoutesAsync`는 짧은 이름을
유지한다 — store는 구현자 대상 표면이라 row 반환이 자명하고, resolver(`ListLivePeersAsync`)·runtime
query(`List*LocationsAsync`)와 이름이 갈리므로 세 계층의 의미 차이가 이름에 드러난다.

`UpdateActorAsync`가 모든 행 필드를 요구하는 문제는 store 계약을 바꾸지 않고 풀어낸다: 행 조립과
lifecycle 결정(owner id, generation, kind 전환)은 runtime 내부 lifecycle(아래)이 맡고, store 구현자는
받은 행을 intent/guard 규칙대로 저장하기만 한다. application 코드가 `UpdateActorAsync`를 직접 부르는
일은 없어야 한다.

### public 계약에서 빠지는 것

이 절은 세 범주를 구분한다. 첫째, public 계약에서 완전히 제거할 API다. 둘째, 일반 application
개발자가 쓰는 public surface에서는 빼고 store backend 구현자·운영 도구·self-check 표면에만 남길
API다. 셋째, public 후보가 아니라 runtime 내부 계약으로 시작해야 하는 API다. 이 구분을 하지 않으면
store row와 운영 진단 모델이 사용자 API처럼 보이고, 샘플이 낮은 수준의 location row를 다시 조립하게
된다.

#### 완전히 제거하는 public API

**`ZLinkLocationCanonicalNames`** — public 계약에서 제거하고 runtime 내부의 단일 매핑 테이블 codec으로
이동한다. `ToCanonicalString`/`TryParse`/`IsKnown` 세 표면이 각자 닫힌 집합을 서술하는 대신, 쌍 테이블
하나에서 전부 파생한다.

```csharp
// Runtime/Locations/ZLinkLocationValueCodec.cs (internal)
internal static class ZLinkLocationValueCodec
{
    // 닫힌 값 집합의 유일한 정의. 추가/삭제는 이 테이블만 고친다.
    private static readonly (ZLinkLocationAutoConnectType Type, string Name)[] AutoConnectTypes =
    {
        (ZLinkLocationAutoConnectType.RouteMesh, "route-mesh"),
        (ZLinkLocationAutoConnectType.ClientServer, "client-server"),
        (ZLinkLocationAutoConnectType.DealerMesh, "dealer-mesh"),
        (ZLinkLocationAutoConnectType.Fanout, "fanout"),
        (ZLinkLocationAutoConnectType.SpotMesh, "spot-mesh"),
    };

    private static readonly (ZLinkLocationRole Role, string Name)[] Roles =
    {
        (ZLinkLocationRole.Spot, "spot"),
        (ZLinkLocationRole.Router, "router"),
        (ZLinkLocationRole.Dealer, "dealer"),
        (ZLinkLocationRole.Pub, "pub"),
        (ZLinkLocationRole.Sub, "sub"),
    };

    // ToCanonicalString / TryParse / IsKnown 전부 위 테이블에서 파생한다.
}
```

canonical 문자열은 cross-language store 호환의 핵심이므로 공통 store codec 문서(draft)가 문자열 값의
정본이 되고, 코드에서는 이 테이블이 유일한 사본이다. custom store 구현자가 공식 codec을 요구하게 되면
그때 별도 public 계약으로 승격을 검토한다(지금은 하지 않는다).

**`StoreUnavailable` 상태값** — `ZLinkLocationWriteStatus.StoreUnavailable`와
`ZLinkLocationWriteResult.StoreUnavailable`은 삭제한다. stale/conflict처럼 예상된 경합은 상태값으로
돌려주고, Redis 장애·network 장애·timeout 같은 인프라 장애는 읽기와 쓰기 모두 예외로 보고한다.
호출자가 상태값과 예외를 동시에 해석하지 않게 만드는 것이 목표다.

**kind별 `RemoveByOwnerAsync`** — `IZLinkPeerLocationStore`, `IZLinkSpotLocationStore`,
`IZLinkActorLocationStore`, `IZLinkRouteLocationStore`에 각각 있던 owner 정리 메서드는 public 계약에서
삭제한다. owner가 남긴 모든 row를 지우는 의도는 하나이므로 `IZLinkLocationStore.RemoveAllByOwnerAsync`
하나로만 표현한다. 이렇게 해야 합성 인터페이스 호출 모호성과 4단계 비원자 정리를 없앨 수 있다.

**문자열로 노출되는 저장 형식** — `ZLinkActorLocation.ActorRef`의 `string` 형태와
`ZLinkLocationChanged.LocationKey`의 `string` 형태는 public 계약에서 제거한다. actor ref는
`ActorRef?`로, 변경 event key는 `ZLinkLocationKey` typed union으로 표현한다. Redis JSON field 이름,
actor ref 문자열 포맷, 인코딩된 key 문자열은 store codec 내부 지식이다.

**중복 actor 위치 필드** — `ZLinkActorLocation.SpotKind`는 삭제한다. actor가 사는 spot의 종류는
`LocationKind` 하나만 정본으로 남긴다. `SpotMeshName`은 init property가 아니라 record 생성자
파라미터로 승격해 필수 값을 누락할 수 없게 한다.

**lease store의 잘못된 반환형** — `RenewOwnerLeaseAsync`와 `RemoveOwnerLeaseAsync`가
`ZLinkLocationWriteResult`를 반환하는 계약은 제거한다. lease에는 row generation 의미가 없으므로
renew는 `ZLinkOwnerLeaseRenewal`, remove는 `bool`을 반환한다.

#### 일반 application surface에서 제외하는 API

아래 API는 완전히 없애는 대상이 아니다. 다만 application 개발자가 따라 쓰는 public surface에서는
빠져야 한다. 위치는 store backend 구현자, 운영 도구, self-check 표면으로 제한한다.

| API | 남는 위치 | application 대체 |
|-----|-----------|------------------|
| `IZLinkRouteLocationResolver` | 제거. route 단건 조회가 필요하면 store/runtime 내부에서 `IZLinkRouteLocationStore.ResolveRouteAsync(...)` 사용 | actor/spot/readiness 상위 API |
| `ZLinkRouteLocation`, `ZLinkRouteLocationKey`, `ZLinkRouteLocationFilter` | store 계약과 운영 조회 | application route lookup에는 노출하지 않음 |
| `ZLinkRouteLocation.Value` | store codec 내부 payload. public row에 남길 경우 "framework route payload"로만 문구 제한 | application key-value 저장 용도로 사용 금지 |
| `IZLinkLocationRuntimeQuery`와 `List*LocationsAsync` | 운영 도구, self-check | `IZLinkLocationReadiness`, actor client/directory |
| `ZLinkLocationTopologyEntry`, `ZLinkLocationServiceSummary` | 운영 조회 결과 | 일반 health check는 `IZLinkLocationReadiness` |
| `IZLink*LocationStore`, `IZLinkOwnerLeaseStore` | store backend 구현자와 runtime wiring | application 코드는 store를 직접 호출하지 않음 |
| `ZLinkLocationOwnerToken`, `ZLinkLocationWriteIntent`, `ZLinkLocationWriteResult`, `ZLinkLocationWriteStatus` | store 구현자와 runtime lifecycle | actor directory/client가 fencing과 generation을 내부 처리 |
| `ZLinkOwnerLease`, `ZLinkOwnerLeaseSnapshot`, `ZLinkOwnerLeaseRenewal` | runtime lease 관리와 운영 진단 | application 코드는 lease를 직접 갱신하지 않음 |
| `ZLinkLocationWatchFilter`, `ZLinkLocationChanged`, `ZLinkLocationChangeStampScope`, `IZLinkLocationWatchStore`, `IZLinkLocationChangeStampStore` | 선택 store 계약과 auto connect wake-up | application event stream으로 노출하지 않음 |

`IZLinkActorAddressResolver`도 일반 application code의 기본 표면이 아니다. actor에게 메시지를 보내는
코드는 `IZLinkActorClient.SendToActor/RequestToActor`를 쓰고, actor ref가 필요한 조회·find-or-create
흐름은 `IZLinkActorDirectory.FindAsync/EnsureAsync`를 쓴다. address resolver는 framework 내부 send
경로와 resolve-once-hold hot path가 row 조회를 피하면서 사용할 수 있는 얇은 escape hatch로만 둔다.
`IZLinkActorRefResolver`는 만들지 않는다 — directory의 `FindAsync`와 같은 질문에 답하는 public
표면의 중복이기 때문이다. framework 내부의 ref 조회는 internal 표면으로 해결한다.

#### runtime 내부 계약으로 시작하는 API

**actor lifecycle 명령** — claim과 actor ref publish의 분리는 public store 계약이 아니라 framework
runtime 내부 계약으로 시작한다.

```csharp
// Runtime/Locations (internal)
internal interface IZLinkActorLocationLifecycle
{
    ValueTask<ZLinkActorClaimResult> ClaimActorAsync(
        string actorId,
        string? registeredActorType,
        RoutingId ownerNodeRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> PublishActorRefAsync(
        ZLinkActorLocationKey key,
        ActorRef actorRef,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);
}
```

store 구현자는 typed row를 저장하고, Redis extension이 JSON과 `ActorRef` 변환을 내부에서 담당한다.
claim 직후 publish 전의 행(`ActorRef == null`)은 resolver와 runtime query 공개 결과에 노출하지 않는다.
`ClaimActorAsync`의 결과 타입 `ZLinkActorClaimResult`도 internal 신설 타입이며 public 계약이 아니다.

**row key 문자열 codec** — key의 문자열 인코딩 지식은 public에 두지 않는다. typed key
union(`ZLinkLocationKey`) 도입 후 public 표면에서 문자열 key가 사라지고, 남는 codec은 두 곳뿐이다:
framework 내부 장부용 `ZLinkLocationKeyCodec`(internal, 기존 존재)과 backend별
codec(Redis는 `ZLinkRedisLocationKeyCodec`). backend 간·언어 간 호환의 정본은 공통 store codec
문서이며, 두 codec은 각자 그 문서를 따른다.

**actor-addressed envelope** — `SendToActor`가 쓰는 envelope의 구체 타입과 메타데이터 키도
internal이다. public 계약과 문서에는 "actor 대상 packet"이라는 사실만 드러나고, framing 세부는
공통 draft/spec의 framing 절이 정본이다.

### 전체 framework public surface 분류 재검토 항목

location 계약을 정리하면서 다른 framework public contract에도 비슷한 노출 문제가 있는지 함께 점검한다.
다만 "일반 application 기본 표면이 아니다"와 "public contract에서 제거해야 한다"는 같은 말이 아니다.
framework에는 사용자 업무 API 외에도 extension API와 운영 API가 필요하다. 아래 항목은 제거 결정을
내린 목록이 아니라, public 표면의 독자를 다시 분류하고 필요한 경우 shape를 좁히기 위한 후속 검토
목록이다.

판단 기준은 spec, guide, 공통 draft에 명시된 계약이다. E2E나 샘플에서 어떤 API를 썼다는 사실은
"검증 경로가 있다" 또는 "현재 사용례가 있다"는 증거일 뿐, 그 API가 public contract로 유지되어야
한다는 근거가 아니다. 아래 항목도 E2E 사용 여부로 유지 여부를 확정하지 않고, 사용자가 직접 따라
쓸 계약인지, extension 작성자가 필요한 계약인지, 운영 도구만 필요한 계약인지로 다시 판정한다.

#### public extension/operational contract로 유지하되 기본 application surface에서 숨길 항목

| API | 재검토 결론 | 문서화 방향 |
|-----|-------------|-------------|
| `IZLinkSpotRemoteAddressResolver`, `ZLinkSpotRemoteAddress`, `AddSpotRemoteAddressResolver<TResolver>()` | SPOT remote routing의 명시적 확장점으로 설계되어 있다. 그러나 E2E에서 사용했다는 사실만으로 public 유지가 확정되지는 않는다. spec/guide가 이 extension을 외부 route directory 연동용 계약으로 인정하는지 재확인해야 한다. location store 기본 resolver가 자리 잡으면 일반 샘플이 직접 구현할 이유는 줄어든다. | 일반 guide에서는 location store 기반 기본 resolver와 상위 actor/spot API를 먼저 보여 준다. 이 resolver는 public으로 유지하더라도 advanced SPOT routing extension으로 분리해 설명한다. |
| location runtime event payload (`ZLinkLocationRuntimeEvent`, `ZLinkLocationPeerEvent`, `ZLinkLocationSpotEvent`, `ZLinkLocationActorEvent`, `ZLinkLocationRouteEvent`, `ZLinkAutoConnectDesiredSetChange`) | 운영자가 location runtime과 auto-connect 변화를 관찰해야 하는 요구는 있다. 하지만 이 타입들이 public monitoring contract로 유지되어야 하는지는 spec/guide 근거로 다시 확인해야 한다. row payload가 application routing API처럼 보일 수 있다는 점이 위험하다. | monitoring spec에서 public 유지 여부를 먼저 결정한다. 유지한다면 운영 관측 모델임을 분명히 하고, application 업무 흐름에서 이 event payload로 actor/route 결정을 내리지 않도록 guide와 sample 규칙에 적는다. |
| `IZLinkRuntimeEventPublisher` | application handler가 직접 호출하는 표면은 아니지만 hosting/monitoring extension seam일 수 있다. Node spec도 publisher를 monitoring public surface로 다룬다. 바로 internal로 밀지 않는다. | public handler 등록 표면과 publisher injection 표면의 독자를 나눠 문서화한다. publisher를 low-level hosting extension으로 유지할지 별도 설계에서 결정한다. |
| `IZLinkRuntimeEventHandler<TEvent>` | monitoring 구독 확장점이다. | public 계약에 남긴다. |

#### shape 개선 후보

| API | 현재 문제 | 검토 방향 |
|-----|-----------|-----------|
| `ZLinkSocketNativeEventType`, `ZLinkSocketDiagnostic.NativeValue` | native monitor bit 값과 raw value가 public event payload에 그대로 노출된다. backend 구현 세부가 stable monitoring contract처럼 읽힐 수 있다. | low-level diagnostics contract로 명시할지, 안정된 `ZLinkSocketEventKind` 중심의 diagnostic model로 감쌀지 결정한다. 즉시 제거하지 않는다. |
| `IZLinkChannelRuntimeOptions` | runtime drain/restore는 운영 API로 가치가 있다. 다만 E2E가 이 표면을 쓴다는 사실은 public 유지 근거가 아니다. spec/guide가 런타임 가용성 제어를 public 운영 계약으로 받아들이는지 먼저 확인해야 한다. 또한 build-time option interface를 그대로 돌려주고, startup 전용 setter는 runtime에서 오류로 거부한다. 같은 인터페이스가 build-time과 runtime에서 다른 의미를 가진다. | public 운영 API로 유지할 근거가 확정되면 `SetWeight`, `Drain`, `RestoreWeight`처럼 runtime에서 실제로 허용되는 좁은 control API로 분리할지 검토한다. |
| `ZLinkHandlerInvocation` / `IZLinkHandlerFilter` | handler filter는 `UseFilter<TFilter>()`로 등록하는 public pipeline extension이다. 제거 후보가 아니다. 다만 `object? Message`와 낮은 수준 context를 넘기므로 일반 handler 사용자가 따라 쓸 표면은 아니다. | handler middleware extension 계약으로 명확히 분류하고, 일반 handler guide에서는 기본 사용 흐름과 분리한다. 필요하면 typed filter 또는 더 좁은 invocation view를 별도 설계한다. |

#### 제거 후보가 아닌 항목

- `IZLinkCodecExtension`, `IZLinkMessageSerializer`, `ZLinkEncodedPayload`는 사용자 codec 확장점이므로 public
  계약에 남긴다.
- `ZLinkFrameworkAssemblyMarker`는 assembly scanning marker라 사용자가 직접 호출하는 API는 아니지만,
  공개되어도 runtime 책임이나 저장소 형식을 누출하지 않으므로 이 계획의 제거 후보로 보지 않는다.

#### 변경 방안

| 항목 | 변경 방안 | 이 계획의 범위 |
|------|-----------|----------------|
| SPOT remote address resolver | 먼저 spec/guide에서 이 resolver가 외부 route directory 연동을 위한 public extension인지 확인한다. 계약 근거가 있으면 `IZLinkSpotRemoteAddressResolver`와 `AddSpotRemoteAddressResolver<TResolver>()`는 유지하되, 공통 guide에서는 location store 기반 기본 resolver를 먼저 설명하고 custom resolver는 advanced extension으로 옮긴다. 계약 근거가 없으면 유지하지 않고 location store 기반 resolver와 상위 actor/spot API로 대체한다. 샘플에서는 TicTacToe처럼 store 기능 자체가 도메인 요구일 때만 직접 구현을 허용하고, DeliveryDispatch/SupportChat/Bingo 같은 일반 actor 흐름은 actor directory/client로 전환한다. | P0에서 계약 근거 확인 후 유지/삭제 결정. E2E 사용 여부만으로 결정하지 않는다. |
| location monitoring event payload | monitoring spec에서 기존 event 타입이 public contract인지 먼저 확인한다. 계약 근거가 있으면 유지하되 `ZLinkLocationActorEvent`와 `ZLinkLocationRouteEvent` 문구에 "운영 관측 전용이며 application routing 결정에 쓰지 않는다"를 명시한다. 계약 근거가 없거나 row payload가 과도하면 summary 중심 event로 바꾸는 monitoring draft를 작성한다. | 이 문서에는 사용 금지 규칙과 후속 spec 항목을 둔다. public 유지 여부와 payload 변경은 별도 monitoring draft에서 결정한다. |
| runtime event publisher | `IZLinkRuntimeEventPublisher`는 당장 internal로 바꾸지 않는다. 대신 public handler 등록(`IZLinkRuntimeEventHandler<TEvent>`)과 publisher injection이 서로 다른 독자라는 점을 monitoring spec에 명시한다. 외부 publisher 교체가 공식 extension인지, hosting package 내부 seam인지 별도 설계에서 결정한다. | 이 계획에서는 제거하지 않는다. 후속 public contract review 항목으로 남긴다. |
| socket native diagnostics | `ZLinkSocketNativeEventType`와 `ZLinkSocketDiagnostic.NativeValue`는 바로 삭제하지 않는다. 먼저 monitoring spec에 이 값들이 backend diagnostic detail인지, stable cross-language event contract인지 판정하는 P0 항목을 추가한다. stable contract가 아니라면 `ZLinkSocketDiagnostic`을 문자열/코드 기반의 backend-neutral diagnostic model로 감싸는 변경안을 작성한다. | 후속 monitoring draft 필요. location contract 구현과 묶어 변경하지 않는다. |
| channel runtime options | 먼저 spec/guide에서 runtime drain/restore가 public 운영 계약인지 확인한다. 계약 근거가 있으면 별도 channel-runtime-control draft에서 현재 option mirror 방식과 `Drain/Restore/SetWeight` 방식 두 가지를 비교한다. 계약 근거가 없으면 E2E 전용 검증 경로로 남기지 말고 public contract 후보에서 제외한다. 새 API가 채택되기 전까지 현행 `Weight` 방식은 현재 구현 검증 경로로만 취급한다. | 별도 channel 계획으로 분리. E2E 사용 여부만으로 public 유지 판정을 하지 않는다. |
| handler filter/invocation | `IZLinkHandlerFilter`는 public middleware extension으로 유지한다. guide에서는 일반 handler 작성법과 분리하고, filter 절에서만 `ZLinkHandlerInvocation`을 설명한다. typed filter가 필요한지는 handler contract review에서 별도 검토한다. | 문서 분류만 변경. API 삭제는 하지 않는다. |

## 사용자 편의 표면 — 샘플 사용례로 본 실제 사용자 API

샘플이 반복해서 보여 준 실제 사용자는 location store 자체를 쓰고 싶은 것이 아니다. 사용자는 다음
작업을 하고 싶다.

- 인증된 사용자의 actor를 찾거나 없으면 만든다.
- actor ref를 세션에 바인딩한다.
- actor가 entry spot에 있든 user spot에 있든 그 actor에게 메시지를 보낸다.
- readiness나 self-check에서 route mesh가 준비됐는지 확인한다.

따라서 샘플에서 반복된 `FindActorReq`, `EnsureActorReq`, `ActorRefSnapshot`,
`ResolveActorSpotAddressAsync` 뒤 `SendToSpot(...)` 조합은 framework가 더 깊은 API로 흡수해야 한다.
아래 표면은 location 계약이 아니라 actor/route 계약(`Contracts/Actors`, `Contracts/Routes`)에 두며,
내부 구현이 location resolver를 사용한다. CLAUDE.md의 public contract parity 정책에 따라 신규 public
API이므로 공통 draft에 계약 후보로 먼저 올린 뒤 구현한다.

### actor 확보 API

actor id는 framework 전체에서 unique하므로 사용자 표면의 actor lookup은 actor id만 받는다. 호출자는
actor type이나 actor 구현 타입을 넘기지 않는다. actor type은 생성, handler 등록, 운영 진단 정보로만
남고 위치 조회 key에 참여하지 않는다.

id 유일성은 애플리케이션의 계약 의무다 — framework가 전역 유일성을 만들어 주지 않으므로, 서로 다른
종류의 actor가 id 공간을 공유하면(예: customerId와 courierId가 같은 값) id 단독 lookup이 잘못된
actor를 돌려주게 된다. 이를 조용히 통과시키지 않기 위해 ensure/claim 경로는 row의 진단용
`ActorType`과 생성 대상 spot의 등록 type을 비교해, 같은 id로 다른 type의 live actor가 있으면
`ActorIdConflict`로 거부한다. 단 `FindAsync`/`SendToActor` 같은 id 단독 lookup은 호출자가 기대
type을 말하지 않으므로 이 검증이 원리적으로 불가능하다 — 이 한계를 계약 문구로 명시한다.

```csharp
public interface IZLinkActorDirectory
{
    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> EnsureAsync(
        string actorId,
        ZLinkMessage createRequest,
        ZLinkActorPlacement placement = default,
        CancellationToken cancellationToken = default);
}

public readonly record struct ZLinkActorPlacement(
    RoutingId? PreferredNodeRid = null,
    string? RouteMesh = null);
```

`IZLinkActorManager`가 local actor manager라면 `IZLinkActorDirectory`는 location store와 route mesh를
사용하는 distributed actor directory다. 사용자는 actor가 어느 node에서 만들어지는지, route request를
어느 mesh로 보내야 하는지, location row에 actor ref가 언제 publish되는지 알 필요가 없다.

#### directory 실패 계약

`FindAsync`에서 "live actor 없음"과 "publish 전"은 오류가 아니라 `null`이다. `EnsureAsync`는 성공 시
non-null `ActorRef`를 반환하므로 조용한 실패 경로가 없고, 실패는 actor client와 같은 규칙으로 분류된
예외다. 생성 경합(두 호출자가 동시에 ensure)은 오류가 아니다 — 먼저 만든 쪽의 actor ref를 양쪽 모두
받는다.

| 상황 | 결과 | 재시도 |
|------|------|--------|
| 같은 id의 live actor가 이미 있음(같은 type) | 기존 `ActorRef` 반환(정상) | — |
| 같은 id, 다른 type의 live actor 존재 | `ZLinkFrameworkException(ActorIdConflict)` | 무의미 — id 공간 설계 오류 |
| 생성 거부(admission/onActorJoin 거절) | `ZLinkFrameworkException(ActorCreateRejected)` | 무의미 — 거부 사유는 업무 판단 |
| route mesh 미연결 | `ZLinkFrameworkException(RouteNotConnected)`, retriable | 가치 있음 |
| store 장애 | store 예외를 원인 보존한 채 전파 | 인프라 장애 |

```csharp
var actorRef = await actors.EnsureAsync(
    courierId,
    ZLinkMessage.From(new EnsureCourierActorReq(courierId)),
    new ZLinkActorPlacement(PreferredNodeRid: topology.CourierPlacement(courierId).NodeRid),
    cancellationToken);

var boundActor = await context.Actors.BindOrGetAsync(actorRef, cancellationToken);
```

### 세션 바인딩 helper — 기존 인터페이스 확장(신설 아님)

현재 샘플의 `Find(...) ?? BindAsync(...)` 반복을 이름 있는 연산으로 흡수한다. `IZLinkSessionActors`는
이미 존재하는 public 인터페이스(`Contracts/Streams/IZLinkSession.cs`, `context.Actors`의 타입 —
`Bound`/`BindAsync`/`Find` 보유)이므로, 새 인터페이스를 만들지 않고 메서드 하나만 추가한다.

```csharp
// 기존 IZLinkSessionActors에 추가 — Bound/BindAsync/Find는 현행 유지
public interface IZLinkSessionActors
{
    // ...기존 멤버 생략...

    ValueTask<IZLinkSessionActor> BindOrGetAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);
}
```

기존 `Find`/`BindAsync`를 그대로 둘지(관용구의 원인이므로 guide에서 `BindOrGetAsync`를 기본으로
안내) 여부는 P0에서 결정한다.

### actor 메시징 API

DeliveryDispatch의 Tracking 흐름(`samples/DeliveryDispatch/Server/Tracking/Handlers.cs`)은
`IZLinkActorLocationResolver`를 주입받아 actor 위치를 직접 조회한 뒤 spot address로 보내고 있다.
실제 사용자는 "customer actor에게 상태 변경을 알려라"라고 쓰는 편이 자연스럽다.

기존 `IZLinkSendCall`/`IZLinkRequestCall`을 재사용하지 않고 actor 전용 call 타입을 분리한다.
`SendToSpot`은 이미 resolve된 주소를 받아 로컬 enqueue만 하므로 `void Submit()`이 정당하지만,
actor 대상 call은 내부에 store 읽기(비동기, 실패 가능)가 들어가므로 await 지점이 필수다. actor
send call에는 `Submit()` 터미널을 아예 두지 않는다 — 두 터미널이 공존하면 resolve 실패를 조용히
삼키는 경로가 계약에 남는다.

```csharp
public interface IZLinkActorClient
{
    IZLinkActorSendCall SendToActor<TMessage>(
        string actorId,
        TMessage message);

    IZLinkActorRequestCall RequestToActor<TRequest>(
        string actorId,
        TRequest request);
}

/// <summary>
/// actor 대상 send call. 터미널은 <see cref="Async"/> 하나다(void Submit 없음).
/// await 완료는 "위치 resolve 성공 + 연결된 route로 로컬 인계"까지를 뜻하며
/// 전달 보장(ack)이 아니다 — 전달 확인이 필요하면 RequestToActor를 쓴다.
/// PacketName 등 구성 옵션은 기존 send call과 동일하게 제공한다.
/// </summary>
public interface IZLinkActorSendCall
{
    IZLinkActorSendCall PacketName(string packetName);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkActorRequestCall
{
    IZLinkActorRequestCall PacketName(string packetName);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}
```

메서드 이름은 기존 문법(`SendToSpot`, `RequestToChannel`)과 동형인 `SendToActor`/`RequestToActor`로
하고 Async suffix는 붙이지 않는다 — 이 framework의 관례는 "구성은 fluent, 종결은 `.Async(ct)`"이며
await 지점은 터미널이 담당한다. actor id는 framework 전체에서 unique하므로 호출자는 actor type이나
actor 구현 타입을 넘기지 않는다. actor type은 생성, handler 등록, 운영 진단 정보로만 남는다.

사용 예시는 다음처럼 되어야 한다. Tracking은 `CustomerActor` 구현 타입이나 actor type 이름을 몰라도
actor id와 메시지만으로 보낼 수 있다.

```csharp
await actors.SendToActor(
        request.CustomerId,
        updated)
    .PacketName(nameof(DeliveryStatusUpdatedMsg))
    .Async(cancellationToken);
```

이 API는 내부에서 actor location을 읽고, owner liveness를 확인하고, entry/user spot address를 고른 뒤
현재 route client의 `SendToSpot(...)` 경로로 내려간다. 호출자는 `IZLinkActorAddressResolver`와
`ZLinkSpotAddress`를 직접 보지 않는다. spot 주소 보유·재-resolve 규칙(캐시 없는 resolve, 실패 시
재조회)은 이 client 내부 정책이 되므로, spot-address messaging draft의 fail-fast 오류 분류도 이
계층에서 흡수한다.

#### 실패 계약 — 위치를 못 찾으면 fail-fast, 분류된 오류

`.Async(ct)`는 다음 규칙으로 실패를 드러낸다. silent drop, 자동 actor 생성(auto-create), 위치를
찾을 때까지 메시지를 붙잡아 두는 파킹은 모두 금지다 — 캐시 전면 제거·fail-fast 결정과 상충하고
send에 생성 부작용을 섞는다.

| 상황 | 결과 | 재시도 |
|------|------|--------|
| actor row 없음 | `ZLinkFrameworkException(ActorRouteNotFound)` | 무의미. actor가 없다는 뜻 — 만들어서 보내려면 `EnsureAsync` 먼저 |
| row는 있으나 owner lease 만료(stale) | 내부에서 1회 re-resolve 후에도 stale이면 `ZLinkFrameworkException(ActorLocationStale)` | 가치 있음(takeover/재배치 중일 수 있음) — 호출자 bounded retry |
| route mesh 미연결 | `ZLinkFrameworkException(RouteNotConnected)`, retriable | 가치 있음 |
| store 장애 | store 예외를 원인 보존한 채 전파 | 인프라 장애 — 오류 규칙 단일화(경합=상태값, 장애=예외)와 동일 |

`ActorRouteNotFound`(재시도 무의미)와 `ActorLocationStale`(재시도 가치 있음)을 하나의 코드로 접지
않는다 — 호출자의 재시도 판단이 갈리는 지점이라 정보를 보존한다.

`SendToActor`/`RequestToActor`는 호출마다 store를 읽는 편의 표면이지 hot path 표면이 아니다. 같은
actor에게 고빈도로 보내는 흐름은 directory로 `ActorRef`/주소를 한 번 확보해 보유하고 기존
경로(session relay, 주소 보유 send)를 쓴다. 이 사용 구분을 계약 문구로 명시한다.

#### 수신 계약 — 새 인터페이스 없음, 기존 actor handler 재사용

`SendToActor`는 송신 표면만 신설한다. 수신은 이미 공통 spec에 확정된 actor dispatch 계약을 그대로
쓴다.

- 송신 wire: `SendToActor`가 메시지를 actor-addressed envelope(actorId 탑재)로 감싸 기존
  `SendToSpot` 프레이밍으로 보낸다. 수신 노드 입장에서 client actor-session relay로 들어온 packet과
  같은 ingress 경로다.
- 수신 dispatch: entry-spot actor dispatch spec대로 ingress가 envelope에서 actor id를 resolve해 대상
  actor의 mailbox로 넣는다. 같은 actor는 순서 보장, 다른 actor는 서로 대기하지 않는다.
- 수신 handler: 기존 `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>` /
  `IZLinkEntrySpotActorSendHandler<...>`(및 Request 변형)를 그대로 구현한다.

```csharp
// 수신 측 — 신규 계약 없이 기존 actor handler로 받는다.
internal sealed class DeliveryStatusUpdatedActorHandler
    : IZLinkEntrySpotActorSendHandler<CustomerEntrySpot, CustomerActor, DeliveryStatusUpdatedMsg>
{
    public ValueTask HandleAsync(
        CustomerEntrySpot entrySpot,
        CustomerActor actor,
        ZLinkSpotActorSendContext context,
        DeliveryStatusUpdatedMsg message,
        CancellationToken cancellationToken)
        => actor.PushStatusAsync(message, cancellationToken);
}
```

이 구조가 자리 잡으면 현재 DeliveryDispatch CustomerGateway의 spot packet handler + 샘플 자체
`CustomerActorDirectory`(Register/PushAsync demux) 구조가 통째로 사라진다 — actor id를 도메인 메시지
본문에 싣고 demux를 샘플이 재구현하는 것은 framework가 이미 가진 spot-actor 지식의 중복이다.

P0 결정 항목: 현재 actor-addressed envelope이 client actor-session relay 전제인지
(`ActorSessionNotBound` 오류 분류 존재) 확인하고, 세션 없는 서버측 `SendToActor`를 스펙이 허용하는
조건(admission 검문 통과 여부 포함)을 draft에 명시한다.

### actor ref wire model

세션 바인딩처럼 actor ref를 wire로 주고받는 흐름은 여전히 필요할 수 있다. 이때도 샘플마다 DTO를
반복하지 않게 framework가 직렬화 가능한 actor ref 모델과 변환 API를 제공한다.

```csharp
public sealed record ZLinkActorRefSnapshot(
    RoutingId NodeRid,
    string ActorId,
    ulong Generation)
{
    public static ZLinkActorRefSnapshot From(ActorRef actorRef);

    public ActorRef ToActorRef();
}
```

protobuf 샘플처럼 `RoutingId`를 직접 싣기 어려운 wire format은 generated message와
`ZLinkActorRefSnapshot` 사이의 변환을 샘플 한 곳에만 둔다. JSON 기반 샘플은 framework contract 타입을
그대로 메시지에 사용할 수 있어야 한다. 이렇게 하면 DeliveryDispatch, SupportChat, Bingo가 각자
`NodeRid.ToString()`, `RoutingId.From(...)`, `ToBytes()` 변환 정책을 반복하지 않는다.

### readiness API

DeliveryDispatch Dispatch host의 health check는 `IZLinkLocationRuntimeQuery.ListTopologyAsync(...)`로
route mesh ready row를 직접 찾는다. 운영 진단에서는 괜찮지만 일반 readiness 코드로는 너무 낮은
수준이다.

```csharp
/// <summary>
/// 일반 application health check용 boolean 질의. 확인 불가(store 장애 포함)는
/// false다 — readiness에서 "모른다"는 "준비 안 됐다"와 같다. 이는 "인프라
/// 장애는 예외" 일반 규칙의 명시된 예외이며, 현재 샘플마다 반복되는
/// try/catch → false 처리를 계약이 흡수한 것이다. 장애 원인 진단은
/// <see cref="IZLinkLocationRuntimeQuery.GetStatusAsync"/>가 담당한다.
/// </summary>
public interface IZLinkLocationReadiness
{
    ValueTask<bool> IsPeerReadyAsync(
        string meshName,
        ZLinkLocationRole role,
        RoutingId? nodeRid = null,
        CancellationToken cancellationToken = default);
}
```

사용 예시는 다음처럼 짧아야 한다.

```csharp
var node1Ready = await readiness.IsPeerReadyAsync(
    SampleNames.CourierActorNodeRouteChannel,
    ZLinkLocationRole.Router,
    topology.CourierActorNode1Rid,
    cancellationToken);
```

`IZLinkLocationRuntimeQuery`는 운영 화면, self-check evidence, 테스트 probe에 남기고, 일반 application
health check는 readiness API를 쓰게 한다.

### 샘플 표면 규칙

| 샘플 코드에서 금지 | 이유 | 대체 |
|-------------------|------|------|
| `ZLinkActorLocation.ActorRef` 직접 읽기 | location row는 운영/런타임 모델이다. | `IZLinkActorDirectory.FindAsync` 또는 actor manager |
| store 인터페이스(`IZLink*LocationStore`, `IZLinkOwnerLeaseStore`) 직접 사용 | store는 backend 구현자 SPI다. row와 owner liveness 규칙이 샘플로 새어 나간다. | `IZLinkActorDirectory.FindAsync` 또는 actor manager |
| `ResolveActorSpotAddressAsync(...)` 뒤 `SendToSpot(...)` 직접 조합 | actor 대상 전송의 내부 순서가 업무 코드에 드러난다. | `IZLinkActorClient.SendToActor/RequestToActor` |
| actor ref 문자열/바이트 직접 파싱 | wire/storage 표현이 샘플마다 달라진다. | `ZLinkActorRefSnapshot.ToActorRef()` |
| owner token/generation guard 직접 전달 | lifecycle fencing은 framework 내부 책임이다. | actor directory, actor client, runtime lifecycle |
| 업무 로직에서 `IZLinkLocationRuntimeQuery`로 목록 스캔 | location store가 application database처럼 쓰인다. | health check는 `IZLinkLocationReadiness`, 진단·self-check evidence만 runtime query |

## 변경 대비표

이름 변경·삭제는 P5의 grep 검증과 P6의 porting draft 차분 반영의 기준 목록이다.

| 기존 | 변경 | 성격 |
|------|------|------|
| `I*LocationStore.RemoveByOwnerAsync` ×4 | `IZLinkLocationStore.RemoveAllByOwnerAsync` 하나 | 계약 축소·원자화 |
| `ZLinkLocationWriteStatus.StoreUnavailable` | 삭제 — store 장애는 예외 | 오류 규칙 단일화 |
| `ZLinkLocationWriteResult.StoreUnavailable` 정적 멤버 | 삭제 | 〃 |
| `RenewOwnerLeaseAsync` → `ZLinkLocationWriteResult` | → `ZLinkOwnerLeaseRenewal` | 반환형 교정 |
| `RemoveOwnerLeaseAsync` → `ZLinkLocationWriteResult` | → `bool` | 〃 |
| `ZLinkActorLocation.ActorRef` (`string`) | `ActorRef?` | 직렬화 형식 은닉 |
| `ZLinkActorLocation.SpotKind` | 삭제 — `LocationKind`가 단독 정본 | 이중 표현 제거 |
| `ZLinkActorLocation.SpotMeshName` (init property) | positional 파라미터 | 필수 필드 강제 |
| `IZLinkSpotLocationResolver` | `IZLinkSpotAddressResolver` | 반환 의미 명명 |
| `IZLinkActorLocationResolver` | `IZLinkActorAddressResolver` | 〃 |
| `IZLinkRouteLocationResolver` | 삭제 — route row 단건 조회는 store/runtime 내부 표면으로 제한 | 사용자 messaging resolver에서 제외 |
| `IZLinkPeerLocationResolver.ListPeersAsync` | `ListLivePeersAsync` | liveness 조인 의미 명명 |
| `IZLinkLocationRuntimeQuery.ListPeersAsync` | `ListPeerLocationsAsync` | row 조회 의미 명명 |
| `IZLinkLocationRuntimeQuery.ListSpotsAsync`/`ListActorsAsync`/`ListRoutesAsync` | `List{Spot,Actor,Route}LocationsAsync` | 〃 |
| `ZLinkLocationChanged.LocationKey` (`string`) | `ZLinkLocationKey` typed union | key 형식 은닉 |
| `ZLinkLocationCanonicalNames` (public) | internal `ZLinkLocationValueCodec` 단일 테이블 | 지식 단일화·은닉 |
| `ZLinkLocationKind` | `Invalid = 0` 추가 | enum 규약 통일 |
| `Models.cs` 단일 파일 | `Values/Rows/Keys/Writes/Watch/Diagnostics.cs` 분리 | 관심사 분리 |
| `Resolvers.cs` 내 `IZLinkLocationRuntimeQuery` | `RuntimeQuery.cs`로 이동 | 표면 분리 |
| `IZLinkLocationStore` 이중 `<summary>` | 첫 번째(오배치) 삭제 | 문서 정리 |
| (없음) | `IZLinkActorDirectory`, `IZLinkActorClient`, `IZLinkLocationReadiness`, `ZLinkActorRefSnapshot` 신설 · 기존 `IZLinkSessionActors`에 `BindOrGetAsync` 추가 | 사용자 편의 표면 |

## cross-language 적용 설계

.NET 구현은 첫 번째 구현 기준일 뿐이다. 완료 판정은 다른 framework 언어가 같은 public contract,
같은 Redis row/codec, 같은 오류 의미를 구현할 수 있는 상태까지 포함한다. E2E나 샘플이 어떤 API를
썼다는 사실은 public 계약 근거가 아니므로, 각 언어 porting은 공통 draft와 이 절의 매핑을 기준으로
진행한다.

### 언어별 public contract 매핑

공통 draft에는 아래 매핑표를 넣고, 각 언어 porting draft는 자기 언어 열의 이름과 파일 위치를 채운다.
언어 특성 때문에 같은 이름을 쓸 수 없으면, 사용자에게 보이는 의미가 같은지와 차이가 필요한 이유를
명시한다.

| 계약 요소 | .NET | Java/Kotlin | Node.js | C++ |
|-----------|------|-------------|---------|-----|
| 비동기 결과 | `ValueTask<T>` / `ValueTask` | Java public contract는 `CompletionStage<T>`를 정본으로 둔다. Kotlin이 Java 계약을 그대로 쓰면 해당 타입을 사용하고, Kotlin facade를 별도로 만들 때만 `suspend` wrapper를 추가한다 | `Promise<T>` | future/promise 계열 또는 callback-free async wrapper. 기존 C++ framework 관례를 따른다 |
| nullable 결과 | `T?` | `Optional<T>` 또는 nullable annotation. Kotlin은 nullable type으로 노출 | `T | null` | `std::optional<T>` |
| typed location key union | `abstract record ZLinkLocationKey` + nested records | sealed interface + record implementations. Kotlin은 sealed interface 또는 Java 타입 재사용 | discriminated union object with `kind` field and typed payload | `std::variant` |
| actor id key | `ZLinkActorLocationKey(string ActorId)` | actor id 단일 key | actor id 단일 key | actor id 단일 key |
| actor ref row field | `ActorRef?` public model, Redis codec 내부 JSON | 언어별 `ActorRef` value. 문자열 포맷 public 노출 금지 | framework actor ref value/object. 문자열 포맷 public 노출 금지 | actor ref value type. 문자열 포맷 public 노출 금지 |
| send terminal | `SendToActor(...).PacketName(...).Async(ct)` | Java 계약 공유 시 Java async terminal을 그대로 쓴다. Kotlin facade를 별도로 만들 때만 `suspend` terminal을 제공한다. 두 경우 모두 `Submit()` 동기 terminal 금지 | promise terminal. 동기 fire-and-forget terminal 금지 | awaitable terminal. void submit terminal 금지 |
| actor not found | `ZLinkFrameworkException(ActorRouteNotFound)` | 같은 오류 code/name | 같은 오류 code/name | 같은 오류 code/name |
| stale actor location | `ActorLocationStale` | 같은 오류 code/name | 같은 오류 code/name | 같은 오류 code/name |
| store 장애 | 원인 보존 예외. write status 아님 | 원인 보존 예외. write status 아님 | rejected promise/error. write status 아님 | exception/error result. write status 아님 |
| readiness 확인 불가 | `false` | `false` | `false` | `false` |

Kotlin은 기본적으로 Java framework 계약을 공유한다. 따라서 별도 Kotlin facade를 만들지 않는 한 Java
porting draft 안에 Kotlin 노출 형태와 테스트 항목을 둔다. Kotlin facade를 만들기로 결정하는 경우에만
별도 Kotlin 차분 문서를 만들고, 그 문서에서 `suspend` 함수, nullable type, sealed type 이름을 고정한다.

### Redis row/codec 공통 스키마

Redis store는 언어 간 상호 운용의 실제 경계다. 따라서 .NET 구현 전에
`framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`의 Redis codec 절에
다음을 예시 JSON과 함께 고정한다. 구현이 끝난 뒤 정식 spec으로 승격할 때
`framework/doc/framework/common/spec/location-store-redis.ko.md`에 나누어 반영한다. 공통 fixture는
`framework/testdata/location/redis/actor-location-v2.json` 경로에 추가하고 모든 언어 Redis 테스트가
이 fixture를 기준으로 read/write roundtrip한다.

| 항목 | 고정해야 할 내용 |
|------|------------------|
| actor key | actor id 단독 key. actor type은 key에 포함하지 않는다 |
| actor row | `ActorId`, optional diagnostic `ActorType`, typed `ActorRef`, `NodeRid`, `LocationKind`, `SpotMeshName`, optional `SpotRid`, `OwnerId`, `Generation`, `UpdatedAt` |
| 제거 필드 | actor row의 `SpotKind` 제거. 문자열 actor ref 제거 |
| publish 전 row | `ActorRef == null` row가 store 내부에 잠깐 존재할 수 있어도 resolver/runtime query 공개 결과에는 노출하지 않는다 |
| route row value | application key-value가 아니라 framework route payload. payload 형식은 codec 내부에 둔다 |
| watch key | backend 문자열 key를 event 소비자에게 넘기지 않고 typed key로 디코딩한다 |
| owner 정리 | `RemoveAllByOwnerAsync`는 peer/spot/actor/route row를 같은 owner 기준으로 한 번에 정리한다 |
| 장애 표현 | Redis/network 장애는 예외로 전파하고 `StoreUnavailable` status를 쓰지 않는다 |

각 언어 Redis 테스트는 같은 fixture를 읽고 써야 한다. `"java-ref"`, `"node-ref"`, `"cpp-ref"` 같은 임의
문자열 fixture는 금지하고, typed actor ref JSON fixture를 공통 test data로 둔다.

### cross-language conformance gate

.NET 구현이 끝났다는 이유만으로 설계가 완료된 것이 아니다. 다음 gate가 모두 통과해야 다른 언어가 같은
디자인을 적용할 수 있다고 본다.

1. 공통 draft가 최종 public contract와 Redis codec 스키마를 설명한다.
2. .NET contract tests가 공통 draft의 예제를 그대로 고정한다.
3. 언어별 porting draft가 변경 대비표를 자기 언어 이름·파일·테스트로 매핑한다.
4. 각 언어는 public surface inventory를 생성하거나 손으로 작성해 공통 draft와 비교한다.
5. 각 언어는 store 장애, owner 정리, actor id 단독 lookup, actor ref typed codec, `SendToActor` 실패
   분류, readiness `false` 규칙을 테스트한다.
6. cross-language Redis fixture를 최소 두 언어 이상에서 read/write roundtrip한다. 모든 언어 구현 전에는
   .NET fixture를 기준 fixture로 두고 porting draft에 미구현 언어의 gap을 남긴다.
7. 언어별 샘플은 업무 흐름에서 location row, owner token, actor ref 문자열, `ResolveActorSpotAddressAsync`
   뒤 `SendToSpot` 직접 조합을 쓰지 않는다.

### 적용 순서

1. 공통 draft와 Redis codec 스키마를 먼저 확정한다.
2. .NET contract와 Redis implementation을 구현하고 contract/unit/Redis tests로 고정한다.
3. .NET 샘플을 새 사용자 편의 표면으로 바꾸고 sample regression을 추가한다.
4. Node/Java/C++ porting draft와 Kotlin 적용 기준을 갱신한다. 이때 draft마다 "이 문서의 변경 대비표 중
   적용 완료/미완료/gap" 표를 둔다.
5. 언어별로 하나씩 적용한다. 각 언어는 public contract 적용 리뷰와 POSD 리뷰가 `이슈 없음`일 때 다음
   언어로 넘어간다.
6. 모든 언어가 끝나기 전까지 공통 draft의 미구현 언어 항목은 완료로 바꾸지 않는다.

## 작업 단계

### P0. 계약 재설계 초안 작성

- [x] ✅(2026-07-04) 원본 draft §0 "POSD 재설계 변경 후보" 절로 반영(참조 방식 — 본문 전면 갱신은
      P6). 공통 draft(`framework-location-resolver-store.ko.md`)에 이 문서의 "목표 계약 전문"과 "변경
      대비표"를 변경 후보로 반영한다.
- [x] 사용자 편의 표면(`IZLinkActorDirectory`, `IZLinkActorClient`, `IZLinkLocationReadiness`,
      `ZLinkActorRefSnapshot` 신설, 기존 `IZLinkSessionActors`에 `BindOrGetAsync` 추가)을 public API
      변경 후보로 공통 draft에 올린다(parity 정책상 draft 없는 public API 선행 구현 금지).
      **완료(2026-07-04)**: location 원본 draft §0(POSD 재설계 변경 후보)과 spot-address draft
      헤더에 변경 후보로 반영.
- [x] `IZLinkSessionActors`의 기존 `Find`/`BindAsync`를 `BindOrGetAsync` 도입 후에도 그대로 둘지
      결정한다(관용구의 원인 — 최소한 guide는 `BindOrGetAsync`를 기본으로 안내).
      **결정(2026-07-04): 둘 다 유지.** `Find`는 bind 없이 조회만 필요한 흐름(존재 확인)에,
      `BindAsync`는 명시적 재바인딩에 각자 용도가 있고 소비자가 광범위하다. guide·샘플은
      `BindOrGetAsync`를 기본으로 안내하고, `Find ?? BindAsync` 관용구만 샘플 금지 규칙에 포함한다.
- [x] `SendToActor`의 actor-addressed envelope 규칙을 확정한다: client actor-session relay와 같은
      framing을 재사용할 수 있는지, 세션 없는 서버측 송신을 허용하는 조건과 admission 검문 통과
      여부를 명시한다. 수신은 기존 actor handler 계약 재사용이 전제다(신규 수신 API 없음).
      **결정(2026-07-04, P0 조사-3): actor-forwarding framing 재사용, bound-session 경로는 사용
      불가.** 근거: actor id는 stream header가 아니라 native actor ref(actor envelope)로 운반되고,
      `SendActorBoundSession` 경로는 세션 bind를 전제(`ZLinkBoundSessionService.cs:93`,
      `ActorSessionNotBound`)하지만 forwarding 경로(`ForwardActorBoundSessionPart` + core
      `service_spot_actor_api.cpp:3105`)는 actor ref+generation+source rid만 검증하고 bound
      session을 요구하지 않는다. 수신 ingress는 기존 entry-spot actor dispatch가 그대로 소화한다.
      admission은 join 경로의 관문이며 SendToActor dispatch는 관문이 아니다.
      **남은 설계 조건(공통 draft에 명시)**: 세션 없는 서버 송신의 source node/session rid 규칙 —
      현행 수신측은 도착한 source rid를 actor의 bound-session route로 갱신하므로
      (`ZLinkEntrySpotActorDispatcher.cs:119`), 서버 송신이 클라이언트 세션 라우팅을 오염시키지
      않도록 bind 갱신을 유발하지 않는 framing 변형(또는 스킵 플래그)이 필요하다.
- [x] ✅(2026-07-04) 원본 draft §0 L4·L5로 반영됨. actor id가 framework 전체에서 unique하다는 계약을 공통 draft에 명시한다. actor location key,
      resolver, actor client, actor directory는 actor id 단독 lookup만 사용한다. actor type은 생성,
      handler 등록, 운영 진단 정보이며 lookup key가 아니다.
- [x] ✅(2026-07-04, P0 조사-4) 확정 — 거부 오류는 기존 `ActorTypeMismatch` 재사용. actor id 충돌(같은 id, 다른 type)의 처리를 확정한다: ensure/claim이 row의 진단용 `ActorType`과
      생성 대상 spot의 등록 type을 비교해 `ActorIdConflict`로 거부한다(silent wrong-actor 반환 금지).
      id 단독 lookup(`FindAsync`/`SendToActor`)은 type 검증이 불가능하다는 한계를 계약 문구로
      명시한다.
- [x] actor 대상 call·directory·readiness의 실패 계약을 draft에 확정한다: 오류 분류, `.Async(ct)`
      완료 의미("resolve 성공 + 로컬 인계", 전달 보장 아님), void `Submit()` 터미널 부재, readiness의
      "확인 불가 = false"(인프라 장애 예외 규칙의 명시된 예외). 전제였던 기존
      `ZLinkFrameworkErrorKind` 대조를 수행했다(P0 조사-4, 전 kind 102개 throw 지점 전수).
      **결정(2026-07-04): `ActorIdConflict`는 신설하지 않고 기존 `ActorTypeMismatch`를 재사용한다**
      — 현행 throw 지점(`ZLinkActorRuntimeState.cs:172`, `ZLinkActorSessionManager.cs:131`)의 의미가
      정확히 "같은 actor id가 이미 다른 type을 사용"이다. **`ActorLocationStale`과
      `ActorCreateRejected`는 신설한다** — stale은 `ActorRouteNotFound`(전부 false)와 재시도 판단이
      갈리고, admission 거절은 `ActorCreateFailed`(factory 미등록·claim 실패)·`RequestRejected`
      (저수준 result 매핑, 조건부 retriable)와 의미·retry 정책이 다르다. 이 문서의 실패 계약
      표들에서 `ActorIdConflict` 표기는 `ActorTypeMismatch`로 읽는다(구현 시 일괄 반영).
      부수 확정: retriable 불일치 실측 4건(`ActorSessionNotBound`/`RequestRejected`/`RequestFailed`/
      `SpotRouteNotFound`) → A3의 kind→retriable 단일 매핑으로 해소. java는 오류 kind enum 자체가
      없고(parity 미구현), cpp는 25종으로 이미 drift — 값 테이블 확정 시 함께 정렬(공통 문서 5절).
- [x] `ZLinkLocationRole`의 ushort 기반과 값 1 결번의 근거(core 상수 정합 여부)를 확인해 주석으로
      고정하거나 기반 타입을 되돌린다.
      **결정(2026-07-04, P0 조사-1): ushort·값 유지 + 주석 고정.** 근거: ① 값 1은 제거된 core
      `service_role_gateway = 1`의 자리(도입 커밋 `c4a1dcebb`, 현행 `zlink_enum.h:316`은 0,2~6)
      ② core discovery wire가 `uint16_t service_role`을 전송(`discovery_protocol.hpp:256,408`),
      C++ framework도 `std::uint16_t` 기반 ③ Redis row JSON에 숫자 값이 실제 저장되어(4언어 공통)
      숫자가 호환성 표면임. 부수 발견: java enum은 ordinal 기반이라 명시 값 부여 필요(D1, java S1).
- [x] `IZLinkLocationRuntimeQuery`의 liveness 조인 여부를 현행 구현과 테스트 의존에서 조사하되,
      계약 문구는 공통 draft에서 확정한다. 테스트 의존은 public contract 근거가 아니라 변경 영향
      범위로만 기록한다.
      **결정(2026-07-04, P0 조사-2): List 계열 = owner liveness 조인된 live row 반환으로 계약 확정.**
      근거: 현행 구현이 전 메서드 live-only(`ZLinkLocationRuntimeQueryService.FilterLiveAsync`)이고
      e2e 3종(RM-A4/RM-B2/SF-A2)이 "종료된 peer가 결과에서 사라진다"에 의존한다. stale 관측은
      `ListTopologyAsync`(peer의 만료 owner를 `Lost`로 노출)와 `ListServiceSummariesAsync`(`Stopped`
      집계)가 담당한다. 원시 row가 필요한 진단은 store SPI의 몫이다. 이에 따라 세 계층 구분은
      "store=원시 / resolver·runtime query=live / stale 관측=topology·summary"로 정리된다.
- [x] **결정(2026-07-04, P0 조사-5): public extension 유지(advanced 분류).** 근거: spec
      (location-runtime.ko.md §5)·guide(dotnet 05-spot §"교체 지점")가 "기본 location store
      resolver를 직접 구현으로 교체하는 고급 확장점"으로 이미 인정하고, dotnet/node/java 3언어에
      동일 표면이 존재하며, TicTacToe 공통 샘플이 정당한 소비자다. 문구는 "외부 route directory"가
      아니라 "기본 resolver 교체용 고급 SPOT 주소 해석 확장점"으로 정정. **parity gap: cpp에는
      동일 extension이 없음** — cpp wave S0에서 동등 기능 확인 후 신설 판정(한 언어 결손).
      `IZLinkSpotRemoteAddressResolver`와 `AddSpotRemoteAddressResolver<TResolver>()`가 외부 route
      directory 연동용 public extension인지 spec/guide 근거로 확인한다. 근거가 있으면 advanced SPOT
      routing extension으로 문서화하고, 근거가 없으면 public 후보에서 제외한다.
- [x] location runtime event payload가 public monitoring contract인지 monitoring spec/guide 근거로
      확인한다. 유지한다면 운영 관측 전용임을 명시하고, 유지하지 않는다면 summary 중심 event draft를
      별도로 작성한다.
      **결정(2026-07-04, P0 조사-6): monitoring public contract 유지 + "운영 관측·디버깅 전용,
      application routing 결정에는 resolver/runtime query 정식 경로 사용" 문구를 공통 spec과
      guide에 추가.** 근거: event source/kind는 공통 spec(location-runtime.ko.md §event source,
      channel-topology.ko.md)에 이미 계약으로 존재. 단 **row 객체 payload 필드 수준은 공통 spec에
      미명시이고 dotnet/node에만 구현됨 — java/cpp에는 동일 row event 타입 부재(parity gap, 해당
      wave S0/S1에서 처리)**. payload의 summary 축소는 하지 않는다(기존 표면·공통 계약과 충돌).
- [x] `ZLinkSocketNativeEventType`와 `ZLinkSocketDiagnostic.NativeValue`가 stable cross-language
      monitoring contract인지 backend diagnostic detail인지 판정한다. stable contract가 아니면
      backend-neutral diagnostic model 변경안을 별도 monitoring draft에 적는다.
      **판정(2026-07-04, P0 조사-7): backend diagnostic detail.** framework 문서는 값 테이블을
      cross-language로 고정하지 않고, common e2e가 요구하는 것은 logical `ZLinkSocketEventKind`뿐.
      java는 값 미노출(diagnostic 0,0), cpp는 숫자 field만. → `ZLinkSocketEventKind` 중심 모델로
      감싸는 변경안을 **별도 monitoring draft**로 작성(이 계획 범위 밖, 후속 항목). 이 항목은
      판정까지 완료로 처리한다. ✅
- [x] `IZLinkChannelRuntimeOptions`의 runtime drain/restore가 public 운영 계약인지 spec/guide 근거로
      확인한다. public 운영 계약이면 별도 channel-runtime-control draft에서 option mirror 방식과
      `Drain`/`Restore`/`SetWeight` 방식 두 가지를 비교한다.
      **판정(2026-07-04, P0 조사-8): 공통 spec 정본 부재 확인(guide·e2e 사용은 실재).** 현행 option
      mirror의 이중성(같은 인터페이스가 build/runtime에서 다른 의미)은 spec으로 닫혀 있지 않다.
      → `Drain`/`RestoreWeight`/`SetWeight` 좁은 control API를 **별도 channel-runtime-control
      draft**로 설계(이 계획 범위 밖, 후속 항목). 이 항목은 판정까지 완료로 처리한다.
- [x] `ZLinkLocationKey` typed union의 형태를 언어 이식성 관점에서 확정한다.
      **결정(2026-07-04)**: C#=중첩 sealed record + private 생성자(닫힌 계층), java=sealed
      interface + record 구현, kotlin=java 공유, node=discriminated union(kind 태그),
      cpp=`std::variant`. 공통 문서 2절 idiom 매핑과 일치.
- [x] actor row의 `SpotKind` 필드 제거와 actor key에서 `ActorType` 제거(actor id 단독 key)가 Redis
      cross-language row/key 형식에 주는 영향을 store codec 문서에 기록한다.
      **완료(2026-07-04)**: location 원본 draft §0의 L4·L5 항목에 기록 — key schema 정본(원본
      draft store 형식 절)에서 함께 갱신하기로 명시. 본문 형식 절 갱신은 P6.
- [x] "cross-language 적용 설계"의 언어별 public contract 매핑표와 Redis row/codec 공통 스키마를
      공통 draft에 반영한다. 각 언어별 이름이 아직 확정되지 않은 칸은 `미확정`으로 남기고, .NET
      구현 전에 확정해야 할 항목으로 표시한다.
      **완료(2026-07-04, 참조 체계로 충족)**: 언어별 매핑은 통합 문서 2절(idiom 매핑)+5절(값
      테이블, D1 값 표 포함)이 정본이고, 언어별 심볼 대조는 각 wave 문서의 S0 보고서가 담당.
      Redis row/codec 공통 스키마 변경 3종(actor key=id 단독·typed actor ref JSON·SpotKind 제거)은
      location-store-redis.ko.md "POSD 재설계 반영" 절에 기록. 원본 draft §0가 이 참조 체계를
      가리킨다.
- [x] 기존 언어 porting draft가 이 변경을 따라가야 함을 기록한다.
      **완료(2026-07-04)**: porting-{node,java,cpp} 문서 헤더에 2차 wave 참조 추가(각 언어 진행
      문서 `framework-public-contract-posd-redesign-{lang}.ko.md`가 후속임을 명시). 대상은 Node, Java, C++이며,
      Kotlin은 Java 계약 공유 여부를 확인해 Java draft 안에 적용 기준을 넣을지 별도 Kotlin 차분
      문서를 만들지 결정한다.

완료 조건:
- 공통 draft에 "현재 공개 계약이 아니라 변경 후보"라는 상태가 명확하다.
- .NET 구현을 시작하기 전에 새 public API가 이 문서와 공통 draft에서 같은 내용으로 정리되어 있다.
- .NET 구현을 시작하기 전에 각 언어가 따라야 할 public API shape, 오류 이름, async terminal, Redis
  schema, Kotlin 적용 기준의 미확정 항목이 0개다. 비핵심 문서 위치 같은 항목만 미확정 사유와 결정자를
  공통 draft에 남길 수 있다.

### P1. .NET contract 파일 재구성 — ✅ 2026-07-04 완료 (LOC-1: 빌드·ContractTests 35/35·구 이름 grep 0 검증 통과)

- [x] `Models.cs`를 `Values/Rows/Keys/Writes/Watch/Diagnostics.cs`로 분리한다.
- [x] `ZLinkActorLocation`을 목표 형태로 바꾼다: actor id unique 계약, `ActorRef?`, `SpotKind` 삭제,
      `SpotMeshName` positional 승격. `ActorType`은 남기더라도 nullable 진단/생성 정보로만 둔다.
- [x] `ZLinkLocationWriteStatus`/`ZLinkLocationWriteResult`에서 `StoreUnavailable`을 삭제한다.
- [x] `ZLinkOwnerLeaseRenewal`을 추가하고 lease store 반환형을 교체한다.
- [x] `ZLinkLocationChanged`의 key를 `ZLinkLocationKey` typed union으로 바꾼다.
- [x] `ZLinkLocationKind`에 `Invalid = 0`을 추가한다.
- [x] `ZLinkLocationCanonicalNames`를 삭제하고 internal `ZLinkLocationValueCodec` 단일 테이블로
      옮긴다.
- [x] `ZLinkPeerLocationKey`에 identity 규칙 주석을, `ZLinkRouteLocation.Value`에 "framework route
      payload" 계약 문구를 박는다.
- [x] `ZLinkPageRequest`/`ZLinkLocationPage<T>` 주석에 store·운영 조회 공통 모델임을 명확히 한다.

완료 조건:
- public model에 저장소 직렬화 형식(문자열 actor ref, canonical 문자열, 인코딩된 key)이 노출되지
  않는다.
- nullable actor ref가 필요한 이유와 노출 금지 규칙이 주석과 draft에 설명되어 있다.

### P2. store 계약 수정 — ✅ 2026-07-04 완료 (LOC-1: A4 포함, Redis 원자 정리 구현, 검증 통과)

- [x] kind별 `RemoveByOwnerAsync` 4개를 삭제하고 `IZLinkLocationStore.RemoveAllByOwnerAsync`를
      추가한다.
- [x] `IZLinkLocationStore`의 오배치된 첫 `<summary>`를 삭제하고 오류 규약 문구를 추가한다.
- [x] InMemory store: `RemoveAllByOwnerAsync`를 단일 lock 구간에서 4맵 일괄 정리로 구현한다.
- [x] Redis store: `RemoveAllByOwnerAsync`를 단일 스크립트(또는 트랜잭션)로 구현해 원자성을
      확보한다.
- [x] 두 store에서 `StoreUnavailable` 반환 경로를 예외로 바꾸고, 호출자(`ZLinkLocationRuntime`,
      heartbeat, auto connect 루프)가 예외를 잡아 fail-static grace를 적용하도록 옮긴다.
- [x] `ZLinkLocationRuntime` shutdown 경로의 4연쇄 호출을 `RemoveAllByOwnerAsync` 한 번으로 줄인다.

완료 조건:
- 합성 인터페이스에서 owner 정리 호출이 모호하지 않고, 명시적 인터페이스 구현 도피가 필요 없다.
- store 장애가 상태값과 예외 두 갈래로 보고되는 경로가 없다.

### P3. resolver·runtime query 분리와 사용자 편의 표면

- [x] ✅2026-07-04(LOC-1) `Resolvers.cs`에는 live peer 조회와 messaging resolver만 남기고 `IZLinkLocationRuntimeQuery`를
      `RuntimeQuery.cs`로 옮긴다. `IZLinkRouteLocationResolver`는 사용자 messaging resolver가 아니므로
      제거하고, route row 단건 조회는 store/runtime 내부 표면에서만 사용한다.
- [x] ✅2026-07-04(LOC-1) `IZLinkSpotLocationResolver` → `IZLinkSpotAddressResolver`, `IZLinkActorLocationResolver` →
      `IZLinkActorAddressResolver`로 개명한다.
- [x] ✅2026-07-04(P3a) actor ref 조회 public 표면은 `IZLinkActorDirectory.FindAsync` 하나로 한다 — 같은 질문에 답하는
      별도 resolver(`IZLinkActorRefResolver`류)를 만들지 않는다. framework 내부 ref 조회는 internal
      표면으로 해결한다.
- [x] ✅2026-07-04(LOC-1) resolver의 `ListPeersAsync`를 `ListLivePeersAsync`로, runtime query의 list를
      `List*LocationsAsync`로 개명한다.
- [x] ✅2026-07-04(P3a) `IZLinkActorDirectory`(actor id 단독 find/ensure + placement)를 추가한다.
- [ ] ⏸보류(2026-07-04, P3b 설계 보고 — core 협력 필요, 통합 문서 9절 Q1) `IZLinkActorClient.SendToActor/RequestToActor`를 actor id 단독 호출로 추가하고, 내부에서 resolve → liveness →
      SendToSpot 순서와 실패 시 1회 re-resolve 정책을 흡수한다. actor 전용 call은
      `IZLinkActorSendCall`/`IZLinkActorRequestCall`로 분리하고 터미널은 `.Async(ct)` 단독으로
      한다(void `Submit()` 금지).
- [ ] 부분 완료(2026-07-04: kind 신설·retriable 매핑·directory·readiness는 P3a로 구현, actor client 분류는 Q1 보류) 실패 계약을 구현한다: `ActorRouteNotFound`/`ActorLocationStale`/`RouteNotConnected`(retriable)
      구분, directory의 `ActorIdConflict`/`ActorCreateRejected`, readiness의 "확인 불가 = false",
      store 장애는 원인 보존 예외, silent drop·auto-create·메시지 파킹 금지.
- [x] ✅2026-07-04(P3a) 기존 `IZLinkSessionActors`에 `BindOrGetAsync`를 추가한다(신설 인터페이스 아님).
- [x] ✅2026-07-04(P3a) `IZLinkLocationReadiness.IsPeerReadyAsync`를 추가해 일반 health check가 topology row를 직접
      스캔하지 않게 한다.
- [x] ✅2026-07-04(P3a) `ZLinkActorRefSnapshot`을 framework contract로 추가한다.
- [x] ✅2026-07-04(P3a) 샘플이 직접 정의한 `ActorRefSnapshot`을 제거하거나 protobuf 경계의 generated message 변환으로만
      제한한다.
- [ ] 부분 완료(2026-07-04: 확보·바인딩·health check는 P3a로 이식, Tracking 전송 이식은 Q1 보류) DeliveryDispatch Tracking, Bingo, SupportChat의 actor 확보/전송/바인딩/health check 흐름을 새
      표면으로 이식한다.

완료 조건:
- resolver가 row page를 반환하지 않고, 세 계층(list-live / list-rows / store)의 의미 차이가 이름에
  드러난다.
- 실제 사용자 코드가 actor find/ensure, actor messaging, readiness를 store row나 spot address 없이
  표현할 수 있다.
- 샘플 코드가 actor ref 문자열/바이트 변환을 반복하지 않는다.

### P4. lifecycle과 Redis codec 수정 — ✅ 2026-07-04 완료 (P4+P4-fix: ContractTests 35/35·UnitTests 264/264·Redis.Tests 22통과6스킵, grep 가드 0)

- [x] `ZLinkLocationLifecycle`에서 `string actorRef` 경로를 `ActorRef` 기반으로 바꾸고, actor row
      생성 시 `LocationKind` 하나만 채운다.
- [x] claim 단계와 actor ref publish 단계를 internal `IZLinkActorLocationLifecycle`로 분리하고,
      publish 전 행을 공개 조회에서 숨긴다.
- [x] `ZLinkActorCreationCoordinator`가 actor ref를 문자열로 직렬화하지 않게 바꾼다.
- [x] `ZLinkStoreLocationResolvers`가 actor row의 typed `ActorRef`를 사용하게 한다.
- [x] Redis row JSON에서 actor ref 저장 형식을 내부 codec으로 캡슐화하고, actor row의 중복
      spot-kind 필드를 제거한다(cross-language 형식 변경으로 기록).
- [x] Redis watch event의 문자열 key 디코딩을 store 내부에서 끝내고 typed key로 발행한다.
- [x] cross-language 테스트 fixture의 `"java-ref"`, `"node-ref"`, `"cpp-ref"` 같은 임의 문자열을
      typed actor ref JSON으로 바꾼다.
- [x] key prefix, generation counter, owner lease 원자성은 그대로 유지한다.

완료 조건:
- runtime 내부에서 actor ref 문자열 포맷을 만들지 않는다.
- actor ref publish 전 상태가 public API 사용자에게 불완전한 row로 보이지 않는다.
- Redis JSON 필드 구성은 extension 내부 테스트에서만 직접 다룬다.

### P5. 테스트와 회귀 검증 — ✅ 2026-07-04 완료 (테스트 4종 그린: 35/35·267/267·23/23·Redis 24, e2e 8/8 PASS, grep 가드 0 — actor client 검증 1건만 Q1 보류)

- [x] contract test에서 새 public API를 예제로 고정한다.
- [x] `RemoveAllByOwnerAsync`가 4개 kind 행을 모두 지우는지, Redis에서 원자적인지 검증한다.
- [x] store 장애 주입 시 읽기/쓰기 모두 예외이고, auto connect가 grace를 적용하는지 검증한다.
- [x] actor claim → publish → move → entry/user 전환과, publish 전 행이 공개 조회에 노출되지 않음을
      검증한다.
- [x] lease renew가 `ZLinkOwnerLeaseRenewal`을 반환하고 만료 시각이 store 시계 기준인지 검증한다.
- [x] Redis test에서 typed actor ref roundtrip과 cross-language row 호환을 검증한다.
- [x] 공통 Redis fixture를 추가하고 .NET Redis test가 그 fixture를 read/write roundtrip한다. 다른
      언어가 아직 구현되지 않은 상태에서는 이 fixture를 porting draft의 기준 fixture로 링크한다.
- [x] 변경 대비표의 기존 이름이 코드베이스에 남지 않았는지 grep으로 검증한다.
- [x] sample regression test에 샘플이 업무 흐름에서 `ZLinkActorLocation`,
      `IZLinkActorLocationStore`, `IZLinkActorAddressResolver`, actor ref 문자열 파싱을 직접 쓰지
      않는다는 검증을 추가한다.
- [ ] ⏸보류(Q1 — actor client 자체가 보류) DeliveryDispatch Tracking처럼 actor에게 메시지를 보내는 흐름이 `IZLinkActorClient`를 쓰는지
      검증한다.
- [x] session bind 흐름이 `BindOrGetAsync`로 표현되는지 검증한다.
- [x] health/readiness 흐름이 `IZLinkLocationReadiness`를 쓰고, `IZLinkLocationRuntimeQuery`는
      self-check/evidence에만 남는지 검증한다.
- [x] **e2e 전수 전환**: LocationMessaging·SpotService·StoreFailure·YieldDispatch·ResilienceLifecycle
      등 location 표면을 쓰는 모든 e2e가 새 계약(개명된 resolver/query, actor client/directory,
      readiness, typed key)으로 전환되고 그린임을 확인한다. e2e는 계약 검증 경로이므로 구 표면
      호출이 하나라도 남으면 해당 항목은 미완료다.
- [x] **샘플 전수 전환**: DeliveryDispatch·SupportChat·Bingo·TicTacToe 등 모든 dotnet 샘플이 새
      표면만 사용한다. 변경 대비표의 구 이름 grep 범위에 `samples/`와 `e2e/`를 포함한다.

필수 명령:

```bash
dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-restore
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore
dotnet test framework/languages/dotnet/tests/Zlink.Framework.Locations.Redis.Tests/Zlink.Framework.Locations.Redis.Tests.csproj --no-restore
```

### P6. 문서와 cross-language 계획 갱신 — ✅ 2026-07-04 완료 (P6-a codex 정합 8문서 + 감독자 직접: 샘플 문서 3건·의미 서술 4건·Redis 형식 변경 기록·미혼입 확인. grace 의미론은 D6-FIX로 코드를 spec에 정합)

- [x] 공통 draft의 API 예제를 최종 .NET 코드와 맞춘다.
- [x] Node/Java/C++ porting draft에 변경 대비표를 차분으로 반영한다 — 세 언어는 현행 계약으로 이식
      중이므로, 어느 단계 이후의 이식분부터 새 계약을 따르는지 명시한다. 각 draft는 "적용 완료 /
      미완료 / public contract gap" 표를 가져야 한다.
- [x] Kotlin 적용 기준을 명시한다. Java framework 계약을 그대로 쓰면 Java porting draft에 Kotlin
      노출 형태와 테스트 항목을 추가하고, Kotlin 전용 facade가 있으면 별도 Kotlin 차분 문서를 만든다.
- [x] 각 언어별 porting draft에 public contract mapping 표를 추가한다. 최소 항목은 async 결과,
      nullable 결과, typed key union, actor id 단독 key, typed actor ref, `SendToActor` terminal,
      오류 code/name, readiness `false` 규칙이다.
- [x] `framework/doc/framework/common/draft/framework-location-resolver-store.ko.md`의 Redis codec 절에
      예시 JSON을 둔다. actor row는 actor id 단독 key, typed actor ref, `LocationKind`,
      `SpotMeshName`, owner/generation 필드를 보여 주고, 제거된 `SpotKind`와 문자열 actor ref가 없음을
      명시한다.
- [x] 공통 Redis fixture `framework/testdata/location/redis/actor-location-v2.json`을 추가하고,
      .NET Redis test와 다른 언어 porting draft가 같은 fixture를 참조하게 한다.
- [x] 공통 샘플 문서(`framework/doc/framework/common/sample/`)의 흐름 설명을 새 사용자 편의 표면
      기준으로 갱신한다.
- [x] framework common spec/guide에 아직 확정되지 않은 내용을 섞지 않는다. 구현 전 설계 후보는
      draft에 남기고, 구현된 계약만 정식 spec으로 승격한다.

완료 조건:
- 공통 draft와 .NET contract test가 서로 다른 API를 말하지 않는다.
- 다른 언어 계획 문서에 `string ActorRef`, kind별 `RemoveByOwnerAsync`, `StoreUnavailable`을
  따라가라는 지시가 남아 있지 않다.
- Node/Java/C++ 및 Kotlin 적용 기준에 같은 public contract mapping, Redis schema, 오류 의미가
  들어 있다.
- 다른 언어가 아직 구현 전이면 porting draft에 gap으로 남아 있고, 완료로 표기되어 있지 않다.

### P7. POSD/DDD 리뷰 게이트 — ✅ 2026-07-04 완료
(리뷰-1 contract 적용: 4차에서 "이슈 없음" / 리뷰-2 POSD·DDD: 5차에서 "이슈 없음". 반복 중 소탕:
검토 문서 B·C·D 전 항목 이행(BATCH-1~5), Redis cross-language fixture 신설(P6 체크 실물 결손 교정),
remote join codec 통합·경계 타입화, route bridge known-but-not-ready fail-fast 정정(spec §4.1 정합),
key codec 의도적 이중화 상호참조 주석. 최종 실측: 배터리 4종 그린(35/267/23/Redis 26) + e2e 8/8
PASS. 잔여 미체크는 Q1 보류 3건뿐(아래 체크박스에 보류 사유 명기).)

구현 후 별도 Codex 에이전트로 다음 두 리뷰를 수행한다(요청당 한 항목, 병렬 가능).

1. public contract 적용 리뷰
   - 공통 draft의 모든 API 변경이 .NET contract와 tests에 반영됐는지 확인한다.
   - 저장소 문자열 형식이 public model, resolver, runtime query로 새지 않는지 확인한다.
   - cross-language 적용 설계가 공통 draft의 Redis codec 절, 공통 Redis fixture
     `framework/testdata/location/redis/actor-location-v2.json`, Node/Java/C++ porting draft, Kotlin
     적용 기준에 모두 반영됐는지 확인한다.
   - 누락이 있으면 구현 단계로 되돌아간다.

2. POSD/DDD 리뷰
   - resolver, runtime query, store, lifecycle, 사용자 편의 표면의 책임이 섞이지 않았는지 확인한다.
   - caller가 owner token, generation, actor ref publish 순서를 불필요하게 알아야 하는지 확인한다.
   - actor ref codec, key codec, canonical 문자열, route payload 같은 format 지식이 각각 한 모듈에
     갇혀 있는지 확인한다.
   - 언어별 차이가 사용자 의미 차이로 새지 않는지 확인한다. 언어 특성 때문에 다른 모양을 쓰는 경우,
     공통 draft의 mapping 표에 이유와 대체 계약이 기록되어 있어야 한다.
   - 리뷰 결과가 `이슈 없음`일 때만 완료로 판정한다.

## 금지 사항

- `string ActorRef`를 compatibility property로 남기지 않는다.
- public API에 Redis JSON 필드명, canonical key 문자열, actor ref 문자열 포맷을 노출하지 않는다.
- 기존 이름을 유지하기 위해 얕은 wrapper를 추가하지 않는다.
- kind별 `RemoveByOwnerAsync`를 어떤 이유로도 재도입하지 않는다.
- `StoreUnavailable`류의 인프라 장애 상태값을 write status에 되돌리지 않는다.
- kind별 store 인터페이스를 제너릭 `IStore<TRow, TKey, TFilter>`로 통합하지 않는다.
- 테스트를 통과시키기 위해 샘플이나 E2E에서 actor ref 문자열을 직접 파싱하지 않는다.
- 공통 draft에 없는 새 public API를 .NET에만 먼저 확정하지 않는다.
- 샘플 업무 로직에서 `IZLinkLocationRuntimeQuery`로 actor/spot/route 목록을 스캔해 routing 결정을
  내리지 않는다.
- 샘플 업무 로직에서 `ResolveActorSpotAddressAsync(...)` 뒤 `SendToSpot(...)`을 직접 조합하지
  않는다. actor 대상 전송은 actor-level client가 맡는다.
- 샘플마다 독자적인 actor ref DTO를 반복해서 만들지 않는다. wire format이 필요하면
  framework-provided snapshot 또는 generated protobuf 경계 변환만 사용한다.

## 완료 판정

이 계획은 다음 조건을 모두 만족해야 완료된다.

- 공통 draft가 이 문서의 "목표 계약 전문"·"사용자 편의 표면"과 같은 내용으로 새 location contract를
  설명한다.
- .NET `Contracts/Locations`가 목표 파일 구성과 일치하고, public model에 저장소 직렬화 형식이 남지
  않는다.
- owner 정리는 `RemoveAllByOwnerAsync` 하나이고, store 장애는 어디서나 예외로만 보고된다.
- actor row는 `ActorRef?`와 단일 `LocationKind`를 갖고, publish 전 행이 공개 조회에 노출되지 않는다.
- resolver는 messaging lookup만, 운영 조회는 `RuntimeQuery.cs`의 별도 표면으로 분리되어 있고, 세
  계층의 list 이름이 의미 차이를 드러낸다.
- 샘플 업무 코드가 actor 확보/전송/바인딩/readiness를 `IZLinkActorDirectory`, `IZLinkActorClient`,
  `BindOrGetAsync`, `IZLinkLocationReadiness`로만 표현한다.
- store와 lifecycle 테스트가 claim, publish, renew, takeover, ownership loss, owner 일괄 정리를 모두
  검증한다.
- Redis tests가 typed actor ref cross-language row를 검증한다.
- 별도 public contract 적용 리뷰와 POSD/DDD 리뷰가 모두 `이슈 없음`이다.
