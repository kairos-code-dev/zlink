# ZLink Framework .NET 공개 계약

이 디렉토리는 `.NET` framework가 제공해야 하는 정식 public contract를 소유한다.
구현과 contract test는 이 디렉토리의 시그니처와 동작을 따라야 한다.

전체 public interface와 attribute 시그니처의 기준은
[handler-interfaces](handler-interfaces.ko.md)다. 기능별 계약은 같은 디렉토리의
ASP.NET Core, actor, stream, location과 monitoring 문서에서 설명한다.
[Stream Connector 공개 계약](stream-connector.ko.md)은 별도 client package의 lifecycle, dispatch,
codec, transport와 종료 의미를 고정한다.

[`public-contract.ko.md`](public-contract.ko.md)는 사람이 읽는 계약 문서와 실제 assembly·NuGet
산출물 사이의 exact 검증 절차를 정의한다. `public-contract/api/`는 모든 public type과 member의
기계 판독 가능한 정식 서명 부속 명세이고, `public-contract/packages/`는 package archive와
metadata의 정식 부속 명세다. 구현에서 자동으로 최신 상태를 받아들이는 파일이 아니며, 공개 계약
리뷰를 마친 변경에서만 함께 갱신한다.

## 취소 표현

`.NET` 비동기 작업의 명시적 취소는 해당 정식 시그니처에 있는
`CancellationToken`으로 전달한다. token이 없는 메서드에 취소 인자가 존재한다고
간주하지 않는다. 취소의 공통 의미는
[비동기 실행과 coroutine 정책](../../async-execution-policy.ko.md)을 따른다.

다른 언어는 같은 취소 의미를 각 언어의 관례로 표현하며,
`CancellationToken` 타입이나 인자 위치를 그대로 복제할 의무가 없다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | 정식 API snapshot과 source assembly의 모든 공개 서명이 일치한다. |
| `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods` | 각 상세 계약 문서의 회귀 test와 E2E scenario 참조가 현재 checkout에 실제로 존재한다. |
