package systems.zlink.framework.execution;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.Objects;
import java.util.function.Supplier;

public final class ZLinkAsyncSerialQueue {
    private CompletionStage<Void> tail = CompletableFuture.completedFuture(null);

    public synchronized CompletionStage<Void> enqueue(Supplier<CompletionStage<Void>> operation) {
        CompletionStage<Void> scheduled = tail
            .handle((ignored, error) -> null)
            .thenCompose(ignored -> {
                try {
                    return Objects.requireNonNull(operation.get(), "operation result");
                } catch (RuntimeException ex) {
                    return CompletableFuture.failedFuture(ex);
                }
            });
        tail = scheduled.handle((ignored, error) -> null);
        return scheduled;
    }
}
