# Framework route resolver 기본 구현 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 .NET Framework 계층에서 Actor와 SPOT 주소를 찾는 기본 구현과
교체 가능한 인터페이스를 정리하기 위한 설계안이다.
정식 spec 문서와 구현에 반영되기 전까지 응용은 이 동작에 의존하면 안 된다.

## 목적

Framework의 Actor 메시징과 SPOT 메시징은 최종적으로 어느 node로 메시지를 보낼지
알아야 한다. 지금 샘플은 이 주소 매핑을 직접 구현하고 있기 때문에, 사용자가
샘플을 볼 때 Registry와 Discovery를 일반 metadata 저장소처럼 써도 되는 것으로
오해할 수 있다.

이 초안의 목적은 Registry와 Discovery를 Framework 내부 주소 매핑 수단으로
정리하고, 사용자가 필요할 때만 resolver 인터페이스를 교체할 수 있게 만드는 것이다.
사용자는 raw metadata를 직접 다루기보다 Actor id 또는 SPOT routing id를 넘기고,
Framework는 그 값을 실제 node routing id로 바꾼다.

## 설계 원칙

1. Actor와 SPOT의 주소 조회는 명시적인 resolver 인터페이스로 표현한다.
2. Framework는 Registry와 Discovery를 이용한 기본 구현을 제공한다.
3. 사용자는 resolver 인터페이스를 직접 구현해서 기본 구현을 교체할 수 있다.
4. Framework 공개 API는 Registry와 Discovery의 raw metadata 저장 기능을 노출하지 않는다.
5. Registry와 Discovery는 Framework에서 주소 매핑 용도로만 사용한다.
6. SPOT 주소 조회는 별도 metadata 저장소를 만들지 않고 Discovery의 SPOT owner 조회를 사용한다.

## 용어

- **Actor route**: Actor id를 실제 play node의 routing id로 바꾼 값이다.
- **SPOT route**: SPOT routing id를 그 SPOT을 현재 소유한 node의 routing id로 바꾼 값이다.
- **owner node rid**: SPOT을 현재 소유한 SPOT node의 routing id이다.

## 인터페이스 목록

이 초안에서 사용하는 인터페이스는 아래로 고정한다. 구현 단계에서 namespace나 클래스
이름은 조정할 수 있지만, 역할을 더 늘리거나 raw metadata API를 공개 인터페이스로
올리지는 않는다.

| 인터페이스 | 공개 여부 | 상태 | 역할 |
|------------|-----------|------|------|
| `IZLinkActorRouteResolver` | 공개 | 기존 유지 | Actor id로 play node routing id 조회 |
| `IZLinkActorRouteWriter` | 공개 | 신규 | Actor route 기록과 삭제 |
| `IZLinkSpotRouteResolver` | 공개 | 신규 | SPOT routing id로 owner node routing id 조회 |
| `IZLinkSpotRouteWriter` | 공개 | 신규 | SPOT ownership 기록과 삭제 (커스텀 구현용) |
| `IZLinkBackendDiscovery.ResolveSpot()` | 내부 | 신규 노출 | Framework 내부에서 Discovery SPOT owner 조회 |

사용자가 교체할 수 있는 지점은 공개 resolver와 writer 인터페이스뿐이다.
`IZLinkBackendDiscovery`는 Framework 내부 어댑터 계약이므로 응용이 직접 구현하거나
등록하는 대상이 아니다.

## 공개 인터페이스

Framework는 아래 인터페이스를 공개 계약으로 둔다. 인터페이스는 호출자가 어떤 저장소를
쓰는지 알 필요 없도록 주소 조회 의미만 드러낸다.

### Actor route resolver

```csharp
namespace Zlink.Framework.Streams;

public interface IZLinkActorRouteResolver
{
    ValueTask<RoutingId> ResolveRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}
```

`ResolveRouteAsync()`는 Actor id를 받아 해당 Actor의 play node routing id를 반환한다.
Actor가 존재하지 않거나 주소를 찾을 수 없으면 Framework 예외로 실패한다.

### Actor route writer

```csharp
namespace Zlink.Framework.Streams;

public interface IZLinkActorRouteWriter
{
    ValueTask BindRouteAsync(
        ZLinkActorRouteBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindRouteAsync(
        ZLinkActorRouteUnbind binding,
        CancellationToken cancellationToken);
}
```

`IZLinkActorRouteWriter`는 Actor가 play node에 배치될 때 주소를 기록하기 위한
인터페이스다. 기본 Registry 구현은 이 인터페이스로만 주소 정보를 저장한다.
응용은 Registry metadata key나 저장 형식을 알 필요가 없다.

