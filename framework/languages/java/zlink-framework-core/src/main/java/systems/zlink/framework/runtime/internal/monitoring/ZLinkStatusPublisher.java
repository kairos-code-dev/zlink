package systems.zlink.framework.runtime.internal.monitoring;

import java.util.Objects;
import java.util.concurrent.Flow;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.ArrayDeque;

public final class ZLinkStatusPublisher<T> implements Flow.Publisher<T> {
    private static final long POLL_NANOS = 25_000_000L;

    private final Supplier<T> snapshot;
    private final Function<T, Object> fingerprint;
    private final Predicate<T> terminal;
    private final Predicate<T> preserve;

    private ZLinkStatusPublisher(
        Supplier<T> snapshot,
        Function<T, Object> fingerprint,
        int capacity,
        Predicate<T> terminal,
        Predicate<T> preserve) {
        if (capacity <= 0) {
            throw new IllegalArgumentException("capacity must be positive");
        }
        this.snapshot = Objects.requireNonNull(snapshot, "snapshot");
        this.fingerprint = Objects.requireNonNull(fingerprint, "fingerprint");
        this.terminal = Objects.requireNonNull(terminal, "terminal");
        this.preserve = Objects.requireNonNull(preserve, "preserve");
    }

    public static <T> Flow.Publisher<T> create(
        Supplier<T> snapshot,
        Function<T, Object> fingerprint,
        int capacity) {
        return new ZLinkStatusPublisher<>(
            snapshot, fingerprint, capacity, ignored -> false, ignored -> false);
    }

    public static <T> Flow.Publisher<T> create(
        Supplier<T> snapshot,
        Function<T, Object> fingerprint,
        int capacity,
        Predicate<T> terminal) {
        return new ZLinkStatusPublisher<>(
            snapshot, fingerprint, capacity, terminal, ignored -> false);
    }

    public static <T> Flow.Publisher<T> create(
        Supplier<T> snapshot,
        Function<T, Object> fingerprint,
        int capacity,
        Predicate<T> terminal,
        Predicate<T> preserve) {
        return new ZLinkStatusPublisher<>(
            snapshot, fingerprint, capacity, terminal, preserve);
    }

    @Override
    public void subscribe(Flow.Subscriber<? super T> subscriber) {
        Objects.requireNonNull(subscriber, "subscriber");
        SnapshotSubscription subscription = new SnapshotSubscription(subscriber);
        subscriber.onSubscribe(subscription);
        subscription.start();
    }

    private final class SnapshotSubscription implements Flow.Subscription {
        private final Flow.Subscriber<? super T> subscriber;
        private final AtomicLong demand = new AtomicLong();
        private final AtomicBoolean cancelled = new AtomicBoolean();

        SnapshotSubscription(Flow.Subscriber<? super T> subscriber) {
            this.subscriber = subscriber;
        }

        void start() {
            Thread.ofVirtual().name("zlink-status-observer").start(this::pump);
        }

        @Override
        public void request(long count) {
            if (count <= 0) {
                cancel();
                subscriber.onError(new IllegalArgumentException(
                    "subscription demand must be positive"));
                return;
            }
            demand.getAndUpdate(current -> {
                long next = current + count;
                return next < 0 ? Long.MAX_VALUE : next;
            });
        }

        @Override
        public void cancel() {
            cancelled.set(true);
        }

        private void pump() {
            Object previous = null;
            ArrayDeque<T> pending = new ArrayDeque<>();
            try {
                while (!cancelled.get()) {
                    T current = snapshot.get();
                    Object currentFingerprint = fingerprint.apply(current);
                    if (!Objects.equals(previous, currentFingerprint)) {
                        if (preserve.test(current)) {
                            pending.addLast(current);
                        } else if (pending.isEmpty()) {
                            pending.addLast(current);
                        } else if (!preserve.test(pending.peekLast())) {
                            pending.removeLast();
                            pending.addLast(current);
                        } else {
                            pending.addLast(current);
                        }
                        previous = currentFingerprint;
                    }
                    if (!pending.isEmpty() && demand.get() > 0) {
                        T delivered = pending.removeFirst();
                        if (demand.get() != Long.MAX_VALUE) {
                            demand.decrementAndGet();
                        }
                        subscriber.onNext(delivered);
                        if (terminal.test(delivered)) {
                            cancelled.set(true);
                            subscriber.onComplete();
                            return;
                        }
                    }
                    LockSupport.parkNanos(POLL_NANOS);
                }
            } catch (Throwable failure) {
                if (!cancelled.get()) {
                    subscriber.onError(failure);
                }
            }
        }
    }
}
