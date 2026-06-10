package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;

final class DefaultZLinkStreamWaitCall implements ZLinkStreamWaitCall {
    private final ZLinkStreamConnector connector;
    private final String name;
    private final Duration timeout;
    private final ZLinkStreamTypedCodec codec;
    private final Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate;

    DefaultZLinkStreamWaitCall(
        ZLinkStreamConnector connector,
        String name,
        Duration timeout,
        ZLinkStreamTypedCodec codec) {
        this(connector, name, timeout, codec, ignored -> true);
    }

    private DefaultZLinkStreamWaitCall(
        ZLinkStreamConnector connector,
        String name,
        Duration timeout,
        ZLinkStreamTypedCodec codec,
        Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate) {
        this.connector = Objects.requireNonNull(connector, "connector");
        this.name = Objects.requireNonNull(name, "name");
        this.timeout = Objects.requireNonNull(timeout, "timeout");
        this.codec = codec;
        this.predicate = Objects.requireNonNull(predicate, "predicate");
    }

    @Override
    public ZLinkStreamWaitCall timeout(Duration timeout) {
        return new DefaultZLinkStreamWaitCall(connector, name, timeout, codec, predicate);
    }

    @Override
    public ZLinkStreamWaitCall where(Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate) {
        Objects.requireNonNull(predicate, "predicate");
        return new DefaultZLinkStreamWaitCall(
            connector,
            name,
            timeout,
            codec,
            this.predicate.and(predicate));
    }

    @Override
    public <TPayload> ZLinkStreamWaitCall where(
        Class<TPayload> payloadType,
        Predicate<ZLinkStreamMessage<TPayload>> predicate) {
        Objects.requireNonNull(payloadType, "payloadType");
        Objects.requireNonNull(predicate, "predicate");
        if (codec == null) {
            throw new IllegalStateException(
                "typed stream payload API requires ZLinkStreamConnectorOptions.typedCodec");
        }
        return where(message -> predicate.test(decodeMessage(message, payloadType)));
    }

    @Override
    public CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit() {
        CompletableFuture<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> result =
            new CompletableFuture<>();
        AutoCloseable[] subscription = new AutoCloseable[1];
        subscription[0] = connector.on(name, message -> {
            try {
                if (predicate.test(message)) {
                    result.complete(message);
                }
            } catch (RuntimeException ex) {
                result.completeExceptionally(ex);
            }
            return CompletableFuture.completedFuture(null);
        });
        result.whenComplete((ignored, error) -> closeQuietly(subscription[0]));
        return result.orTimeout(timeout.toMillis(), TimeUnit.MILLISECONDS);
    }

    @Override
    public <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> submit(Class<TPayload> payloadType) {
        Objects.requireNonNull(payloadType, "payloadType");
        if (codec == null) {
            throw new IllegalStateException(
                "typed stream payload API requires ZLinkStreamConnectorOptions.typedCodec");
        }
        return submit().thenApply(message -> decodeMessage(message, payloadType));
    }

    private <TPayload> ZLinkStreamMessage<TPayload> decodeMessage(
        ZLinkStreamMessage<ZLinkStreamEncodedPayload> message,
        Class<TPayload> payloadType) {
        return new ZLinkStreamMessage<>(
            message.packetName(),
            codec.decode(message.payload(), payloadType),
            message.metadata());
    }

    private static void closeQuietly(AutoCloseable closeable) {
        if (closeable == null) {
            return;
        }
        try {
            closeable.close();
        } catch (Exception ignored) {
        }
    }
}