```csharp
namespace Zlink.Framework.Streams;

public readonly record struct ZLinkActorRouteBinding(
    string ActorId,
    RoutingId NodeRid);

public readonly record struct ZLinkActorRouteUnbind(
    string ActorId,
    RoutingId NodeRid);
```

`UnbindRouteAsync()`는 같은 Actor id가 다른 node로 이미 갱신된 경우 새 값을 지우면
안 된다. 기본 구현은 저장된 routing id가 `NodeRid`와 같은 경우에만 삭제한다.

### SPOT route resolver

```csharp
namespace Zlink.Framework.Spots;

public interface IZLinkSpotRouteResolver
{
    ValueTask<RoutingId> ResolveRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}
```

`ResolveRouteAsync()`는 SPOT routing id를 받아 현재 owner node routing id를 반환한다.
기본 구현은 Discovery의 SPOT owner 조회를 사용한다. 별도 Registry metadata key에
`spotRid -> nodeRid` 값을 저장하지 않는다.

### SPOT route writer

```csharp
namespace Zlink.Framework.Spots;

public interface IZLinkSpotRouteWriter
{
    ValueTask BindRouteAsync(
        ZLinkSpotRouteBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindRouteAsync(
        ZLinkSpotRouteUnbind binding,
        CancellationToken cancellationToken);
}
```

`IZLinkSpotRouteWriter`는 커스텀 `IZLinkSpotRouteResolver`와 함께 사용할 때 필요하다.
Framework은 SPOT 생성 시점에 `BindRouteAsync()`를, SPOT 삭제 시점에 `UnbindRouteAsync()`를
호출한다. 커스텀 구현자는 "언제 쓸지"를 직접 추적하지 않아도 된다.

기본 Discovery 기반 구현은 Writer를 사용하지 않는다.
Discovery가 SPOT ownership을 mesh 프로토콜로 직접 추적하기 때문이다.

```csharp
namespace Zlink.Framework.Spots;

public readonly record struct ZLinkSpotRouteBinding(
    RoutingId SpotRid,
    RoutingId OwnerNodeRid);

public readonly record struct ZLinkSpotRouteUnbind(
    RoutingId SpotRid);
```

## 기본 구현

Framework는 아래 기본 구현을 제공한다.

| 구현 | 구현 인터페이스 | 주소 조회 방식 |
|------|----------------|----------------|
| `ZLinkRegistryActorRouteStore` | `IZLinkActorRouteResolver`, `IZLinkActorRouteWriter` | Registry 기반 Actor id 매핑 |
| `ZLinkDiscoverySpotRouteResolver` | `IZLinkSpotRouteResolver` | Discovery의 SPOT owner 조회 |

기본 구현 이름은 확정 전이며, 구현 단계에서 namespace와 등록 API 이름은 Framework의
기존 명명 규칙에 맞춰 조정할 수 있다. 그러나 각 구현이 맡는 인터페이스와 책임은
이 표를 기준으로 한다.

## SPOT route 기본 구현 동작

SPOT route 기본 구현은 다음 순서로 동작한다.

1. Framework가 `ZLINK_AUTO_CONNECT_SPOT_MESH` Discovery handle을 준비한다.
2. resolver가 `Discovery.ResolveSpot(spotRid)`를 호출한다.
3. Discovery는 현재 service view나 Registry refresh를 통해 owner node rid를 찾는다.
4. resolver는 owner node rid를 그대로 반환한다.

core 공개 API의 기준 함수는 아래와 같다.

```c
zlink_config_result_t zlink_discovery_resolve_spot(
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

.NET binding에는 같은 의미의 메서드가 있다.

```csharp
public RoutingId ResolveSpot(RoutingId spotRid);
```

따라서 Framework의 SPOT route resolver는 raw Registry metadata를 직접 읽지 않는다.
SPOT owner는 SPOT mesh Discovery가 이미 알고 있는 topology 정보에서 조회한다.

## Framework 등록 API

resolver와 writer는 `Register...<T>()` 메서드로 등록한다. 기본 구현과 커스텀 구현
모두 같은 패턴을 사용하며, 등록하지 않으면 해당 기능은 비활성 상태다.

```csharp
namespace Zlink.Framework.Configuration;

public interface IZLinkFrameworkOptions
{
    void RegisterActorRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorRouteResolver;

    void RegisterActorRouteWriter<TWriter>()
        where TWriter : class, IZLinkActorRouteWriter;

