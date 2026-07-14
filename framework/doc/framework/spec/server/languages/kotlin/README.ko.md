# ZLink Framework Kotlin 공개 계약

이 디렉토리는 `zlink-framework-kotlin`이 Java runtime 위에 추가하는 Kotlin 전용
public contract를 소유한다. 그대로 사용하는 Java 타입과 메서드는
[Java 공개 계약](../java/README.ko.md)을 따르고 여기서 복사해 다시 정의하지 않는다.

Kotlin suspending handler와 adapter 시그니처는
[handler-interfaces](02-handler-interfaces.ko.md)를 기준으로 한다. Java API를 기다리는
extension, Stream Connector coroutine wrapper와 `Flow` 표면도 이 디렉토리의 정식
계약에 포함한다. Kotlin source와 contract test는 이 계약을 따라야 한다.

## 취소 인자

Kotlin public interface에는 framework `CancellationToken`이나 같은 목적의 별도 취소
인자를 두지 않는다. `suspend` 함수는 호출한 coroutine의 lifecycle을 따르며, 이 동작을
별도 token parameter로 중복 표현하지 않는다. timeout, host shutdown과 resource cleanup은
각 기능 계약을 따른다.
