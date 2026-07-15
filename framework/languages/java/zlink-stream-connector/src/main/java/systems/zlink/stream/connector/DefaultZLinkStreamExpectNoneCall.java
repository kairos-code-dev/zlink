package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;

final class DefaultZLinkStreamExpectNoneCall implements ZLinkStreamExpectNoneCall {
    private final ZLinkStreamConnector connector;
    private final String name;
    private final Duration window;

    DefaultZLinkStreamExpectNoneCall(ZLinkStreamConnector connector, String name) {
        this(connector, name, null);
    }

    private DefaultZLinkStreamExpectNoneCall(
        ZLinkStreamConnector connector,
        String name,
        Duration window) {
        this.connector = Objects.requireNonNull(connector, "connector");
        this.name = Objects.requireNonNull(name, "name");
        this.window = window;
    }

    @Override
    public ZLinkStreamExpectNoneCall within(Duration window) {
        Objects.requireNonNull(window, "window");
        if (window.isNegative()) {
            throw new IllegalArgumentException("window must not be negative");
        }
        return new DefaultZLinkStreamExpectNoneCall(connector, name, window);
    }

    @Override
    public CompletionStage<Void> submit() {
        if (window == null) {
            throw new IllegalStateException("expectNone requires within(window)");
        }
        CompletableFuture<Void> result = new CompletableFuture<>();
        AutoCloseable subscription = connector.on(name, message -> {
            message.payload().payload().close();
            result.completeExceptionally(new IllegalStateException(
                "Expected no '" + name + "' message within " + window + "."));
            return CompletableFuture.completedFuture(null);
        });
        result.whenComplete((ignored, error) -> closeQuietly(subscription));
        CompletableFuture.delayedExecutor(window.toMillis(), TimeUnit.MILLISECONDS)
            .execute(() -> result.complete(null));
        return result;
    }

    private static void closeQuietly(AutoCloseable closeable) {
        try {
            closeable.close();
        } catch (Exception ignored) {
        }
    }
}
