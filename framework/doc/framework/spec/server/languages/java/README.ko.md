# ZLink Framework Java 공개 계약

이 디렉토리는 Java framework가 제공해야 하는 **정식 public contract**를 소유한다. 구현과
regression test는 이 계약을 따라야 한다.

Kotlin이 Java 계약을 그대로 사용하는 경우 이 문서를 따르며, Kotlin 전용 `suspend`와 `Flow`
표면은 [Kotlin 공개 계약](../kotlin/README.ko.md)이 별도로 고정한다.

Channel 호출은 process-local ChannelName만 사용한다. Host 종료는 `Retire`와 `Shutdown`이 정본이며, Location
provider는 descriptor·location 기능과 opaque authority CAS capability를 함께 제공한다.

| 문서 | 범위 |
|---|---|
| [기능별 interfaces](interfaces/README.ko.md) | runtime, 구성, Channel, Spot, Actor, STREAM, Location·maintenance와 monitoring의 정확한 signature |
| [Stream Connector](../../../stream-connector/languages/java/03-stream-connector.ko.md) | client connector의 public 표면 |

**기능의 의미와 동작 규칙은 [공통 스펙](../../../README.ko.md)이 소유한다.** 이 디렉토리는 그 의미가
이 언어에서 갖는 **정확한 public API**만 고정한다.
