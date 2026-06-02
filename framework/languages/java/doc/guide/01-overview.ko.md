<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [다음: Getting Started](./02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework for Java/Kotlin Overview

## 1. 한 줄 정의

`ZLink Framework for Java/Kotlin`은 zlink Java binding 위에 올라가, 별도 gateway나
전용 로드밸런서 없이 논리 `channel name` 기준 서버 간 호출, pub/sub, `SPOT`,
`STREAM`을 Spring Boot bean lifecycle과 DI 모델 안에서 쓰게 해 주는 상위 계층이다.

개발자는 low-level socket, discovery, reconnect, correlation을 직접 다루지 않는다.
handler와 outbound client만 작성하고, 연결과 라우팅은 framework가 처리한다.

## 2. 아키텍처

```text
+----------------------------------------------------------+
| Spring Boot app (bean, DI, lifecycle, handler)           |
+----------------------------------------------------------+
| ZLink Framework for Java/Kotlin                          |
| - channel messaging - SPOT/actor - STREAM session        |
| - registry/monitoring - stream connector client          |
+----------------------------------------------------------+
| zlink Java binding                                       |
+----------------------------------------------------------+
| zlink core (C ABI) - transport, ZMP, I/O threads         |
+----------------------------------------------------------+
```

framework는 새 transport를 만들지 않는다. 기존 binding 기능을 Spring Boot와 Java/Kotlin
사용 표면으로 감싼다.

## 3. 통합 축

| 축 | 사용자에게 보이는 것 |
|----|----------------------|
| channel messaging | `@ZLinkRequest`, `ZLinkClient` |
| fanout | `@ZLinkPublish`, `ZLinkFanoutClient` |
| Spot | typed Spot factory, timer, outbound |
| actor/session | actor factory, Entry Spot, `ZLinkBoundSession` |
| STREAM | `ZLinkSession`, Stream Connector |
| Registry | embedded registry, topology query |
| Monitoring | typed runtime event handler |

## 4. 현재 상태

이 guide는 현재 Java/Kotlin framework 구현과 sample release gate를 기준으로
작성한다. 공개 계약의 정확한 시그니처는
[인터페이스 카탈로그](../spec/handler-interfaces.ko.md)를 기준으로 보고, `.NET`과
Java/Kotlin 표면을 대조해야 할 때는
[표면 매핑 정책](../internals/dotnet-to-java-surface-mapping.ko.md)을 함께 읽는다.

## 5. 읽는 순서

1. [02-getting-started](./02-getting-started.ko.md)
2. [03-concepts](./03-concepts.ko.md)
3. [04-feature-map](./04-feature-map.ko.md)
4. [05-channel-messaging](./05-channel-messaging.ko.md)
5. [06-spot](./06-spot.ko.md)
6. [07-actor-session](./07-actor-session.ko.md)
7. [08-stream](./08-stream.ko.md)
8. [09-registry](./09-registry.ko.md)
9. [10-monitoring](./10-monitoring.ko.md)
10. [11-interface-catalog](./11-interface-catalog.ko.md)
11. [12-grpc-alternative](./12-grpc-alternative.ko.md)
