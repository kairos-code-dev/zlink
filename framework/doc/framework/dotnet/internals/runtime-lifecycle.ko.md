<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Backend Policy](backend-dependency-policy.ko.md) |
[공개 lifecycle 계약](../../spec/server/languages/dotnet/interfaces/10-topology-monitoring.ko.md)

# ZLink Framework .NET Runtime Lifecycle

## 1. 목적

이 문서는 `.NET` framework 유지보수자가 host와 내부 runtime의 시작·종료 배선을
파악할 때 필요한 순서만 설명한다. 사용자가 관찰하는 오류, timeout, cancellation,
reconnect 계약은 각 기능 spec이 소유하며 여기서 반복하지 않는다.

## 2. 시작 순서

`ZLinkFrameworkHostedService`는 host 전체의 `ZLinkFrameworkRuntimeState`를 소유한다. Host가 시작될 때 내부
구성은 다음 순서로 준비된다.

1. 등록 정보와 handler 노출 조합을 검증한다.
2. `Preparing` state와 host maintenance barrier를 만든다.
3. location store, owner lease와 자동 연결 loop를 준비한다.
4. Public raw socket adapter로 RouteMesh, ClientServer, fanout과 STREAM listener를 시작한다.
5. monitoring source가 실제 runtime을 참조할 수 있는지 확인하고 event 수집을 붙인다.
6. 모든 required component가 ready이면 `Serving`을 한 번 게시하고 host 시작을 완료한다.

설정 오류는 runtime 객체를 부분적으로 사용하기 전에 검증한다. 시작 도중 실패하면 `Error`를 게시하고 이미
만든 runtime을 역순으로 정리한다. Host maintenance authority는 하나지만 기존 MeshNode scoped drain의
`ZLinkMeshNodeState`는 source compatibility를 위해 유지한다. 이 component 상태는 host state를 결정하거나
다른 component의 종료를 시작하지 않는다. Descriptor와 새 host snapshot은 `ZLinkFrameworkRuntimeState`를
투영한다.

## 3. Retire와 Shutdown

`RetireAsync(...)` preflight와 `ShutdownAsync(...)` seal은 같은 host maintenance barrier에서 순서를 정한다.
Retire는 User Spot 잔존 여부, Actor·Instance policy, target version·capability·capacity·wave와 Store를 모두
확인한 뒤에만 `Draining`을 commit한다. User Spot 하나라도 남으면 state와 admission을 바꾸지 않고
`Blocked/TransferDisabled`로 끝낸다.

`Draining`부터 first intent와 deadline을 shared operation에 고정한다. Cross-intent caller는 같은 operation과
`EffectiveIntent` result를 기다린다. Caller token은 waiter completion만 취소하고 shared operation이나 transfer를
취소하지 않는다. `Blocked`는 preflight waiter에게만 전달하고 terminal cache에 넣지 않는다.

Retire는 admission seal, accepted work, Actor·Instance transfer, session barrier, authority cleanup, listener와 raw
socket cleanup 순서로 진행한다. Shutdown은 admission을 seal하되 새 transfer와 reservation을 시작하지 않는다.
ASP.NET Core `StopAsync`는 `ShutdownAsync()`를 사용하고 terminal result를 확인한 뒤 hosted service를 끝낸다.
두 operation의 기본 deadline은 30초다.

기존 `IZLinkDrainControl.DrainAsync(...)`는 같은 host coordinator의 `ShutdownAsync(...)`에 연결한다.
`IZLinkRouteMeshRuntime.DrainAsync(meshName, ...)`는 지정한 MeshNode만 정리하는 v10 non-continuity operation으로
유지하며 host `Retire` preflight와 transfer를 시작하지 않는다. 두 compatibility surface의 public 이름과
signature는 Core service 구현 이관 때문에 바꾸지 않는다.

`IZLinkSpotManager.CreateAsync`와 `GetOrCreateAsync`는 local owner만 만들고 `ResolveAsync`는 existing-only다.
Missing Instance Spot을 cold activation하는 경로는 `InstanceSpotAddress` call뿐이다. Maintenance target
materialization은 application manager call을 재사용하지 않는 Framework internal operation이다.

## 4. 책임 경계

- `ZLinkFrameworkHostedService`는 ASP.NET Core host lifecycle과 `IZLinkFrameworkRuntime`을 연결한다.
- Host runtime coordinator는 state, maintenance barrier, first intent, deadline과 terminal result를 소유한다.
- channel, Spot, stream runtime은 자신의 listener와 pending 작업을 스스로 정리한다.
- location hosted service는 Store lease, automatic discovery와 authority recovery loop를 소유한다.
- monitoring hosted service는 다른 runtime이 준비된 뒤 source를 붙이고 가장 먼저
  분리한다.

Application callback과 provider callback에 authority version, phase, checkpoint reference와 raw socket을
노출하지 않는다. Typed transfer adapter는 application state만 capture·restore한다.

## 5. Transport liveness와 binding 경계

```csharp
internal static class ZLinkServiceTransportLiveness
{
    internal static readonly TimeSpan IdleProbeInterval = TimeSpan.FromSeconds(5);
    internal static readonly TimeSpan InboundDeadline = TimeSpan.FromSeconds(15);
}
```

이 timing은 public option으로 투영하지 않는다. RouteMesh와 ClientServer connection은 service protocol의
`livenessProbe`·`livenessAck`을 사용한다. Fanout subscriber는 publisher마다 전용 SUB socket과 receive loop를
두고, publisher가 idle이면 [exact two-frame beacon](../../common/internals/service-wire-protocol.ko.md#411-classic-fanout-liveness-frame)을
받는다. 첫 valid application record나 beacon을 받기 전에는 해당 publisher를 ready로 만들지 않는다. Runtime은
`bindings/dotnet`의 public raw socket API만 호출한다. Core service C API, service binding object,
`NativeMethods`, non-public reflection과 native symbol 직접 호출은 사용하지 않는다. Owner lease renew interval은
Store liveness이며 위 service liveness와 별도다.

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:RL-C1` | 여러 framework host를 생성하고 종료한 뒤 follow-up request가 성공해 host lifecycle 정리를 검증한다. |
| `FrameworkRuntimeTests.Retire_Blocks_When_UserSpot_Remains` | User Spot 잔존 preflight가 admission을 바꾸지 않고 `TransferDisabled`로 끝난다. |
| `FrameworkRuntimeTests.CrossIntent_Waits_For_FirstEffectiveIntent` | Draining 이후 cross-intent waiter가 first operation result를 관찰한다. |
| `FrameworkRuntimeTests.WaiterCancellation_DoesNotCancelTermination` | caller cancellation이 shared termination을 중단하지 않는다. |
| `TransportLivenessTests.Profile_Is_Five_And_Fifteen_Seconds` | RouteMesh·ClientServer는 probe·ACK을 사용하고, fanout은 publisher별 전용 SUB socket에서 two-frame beacon을 받으며, 모두 5초 idle과 15초 inbound deadline을 public option 없이 적용한다. |
| `ZLinkAsyncSubmitterTests.DisposeAsync_FailsPendingItems` | runtime dispose가 pending submit을 남겨 두지 않는다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:bottom:end -->
