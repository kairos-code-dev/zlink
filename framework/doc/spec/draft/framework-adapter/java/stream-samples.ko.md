[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Java STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM` 초안을 코드 흐름으로 보기 위한 샘플 문서다.

## 1. Packet handler

```java
@Component
public final class RouteHandlers implements ZLinkStreamPacketHandler<RouteHeader> {
    @Override
    public CompletionStage<Void> handleAsync(
        RouteHeader header,
        Message body,
        ZLinkStreamContext context) {
        return CompletableFuture.completedFuture(null);
    }
}
```

## 2. Raw handler

```java
@Component
public final class RawHandlers implements ZLinkStreamRawHandler {
    @Override
    public CompletionStage<Void> handleAsync(
        Message payload,
        ZLinkStreamContext context) {
        return CompletableFuture.completedFuture(null);
    }
}
```

`STREAM`은 recv loop를 직접 드러내기보다 handler registration으로 설명하는 편을
기본으로 본다.
