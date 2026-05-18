<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Java Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md) | [다음: Java STREAM Open Items](stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework Spring Boot STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 1. 방향

`STREAM`은 일반 request handler와 다른 전용 session 그룹으로 설명한다.

- packet session
- raw session

recv loop를 application 표면에 직접 노출하지 않는 편을 기본으로 본다.

## 2. 등록

```java
@Configuration
public class StreamConfig {
    @Bean
    ZLinkStreamCustomizer gameStream() {
        return options -> {
            options.bind("tcp://0.0.0.0:7201");
            options.addPacketSession(GameStreamSession.class);
        };
    }
}
```

## 3. Session

```java
@Component
public final class GameStreamSession implements ZLinkPacketStreamSession {
    @Override
    public CompletionStage<Void> onPacketAsync(
        ZLinkStream stream,
        Message header,
        Message payload) {
        return CompletableFuture.completedFuture(null);
    }
}
```

stream 객체가 write와 peer metadata를 같이 들고 있고, packet/raw path는 session
lifecycle 위에서 설명하는 편을 기본으로 본다.
