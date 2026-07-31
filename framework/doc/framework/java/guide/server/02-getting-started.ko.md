<!-- framework-adapter-nav:start -->
[가이드 홈](../../../index.ko.md) | [이전: 1. 개요](01-overview.ko.md) | [다음: 3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# 2. 시작하기

> 이 장이 따라가는 코드는 저장소의 `framework/languages/java/samples/java/TicTacToe`다.
> 등록 표면의 정식 계약은
> [Java configuration과 host 공개 계약](../../../common/spec/server/languages/java/interfaces/configuration-host.ko.md)이 다룬다.

두 프로세스가 서버 간 channel로 대화하는 최소 구성을 만든다. API 서버가 요청을 받아
Play 서버에 넘기는 흐름 하나만 본다.

## 1. 의존성 추가

```kotlin
// build.gradle.kts
dependencies {
    implementation("systems.zlink:zlink-framework-core:0.1.0-SNAPSHOT")
    implementation("systems.zlink:zlink-framework-spring-boot-starter:0.1.0-SNAPSHOT")
    // 여러 node를 쓸 때만.
    implementation("systems.zlink:zlink-framework-locations-redis:0.1.0-SNAPSHOT")
}
```

Maven이면 같은 좌표를 `groupId` `systems.zlink`로 적는다.

## 2. 애플리케이션에 얹기

`@EnableZLinkFramework`가 자동 구성을 켜고, `ZLinkFrameworkConfigurer` bean이 무엇을
등록할지 정한다.

```java
@EnableZLinkFramework
@SpringBootApplication
public class ApiServerApplication {

    public static void main(String[] args) {
        SpringApplication.run(ApiServerApplication.class, args);
    }

    @Bean
    ZLinkFrameworkConfigurer zlink(ApiSettings settings) {
        return options -> {
            // 이 package 아래의 handler를 찾아 등록한다.
            options.addHandlersFromPackageOf(ApiServerApplication.class);

            // 이 process가 호출하는 쪽이다 — Play 서버에 연결만 한다.
            ZLinkMeshNodeBuilder mesh = options.addRouteMesh("play");
            mesh.listen(settings.routeEndpoint())
                .setRoutingIdPrefix("tictactoe-api");
            mesh.channelName("play.game").client();
            mesh.peerConnections().connect(settings.playEndpoint());
        };
    }
}
```

**`ZLinkFrameworkConfigurer`는 람다 하나다.** `options`를 받아 등록하고 끝낸다.
Spring bean이므로 다른 bean(여기서는 `ApiSettings`)을 생성자 인자로 받을 수 있다.

## 3. 받는 쪽 — handler

handler는 interface를 구현한 평범한 class다. `@ZLinkHandlerGroup`으로 묶고, 어느
channel에 노출할지는 등록이 정한다.

```java
@ZLinkHandlerGroup("play")
public final class CreateGameHandler
    implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {

    private final GameStore games;   // 생성자 주입 — Spring 컨테이너에서 온다.

    public CreateGameHandler(GameStore games) {
        this.games = games;
    }

    @Override
    public CompletionStage<CreateGameRes> handle(
        CreateGameReq request, ZLinkMessageContext context) {
        return games.create(request.gameName())
            .thenApply(game -> new CreateGameRes(game.id()));
    }
}
```

**handler class에 `@Component`를 붙이지 않는다.** `addHandlersFromPackageOf(...)`가
찾아 등록하고, 생성자 인자만 Spring 컨테이너에서 주입된다. 둘 다 붙이면 bean이 중복
등록된다.

Play 서버는 같은 channel을 **server**로 열고 handler group을 붙인다.

```java
ZLinkMeshNodeBuilder mesh = options.addRouteMesh("play");
mesh.listen(settings.routeEndpoint())
    .setRoutingIdPrefix("tictactoe-play");
mesh.channelName("play.game").server()
    .addHandlerGroup("play");
```

## 4. 보내는 쪽 — client 주입

`ZLinkRouteClient`를 bean으로 주입받아 ChannelName으로 부른다. 주소도 MeshName도
넘기지 않는다.

```java
@RestController
public class GameController {

    private final ZLinkRouteClient client;

    public GameController(ZLinkRouteClient client) {
        this.client = client;
    }

    @PostMapping("/games")
    public CompletionStage<CreateGameRes> create(@RequestBody CreateGameReq request) {
        return client
            .requestToChannel("play.game", request)
            .timeout(Duration.ofSeconds(3))
            .submit(CreateGameRes.class);
    }
}
```

`submit(...)`이 `CompletionStage<T>`를 돌려준다. Spring MVC가 그대로 받아 비동기로
응답하므로 `join()`으로 막지 않는다.

## 5. 실행과 확인

```bash
framework/languages/java/samples/java/TicTacToe/run_sample.sh
```

runner가 서버 여러 개와 client 시나리오를 함께 띄우고 검증까지 한다. Redis가 필요한
샘플은 runner가 컨테이너를 직접 띄우고 끝나면 정리하므로 `docker`만 있으면 된다.

동작을 확인하는 순서는 셋이다.

1. **기동 로그** — 등록이 잘못되면 첫 호출까지 미루지 않고 Spring 컨텍스트 시작에서
   실패한다. 예외 메시지가 어느 channel·node 때문인지 알려 준다.
2. **client의 exit code** — 시나리오 성공 여부의 판정 기준이다.
3. **서버 로그의 dispatch 오류** — client가 통과해도 서버가 오류를 기록할 수 있다.

## 6. 다음에 볼 것

| 하려는 것 | 볼 장 |
| --- | --- |
| 개념을 먼저 잡기 | [3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md) |
| 요청 방식 세 가지(request · send · publish) | [5. Channel Messaging](../../../common/guide/server/05-channel-messaging.ko.md) |
| 방·세션 상태를 담기 | [6. Spot](../../../common/guide/server/06-spot.ko.md) |
| client 실시간 연결 | [9. STREAM](../../../common/guide/server/09-stream.ko.md) |
| 설정 값 목록 | `16. Options` 장 |

## 7. 관련 문서

- 정식 계약: [Java configuration과 host 공개 계약](../../../common/spec/server/languages/java/interfaces/configuration-host.ko.md)
- 이전 장: [1. 개요](01-overview.ko.md)
- 다음 장: [3. 핵심 개념](../../../common/guide/server/03-concepts.ko.md)
- 샘플 전체: [14. 샘플 고르기](../../../common/guide/server/14-samples.ko.md)
