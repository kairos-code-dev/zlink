---
title: "1. 개요 · Java"
---

<!-- generated:start -->
<!-- 이 파일은 `common/guide/server/01-overview.ko.md`에서 생성한다. 직접 고치지 않는다.
     고칠 곳은 공통 소스이고, `python3 doc/site/scripts/generate_language_guides.py`로 다시 만든다. -->
<!-- generated:end -->

<!-- framework-adapter-nav:start -->
[가이드 홈](README.ko.md) | [다음: 2. 시작하기](02-getting-started.ko.md)
<!-- framework-adapter-nav:end -->

<!-- language-switch:start -->
다른 언어로 보기 — [C#/.NET](../../../dotnet/guide/server/01-overview.ko.md) · [C++](../../../cpp/guide/server/01-overview.ko.md) · **Java** · [Kotlin](../../../kotlin/guide/server/01-overview.ko.md) · [Node/TypeScript](../../../node/guide/server/01-overview.ko.md)
<!-- language-switch:end -->

# 1. 개요

> **이 장의 계약 소유 문서** — [Framework 개요](../../../common/spec/02-overview.ko.md)가
> 무엇을 제공하는지를, [언어별 공개 계약 목차](../../../common/spec/server/languages/README.ko.md)가
> 각 언어 표면의 정확한 계약을 소유한다. 이 문서는 그 가운데 **어디서 시작하는지**를
> 정리한다.

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

**어떤 상황에서 후보가 되는지**와 gRPC · Orleans · Akka와의 비교는
[17. ZLink를 어디에 쓰나](17-alternative.ko.md)가 다룬다.

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
`suspend`·`flow` 표면을 얹는 얇은 레이어이지 별도 구현이 아니다.

설치 절차와 최소 예제는 [2. 시작하기](02-getting-started.ko.md)가 다룬다.

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

이 가이드의 03~17장은 **다섯 언어가 같은 정본에서 생성된다.** 예제는 이 언어의 코드만
담기며 다른 언어 코드가 섞이지 않는다. 읽는 순서는 이 언어의 가이드 진입점이 제시한다.

먼저 [3. 핵심 개념](03-concepts.ko.md)에서 channel · Spot · Actor · stream ·
relocation 다섯 개념을 잡는다. 나머지 장은 그 조합이다.

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
- Java 공개 계약: [exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)

- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
