package systems.zlink.stream.connector;

import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.Queue;

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

    void drain() {
        while (true) {
            QueuedDispatch next;
            synchronized (queue) {
                next = queue.poll();
                if (next != null) {
                    decrementReceivedCount(next.packetName());
                }
            }
            if (next == null) {
                return;
            }
            next.action().run();
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

    private record QueuedDispatch(String packetName, Runnable action) {
    }
}
