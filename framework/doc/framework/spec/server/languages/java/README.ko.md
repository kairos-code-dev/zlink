# ZLink Framework Java 공개 계약

이 디렉토리는 Java framework가 제공해야 하는 **정식 public contract**를 소유한다. 구현과
regression test는 이 계약을 따라야 한다.

Kotlin이 Java 계약을 그대로 사용하는 경우 이 문서를 따르며, Kotlin 전용 `suspend`와 `Flow`
표면은 [Kotlin 공개 계약](../kotlin/README.ko.md)이 별도로 고정한다.

| 번호 | 문서 | 범위 |
|---|------|------|
| `01` | [시스템 구조](01-system-structure.ko.md) | 패키지 구조·배포, Spring Boot 등록, DI, lifecycle, startup validation |
| `02` | [인터페이스](02-handler-interfaces.ko.md) | 전체 public interface·타입·시그니처 카탈로그 |
| `03` | [Location Store·Redis](03-location-store.ko.md) | store-neutral descriptor·location·lease·transfer authority와 공식 Redis 구현 |
| `04` | [Routing ID 자동 할당](04-routing-id-allocation.ko.md) | slot allocation store, 닫힌 결과 타입과 준비 상태 조회 |
| `SC` | [Stream Connector](../../../stream-connector/languages/java/03-stream-connector.ko.md) | client connector의 public 표면 |

**기능의 의미와 동작 규칙은 [공통 스펙](../../../README.ko.md)이 소유한다.** 이 디렉토리는 그 의미가
이 언어에서 갖는 **정확한 public API**만 고정한다.
