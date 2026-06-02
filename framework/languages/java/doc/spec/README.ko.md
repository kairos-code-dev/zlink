# ZLink Framework for Java/Kotlin -- Spec

이 디렉토리는 Java/Kotlin framework의 공개 계약을 설명한다. 정식 spec에는 현재
구현과 regression test에 존재하는 API만 둔다. 후속 편의 기능이나 구현 전 설계는
`../draft/`에 남긴다.

| 문서 | 범위 |
|------|------|
| [handler-interfaces](./handler-interfaces.ko.md) | interface, annotation, context, options |
| [spring-boot-channel-messaging](./spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](./spring-boot-spot.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](./spring-boot-actor-session.ko.md) | actor factory, ActorGateway, bound session |
| [spring-boot-stream](./spring-boot-stream.ko.md) | stream node, header session |
| [stream-connector](./stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](./spring-boot-registry.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](./spring-boot-monitoring.ko.md) | runtime event, typed handler |
| [stage-wrapper-on-spot](./stage-wrapper-on-spot.ko.md) | Spot 위에 stage 모델을 얹는 조건 |
