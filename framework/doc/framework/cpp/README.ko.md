# ZLink Framework C++ — 문서

`zlink::framework` C++ 산출물의 문서 허브다.

| 분류 | 위치 | 내용 |
|------|------|------|
| **사용자 가이드** | [guide/](guide/README.ko.md) | 이 프레임워크로 시스템을 작성하는 방법. 이것부터 읽는다. |
| **spec (상세 계약)** | [중앙 C++ spec](../common/spec/languages/cpp/README.ko.md) | 기능별 상세 계약. 가이드와 코드가 어긋나면 계약 불일치로 처리한다. |
| **internals** | [internals/](internals) | 설계 정책·구현 계획·리팩토링 기록. 프레임워크를 고치는 사람용. |

## spec 목록

| 문서 | 계약 |
|------|------|
| [cpp-application-framework](../common/spec/languages/cpp/01-system-structure.ko.md) | application host, DI, configuration, handler 모델 |
| [cpp-framework-interfaces](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | 공개 인터페이스 표면 |
| [handler-interfaces](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | 핸들러 계약 |
| [cpp-channel-messaging](../common/spec/languages/cpp/01-system-structure.ko.md) | 채널 메시징 |
| [cpp-spot](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | SPOT |
| [stage-wrapper-on-spot](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | stage wrapper |
| [actor-gateway-session-relay](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | actor gateway / session relay |
| [cpp-stream](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | stream |
| [cpp-http-hosting](../common/spec/languages/cpp/60-http-hosting.ko.md) | HTTP hosting |
| [cpp-embedded-http-server](../common/spec/languages/cpp/61-embedded-http-server.ko.md) | embedded HTTP server |
| [cpp-registry](../common/spec/languages/cpp/01-system-structure.ko.md) | registry |
| [cpp-monitoring](../common/spec/languages/cpp/02-framework-interfaces.ko.md) | monitoring |

## 별도 산출물 문서

| 산출물 | 문서 |
|--------|------|
| HTTP client (`zlink::http_client`) | [가이드](../../http-client/cpp/README.ko.md) · [spec](../../http-client/cpp/spec/cpp-http-client.ko.md) |
| Stream connector (`zlink::stream_connector`) | [사용자 가이드](../../stream-connector/cpp/guide/INDEX.ko.md) |

## internals 목록

[runtime architecture](internals/runtime-architecture.ko.md) ·
[backend dependency policy](internals/backend-dependency-policy.ko.md) ·
[regression test matrix](internals/regression-test-matrix.ko.md)

공통 6종의 역할, DTO와 검증 기준은 [공통 샘플](../common/sample/README.ko.md)에서 확인한다.

상위 framework 공통 문서는 [framework/doc](../../README.ko.md)을 본다.
