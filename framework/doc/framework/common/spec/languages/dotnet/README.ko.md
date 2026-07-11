# ZLink Framework .NET 공개 계약

이 디렉토리는 `.NET` framework가 제공해야 하는 정식 public contract를 소유한다.
구현과 contract test는 이 디렉토리의 시그니처와 동작을 따라야 한다.

전체 public interface와 attribute 시그니처의 기준은
[handler-interfaces](handler-interfaces.ko.md)다. 기능별 계약은 같은 디렉토리의
ASP.NET Core, actor, stream, location과 monitoring 문서에서 설명한다.

## 취소 표현

`.NET` 비동기 작업의 명시적 취소는 해당 정식 시그니처에 있는
`CancellationToken`으로 전달한다. token이 없는 메서드에 취소 인자가 존재한다고
간주하지 않는다. 취소의 공통 의미는
[비동기 실행과 coroutine 정책](../../async-execution-policy.ko.md)을 따른다.

다른 언어는 같은 취소 의미를 각 언어의 관례로 표현하며,
`CancellationToken` 타입이나 인자 위치를 그대로 복제할 의무가 없다.
