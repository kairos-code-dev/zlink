# 1. 개요

> **이 장의 계약 소유 문서** — [Framework 개요](../../../common/spec/02-overview.ko.md)가
> 무엇을 제공하는지를, [언어별 공개 계약 목차](../../../common/spec/server/languages/README.ko.md)가
> 각 언어 표면의 정확한 계약을 소유한다. 이 문서는 그 가운데 **어디서 시작하는지**를
> 정리한다.

## 1. 무엇을 만드는가

실시간 메시징이 중요한 서버 시스템을 여러 프로세스로 나눠 만든다. 서버 간 typed
메시징, 상태 단위(Spot)의 직렬 실행, 외부 client 실시간 연결, 무중단 이전을 한 선언
모델 위에서 조합한다.

=== "C#/.NET"

    `.NET`에서는 **`ASP.NET Core` 애플리케이션 안에 얹는다.** 별도 프로세스나 사이드카가
    아니라 같은 프로세스에서 `ASP.NET Core`의 DI·설정·수명주기를 그대로 쓴다.

=== "C++"

    C++에서는 **framework가 프로세스 자체를 구성한다.** 다른 언어처럼 기존 애플리케이션
    프레임워크(Spring Boot · NestJS · ASP.NET Core) 위에 얹는 것이 아니라, DI 컨테이너 ·
    설정 바인딩 · HTTP hosting을 framework가 함께 제공한다. 그래서 C++ 가이드에는 다른
    언어에 없는 세 장이 더 있다([18](18-di-container.ko.md) ·
    [19](19-configuration.ko.md) · [20](20-http-hosting.ko.md)).

=== "Java"

    Java에서는 **Spring Boot 애플리케이션 안에 얹는다.** 별도 프로세스나 사이드카가 아니라
    같은 JVM에서 Spring의 DI·설정·수명주기를 그대로 쓴다.

=== "Kotlin"

    Kotlin에서는 **Spring Boot 애플리케이션 안에 얹는다.** 별도 프로세스가 아니라 같은
    JVM에서 Spring의 DI·설정·수명주기를 그대로 쓴다.

    ### 이 가이드가 다루는 범위

    **Kotlin은 Java 런타임을 그대로 쓴다.** `zlink-framework-kotlin`은 별도 구현이 아니라
    그 위에 coroutine idiom을 얹는 얇은 레이어다. 그래서 이 가이드는 **Java와 다른 지점만**
    설명하고 나머지는 Java 문서를 가리킨다.

    | 장 | Kotlin 전용 문서 | 이유 |
    | --- | --- | --- |
    | 1 · 2 | 이 가이드가 쓴다 | 의존성과 등록 코드 모양이 다르다 |
    | 3 ~ 10 · 12 · 14 · 15 · 17 | 공통 정본. `Kotlin` 탭을 본다 | 개념과 동작이 같다 |
    | 11 · 13 · 16 | 이 가이드가 **차이만** 쓴다 | 표면은 Java와 같고 idiom만 다르다 |

    같은 내용을 두 벌로 두지 않는 것이 목적이다. Java 문서가 바뀌면 Kotlin 독자도 같은
    문서를 본다.

=== "Node/TypeScript"

    Node에서는 **NestJS 애플리케이션 안에 얹는다.** 별도 프로세스가 아니라 같은 Node
    런타임에서 Nest의 DI·모듈·수명주기를 그대로 쓴다.

## 2. 무엇을 대체하나

=== "C#/.NET"

    | 지금 쓰는 것 | ZLink가 대신하는 부분 |
    | --- | --- |
    | 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
    | 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
    | SignalR · WebSocket 세션 관리 코드 | **STREAM session**과 Actor binding |
    | 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

    HTTP는 대체하지 않는다. 외부 진입은 `ASP.NET Core` controller·minimal API가 그대로
    맡고, ZLink는 그 뒤의 서버 간 통신과 상태 처리를 맡는다.

    **어떤 상황에서 후보가 되는지**와 gRPC · Orleans · Akka와의 비교는
    [17. ZLink를 어디에 쓰나](17-alternative.ko.md)가 다룬다.

