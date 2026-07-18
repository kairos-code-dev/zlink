# ZLink Framework 10.0.0 .NET 공개 계약

이 디렉토리는 server framework의 정확한 .NET public interface를 소유한다. 기능의 언어 중립 의미는
[공통 스펙](../../../README.ko.md)이 정의하고, 이 디렉토리는 C# 타입, 메서드, generic 제약, nullable과
비동기 반환 타입을 고정한다.

| 문서 | 소유하는 계약 |
|---|---|
| [01 시스템 구조](01-system-structure.ko.md) | ASP.NET Core 등록, package 경계, DI와 startup |
| [02 handler와 client](02-handler-interfaces.ko.md) | Node·ChannelName·Spot·Actor·STREAM의 handler, context와 client 시그니처 |
| [04 routing ID 자동 할당](04-routing-id-allocation.ko.md) | allocation store, 결과와 readiness provider 시그니처 |
| [05 RouteMesh·MeshNode](05-route-mesh.ko.md) | RouteMesh builder, ChannelName membership, manual peer와 runtime option 시그니처 |
| [06 Location Store·Redis](06-location-store.ko.md) | location capability, descriptor·location record, transfer authority와 공식 Redis 시그니처 |

Stream connector client는 별도 package이며
[.NET Stream connector 계약](../../../stream-connector/languages/dotnet/03-stream-connector.ko.md)이
정확한 interface를 소유한다.

## 계약 적용 규칙

- RouteMesh 등록은 `AddRouteMesh(meshName)`으로 시작하고 하나 이상의 `ChannelName(...)`을 등록한다.
- Node direct handler와 ChannelName handler는 서로 다른 interface family를 사용한다.
- typed payload는 JSON을 기본으로 직렬화한다. JSON 사용을 위해 message type마다 codec을 등록하지 않는다.
- metadata는 handler에 변경할 수 없는 `ZLinkMessageMetadata` snapshot으로 전달한다.
- 자동 discovery와 분산 location 기능을 사용하는 host는 Redis location store instance를 명시적으로
  등록한다.
- Logical Multicast의 `NoDrop` 기본값과 manual peer의 expected RID 의미는
  [05 RouteMesh·MeshNode](05-route-mesh.ko.md)가 소유한다.

## 취소

.NET 비동기 operation은 시그니처에 `CancellationToken`이 있을 때만 명시적 취소를 받는다. Token이 없는
메서드에 취소 인자가 있다고 해석하지 않는다. 취소 후의 terminal 결과는
[비동기 실행 정책](../../../04-async-execution-policy.ko.md)을 따른다.

## 검증

Contract test는 source assembly와 실제 NuGet package의 public export를 이 디렉토리의 시그니처와
비교한다. Nullable annotation, 기본값, generic 제약과 overload도 계약에 포함한다.

## 회귀 테스트

| 테스트 | 확인 범위 |
|---|---|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | 정식 spec snapshot과 source·package의 공개 서명이 일치하는지 확인한다. |
| `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods` | 문서가 가리키는 회귀 테스트와 E2E 시나리오가 현재 test tree에 존재하는지 확인한다. |
