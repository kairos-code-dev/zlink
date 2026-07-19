package systems.zlink.framework.runtime.binding;

import java.util.Objects;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.function.IntUnaryOperator;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;

/**
 * Converts native ready callbacks into serialized pull-dispatch work.
 *
 * <p>The native callback only records readable domains and schedules the pump.
 * It never drains a claim or invokes application code.
 */
final class ZLinkJavaMeshDispatchPump implements AutoCloseable {
    interface Source extends AutoCloseable {
        void setReadyHandler(IntUnaryOperator handler);

        boolean drain(int domains, Consumer<ZLinkMeshDispatchRecord> receiver);

        @Override
        void close();
    }

    private final Source source;
    private final Consumer<ZLinkMeshDispatchRecord> receiver;
    private final ExecutorService executor;
    private final AtomicInteger pendingDomains = new AtomicInteger();
    private final AtomicBoolean scheduled = new AtomicBoolean();
    private final AtomicBoolean closed = new AtomicBoolean();

    ZLinkJavaMeshDispatchPump(
        Source source,
        Consumer<ZLinkMeshDispatchRecord> receiver) {
        this(
            source,
            receiver,
            Executors.newSingleThreadExecutor(Thread.ofVirtual()
                .name("zlink-mesh-dispatch-", 0)
                .factory()));
    }

    ZLinkJavaMeshDispatchPump(
        Source source,
        Consumer<ZLinkMeshDispatchRecord> receiver,
        ExecutorService executor) {
        this.source = Objects.requireNonNull(source, "source");
        this.receiver = Objects.requireNonNull(receiver, "receiver");
        this.executor = Objects.requireNonNull(executor, "executor");
        source.setReadyHandler(this::onReady);
    }

    private int onReady(int domains) {
        if (closed.get()) {
            return 0;
        }
        pendingDomains.getAndUpdate(current -> current | domains);
        schedule();
        return domains;
    }

    private void schedule() {
        if (scheduled.compareAndSet(false, true)) {
            executor.execute(this::run);
        }
    }

    private void run() {
        try {
            while (!closed.get()) {
                int domains = pendingDomains.getAndSet(0);
                if (domains == 0) {
                    break;
                }
                boolean residue;
                do {
                    residue = source.drain(domains, receiver);
                } while (residue && !closed.get());
            }
        } finally {
            scheduled.set(false);
            if (!closed.get() && pendingDomains.get() != 0) {
                schedule();
            }
        }
    }

    @Override
    public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        source.setReadyHandler(null);
        executor.shutdown();
        boolean interrupted = false;
        try {
            if (!executor.awaitTermination(5, TimeUnit.SECONDS)) {
                executor.shutdownNow();
                executor.awaitTermination(5, TimeUnit.SECONDS);
            }
        } catch (InterruptedException interruption) {
            interrupted = true;
            executor.shutdownNow();
        }
        source.close();
        if (interrupted) {
            Thread.currentThread().interrupt();
        }
    }
}
