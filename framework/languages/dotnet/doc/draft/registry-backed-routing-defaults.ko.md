<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- Session-Attached Actor Route](./session-attached-actor-route.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Registry](../spec/aspnet-core-registry.ko.md) | [SPOT](../spec/aspnet-core-spot.ko.md) | [ActorGateway Relay](./actor-gateway-session-relay.ko.md)

# Draft -- Registry-Backed Routing Defaults

> 이 초안의 actor route 부분은 **ActorGateway session relay 설계로 대체되었다**.
> 현재 남는 범위는 Spot name/rid lookup 을 위한 Registry-backed Spot remote address 기본값이다.

## 1. 현재 결정

Registry 기본 구현은 Spot owner 조회와 Spot name directory 를 돕는다. session actor relay 는
Registry actor route lookup 을 hot path 로 사용하지 않는다.

- `UseRegistrySpotRemoteAddresses(...)` 는 유지한다.
- `AddSpotRemoteAddressResolver<TResolver>()` 는 custom Spot lookup 이 필요할 때만 사용한다.
- session actor bind 는 stream 의 ActorGateway attach 와 logical actor handle 을 사용한다.
- actor-session binding 은 framework/core runtime state 이며 Registry row 로 저장하지 않는다.
- sample-only route store 또는 metadata store 는 유지하지 않는다.

## 2. Sample 반영 기준

| 샘플 | 기준 |
|------|------|
| Bingo | sample-only metadata store 없이 Registry Spot 기본값과 ActorGateway attach 를 사용한다 |
| TicTacToe session gateway | Bingo 와 같은 구조를 사용한다 |

## 3. 문서 반영 기준

| 문서 | 역할 |
|------|------|
| `../spec/aspnet-core-registry.ko.md` | Registry-backed Spot lookup 계약 |
| `../guide/08-registry.ko.md` | 사용자용 Registry 사용 흐름 |
| `./actor-gateway-session-relay.ko.md` | session actor relay 와 Registry lookup 분리 |

## 4. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Registers_Default_Service` | Registry Spot 기본 resolver 가 등록된다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Require_SpotDiscovery` | Spot discovery 없이 Registry Spot 기본값을 켜면 validation 오류가 난다. |
| `RegistryRemoteAddressesTests.RegistryRouteResolvers_Reject_Custom_Duplicate` | 기본 Spot resolver 와 custom Spot resolver 중복 등록을 거부한다. |
| `RegistryRemoteAddressesTests.RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous` | route channel 이 둘 이상이면 Registry Spot resolver 설정에 명시적 channel 이 필요하다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Name` | 생성된 Spot 을 이름으로 찾는다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Rid` | 생성된 Spot 을 RID 로 찾는다. |
| `RegressionTests.Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | Bingo sample 에 sample-only route/metadata store 가 없다. |
| `RegressionTests.TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store` | TicTacToe sample 에도 sample-only route/metadata store 가 없다. |
