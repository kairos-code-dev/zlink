[스펙 목차](../../../README.ko.md)

[Java 묶음](./README.ko.md) | [channel](./spring-boot-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework Java Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java`에서 `ZLink Framework`가 노출할 인터페이스와
> annotation을 한 곳에 모은 기준 문서다.

## 1. 인터페이스 전체 목록

| 분류 | 인터페이스 또는 타입 | 역할 |
|------|----------------------|------|
| context | `ZLinkHandlerContext` | 모든 handler context의 공통 기반 |
| handler | `ZLinkRequestHandler<TReq, TRep>` | request-response handler |
| handler | `ZLinkSendHandler<TMsg>` | one-way send handler |
| handler | `ZLinkEventHandler<TEvent>` | event handler |
| handler | `ZLinkStreamPacketHandler` | raw packet handler |
| handler | `ZLinkStreamPacketHandler<THeader>` | typed header packet handler |
| handler | `ZLinkStreamRawHandler` | raw stream payload handler |
| serializer | `ZLinkMessageSerializer` | payload codec 추상화 |
| client | `ZLinkClient` | channel messaging outbound client |
| client | `ZLinkSpotClient` | `SPOT` outbound client |
| client | `ZLinkEventPublisher` | event publisher |
| filter | `ZLinkHandlerFilter` | handler 전후 공통 처리 |
| marker | `ZLinkRequest<TReply>` | request/reply 타입 연결 marker |
| registry | `ZLinkRegistryQuery`, `ZLinkRegistryQueryClient` | registry 조회 |

## 2. Context

```java
public interface ZLinkHandlerContext {
    @Nullable String channelName();
    @Nullable String packetName();
    @Nullable String contentType();
    @Nullable String correlationId();
    @Nullable Instant deadline();
    ApplicationContext services();
}
```

파생 context는 아래처럼 나눈다.

- `ZLinkRequestContext`
- `ZLinkSendContext`
- `ZLinkEventContext`
- `ZLinkSpotRequestContext`
- `ZLinkSpotSubscriptionContext`
- `ZLinkStreamContext`

## 3. Handler

```java
public interface ZLinkRequestHandler<TRequest, TReply> {
    CompletionStage<TReply> handleAsync(
        TRequest request,
        ZLinkRequestContext context);
}

public interface ZLinkSendHandler<TMessage> {
    CompletionStage<Void> handleAsync(
        TMessage message,
        ZLinkSendContext context);
}

public interface ZLinkEventHandler<TEvent> {
    CompletionStage<Void> handleAsync(
        TEvent message,
        ZLinkEventContext context);
}
```

stream은 packet handler와 raw handler 두 축으로 본다.

```java
public interface ZLinkStreamPacketHandler {
    CompletionStage<Void> handleAsync(
        Message header,
        Message body,
        ZLinkStreamContext context);
}

public interface ZLinkStreamPacketHandler<THeader> {
    CompletionStage<Void> handleAsync(
        THeader header,
        Message body,
        ZLinkStreamContext context);
}

public interface ZLinkStreamRawHandler {
    CompletionStage<Void> handleAsync(
        Message payload,
        ZLinkStreamContext context);
}
```

## 4. Client 와 Options

```java
public final class ZLinkSendOptions {
    @Nullable private String packetName;
}

public final class ZLinkRequestOptions {
    @Nullable private String packetName;
    @Nullable private Duration timeout;
}

public interface ZLinkClient {
    <TMessage> CompletionStage<Void> sendAsync(
        String channelName,
        TMessage message,
        @Nullable ZLinkSendOptions options);

    <TReply> CompletionStage<TReply> requestAsync(
        String channelName,
        ZLinkRequest<TReply> request,
        @Nullable ZLinkRequestOptions options);
}
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packetName`
2. payload 타입의 `@ZLinkPacket`
3. payload 타입 `SimpleName`

## 5. Annotation

```java
@Target(ElementType.TYPE)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkPacket {
    String value();
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkRequestMapping {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkSendMapping {
    String packetName() default "";
}

@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface ZLinkEventMapping {
    String packetName() default "";
}
```

`SPOT`, `STREAM`용 annotation도 같은 방식으로 분리한다.

## 6. Filter

`Spring`의 `HandlerInterceptor`와 비슷한 공통 처리 층을 둔다.

```java
public interface ZLinkHandlerFilter {
    <T> CompletionStage<T> invokeAsync(
        ZLinkInvocationContext context,
        ZLinkNext<T> next);
}
```

## 7. 중요한 규칙

- 일반 channel messaging의 request/send dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- 같은 outbound channel은 자동 연결 또는 수동 연결 중 하나만 선택한다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
