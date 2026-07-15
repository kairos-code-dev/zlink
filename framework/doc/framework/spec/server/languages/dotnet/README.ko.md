# ZLink Framework .NET 공개 계약

이 디렉토리는 `.NET` framework가 제공해야 하는 **정식 public contract**를 소유한다. 구현과
contract test는 이 디렉토리의 시그니처와 동작을 따라야 한다.

| 번호 | 문서 | 범위 |
|---|------|------|
| `01` | [시스템 구조](01-system-structure.ko.md) | 패키지 구조·배포, ASP.NET Core host 등록, DI, lifecycle, startup validation |
| `02` | [인터페이스](02-handler-interfaces.ko.md) | 전체 public interface·타입·시그니처 카탈로그, 공개 계약 산출물 검증 절차(§17) |
| `03` | [Stream Connector](../../../stream-connector/languages/dotnet/03-stream-connector.ko.md) | client connector의 public 표면 |
| `04` | [routing id 자동 할당](04-routing-id-allocation.ko.md) | builder, allocation store와 결과 조회의 정확한 public 시그니처 |

**기능의 의미와 동작 규칙은 [공통 스펙](../../../README.ko.md)이 소유한다.** 이 디렉토리는 그 의미가
이 언어에서 갖는 **정확한 public API**만 고정한다.

계약 검증에 쓰는 기계 판독용 snapshot은 문서가 아니라 **코드 옆**에 둔다 —
`framework/languages/dotnet/contract/api/`(모든 public type과 member의 정식 서명),
`framework/languages/dotnet/contract/packages/`(package archive와 metadata). **구현에서 자동으로
최신 상태를 받아들이는 파일이 아니며, 공개 계약 리뷰를 마친 변경에서만 함께 갱신한다.**

## 취소 표현

`.NET` 비동기 작업의 명시적 취소는 정식 시그니처에 있는 `CancellationToken`으로 전달한다.
**token이 없는 메서드에 취소 인자가 존재한다고 간주하지 않는다.** 취소의 공통 의미는
[비동기 실행과 coroutine 정책](../../../04-async-execution-policy.ko.md)을 따른다.

다른 언어는 같은 취소 의미를 각 언어의 관례로 표현하며, `CancellationToken` 타입이나 인자 위치를
그대로 복제할 의무가 없다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ContractSurfaceCoverage.Fixed_spec_snapshot_matches_every_exported_contract_signature` | 정식 API snapshot과 source assembly의 모든 공개 서명이 일치한다. |
| `RegressionTests.DotNetContractRegressionTestReferences_Resolve_ToActiveTestMethods` | 각 계약 문서의 회귀 test와 E2E scenario 참조가 현재 checkout에 실제로 존재한다. |
