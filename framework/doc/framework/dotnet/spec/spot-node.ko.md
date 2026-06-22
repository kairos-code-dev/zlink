# ZLink Framework SpotNode 계약

[.NET 묶음](../README.ko.md) | [SPOT](aspnet-core-spot.ko.md) | [인터페이스](handler-interfaces.ko.md)

이 문서는 `.NET` framework의 `SpotNode` 설정 중 core route 계약과 직접 맞물리는
부분을 정리한다. Spot handler 작성법과 packet dispatch 규칙은
[aspnet-core-spot.ko.md](aspnet-core-spot.ko.md)를 기준으로 한다.

## Entry Spot 설정

Entry Spot은 Actor가 생성 직후 머무르는 기본 Spot이다. Actor가 user Spot에서
leave하면 같은 node의 Entry Spot으로 돌아온다. 따라서 Entry Spot의 routing id는
Actor remote location의 `CurrentSpotRid`가 될 수 있다.

framework는 Entry Spot routing id 설정을 `SpotNode` builder에서 제공한다.

```csharp
{
    var entry = spotNode.ConfigureEntrySpot();
    entry.RoutingId = RoutingId.From("entry");

}
```

`ConfigureEntrySpot(...)`은 `AddEntrySpot<TEntrySpot>()`과 별개다.
`AddEntrySpot<TEntrySpot>()`은 Entry Spot 구현 타입(`TEntrySpot : IZLinkEntrySpot`)을
등록한다. actor packet handler 는 그 Entry Spot 의 `Configure()` 에서 등록한다.
`ConfigureEntrySpot(...)`은 Entry Spot 타입 등록 여부와 관계없이 native
Entry Spot facade의 설정을 적용한다.

## 적용 순서

framework는 Entry Spot routing id를 native SpotNode가 bind되기 전에 적용한다.
core는 SpotNode bind 이후 Entry Spot rid 변경을 잠그기 때문에 이 순서가 필요하다.

1. `ApplyEntrySpotRoutingIdBeforeBind()`로 `ConfigureEntrySpot(...)`에 설정된
   `RoutingId`를 native Entry Spot facade(`entrySpot.SetRoutingId(...)`)에 적용한다.
2. router/pub bind endpoint를 설정한다(`SetRouterBind`/`SetPubBind`).
3. discovery, manual peer, accepted spot route channel, publisher 같은 node 역할을 붙인다.
4. `InitializeEntrySpotAsync()`로 Entry Spot 을 초기화한다(activation·dispatch pump 포함).
5. 이후 생성되는 Actor 는 설정된 Entry Spot rid 를 사용한다.

이 순서는 Actor가 생성되기 전에 Entry Spot rid가 정해지도록 하기 위한 것이다.

> **설계상 모델 — 미구현.** Actor remote address publish/sync 와
> Actor remote location(`CurrentSpotKind`/`CurrentSpotRid`)은 현재 `.NET` framework
> 표면에 없다. 구현된 remote address 표면은 아래 `ZLinkSpotRemoteAddress` 다.

## Route 의미

framework가 core discovery route를 노출할 때 Entry Spot과 user Spot을 구분한다.
구현된 표면은 `ZLinkSpotRemoteAddress`(`TargetNodeRid`, `SpotRid`, `SpotKind`)다.

- `ZLinkSpotRemoteAddress.SpotKind`가 `Entry`이면 `SpotRid`는 Entry Spot rid다.
- `ZLinkSpotRemoteAddress.SpotKind`가 `User`이면 `SpotRid`는 user Spot rid다.
- Spot remote address resolver의 `ZLinkSpotRemoteAddress.SpotKind`는 core `ResolveSpot()` 결과를
  보존한다.

Spot RID route는 framework가 관리하는 이름 색인이다. 이 색인은 Spot rid를 찾는
용도로만 사용한다. owner node rid와 Spot kind는 core `ResolveSpot(spotRid)` 결과를
source of truth로 사용한다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `EntryRoutingTests.EntrySpotRoutingId_IsApplied_ToNativeEntrySpot` | `ConfigureEntrySpot(...)`에서 지정한 routing id가 native Entry Spot facade에 적용되고 Entry Spot activation의 `SpotRid`로 노출된다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Registers_Default_Service` | Spot RID route resolver를 registry 기반 resolver로 등록한다. |