=== "C++"

    | 지금 쓰는 것 | ZLink가 대신하는 부분 |
    | --- | --- |
    | 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
    | 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
    | 직접 만든 WebSocket 세션 관리 | **STREAM session**과 Actor binding |
    | 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |
    | 앞단 gateway · 로드밸런서 | location store descriptor 기반 자동 연결과 select-one |

    HTTP는 대체하지 않는다. 외부 진입은 framework가 내장한 HTTP hosting이 맡고, ZLink는 그
    뒤의 서버 간 통신과 상태 처리를 맡는다. **HTTP 요청을 보내는 쪽**은 별도 산출물
    `zlink::http_client`다.

=== "Java"

    | 지금 쓰는 것 | ZLink가 대신하는 부분 |
    | --- | --- |
    | 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
    | 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
    | WebSocket 세션 관리 코드 | **STREAM session**과 Actor binding |
    | 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

    HTTP는 대체하지 않는다. 외부 진입은 Spring MVC·WebFlux가 그대로 맡고, ZLink는 그
    뒤의 서버 간 통신과 상태 처리를 맡는다.

=== "Kotlin"

    | 지금 쓰는 것 | ZLink가 대신하는 부분 |
    | --- | --- |
    | 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
    | 방·세션 상태를 담는 분산 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
    | WebSocket 세션 관리 코드 | **STREAM session**과 Actor binding |
    | 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

    HTTP는 대체하지 않는다. 외부 진입은 Spring MVC·WebFlux가 그대로 맡는다.

    ### Kotlin 레이어가 얹는 것

    네 가지다. 이것이 Java와 다른 전부다.

    ### 2.1 suspend handler 계약

    Java handler는 `CompletionStage`를 돌려주고, Kotlin은 `suspend`로 쓴다. 같은 자리마다
    `ZLinkSuspending*` 짝이 있다.

    | Java | Kotlin |
    | --- | --- |
    | `ZLinkRequestHandler` | `ZLinkSuspendingRequestHandler` |
    | `ZLinkSendHandler` | `ZLinkSuspendingSendHandler` |
    | `ZLinkFanoutHandler` | `ZLinkSuspendingPublishHandler` |
    | `ZLinkSpotPacketHandler` · `ZLinkSpotRequestHandler` | `ZLinkSuspendingSpot*Handler` |
    | `ZLinkSpotSubscriptionHandler` · `ZLinkSpotTimerHandler` | `ZLinkSuspendingSpot*Handler` |
    | `ZLinkSpotActorSendHandler` · `...RequestHandler` | `ZLinkSuspendingSpotActor*Handler` |
    | `ZLinkEntrySpotActorSendHandler` · `...RequestHandler` | `ZLinkSuspendingEntrySpotActor*Handler` |
    | `ZLinkRouteSendHandler` · `ZLinkRouteRequestHandler` | `ZLinkSuspendingRoute*Handler` |
    | `ZLinkTypedSessionPacketHandler` | `ZLinkSuspendingTypedSessionPacketHandler` |

    **둘을 섞어 등록해도 된다.** 등록 쪽이 어느 계약인지 보고 맞게 호출한다.

    ### 2.2 `.kotlin()` wrapper

    Java client는 `CompletionStage`를 돌려준다. `.kotlin()`을 부르면 같은 호출이 suspend
    표면으로 바뀐다.

    ```kotlin
    // Java 표면 그대로 — CompletionStage를 await로 받는다.
    val reply = client.requestToChannel("orders", request)
        .submit(OrderPlaced::class.java)
        .await()

    // Kotlin wrapper — 호출 자체가 suspend다.
    val reply = client.kotlin().requestToChannel("orders", request).submit<OrderPlaced>()
    ```

    wrapper가 있는 표면은 `ZLinkClient` · `ZLinkRouteClient` · `ZLinkFanoutClient` ·
    `ZLinkActorClient` · `ZLinkActorManager`다.

    ### 2.3 `CompletionStage.await()`

    wrapper가 없는 자리에서는 확장 함수 하나로 받는다.

    ```kotlin
    val status = runtime.relocate(options).await()
    ```

    **이 `await()`가 turn을 안다.** Spot이나 Actor의 turn 안에서 불러도 그 turn의 실행
    보장을 깨지 않는다. `kotlinx.coroutines`의 일반 `await`와 바꿔 쓰지 않는다.

    ### 2.4 확장 함수와 `Flow`

    | 확장 | 무엇을 바꾸나 |
    | --- | --- |
    | `ZLinkSpotHandlerRegistry.addHandler<T>()` | `addHandler(T::class.java)` 대신 reified 타입 |
    | `ZLinkFrameworkOptions.routeMesh(...)` · `ZLinkMeshNodeBuilder.channelName(...)` | 등록을 람다 블록으로 |
    | `ZLinkMessage.decode<T>()` · `messageOf(...)` | reified decode와 생성 |
    | `ZLinkLocationRuntimeQuery`의 조회 | `Flow`로 페이지를 이어 받는다 |
    | `Flow.Publisher.asFlow()` | 상태 stream을 `Flow`로 |

