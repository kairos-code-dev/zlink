<!-- framework-adapter-nav:start -->
[가이드 홈](../../../index.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

# 1. 개요

> 이 문서는 Java 가이드의 진입점이다. 언어 중립 정의는
> [공통 스펙 목차](../../../common/README.ko.md)가, Java 표면의 정확한 계약은
> [Java exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)가 소유한다.
> Kotlin으로 쓴다면 [Kotlin 가이드](../../../kotlin/guide/server/README.ko.md)를 본다 — 런타임은 같고 idiom만 다르다.

## 1. 무엇을 만드는가

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만든다. 서버 간 typed
메시징, 상태 단위(Spot)의 직렬 실행, 외부 client 실시간 연결, 무중단 이전을 한 선언
모델 위에서 조합한다.

Java에서는 **Spring Boot 애플리케이션 안에 얹는다.** 별도 프로세스나 사이드카가 아니라
같은 JVM에서 Spring의 DI·설정·수명주기를 그대로 쓴다.

## 2. 무엇을 대체하나

| 지금 쓰는 것 | ZLink가 대신하는 부분 |
| --- | --- |
| 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
| 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
| WebSocket 세션 관리 코드 | **STREAM session**과 Actor binding |
| 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

HTTP는 대체하지 않는다. 외부 진입은 Spring MVC·WebFlux가 그대로 맡고, ZLink는 그
뒤의 서버 간 통신과 상태 처리를 맡는다.

## 3. 산출물

| 아티팩트 | 언제 넣나 |
| --- | --- |
| `systems.zlink:zlink-framework-core` | 항상. 계약과 런타임 |
| `systems.zlink:zlink-framework-spring-boot-starter` | Spring Boot에 얹을 때. 대부분 함께 넣는다 |
| `systems.zlink:zlink-framework-locations-redis` | 여러 node를 쓸 때. Redis location store |
| `systems.zlink:zlink-framework-codec-protobuf` · `-codec-msgpack` | 기본 JSON 대신 다른 형식을 쓸 때 |
| `systems.zlink:zlink-stream-connector` | client 쪽 실시간 연결. 서버에는 필요 없다 |
| `systems.zlink:zlink-http-client` | HTTP 요청을 보내는 쪽 |
| `systems.zlink:zlink-framework-testkit` | E2E 테스트 |
| `systems.zlink:zlink-framework-kotlin` | Kotlin coroutine idiom 레이어 |

Kotlin에서 쓰더라도 **런타임은 `zlink-framework-core` 하나다.** `zlink-framework-kotlin`은
`suspend`·`Flow` 표면을 얹는 얇은 레이어이지 별도 구현이 아니다.

## 4. 등록 진입점

Spring Boot 애플리케이션에 `@EnableZLinkFramework`를 붙이고, 구성은
`ZLinkFrameworkConfigurer` bean 하나에 모은다.

```java
@EnableZLinkFramework
@SpringBootApplication
public class PlayServerApplication {

    @Bean
    ZLinkFrameworkConfigurer zlink(PlaySettings settings) {
        return options -> {
            options.addHandlersFromPackageOf(PlayServerApplication.class);

            ZLinkMeshNodeBuilder mesh = options.addRouteMesh("play")
                .listen(settings.meshEndpoint())
                .setRoutingIdPrefix("play");
            mesh.objects().server()
                .addEntrySpot(PlayEntrySpot.class);
        };
    }
}
```

**handler는 Spring bean이 아니다.** `addHandlersFromPackageOf(...)`가 찾아 등록하고,
생성자 인자는 Spring 컨테이너에서 주입된다. handler class 자체에 `@Component`를 붙이지
않는다.

## 5. 읽는 순서

이 가이드의 03~17장은 **다섯 언어가 같은 정본을 공유한다.** 예제는 언어 탭으로 나뉘며
`Java` 탭을 고르면 Java 코드로 바뀐다. 순서는
[Java 가이드 진입점](README.ko.md)이 제시한다.

먼저 [3. 핵심 개념](03-concepts.ko.md)에서 channel · Spot ·
Actor · stream · relocation 다섯 개념을 잡는다. 나머지 장은 그 조합이다.

## 6. 도입 순서 고르기

전부 한 번에 쓰지 않는다. 지금 겪는 문제부터 고른다.

| 지금 겪는 문제 | 먼저 볼 장 |
| --- | --- |
| 서비스가 어디 있는지 관리하기 번거롭다 | [5. Channel Messaging](05-channel-messaging.ko.md) |
| 방·세션 상태에 락이 얽힌다 | [6. Spot](06-spot.ko.md) |
| client 실시간 연결을 직접 관리한다 | [9. STREAM](09-stream.ko.md) |
| 배포할 때 세션이 끊긴다 | [10. Location](10-location.ko.md) · [7. Actor와 Spot](07-actor-spot.ko.md) |
| 부하가 몰릴 때 동작을 모르겠다 | [4. Backpressure](04-backpressure.ko.md) |

## 7. 관련 문서

- 읽는 순서: [Java 가이드 진입점](README.ko.md)
- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- Java 공개 계약: [exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
