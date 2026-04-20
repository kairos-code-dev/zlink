[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework Spring Boot STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Spring Boot`에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 1. 방향

`STREAM`은 일반 request handler와 다른 전용 handler 그룹으로 설명한다.

- packet handler
- typed header packet handler
- raw handler

recv loop를 application 표면에 직접 노출하지 않는 편을 기본으로 본다.

## 2. 등록

```java
@Configuration
public class StreamConfig {
    @Bean
    ZLinkStreamCustomizer gameStream() {
        return options -> options.bind("tcp://0.0.0.0:7201");
    }
}
```

## 3. Handler

```java
@Component
public final class GameStreamHandlers implements ZLinkStreamPacketHandler<RouteHeader> {
    @Override
    public CompletionStage<Void> handleAsync(
        RouteHeader header,
        Message body,
        ZLinkStreamContext context) {
        return CompletableFuture.completedFuture(null);
    }
}
```

header만 typed로 올리고 body는 raw `Message`로 두는 편을 기본으로 본다.
