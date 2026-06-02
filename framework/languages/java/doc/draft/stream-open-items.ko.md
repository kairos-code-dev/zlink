<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Java Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md) | [다음: Draft -- ZLink Framework Java STREAM Samples](./stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Stream Connector](./stream-connector.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- Java STREAM Decisions And Follow-up Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM`에서 이미 닫은 결정과 첫 구현 이후의
> 편의 기능 후보를 모아 둔다.

## 1. 닫힌 결정

아래 항목은 `.NET` framework 기준으로 이미 포팅 방향을 닫는다.

- server session은 header 기반 `ZLinkSession` 하나로 둔다.
- `packet session`과 `raw session`을 public type으로 나누지 않는다.
- stream node 등록은 `bind`, `attachActorGateway`, `registerSession`을 기본
  표면으로 둔다.
- session callback은 native/socket callback 안에서 직접 실행하지 않는다.
- 같은 session의 callback은 직렬로 실행한다.
- session callback으로 전달된 payload는 callback 동안 빌려온 값이다.
- server-to-client 전송은 `ZLinkSessionContext.client()`와 actor
  `ZLinkBoundSession`을 사용한다.
- actor/session relay는 ActorGateway를 사용한다. route mesh channel packet으로
  대신 구현하지 않는다.
- client connector는 server framework와 별도 모듈로 둔다.
- connector는 TCP, TLS, WebSocket, secure WebSocket transport를 지원한다.
- connector는 manual dispatch mode를 기본값으로 둔다.
- connector sample과 game sample client는 Java connector를 실제로 사용한다.

## 2. 첫 구현 이후 편의 기능

- Kotlin `Flow` adapter에서 backpressure를 어떤 buffer 정책으로 노출할지 정해야 한다.
- Spring Boot starter가 connector client bean을 자동 구성할지는 첫 구현 범위에서
  제외한다. connector는 별도 모듈이므로 sample은 직접 생성한다.

위 항목은 `.NET`과 같은 사용성과 기능을 갖춘 첫 Java/Kotlin 구현을 막지 않는다. 첫
구현의 필수 범위는 `spring-boot-stream.ko.md`, `stream-connector.ko.md`,
`sample-implementation-plan.ko.md`에 적힌 server session, actor relay, connector,
sample smoke 기준으로 닫는다.
