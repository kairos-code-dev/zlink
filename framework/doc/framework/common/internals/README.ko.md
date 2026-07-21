# Framework 공통 내부 구조

[Framework 공통 문서](../README.ko.md) · [정식 spec](../../spec/README.ko.md)

이 디렉터리는 C++·.NET·JVM·Node.js service runtime이 서로 다른 언어로 구현되더라도 동일하게 지켜야 하는
내부 경계와 wire 불변 조건을 설명한다. 이 문서는 application public API가 아니다. 공개 동작은 정식 spec과
언어별 `interfaces/` 문서가 소유한다.

| 문서 | 범위 |
|---|---|
| [Service runtime architecture](service-runtime-architecture.ko.md) | raw binding 경계, mailbox, completion과 resource ownership |
| [Service wire protocol](service-wire-protocol.ko.md) | multipart envelope, command ID, extension과 compatibility |
| [Stateful maintenance](stateful-maintenance-runtime.ko.md) | host barrier, authority CAS, checkpoint, recovery와 STREAM barrier |
| [Transport liveness](transport-liveness-runtime.ko.md) | Service probe·fanout beacon, raw monitor, readiness와 reconnect 경계 |

네 runtime은 이 문서의 의미를 구현하지만 source나 공통 native binary를 공유하지 않는다. Wire byte layout과
상수의 단일 기준은 `framework/runtime/protocol/service-wire-v1.schema.json`이다. [Service wire protocol](service-wire-protocol.ko.md)은
schema의 field 관계, 검증 순서와 state transition을 설명하며 둘은 같은 review에서 동기화한다. V11 구현 작업자가
사용하는 [구현 source package](../../../plan/v11.0/target-internals/README.ko.md)는 한 디렉터리에서 전체 구조를
확인할 수 있게 묶었다. 이 package는 이 정식 내부 계약과 다른 계약을 정의하지 않는다.
