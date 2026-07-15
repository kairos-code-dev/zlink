package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;

final class DefaultZLinkStreamSequenceCall implements ZLinkStreamSequenceCall {
    private final ZLinkStreamConnector connector;
    private final String name;
    private final Duration timeout;
    private final ZLinkStreamTypedCodec codec;
    private final List<Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>> predicates;

    DefaultZLinkStreamSequenceCall(
        ZLinkStreamConnector connector,
        String name,
        Duration timeout,
        ZLinkStreamTypedCodec codec) {
        this(connector, name, timeout, codec, List.of());
    }

    private DefaultZLinkStreamSequenceCall(
        ZLinkStreamConnector connector,
        String name,
        Duration timeout,
        ZLinkStreamTypedCodec codec,
        List<Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>> predicates) {
        this.connector = Objects.requireNonNull(connector, "connector");
        this.name = Objects.requireNonNull(name, "name");
        this.timeout = Objects.requireNonNull(timeout, "timeout");
        this.codec = codec;
        this.predicates = List.copyOf(predicates);
    }

    @Override
    public ZLinkStreamSequenceCall expect(
        Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate) {
        Objects.requireNonNull(predicate, "predicate");
        List<Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>> next =
            new ArrayList<>(predicates);
        next.add(predicate);
        return new DefaultZLinkStreamSequenceCall(connector, name, timeout, codec, next);
    }

    @Override
    public <TPayload> ZLinkStreamSequenceCall expect(
        Class<TPayload> payloadType,
        Predicate<ZLinkStreamMessage<TPayload>> predicate) {
        Objects.requireNonNull(payloadType, "payloadType");
        Objects.requireNonNull(predicate, "predicate");
        if (codec == null) {
            throw new IllegalStateException(
                "typed stream payload API requires ZLinkStreamConnectorOptions.typedCodec");
        }
        return expect(message -> predicate.test(decodeMessage(message, payloadType)));
    }

    @Override
    public ZLinkStreamSequenceCall timeout(Duration timeout) {
        Objects.requireNonNull(timeout, "timeout");
        if (timeout.isNegative()) {
            throw new IllegalArgumentException("timeout must not be negative");
        }
        return new DefaultZLinkStreamSequenceCall(connector, name, timeout, codec, predicates);
    }

    @Override
    public CompletionStage<List<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>> submit() {
        if (predicates.isEmpty()) {
            throw new IllegalStateException("waitForSequence requires at least one expectation");
        }
        CompletableFuture<List<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>> result =
            new CompletableFuture<>();
        List<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> messages = new ArrayList<>();
        Object sequenceLock = new Object();
        AutoCloseable subscription = connector.on(name, message -> {
            synchronized (sequenceLock) {
                if (result.isDone()) {
                    message.payload().payload().close();
                } else try {
                    int current = messages.size();
                    if (!predicates.get(current).test(message)) {
                        message.payload().payload().close();
                        result.completeExceptionally(new IllegalStateException(
                            "Message '" + name + "' arrived out of the expected sequence."));
                    } else {
                        messages.add(message);
                        if (messages.size() == predicates.size()) {
                            result.complete(List.copyOf(messages));
                        }
                    }
                } catch (RuntimeException error) {
                    message.payload().payload().close();
                    result.completeExceptionally(error);
                }
            }
            return CompletableFuture.completedFuture(null);
        });
        result.whenComplete((ignored, error) -> {
            closeQuietly(subscription);
            if (error != null) {
                synchronized (sequenceLock) {
                    messages.forEach(item -> item.payload().payload().close());
                }
            }
        });
        return result.orTimeout(timeout.toMillis(), TimeUnit.MILLISECONDS);
    }

    @Override
    public <TPayload> CompletionStage<List<ZLinkStreamMessage<TPayload>>> submit(
        Class<TPayload> payloadType) {
        Objects.requireNonNull(payloadType, "payloadType");
        if (codec == null) {
            throw new IllegalStateException(
                "typed stream payload API requires ZLinkStreamConnectorOptions.typedCodec");
        }
        return submit().thenApply(messages -> messages.stream()
            .map(message -> {
                try {
                    return decodeMessage(message, payloadType);
                } finally {
                    message.payload().payload().close();
                }
            })
            .toList());
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
        try {
            closeable.close();
        } catch (Exception ignored) {
        }
    }
}
