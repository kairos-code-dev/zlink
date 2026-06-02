package systems.zlink.stream.connector;

import java.util.ArrayDeque;
import java.util.Queue;

final class ZLinkStreamDispatchQueue {
    private final Queue<Runnable> queue = new ArrayDeque<>();

    int size() {
        synchronized (queue) {
            return queue.size();
        }
    }

    void add(Runnable item) {
        synchronized (queue) {
            queue.add(item);
        }
    }

    void clear() {
        synchronized (queue) {
            queue.clear();
        }
    }

    void drain() {
        while (true) {
            Runnable next;
            synchronized (queue) {
                next = queue.poll();
            }
            if (next == null) {
                return;
            }
            next.run();
        }
    }
}
