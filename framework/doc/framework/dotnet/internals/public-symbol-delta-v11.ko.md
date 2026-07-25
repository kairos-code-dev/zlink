# .NET v11 public symbol delta

[Exact interface](../../common/spec/server/languages/dotnet/interfaces/README.ko.md) ·
[Runtime lifecycle](runtime-lifecycle.ko.md)

## 1. 분류 기준

Core service 구현을 Framework 내부로 옮기는 작업만으로 application public API를 바꾸지 않는다. 기존
Channel, Spot, Actor, STREAM handler·call·builder는 같은 이름과 member를
유지한다. `interfaces/` 분할은 문서 위치만 바꾸며 symbol delta가 아니다.

## 2. 내부 이관으로 인한 public delta

| 분류 | 결과 |
|---|---|
| Channel·Spot·Actor·STREAM handler와 call rename | 0건 |
| 기존 builder와 manager 제거 | 0건 |
| Core service C API를 대신하기 위한 public adapter 추가 | 0건 |
| Private member·reflection·native symbol 우회 option 추가 | 0건 |

Framework runtime은 bindings의 public raw socket API만 사용한다. Binding service SPI는 Framework public
contract로 투영하지 않는다.

이 판정의 source baseline은 현재 tree의
`Contracts/Configuration/IZLinkRouteMeshRuntime.cs`, `ZLinkDrainContracts.cs`와 각 handler·builder contract다.
Baseline의 `ZLinkMeshNodeState` 7개 값, `ZLinkMeshDrainSnapshot`, `ZLinkMeshDrainResult`, MeshName별
`DrainAsync(...)`·`AwaitDrainedAsync(...)`, host-wide `IZLinkDrainControl`은 모두 v11 exact interface에 남긴다.
Internal transport를 Core service object에서 public raw socket으로 바꾸는 작업은 이 이름이나 signature를
바꾸는 근거가 아니다.

## 3. 신규 maintenance 계약의 최소 delta

| 종류 | Public symbol | 근거 |
|---|---|---|
| 추가 | `IZLinkFrameworkRuntime`, `ZLinkFrameworkRuntimeState`, termination intent·outcome·reason·result·snapshot·event | host 단위 Retire·Shutdown과 first-intent-wins를 표현함 |
| 유지 | `ZLinkMeshNodeState`, Mesh drain snapshot·result와 `IZLinkRouteMeshRuntime.DrainAsync(...)`·`AwaitDrainedAsync(...)` | 기존 MeshName scoped non-continuity operation이며 host `Retire`와 의미가 다름 |
| 유지 | `IZLinkDrainControl`, `ZLinkDrainResult`, `Drained`, `ForceStopped` | 기존 host-wide drain을 `Shutdown` compatibility facade로 연결함 |
| 추가 | `SetApplicationVersion(long)`, `SetMaintenanceWave(...)`, descriptor version·capability·capacity·wave·state | relocation target compatibility와 reservation을 판정함 |
| 추가 | `IZLinkActorRelocationAdapter<TActor>`, `IZLinkSpotRelocationAdapter<TSpot>`, `ZLinkRelocationPolicy<TInstance>`와 typed factory overload | factory type과 `Disabled/Recreate/Snapshot`을 한 등록에 고정하고 Snapshot application state를 opaque bytes로 capture·restore함 |
| 유지 | `IZLinkActorFactory`, 기존 Actor·Instance factory overload | 기존 등록은 `Disabled` policy로 해석해 source compatibility를 보존함 |
| 추가 | `IZLinkLocationStore`와 `AddLocationStore(...)` | owner·phase·participant set·generation·relocation reference를 하나의 authority transaction으로 commit함 |
| 추가 | `IZLinkRelocationStore`와 `AddRelocationStore(...)` | application state·accepted journal·timer·queue payload를 immutable relocation root로 저장함 |
| 추가 | `ZLinkRedisLocationStore`, `ZLinkRedisRelocationStore` | 두 Store를 같은 Redis deployment 또는 별도 deployment에 구성할 수 있게 분리함 |
| 유지 | `HeartbeatInterval` | owner lease 갱신 주기를 정하며 transport liveness와 구분함 |

`ZLinkFrameworkRuntimeState`는 기존 `ZLinkMeshNodeState`를 rename하거나 숫자를 덮어쓴 타입이 아니다. 전자는
host maintenance의 닫힌 0..4 값이고 후자는 기존 scoped Mesh drain의 0..6 값을 그대로 유지한다. 기존
`IZLinkDrainControl`은 `Shutdown` 결과를 v10 result로 투영하지만 continuity preflight가 필요한 application은
새 `RetireAsync(...)`를 명시적으로 호출한다.

## 4. No-loss 대조

현재 package baseline은 `Zlink.Framework.api.txt`, `Zlink.Framework.Contracts.api.txt`,
`Zlink.Framework.AspNetCore.api.txt`, `Zlink.Framework.Locations.Redis.api.txt`다. 네 snapshot에서 추출한 public
type 단순 이름은 275개이며 exact category 문서에서 source-only 이름은 0개다. 문서에는 maintenance와 다른
확정 topology 계약을 포함해 355개의 unique public type 이름이 있다. Delegate 선언처럼 반환 type이 이름 앞에
오는 `ZLinkHandlerFilterNext`도 별도로 확인했다.

Type 이름 비교 뒤에는 Actor manager와 join default body, STREAM session bind와 relay, dispatch event constructor,
location descriptor·result factory, routing allocation constructor, codec registrar와 encoded payload operator를
member 단위로 snapshot과 대조했다. Nullable annotation, generic constraint, enum 숫자와 default value도 각
category의 declaration에 포함한다.
