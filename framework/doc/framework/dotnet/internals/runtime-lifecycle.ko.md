<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Backend Policy](backend-dependency-policy.ko.md) |
[공개 lifecycle 계약](../../common/spec/server/languages/dotnet/interfaces/10-topology-monitoring.ko.md)

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

## 3. Relocate와 Shutdown

`RelocateAsync(...)` preflight와 `ShutdownAsync(...)` seal은 같은 host maintenance barrier에서 순서를 정한다.
Relocate는 Actor·Spot policy, target version·capability·capacity·wave와 Store를 모두 확인한 뒤에만
`Relocating`을 commit한다. `DisableRelocation` policy인 Actor·Spot이 남아 있거나 필요한 target·Store가 없으면 state와
admission을 바꾸지 않고 해당 `Blocked` reason으로 끝낸다.

`Relocating`부터 mode와 deadline을 shared operation에 고정한다. 같은 작업을 호출한 caller는 동일한 결과를
기다린다. Caller token은 해당 caller의 대기만 취소하고 shared operation이나 relocation을 취소하지 않는다.
`Blocked` 결과는 preflight를 다시 실행할 수 있게 terminal cache에 넣지 않는다.

Relocate는 admission seal 뒤 현재 turn만 완료하고, 다음 queued job·accepted journal·timer를 freeze한다.
Framework는 byte permit을 확보한 aggregate부터 Actor·Spot relocation을 시작하며 User Spot과 소속 Actor는
하나의 aggregate로 이동한다. 모든 authority commit과 source cleanup이 끝나면 host는 `Relocated`에서
infrastructure를 유지한다. Shutdown은 admission을 seal하되 새 relocation과 reservation을 시작하지 않고,
listener와 raw socket을 포함한 infrastructure를 정리한다.
ASP.NET Core `StopAsync`는 `ShutdownAsync()`를 사용하고 terminal result를 확인한 뒤 hosted service를 끝낸다.
두 operation의 기본 deadline은 30초다.

`IZLinkRouteMeshRuntime.DrainAsync(meshName, ...)`는 지정한 MeshNode만 정리하는 v10 non-continuity operation으로
유지하며 host relocation preflight를 시작하지 않는다.

Application은 global SpotId를 대상으로 direct Spot send/request를 구성하고 fluent builder에서 Instance marker를
지정한다. Missing Instance는 marker가 정확히 한 factory type을 선택할 때만 cold placement를 시작한다.
Maintenance target materialization은 이 application call을 재사용하지 않는 Framework internal operation이다.

## 4. 책임 경계

- `ZLinkFrameworkHostedService`는 ASP.NET Core host lifecycle과 `IZLinkFrameworkRuntime`을 연결한다.
- Host runtime coordinator는 state, maintenance barrier, first intent, deadline과 terminal result를 소유한다.
- channel, Spot, stream runtime은 자신의 listener와 pending 작업을 스스로 정리한다.
- location hosted service는 Store lease, automatic discovery와 authority recovery loop를 소유한다.
- monitoring hosted service는 다른 runtime이 준비된 뒤 source를 붙이고 가장 먼저
  분리한다.

Application callback과 provider callback에 authority version, phase, relocation reference와 raw socket을
노출하지 않는다. Relocation adapter는 application state를 opaque bytes로만 capture·restore한다.

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
두고, publisher가 idle이면 [exact two-frame beacon](../../common/internals/service-wire-protocol.ko.md#5-service-liveness)을
받는다. 첫 valid application record나 beacon을 받기 전에는 해당 publisher를 ready로 만들지 않는다. Runtime은
`bindings/dotnet`의 public raw socket API만 호출한다. Core service C API, service binding object,
`NativeMethods`, non-public reflection과 native symbol 직접 호출은 사용하지 않는다. Owner lease renew interval은
Store liveness이며 위 service liveness와 별도다.

## 6. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `E2E:RL-C1` | 여러 framework host를 생성하고 종료한 뒤 follow-up request가 성공해 host lifecycle 정리를 검증한다. |
| `FrameworkRuntimeContracts.Relocation_and_shutdown_are_separate_host_operations` | `RelocateAsync(...)`가 workload만 이전하고 `ShutdownAsync(...)`가 host 종료만 수행하는 public 경계를 고정한다. |
| `DrainCoordinatorTests.Relocate_Detaches_Workload_Without_Shutting_Down_Infrastructure` | relocation 완료 뒤 infrastructure가 유지되고 별도 shutdown을 기다린다. |
| `DrainCoordinatorTests.Waiter_Cancellation_Does_Not_Cancel_Shared_Drain` | caller cancellation이 shared maintenance operation을 중단하지 않는다. |
| `ServiceRuntimeFoundationTests.Liveness_RetransmitsOutstandingProbeAndExtendsOnlyOnExactAck` | outstanding probe를 재전송하고 exact ACK에서만 inbound deadline을 연장한다. |
| `ZLinkAsyncSubmitterTests.DisposeAsync_FailsPendingItems` | runtime dispose가 pending submit을 남겨 두지 않는다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [다음: Runtime Execution](runtime-execution.ko.md)
<!-- framework-adapter-nav:bottom:end -->
