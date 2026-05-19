<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- Java STREAM Open Items](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Java STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java` `STREAM` 초안을 코드 흐름으로 보기 위한 샘플 문서다.

## 1. Packet session

```java
@Component
public final class RouteSession implements ZLinkPacketStreamSession {
    @Override
    public CompletionStage<Void> onPacketAsync(
        ZLinkStream stream,
        Message header,
        Message payload) {
        return CompletableFuture.completedFuture(null);
    }
}
```

## 2. Raw session

```java
@Component
public final class RawSession implements ZLinkRawStreamSession {
    @Override
    public CompletionStage<Void> onRawAsync(
        ZLinkStream stream,
        Message payload) {
        return CompletableFuture.completedFuture(null);
    }
}
```

`STREAM`은 recv loop를 직접 드러내기보다 session registration으로 설명하는 편을
기본으로 본다.
