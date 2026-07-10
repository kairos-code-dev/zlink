# ZLink Framework C++ — 문서

`zlink::framework` C++ 산출물의 문서 허브다.

| 분류 | 위치 | 내용 |
|------|------|------|
| **사용자 가이드** | [guide/](guide/README.ko.md) | 이 프레임워크로 시스템을 작성하는 방법. 이것부터 읽는다. |
| **spec (상세 계약)** | [spec/](spec) | 기능별 상세 계약. 가이드와 어긋나면 spec과 코드가 정답이다. |
| **internals** | [internals/](internals) | 설계 정책·구현 계획·리팩토링 기록. 프레임워크를 고치는 사람용. |

## spec 목록

| 문서 | 계약 |
|------|------|
| [cpp-application-framework](spec/cpp-application-framework.ko.md) | application host, DI, configuration, handler 모델 |
| [cpp-framework-interfaces](spec/cpp-framework-interfaces.ko.md) | 공개 인터페이스 표면 |
| [handler-interfaces](spec/handler-interfaces.ko.md) | 핸들러 계약 |
| [cpp-channel-messaging](spec/cpp-channel-messaging.ko.md) | 채널 메시징 |
| [cpp-spot](spec/cpp-spot.ko.md) | SPOT |
| [stage-wrapper-on-spot](spec/stage-wrapper-on-spot.ko.md) | stage wrapper |
| [actor-gateway-session-relay](spec/actor-gateway-session-relay.ko.md) | actor gateway / session relay |
| [cpp-stream](spec/cpp-stream.ko.md) | stream |
| [cpp-http-hosting](spec/cpp-http-hosting.ko.md) | HTTP hosting |
| [cpp-embedded-http-server](spec/cpp-embedded-http-server.ko.md) | embedded HTTP server |
| [cpp-registry](spec/cpp-registry.ko.md) | registry |
| [cpp-monitoring](spec/cpp-monitoring.ko.md) | monitoring |

## 별도 산출물 문서

| 산출물 | 문서 |
|--------|------|
| HTTP client (`zlink::http_client`) | [가이드](../../http-client/cpp/README.ko.md) · [spec](../../http-client/cpp/spec/cpp-http-client.ko.md) |
| Stream connector (`zlink::stream_connector`) | [사용자 가이드](../../stream-connector/cpp/guide/INDEX.ko.md) |

## internals 목록

[runtime architecture](internals/runtime-architecture.ko.md) ·
[backend dependency policy](internals/backend-dependency-policy.ko.md) ·
[regression test matrix](internals/regression-test-matrix.ko.md)

기능별 실행 예제는 guide의 [channel](guide/samples/channel-messaging-samples.ko.md),
[Spot](guide/samples/spot-samples.ko.md),
[stream](guide/samples/stream-samples.ko.md) 문서에서 확인한다.

상위 framework 공통 문서는 [framework/doc](../../README.ko.md)을 본다.
