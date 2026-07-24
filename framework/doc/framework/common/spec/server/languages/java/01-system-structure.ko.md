# Java system structure 문서 위치

Java server public signature는 [exact interface 목차](interfaces/README.ko.md)에서 기능별로 제공한다.

- [공통 runtime](interfaces/common-runtime.ko.md)
- [구성과 host](interfaces/configuration-host.ko.md)
- [Channel messaging](interfaces/channel-messaging.ko.md)
- [Monitoring](interfaces/monitoring.ko.md)

Host 종료의 정본은 `Retire`와 `Shutdown`이며, 호환용 host drain과 MeshNode scoped drain은
[공통 runtime](interfaces/common-runtime.ko.md)과 [Monitoring](interfaces/monitoring.ko.md)에서 구분한다.

공통 동작은 [Framework 공통 spec](../../../README.ko.md)을 따른다.