=== "Node/TypeScript"

    | 지금 쓰는 것 | ZLink가 대신하는 부분 |
    | --- | --- |
    | 서비스 간 gRPC · REST 호출 | host · port · stub 대신 **ChannelName**으로 부른다 |
    | 방·세션 상태를 담는 Redis 락 | **Spot**의 직렬 실행 — 같은 상태에 두 요청이 겹치지 않는다 |
    | `socket.io` 세션 관리 코드 | **STREAM session**과 Actor binding |
    | 배포 시 세션 드레이닝 스크립트 | **relocation** — 상태를 다른 node로 옮기고 내린다 |

    HTTP는 대체하지 않는다. 외부 진입은 Nest controller가 그대로 맡고, ZLink는 그 뒤의
    서버 간 통신과 상태 처리를 맡는다.

**어떤 상황에서 후보가 되는지**와 gRPC · Orleans · Akka와의 비교는
[17. ZLink를 어디에 쓰나](17-alternative.ko.md)가 다룬다.

## 3. 산출물

=== "C#/.NET"

    | 아티팩트 | 언제 넣나 |
    | --- | --- |
    | `Systems.Zlink` · `Zlink.Framework` | 항상. 계약과 런타임 |
    | `Zlink.Framework.AspNetCore` | `ASP.NET Core`에 얹을 때. 등록 진입점을 제공한다 |
    | `Zlink.Framework.Locations.Redis` | 여러 node를 쓸 때. Redis location store |
    | `Zlink.Framework.Codecs.Protobuf` · `.MessagePack` | 기본 JSON 대신 다른 형식을 쓸 때 |
    | `Systems.Zlink.Stream.Connector` | client 쪽 실시간 연결. 서버에는 필요 없다 |
    | `Zlink.HttpClient` | HTTP 요청을 보내는 쪽 |

    네임스페이스는 `Zlink.Framework`와 `Zlink.Framework.Contracts.*`다. 설치 절차와 최소
    예제는 [2. 시작하기](02-getting-started.ko.md)가 다룬다.

=== "C++"

    | 항목 | 값 |
    | --- | --- |
    | CMake target | `zlink::framework` |
    | facade header | `#include <zlink/framework.hpp>` |
    | 네임스페이스 | `zlink::framework` |
    | public 계약 | `zlink/framework/contracts/*` — Boost 같은 구현 의존성을 노출하지 않는다 |
    | HTTP 요청 client | `zlink::http_client`([가이드](../http-client/README.ko.md)) |
    | codec 확장 | `zlink::framework_codec_protobuf` · `zlink::framework_codec_msgpack` |
    | location store | `zlink::framework_locations_redis` — 여러 node를 쓸 때 |

=== "Java"

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

