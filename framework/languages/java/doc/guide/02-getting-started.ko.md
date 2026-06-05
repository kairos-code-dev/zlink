<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Overview](./01-overview.ko.md) | [다음: Concepts](./03-concepts.ko.md)
<!-- framework-adapter-nav:end -->

# Java Getting Started

## 1. 의존성

첫 구현은 모듈을 아래처럼 나눈다.

```kotlin
dependencies {
    implementation("systems.zlink:zlink-framework-core")
    implementation("systems.zlink:zlink-framework-spring-boot-starter")
    implementation("systems.zlink:zlink-binding")
}
```

버전과 artifact id는 구현 시점의 빌드 정책에 맞춰 확정한다.

Spring Boot 앱이 아니면 `ZLinkFramework.start(...)` public facade로 같은 option
builder를 넘겨 framework host를 시작한다. sample runner는 이 경로를 사용해
channel request와 Spot manager가 실제 runtime을 통과하는지 확인한다.

## 2. 토폴로지

이 예제는 두 개의 Spring Boot 앱과 Registry 하나로 구성한다.

```mermaid
flowchart LR
  Caller["caller 앱<br/>(enableClient)"] -- "requestToChannel(\"price\", ...)" --> PriceServer["price-server 앱<br/>(enableServer + handler)"]
```

- `price-server` : `price` channel에 server capability를 열고 handler를 둔다.
- `caller` : `price` channel에 client capability만 열고 호출한다.

두 앱은 서로의 주소를 직접 모른다. 위치는 `Discovery`(또는 수동 연결)가 해결한다.

## 3. 공유 메시지 타입

두 앱이 공유하는 DTO다. 단순 record면 충분하다.

```java
public record PriceRequest(String symbol) {}
public record PriceReply(String symbol, double price) {}
```

## 4. price-server: handler + channel 등록

```java
@Component
public final class GetPriceHandler
    implements ZLinkRequestHandler<PriceRequest, PriceReply> {

    @Override
    public CompletionStage<PriceReply> handleAsync(
        PriceRequest request,
        ZLinkRequestContext context) {
        // 실제로는 시세 캐시/DB 조회. 여기서는 고정값.
        return CompletableFuture.completedFuture(
            new PriceReply(request.symbol(), 187.42));
    }
}

@Configuration
@EnableZLinkFramework
public class PriceServerConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addClientServerChannel("price", channel -> {
            channel.enableServer(server -> server.bind("tcp://0.0.0.0:7301"));
            channel.addRequestHandler(GetPriceHandler.class);
        });

        // 위치 해결: 같은 Registry를 가리키게 한다.
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:5551"));
    }
}
```

핵심: server capability는 `bind(...)`가 필수다. 다른 프로세스가 접근할 local
endpoint가 있어야 한다.

## 5. caller: outbound client

```java
@Configuration
@EnableZLinkFramework
public class CallerConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addClientServerChannel("price", channel -> channel.enableClient());
        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:5551"));
    }
}

@RestController
public final class PriceController {
    private final ZLinkClient client;

    public PriceController(ZLinkClient client) {
        this.client = client;
    }

    @GetMapping("/price/{symbol}")
    public CompletionStage<PriceReply> price(@PathVariable String symbol) {
        return client.requestToChannel("price", new PriceRequest(symbol))
            .timeout(Duration.ofSeconds(1))
            .submitAsync(PriceReply.class);
    }
}
```

`enableClient()`만 선언한 앱은 inbound handler 없이 outbound 전용으로 동작한다.
`bind(...)`는 필요 없다. `requestToChannel`은 target endpoint를 받지 않는다.
`Discovery`나 manual connection 설정이 peer acquisition을 담당한다. client
capability에 둘 다 없으면 startup validation 오류다.

## 6. Registry 띄우기

`Discovery`가 위치를 해결하려면 Registry 서버가 하나 떠 있어야 한다. 가장 간단한
방법은 별도 Registry 앱이다.

```java
@Configuration
public class RegistryConfig {
    @Bean
    ZLinkRegistryCustomizer registryCustomizer() {
        return registry -> {
            registry.setPubEndpoint("tcp://0.0.0.0:5550");
            registry.setRouterEndpoint("tcp://0.0.0.0:5551");
        };
    }
}
```

배포 모델(embedded/standalone)과 topology 조회는 [09-registry](./09-registry.ko.md)에서
다룬다. 수동 연결만으로 Registry 없이 붙이는 방법은
[channel 샘플 §2](./samples/channel-messaging-samples.ko.md)에 있다.

## 7. 실행과 확인

1. `registry` -> `price-server` -> `caller` 순으로 띄운다.
2. `caller`에 `GET /price/AAPL`을 호출한다.
3. `{ "symbol": "AAPL", "price": 187.42 }`가 돌아오면 Discovery 연결과 inbound
   handler dispatch까지 정상이다.

## 8. 다음 단계

- pub/sub는 [channel messaging](../spec/spring-boot-channel-messaging.ko.md)을 본다.
- room/stage/zone은 [Spot](../spec/spring-boot-spot.ko.md)을 본다.
- 외부 client는 [STREAM](../spec/spring-boot-stream.ko.md)과
  [Stream Connector](../spec/stream-connector.ko.md)를 본다.
