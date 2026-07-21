[계약][major] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:151 — 카탈로그가 exact interface에 없는 다수의 public 타입을 목표 계약으로 열거한다 — `IZLinkCodecRegistrar`, `IZLinkActorDirectory`, `IZLinkRuntimeEventPublisher`, `IZLinkLocationRuntimeQuery` 등 16개 이름이 .NET exact interface에 없다 — 카탈로그를 exact spec·inventory 기준으로 전수 정리하고 모든 `IZLink*` 기호의 inventory 포함 여부를 verifier에서 검사한다.

[계약][major] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:521 — Actor 예제가 존재하지 않는 `GetOrCreateAsync`·`FindAsync` 표면을 사용한다 — exact interface의 `IZLinkActorManager`는 `CreateAsync`, `ResolveAsync`, `DestroyAsync`만 정의한다 — 예제와 설명을 exact signature로 교체한다.

[계약][major] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:629 — Session actor·stream 설명이 exact interface에 없는 `BindOrGetAsync`, 중복 `BindAsync`, `IZLinkStream`을 계약으로 제시한다 — exact `IZLinkSessionActors`는 `Bound`, `BindAsync(ActorRef, ...)`, `Find`만 정의한다 — 표와 예제를 exact interface에 맞춘다.

[계약][minor] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:32 — 비동기 명명 규칙이 `Submit`을 callback request에만 남긴다고 단정한다 — exact `IZLinkWorkerCall<TResult>.Submit(CancellationToken)`은 callback이 아닌 void 종결자다 — 예외를 실제 exact surface 기준으로 설명한다.

[원칙][major] framework/doc/framework/dotnet/guide/13-interface-catalog.ko.md:791 — 현재 source의 `ContractSurfaceCoverage`를 목표 계약 전체의 완료 증거처럼 기술한다 — 문서 서두는 source/package와 목표 계약에 구현 차이가 있음을 명시하고, 카탈로그 자체도 exact interface와 불일치한다 — 목표 검증 기준과 현재 구현 증거를 분리하고 미구현 차이는 gap 문서가 소유하도록 수정한다.

[계약][major] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:378 — Node exact interface가 폐기 대상 전역 drain 표면과 MeshName별 runtime 표면을 동시에 정의한다 — 공통 fixed drain 계약은 MeshName별 `ZLinkRouteMeshRuntime` 하나만 요구한다 — 전역 `ZLinkDrainControl`, event, result, state 정의를 제거한다.

[계약][major] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:1629 — `ZLinkMeshDrainResult`가 force reason을 열린 `string`으로 둔다 — 공통 계약은 네 개의 snake_case reason만 허용한다 — 닫힌 reason union을 정의하고 result가 이를 참조하게 한다.

[계약][major] framework/doc/framework/node/internals/regression-test-matrix.ko.md:466 — 회귀 행렬이 폐기 대상 전역 drain API와 PascalCase reason을 검증 대상으로 고정한다 — Node exact 목표 표면과 공통 fixed drain reason 계약에 모두 어긋난다 — MeshName별 runtime과 닫힌 snake_case reason을 기준으로 회귀 항목을 다시 작성한다.

[계약][major] framework/doc/framework/common/e2e/config-11-observability-ops.ko.md:286 — 공통 E2E가 `ForceStopped(DeadlineExceeded)`를 기대한다 — 정식 계약 값은 `ForceStopped(deadline_exceeded)`다 — 시나리오 기대값을 정식 닫힌 reason과 일치시킨다.

[계약][minor] framework/languages/cpp/e2e/ObservabilityOps/feature-map.ko.md:25 — C++ feature map이 `ForceStopped(DeadlineExceeded)`를 완료 조건으로 기록한다 — C++ exact enum과 공통 계약은 `deadline_exceeded`를 사용한다 — 표기를 exact enum 값으로 수정한다.

[계약][major] framework/doc/framework/dotnet/guide/12-operations.ko.md:53 — 운영 가이드의 계기 카탈로그가 정식 metrics 계약과 다른 이름 및 미계약 계기를 다수 열거한다 — `actor.mailbox.depth`, `channel.request.*`, `observability.observer.overflow`는 각각 정식 `actor.queue.depth`, `mesh_node.request.*`, `observability.events.overflow`와 불일치한다 — 정식 51·54 문서의 exact 이름만 남기고 guide 계기명을 자동 검증한다.

[원칙][minor] framework/doc/framework/spec/90-implementation-gap.ko.md:99 — 현재 차이 문서에 다른 언어가 이전 gap을 “닫았다”는 완료 이력이 남아 있다 — gap 문서는 현재 목표와 구현의 열린 차이만 소유해야 한다 — 완료 이력을 제거하고 verifier에 완료 이력 표현 검사를 추가한다.

NOT CLEAN
