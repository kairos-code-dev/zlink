# ZLink Framework SpotNode 계약

[.NET 묶음](../README.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [인터페이스](./handler-interfaces.ko.md)

이 문서는 `.NET` framework의 `SpotNode` 설정 중 core route 계약과 직접 맞물리는
부분을 정리한다. Spot handler 작성법과 packet dispatch 규칙은
[aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md)를 기준으로 한다.

## Entry Spot 설정

Entry Spot은 Actor가 생성 직후 머무르는 기본 Spot이다. Actor가 user Spot에서
leave하면 같은 node의 Entry Spot으로 돌아온다. 따라서 Entry Spot의 routing id는
Actor remote location의 `CurrentSpotRid`가 될 수 있다.

framework는 Entry Spot routing id 설정을 `SpotNode` builder에서 제공한다.

```csharp
spotNode.ConfigureEntrySpot(entry =>
{
    entry.RoutingId = RoutingId.From("entry");
});
```

`ConfigureEntrySpot(...)`은 `AddEntrySpot<TEntrySpot>()`과 별개다.
`AddEntrySpot<TEntrySpot>()`은 Entry Spot에서 실행할 handler registry 타입을
등록한다. `ConfigureEntrySpot(...)`은 handler 타입 등록 여부와 관계없이 native
Entry Spot facade의 설정을 적용한다.

## 적용 순서

framework는 Entry Spot routing id를 native SpotNode가 bind되기 전에 적용한다.
core는 SpotNode bind 이후 Entry Spot rid 변경을 잠그기 때문에 이 순서가 필요하다.

1. `Node.EntrySpot()`으로 native Entry Spot facade를 얻는다.
2. `ConfigureEntrySpot(...)`에서 `RoutingId`가 설정되어 있으면
   `entrySpot.SetRoutingId(...)`를 호출한다.
3. SpotNode를 bind한다.
4. discovery, route channel, publisher 같은 node 역할을 붙인다.
5. Entry Spot activation을 만든다.
6. Entry Spot dispatch pump를 붙인다.
7. 이후 Actor 생성과 Actor remote address publish는 설정된 Entry Spot rid를 사용한다.

이 순서는 Actor가 생성되기 전에 Entry Spot rid가 정해지도록 하기 위한 것이다.
Actor remote address sync가 켜져 있으면 Entry Spot에 있는 Actor의 `CurrentSpotRid`는 설정된
Entry Spot rid와 같아야 한다.

## Route 의미

framework가 core discovery route를 노출할 때 Entry Spot과 user Spot을 구분한다.

- Actor remote location의 `CurrentSpotKind`가 `Entry`이면 `CurrentSpotRid`는 Entry
  Spot rid다.
- Actor remote location의 `CurrentSpotKind`가 `User`이면 `CurrentSpotRid`는 user
  Spot rid다.
- Spot remote address resolver의 `ZLinkSpotRemoteAddress.SpotKind`도 core `ResolveSpot()` 결과를
  보존한다.

Spot RID route는 framework가 관리하는 이름 색인이다. 이 색인은 Spot rid를 찾는
용도로만 사용한다. owner node rid와 Spot kind는 core `ResolveSpot(spotRid)` 결과를
source of truth로 사용한다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `EntryRoutingTests.EntrySpotRoutingId_IsApplied_ToNativeEntrySpot` | `ConfigureEntrySpot(...)`에서 지정한 routing id가 native Entry Spot facade에 적용되고 Entry Spot activation의 `SpotRid`로 노출된다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Rid_And_Removes_Route` | Spot RID route는 Spot rid만 찾는 색인으로 쓰고, resolver가 core `ResolveSpot()` 결과의 owner node rid와 `SpotKind.User`를 보존한다. |