=== "Kotlin"

    Java 아티팩트에 `zlink-framework-kotlin` 하나를 더한다.

    ```kotlin
    dependencies {
        implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
        implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
        implementation("systems.zlink:zlink-framework-kotlin:0.1.0-SNAPSHOT")  // coroutine idiom
        implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
    }
    ```

    **`zlink-framework-kotlin`은 선택이다.** 빼도 Kotlin에서 쓸 수 있다 — Java 표면을
    그대로 부르면 된다. 넣으면 `suspend`·`Flow`·reified 표면이 생긴다.

    나머지 아티팩트 목록은 [Java 1. 개요](../../../java/guide/server/01-overview.ko.md) §3과 같다.

=== "Node/TypeScript"

    | 패키지 | 언제 넣나 |
    | --- | --- |
    | `@zlink-systems/framework` | 항상. 계약과 런타임 |
    | `@zlink-systems/nestjs` | NestJS에 얹을 때. 대부분 함께 넣는다 |
    | `@zlink-systems/framework-locations-redis` | 여러 node를 쓸 때. Redis location store |
    | `@zlink-systems/framework-codec-protobuf` · `-codec-msgpack` | 기본 JSON 대신 다른 형식을 쓸 때 |
    | `@zlink-systems/stream-connector` | client 쪽 실시간 연결. 서버에는 필요 없다 |
    | `@zlink-systems/stream-wire` | connector가 쓰는 wire 계층 |
    | `@zlink-systems/http-client` | HTTP 요청을 보내는 쪽 |

    **계약 타입은 `@zlink-systems/framework`에서, 등록과 데코레이터는
    `@zlink-systems/nestjs`에서 온다.** import 출처가 둘로 나뉘는 것이 Node의 특징이다.

설치 절차와 최소 예제는 [2. 시작하기](02-getting-started.ko.md)가 다룬다.

## 4. 등록 진입점

=== "C#/.NET"

    `builder.Services.AddZLinkFramework(...)` 하나에 topology를 선언한다.

    ```csharp
    builder.Services.AddZLinkFramework(options =>
    {
        options.AddLocationStore(new ZLinkRedisLocationStore(...)); // node·Spot·Actor 위치를 이 store가 소유한다.

        options.AddRouteMesh("services")            // 서버 간 request·send용 MeshNode.
            .Listen("tcp://0.0.0.0:7301")
            .SetRoutingId(RoutingId.From("service-a"))
            .Channel("orders").Server();            // 이 node가 처리할 논리 membership.

        options.AddFanoutChannel("events")
            .EnablePublisher("tcp://0.0.0.0:7302"); // 연결된 구독자 전원에게 보내는 pub/sub.

        options.AddStreamNode("gateway")
            .Bind("tcp://0.0.0.0:7400");            // 외부 client가 접속할 endpoint.
    });
    ```

    location store를 등록했으므로 서버가 늘거나 줄면 연결이 따라 갱신된다 — 설정 파일이나
    로드밸런서를 고치지 않는다.

    **handler는 DI에 직접 등록하지 않는다.** framework가 찾아 등록하고 생성자 인자만
    컨테이너에서 주입된다.

=== "C++"

    `app_t` 하나를 만들고 `add_zlink_framework`에 구성 람다를 넘긴다. 프로세스 수명은
    `run`이 맡는다.

    ```cpp
    #include <zlink/framework.hpp>

    int main (int argc, char **argv)
    {
        auto app = zlink::framework::app_t::create ();
        app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &options) {
            options.http ()
              .listen ("http://0.0.0.0:8080")
              .map_post<open_conversation_http_handler_t> ("/conversations");
        });
        return app.run (argc, argv);
    }
    ```

    **handler는 클래스 하나로 등록한다.** 의존성은 `dependency_types`를 선언하면 생성자로
    주입된다([18. DI 컨테이너](18-di-container.ko.md)). Spot packet과 Actor payload handler는
    handler class가 아니라 **Spot member 함수**다 — 이 차이는 [6. Spot](06-spot.ko.md)이
    다룬다.

=== "Java"

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

