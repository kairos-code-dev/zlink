# Runtime unit gate와 POSD·DDD 점검

## 판정

DN-IMP-001~018의 production source와 unit/regression 경로는 현재 working tree에서
구현되어 있으며, 다음 fresh test 분모를 통과했다.

```text
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --filter 'FullyQualifiedName~Runtime' --nologo
Passed 744, Failed 0, Skipped 0, Total 744

dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --filter 'FullyQualifiedName~Runtime.Dispatch.InboundDispatchBudgetTests|FullyQualifiedName~StatefulServiceRuntimeTests|FullyQualifiedName~StreamSessionForcedCleanupTests|FullyQualifiedName~StreamFlowEndToEndTests|FullyQualifiedName~ClientServerChannelRuntimeTests|FullyQualifiedName~DrainCoordinatorTests|FullyQualifiedName~ActorHandoffTests|FullyQualifiedName~ActorRelocationProtocolTests|FullyQualifiedName~DeferredActorJoinDurabilityTests|FullyQualifiedName~StandaloneActorRelocationPrecommitTests|FullyQualifiedName~RelocationRuntimeTests|FullyQualifiedName~RuntimeMetricsTests|FullyQualifiedName~EntrySpotActorDispatchTests|FullyQualifiedName~SpotPeerConnectorTests|FullyQualifiedName~StandaloneActorRelocationRuntimeTests' --nologo
Passed 541, Failed 0, Skipped 0, Total 541

dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --filter 'FullyQualifiedName~InboundDispatchOptionsTests|FullyQualifiedName~FanoutAutomaticDiscoveryTests|FullyQualifiedName~RouteMeshRuntimeServiceTests|FullyQualifiedName~BackendAdapterFactoryTests|FullyQualifiedName~TopologyExactSurface' --nologo
Passed 54, Failed 0, Skipped 0, Total 54
```

The 541-test command is a focused architecture/regression subset and overlaps the 744-test
runtime filter. The 54-test command covers configuration validation, automatic fanout,
RouteMesh service wiring, backend adapter behavior and topology exact surface. These results
close the runtime implementation/unit gate only. Cross-process process E2E, package-only
process behavior and independent audit remain open.

## POSD·DDD 점검

### PDD-DOTNET-001 — host payload accounting owner

- 원칙: POSD information hiding·deep module, DDD aggregate invariant owner.
- 근거: `ZLinkInboundDispatchBudget`이 host 전체의 `PendingPayloadBytes`, queued/active
  분리와 receive pause를 단일 invariant로 소유한다. RouteMesh, Spot, Actor와 STREAM
  adapter는 payload를 직접 합산하지 않고 lease를 획득·전달한다.
- 대안 1: ingress마다 별도의 byte counter를 두고 `ZLinkFrameworkRuntime`이 주기적으로
  합산한다. 구현은 국소적이지만 ownership transfer와 duplicate accounting을 중앙에서
  복구해야 하며, pause 시점과 status 시점이 어긋난다.
- 대안 2: 각 ingress가 host status 값을 직접 갱신한다. 호출부가 lifecycle과 terminal
  release를 알아야 하므로 정보가 새고, relocation queue와 active handler의 경계가
  중복된다.
- 처리: host state가 budget aggregate와 lease lifecycle을 소유하는 현재 구조를 유지한다.
  adapter는 transport/domain 상태를 결정하지 않고 admission 결과만 전달한다. `InboundDispatch`
  unit과 mixed ingress 관련 targeted test가 이 invariant를 고정한다.
- 재검토: 현재 단계에서 blocking finding 없음. mixed-topology process 확인은 별도
  test/evidence gate로 남긴다.

### PDD-DOTNET-002 — admission-pending peer cleanup 책임 경계

- 원칙: POSD pass-through layer와 information leakage 제거, DDD lifecycle ownership.
- 근거: discovery host는 descriptor 변화와 auto-connect 의도만 결정하고, peer state와
  physical transport 제거의 원자성은 `ZLinkManagedMeshNode`가 소유한다. `ZLinkSpotPeerConnector`
  는 backend adapter로서 target identity를 전달할 뿐 admitted/draining 상태를 판단하지
  않는다.
- 대안 1: `ZLinkLocationAutoConnectHost`가 peer state를 읽고 직접 disconnect한다. discovery
  module에 mesh state 지식이 새고, admitted transport를 잘못 닫을 위험이 있다.
- 대안 2: public peer-state API를 노출해 caller가 admitted 여부를 확인한 뒤 두 단계로
  제거한다. 호출 순서와 race를 public surface로 밀어내며 check/remove 사이의 TOCTOU를
  남긴다.
- 처리: `RemovePeerConnectionIfNotAdmitted`가 mesh node lock 안에서 상태 확인과 제거를
  수행하도록 유지한다. backend contract의 기본값은 `false`로 fail-closed하고, stale
  non-initiating intent만 connector 경계를 통해 요청한다.
- 재검토: `Auto_NonInitiator_Delegates_Admission_Pending_Cleanup`과
  `AutoConnect_Cleans_Connecting_Peer_And_Preserves_Admitted_Peer`가 통과했다. ST-B2
  process evidence는 별도 기록으로 보존하며, 이 점검은 process 전체 완료를 의미하지 않는다.

## 다음 단계

사용자 지정 순서에 따라 runtime source/unit gate 뒤 sample source를 common spec과
대조한다. sample에는 runtime 내부 상태를 새 public API로 노출하지 않고, 이미 공개된
`IZLinkFrameworkRuntime.Status`를 role server가 읽어 client-visible evidence로 투영한다.
sample fixture에만 필요한 control은 공통 sample role의 application endpoint로 두며,
message codec·wire contract를 복제하는 별도 DTO/adapter는 추가하지 않는다.
