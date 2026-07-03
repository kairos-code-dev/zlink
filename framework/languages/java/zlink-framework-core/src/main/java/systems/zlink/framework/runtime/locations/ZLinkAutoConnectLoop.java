package systems.zlink.framework.runtime.locations;

import java.util.Objects;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.locations.ZLinkLocationOptions;

final class ZLinkAutoConnectLoop implements AutoCloseable {
    private final ZLinkAutoConnectReconciler reconciler;
    private final ZLinkLocationOptions options;
    private final ScheduledExecutorService executor;
    private ScheduledFuture<?> task;
    private volatile boolean running;

    ZLinkAutoConnectLoop(
        ZLinkAutoConnectReconciler reconciler,
        ZLinkLocationOptions options) {
        this.reconciler = Objects.requireNonNull(reconciler, "reconciler");
        this.options = Objects.requireNonNull(options, "options");
        this.executor = Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-location-auto-connect");
            thread.setDaemon(true);
            return thread;
        });
    }

    CompletionStage<Void> startAsync() {
        running = true;
        return tickAsync().whenComplete((ignored, failure) -> {
            if (running) {
                task = executor.scheduleWithFixedDelay(
                    this::tickOnLoop,
                    options.pollingInterval().toMillis(),
                    options.pollingInterval().toMillis(),
                    TimeUnit.MILLISECONDS);
            }
        });
    }

    CompletionStage<Void> stopAsync() {
        running = false;
        if (task != null) {
            task.cancel(false);
            task = null;
        }
        executor.shutdownNow();
        return reconciler.shutdownAsync();
    }

    private CompletionStage<Void> tickAsync() {
        return reconciler.tickAsync();
    }

    private void tickOnLoop() {
        if (!running) {
            return;
        }
        try {
            tickAsync().toCompletableFuture().join();
        } catch (RuntimeException ignored) {
            // The reconciler records store failures as fail-static ticks.
        }
    }

    @Override
    public void close() {
        stopAsync().toCompletableFuture().join();
        executor.close();
    }
}