=== "Kotlin"

    Java와 같다. `@EnableZLinkFramework`와 `ZLinkFrameworkConfigurer` bean이다.

    ```kotlin
    @EnableZLinkFramework
    @SpringBootApplication
    class PlayServerApplication {

        @Bean
        fun zlink(settings: PlaySettings): ZLinkFrameworkConfigurer =
            ZLinkFrameworkConfigurer { options ->
                options.addHandlersFromPackageOf(PlayServerApplication::class.java)

                val mesh = options.addRouteMesh("play")
                mesh.listen(settings.meshEndpoint)
                    .setRoutingIdPrefix("play")
                mesh.objects().server()
                    .addEntrySpot(PlayEntrySpot::class.java)
            }
    }
    ```

    `ZLinkFrameworkConfigurer`는 Java의 functional interface라 Kotlin에서 SAM 변환으로
    람다를 넘긴다.

=== "Node/TypeScript"

    `ZLinkModule.forRootFactory(...)`가 등록을 받고, `zlinkFramework()`가 builder를 만든다.

    ```typescript
    import { ZLinkModule, zlinkFramework, zlinkModule } from '@zlink-systems/nestjs';

    @Module({
      imports: [
        ZLinkModule.forRootFactory({
          useFactory: () => {
            const builder = zlinkFramework();

            const mesh = builder.addRouteMesh('play')
              .listen(config.meshEndpoint)
              .setRoutingIdPrefix('play');
            mesh.objects().server().addEntrySpot(PlayEntrySpot);

            return builder;
          }
        }),
        // 이 디렉터리 아래의 handler·Spot·Actor를 provider로 모아 준다.
        zlinkModule(__dirname, { })
      ]
    })
    export class PlayModule {}
    ```

    **`useFactory`는 builder를 돌려준다.** 등록만 하고 끝내는 것이 아니라 마지막에
    `return builder`가 있어야 한다.

    **handler는 데코레이터로 등록한다.** Node handler는 interface를 구현하고 데코레이터로 어느 group·packet인지 밝힌다.

    ```typescript
    import { zlinkRequestHandler } from '@zlink-systems/nestjs';
    import type { ZLinkRequestHandler } from '@zlink-systems/framework';

    @zlinkRequestHandler('api', PacketNames.getProfile)
    export class GetProfileHandler
      implements ZLinkRequestHandler<GetProfileReq, GetProfileRes> {

      async handle(request: GetProfileReq): Promise<GetProfileRes> {
        return getProfileRes(request.accountId);
      }
    }
    ```

    데코레이터는 받는 것마다 하나씩 있다 — `zlinkRequestHandler` · `zlinkSendHandler` ·
    `zlinkPublishHandler` · `zlinkSpotPacketHandler` · `zlinkSpotSubscriptionHandler` ·
    `zlinkSpotTimerHandler` · `zlinkSpotActorSendHandler` · `zlinkSpotActorRequestHandler` ·
    `zlinkEntrySpot*Handler` 넷이다. 목록은
    [13. 주요 interface 사용 색인](13-interface-catalog.ko.md)에 있다.

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

=== "C#/.NET"

    - 읽는 순서: [.NET 가이드 진입점](README.ko.md)
    - `.NET` 공개 계약: [exact interface 목차](../../../common/spec/server/languages/dotnet/interfaces/README.ko.md)

=== "C++"

    - 읽는 순서: [C++ 가이드 진입점](README.ko.md)
    - C++ 공개 계약: [exact interface 목차](../../../common/spec/server/languages/cpp/interfaces/README.ko.md)

=== "Java"

    - 읽는 순서: [Java 가이드 진입점](README.ko.md)
    - Java 공개 계약: [exact interface 목차](../../../common/spec/server/languages/java/interfaces/README.ko.md)

=== "Kotlin"

    - 읽는 순서: [Kotlin 가이드 진입점](README.ko.md)
    - Kotlin 전용 계약: [Kotlin 공개 계약](../../../common/spec/server/languages/kotlin/README.ko.md)

=== "Node/TypeScript"

    - 읽는 순서: [Node.js 가이드 진입점](README.ko.md)
    - Node 공개 계약: [exact interface 목차](../../../common/spec/server/languages/node/interfaces/README.ko.md)

- 언어 중립 정의: [공통 스펙 목차](../../../common/README.ko.md)
- 다음 장: [2. 시작하기](02-getting-started.ko.md)
