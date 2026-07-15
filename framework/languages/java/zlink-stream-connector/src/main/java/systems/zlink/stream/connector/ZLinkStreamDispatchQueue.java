package systems.zlink.stream.connector;

import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;

final class ZLinkStreamDispatchQueue {
    private final Queue<QueuedDispatch> queue = new ArrayDeque<>();
    private final Map<String, Integer> receivedCounts = new HashMap<>();
    private final int maxReceivedMessages;

    ZLinkStreamDispatchQueue(int maxReceivedMessages) {
        this.maxReceivedMessages = maxReceivedMessages;
    }

    int size() {
        synchronized (queue) {
            return queue.size();
        }
    }

    void add(Runnable item) {
        add(null, item);
    }

    void add(String packetName, Runnable item) {
        addAsync(packetName, () -> {
            item.run();
            return CompletableFuture.completedFuture(null);
        });
    }

    void addAsync(Supplier<CompletionStage<Void>> item) {
        addAsync(null, item);
    }

    void addAsync(String packetName, Supplier<CompletionStage<Void>> item) {
        synchronized (queue) {
            if (packetName != null && receivedMessageCount() >= maxReceivedMessages) {
                dropOldestReceivedMessage();
            }
            queue.add(new QueuedDispatch(packetName, item));
            if (packetName != null) {
                receivedCounts.merge(packetName, 1, Integer::sum);
            }
        }
    }

    void clear() {
        synchronized (queue) {
            queue.clear();
            receivedCounts.clear();
        }
    }

    int receivedCount(String packetName) {
        synchronized (queue) {
            return receivedCounts.getOrDefault(packetName, 0);
        }
    }

    CompletionStage<Void> drainAsync() {
        QueuedDispatch next;
        synchronized (queue) {
            next = queue.poll();
            if (next != null) {
                decrementReceivedCount(next.packetName());
            }
        }
        if (next == null) {
            return CompletableFuture.completedFuture(null);
        }
        try {
            return next.action().get().thenCompose(ignored -> drainAsync());
        } catch (Throwable error) {
            return CompletableFuture.failedFuture(error);
        }
    }

    private int receivedMessageCount() {
        int total = 0;
        for (int value : receivedCounts.values()) {
            total += value;
        }
        return total;
    }

    private void dropOldestReceivedMessage() {
        Iterator<QueuedDispatch> iterator = queue.iterator();
        while (iterator.hasNext()) {
            QueuedDispatch item = iterator.next();
            if (item.packetName() != null) {
                iterator.remove();
                decrementReceivedCount(item.packetName());
                return;
            }
        }
    }

    private void decrementReceivedCount(String packetName) {
        if (packetName == null) {
            return;
        }
        int next = receivedCounts.getOrDefault(packetName, 0) - 1;
        if (next <= 0) {
            receivedCounts.remove(packetName);
        } else {
            receivedCounts.put(packetName, next);
        }
    }

    private record QueuedDispatch(
        String packetName,
        Supplier<CompletionStage<Void>> action) {
    }
}
