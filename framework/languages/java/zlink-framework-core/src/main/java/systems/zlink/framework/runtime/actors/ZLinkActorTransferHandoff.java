package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;

/** Owns the transfer-only backlog and bounded source-retirement state. */
final class ZLinkActorTransferHandoff implements AutoCloseable {
    private final ScheduledExecutorService retirementsExecutor =
        Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-actor-transfer-retirement");
            thread.setDaemon(true);
            return thread;
        });

    private final Map<String, List<ZLinkActorHandoffPacket>> backlogs =
        new ConcurrentHashMap<>();
    private final AtomicLong arrivalIndex = new AtomicLong();
    private final Map<String, ForwardingSource> forwardingSources =
        new ConcurrentHashMap<>();
    private final java.util.Set<Retention> retirements = ConcurrentHashMap.newKeySet();
    private final AtomicLong forwardingToken = new AtomicLong();
    private boolean closed;

    synchronized void begin(String actorId) {
        requireOpen();
        backlogs.put(actorId, Collections.synchronizedList(new ArrayList<>()));
    }

    ZLinkActorHandoffPacket capture(
        String actorId,
        ZLinkStreamHeader header,
        Message payload,
        ZLinkActorReplyRoute replyRoute) {
        List<ZLinkActorHandoffPacket> backlog = backlogs.get(actorId);
        if (backlog == null) {
            return null;
        }
        ZLinkActorHandoffPacket packet =
            new ZLinkActorHandoffPacket(
                arrivalIndex.incrementAndGet(), header, payload, replyRoute);
        synchronized (backlog) {
            if (backlogs.get(actorId) != backlog) {
                packet.close();
                return null;
            }
            backlog.add(packet);
        }
        return packet;
    }

    List<ZLinkActorHandoffPacket> take(String actorId) {
        List<ZLinkActorHandoffPacket> backlog = backlogs.get(actorId);
        if (backlog == null) {
            return List.of();
        }
        synchronized (backlog) {
            List<ZLinkActorHandoffPacket> snapshot = List.copyOf(backlog);
            backlog.clear();
            return snapshot;
        }
    }

    int pendingCount(String actorId) {
        List<ZLinkActorHandoffPacket> backlog = backlogs.get(actorId);
        if (backlog == null) {
            return 0;
        }
        synchronized (backlog) {
            return backlog.size();
        }
    }

    List<ZLinkActorHandoffPacket> finish(String actorId) {
        List<ZLinkActorHandoffPacket> backlog = backlogs.remove(actorId);
        if (backlog == null) {
            return List.of();
        }
        synchronized (backlog) {
            return List.copyOf(backlog);
        }
    }

    void fail(String actorId, Throwable error) {
        List<ZLinkActorHandoffPacket> backlog = backlogs.remove(actorId);
        if (backlog == null) {
            return;
        }
        synchronized (backlog) {
            backlog.forEach(packet -> {
                if (packet.fail(error)) {
                    packet.close();
                }
            });
        }
    }

    synchronized void retain(
        String actorId,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        Duration window,
        Consumer<ForwardingSource> retirement) {
        requireOpen();
        ForwardingSource source = new ForwardingSource(
            sourceActorRef, targetActorRef, forwardingToken.incrementAndGet());
        forwardingSources.put(actorId, source);
        Retention retained = new Retention(actorId, source, retirement);
        retirements.add(retained);
        ScheduledFuture<?> future = retirementsExecutor.schedule(
            () -> retire(retained),
            window.toMillis(),
            TimeUnit.MILLISECONDS);
        retained.future(future);
    }

    Optional<ForwardingSource> forwardingSource(String actorId) {
        return Optional.ofNullable(forwardingSources.get(actorId));
    }

    synchronized Optional<ForwardingSource> takeForwardingSource(String actorId) {
        ForwardingSource source = forwardingSources.remove(actorId);
        if (source == null) {
            return Optional.empty();
        }
        Retention retained = retirements.stream()
            .filter(candidate -> candidate.actorId().equals(actorId)
                && candidate.source().equals(source))
            .findFirst()
            .orElse(null);
        if (retained != null && retirements.remove(retained)) {
            ScheduledFuture<?> future = retained.future();
            if (future != null) {
                future.cancel(false);
            }
        }
        return Optional.of(source);
    }

    int forwardingSourceCount() {
        return forwardingSources.size();
    }

    @Override
    public synchronized void close() {
        if (closed) {
            return;
        }
        closed = true;
        retirementsExecutor.shutdownNow();
        List.copyOf(retirements).forEach(retained -> {
            ScheduledFuture<?> future = retained.future();
            if (future != null) {
                future.cancel(false);
            }
            try {
                retire(retained);
            } catch (RuntimeException ignored) {
                // Runtime shutdown must continue retiring the remaining owned sources.
            }
        });
        backlogs.keySet().forEach(actorId -> fail(
            actorId, new IllegalStateException("Actor runtime closed during transfer.")));
    }

    private void retire(Retention retained) {
        if (!retirements.remove(retained)) {
            return;
        }
        forwardingSources.remove(retained.actorId(), retained.source());
        retained.retirement().accept(retained.source());
    }

    private void requireOpen() {
        if (closed) {
            throw new IllegalStateException("Actor transfer handoff is closed.");
        }
    }

    private static final class Retention {
        private final String actorId;
        private final ForwardingSource source;
        private final Consumer<ForwardingSource> retirement;
        private volatile ScheduledFuture<?> future;

        private Retention(
            String actorId,
            ForwardingSource source,
            Consumer<ForwardingSource> retirement) {
            this.actorId = actorId;
            this.source = source;
            this.retirement = retirement;
        }

        String actorId() {
            return actorId;
        }

        ForwardingSource source() {
            return source;
        }

        Consumer<ForwardingSource> retirement() {
            return retirement;
        }

        ScheduledFuture<?> future() {
            return future;
        }

        void future(ScheduledFuture<?> future) {
            this.future = future;
        }
    }

    record ForwardingSource(
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        long token) {
    }
}
