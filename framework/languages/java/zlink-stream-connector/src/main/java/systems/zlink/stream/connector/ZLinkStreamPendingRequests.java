package systems.zlink.stream.connector;

import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

final class ZLinkStreamPendingRequests {
    private final Map<Long, CompletableFuture<ZLinkStreamEncodedPayload>> requests =
        new ConcurrentHashMap<>();

    CompletableFuture<ZLinkStreamEncodedPayload> add(
        long requestSeq,
        Duration timeout,
        ScheduledExecutorService scheduler) {
        CompletableFuture<ZLinkStreamEncodedPayload> pending = new CompletableFuture<>();
        requests.put(requestSeq, pending);
        var timeoutTask = scheduler.schedule(() -> {
            if (requests.remove(requestSeq) != null) {
                pending.completeExceptionally(
                    new TimeoutException("request timed out after " + timeout));
            }
        }, timeout.toMillis(), TimeUnit.MILLISECONDS);
        pending.whenComplete((reply, ex) -> timeoutTask.cancel(false));
        return pending;
    }

    void complete(long requestSeq, ZLinkStreamEncodedPayload payload) {
        CompletableFuture<ZLinkStreamEncodedPayload> pending = requests.remove(requestSeq);
        if (pending != null) {
            pending.complete(payload);
        }
    }

    void fail(long requestSeq, Throwable ex) {
        CompletableFuture<ZLinkStreamEncodedPayload> pending = requests.remove(requestSeq);
        if (pending != null) {
            pending.completeExceptionally(ex);
        }
    }

    void failAll(Throwable ex) {
        for (Map.Entry<Long, CompletableFuture<ZLinkStreamEncodedPayload>> entry
            : requests.entrySet()) {
            if (requests.remove(entry.getKey()) != null) {
                entry.getValue().completeExceptionally(ex);
            }
        }
    }
}
