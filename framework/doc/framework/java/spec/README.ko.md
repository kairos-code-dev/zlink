# ZLink Framework for Java/Kotlin -- Spec

이 디렉토리는 Java/Kotlin framework의 공개 계약을 설명한다. 정식 spec에는 현재
구현과 regression test에 존재하는 API만 둔다.

`draft/` 아래 문서는 구현 전 초안이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤에만
정식 spec 문서로 나누어 반영한다.

| 문서 | 범위 |
|------|------|
| [handler-interfaces](handler-interfaces.ko.md) | interface, annotation, context, options |
| [spring-boot-channel-messaging](spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](spring-boot-spot.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](spring-boot-actor-session.ko.md) | actor factory, SessionRelay, bound session |
| [spring-boot-stream](spring-boot-stream.ko.md) | stream node, header session |
| [stream-connector](stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](spring-boot-registry.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](spring-boot-monitoring.ko.md) | runtime event, typed handler |
| [stage-wrapper-on-spot](stage-wrapper-on-spot.ko.md) | Spot 위에 상위 stage 모델(playhouse 등)을 얹는 조건 — 기본 공개 API 계약이 아니라 상위 모델 가이드 |

## Draft

| 문서 | 범위 |
|------|------|
| [yield-cancellation](draft/yield-cancellation.ko.md) | cancellation-aware `yield(...)` 후보 API와 `YD-E2` 검증 조건 |