    void RegisterSpotRouteResolver<TResolver>()
        where TResolver : class, IZLinkSpotRouteResolver;

    void RegisterSpotRouteWriter<TWriter>()
        where TWriter : class, IZLinkSpotRouteWriter;
}
```

기본 구현을 사용하는 경우:

```csharp
options.RegisterActorRouteResolver<ZLinkRegistryActorRouteStore>();
options.RegisterActorRouteWriter<ZLinkRegistryActorRouteStore>();
options.RegisterSpotRouteResolver<ZLinkDiscoverySpotRouteResolver>();
```

같은 역할에 두 번 등록하면 구성 오류로 실패한다.

`RegisterSpotRouteWriter<T>()`는 `RegisterSpotRouteResolver<T>()`와 독립적으로
등록한다. Discovery 기반 resolver를 쓰면서 Writer만 따로 등록하는 것도 가능하다.

## 내부 backend 계약

Framework 내부 backend wrapper는 SPOT owner 조회를 노출해야 한다. 이 인터페이스는
Framework 내부 구현 계약이며 응용에 공개하지 않는다.

```csharp
namespace Zlink.Framework.Backend.Contracts;

internal interface IZLinkBackendDiscovery : IZLinkBackendObject, IAsyncDisposable
{
    void ConnectRegistry(string endpoint);

    RoutingId ResolveSpot(RoutingId spotRid);

    IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers();
}
```

`ResolveSpot()`은 .NET binding의 `Discovery.ResolveSpot()`을 호출한다.
`MemberPeers()`는 SPOT peer reconciliation 같은 Framework 내부 작업에만 사용한다.

## metadata API 노출 범위

Framework 공개 API에서는 아래 기능을 제공하지 않는다.

- Discovery raw metadata 설정
- Discovery raw metadata 조회
- Discovery member peer metadata 조회
- Registry member peer metadata 조회
- 임의 key/value Registry metadata 저장소

이 기능들은 core 또는 low-level binding에 남아 있을 수 있지만, Framework에서는
주소 매핑 구현을 위한 내부 수단으로만 사용한다. 응용이 주소 매핑을 바꾸고 싶으면
metadata API를 직접 쓰는 대신 resolver와 writer 인터페이스를 구현한다.

## 실패 조건

구현은 아래 조건에서 명확한 Framework 구성 오류나 런타임 오류를 반환해야 한다.

| 조건 | 실패 시점 |
|------|----------|
| 같은 역할에 두 번 등록 | 구성 검증 |
| SPOT resolver 등록 시 SPOT mesh Discovery가 없음 | 구성 검증 |
| Actor route를 찾을 수 없음 | 런타임 |
| `Discovery.ResolveSpot(spotRid)`가 owner node를 찾지 못함 | 런타임 |
| unbind 시 저장된 routing id와 요청 routing id가 다름 | 삭제하지 않고 성공 |

routing id 불일치는 오류가 아니다. 이미 새 route가 기록된 상태일 수 있으므로
오래된 unbind 요청은 저장 값을 지우지 않는다.

## 샘플 반영 방향

`TicTacToe(session-gateway)` 샘플은 Framework 기본 구현을 보여주는 형태로 단순화한다.

- 샘플의 Actor route 저장소는 `ZLinkRegistryActorRouteStore` 등록으로 대체한다.
- 샘플의 SPOT route 저장소는 제거하고 `ZLinkDiscoverySpotRouteResolver` 등록으로 대체한다.
- 샘플 코드는 raw Registry metadata key를 직접 만들지 않는다.
- 샘플은 사용자 구현 예제가 필요할 때만 별도 resolver 구현을 둔다.

이렇게 하면 샘플의 핵심 내용은 "session gateway가 Actor와 SPOT route resolver를
사용해 메시지를 올바른 node로 넘긴다"는 흐름으로 정리된다. Registry와 Discovery의
저장 형식은 Framework 내부 구현으로 내려간다.

## 구현 후 정식 문서 반영 위치

구현이 끝나면 이 draft 내용은 아래 문서로 나누어 반영한다.

- Framework .NET spec: resolver와 writer 공개 인터페이스 계약
- Discovery spec: `ResolveSpot`의 SPOT owner 조회 계약
- Registry spec: Framework 기본 구현이 사용하는 주소 매핑 범위
- guide 문서: 기본 구현 선택 방법과 사용자 resolver 교체 예제

정식 문서에 반영할 때는 구현된 public API와 테스트를 기준으로 이름과 실패 조건을
다시 맞춘다.
