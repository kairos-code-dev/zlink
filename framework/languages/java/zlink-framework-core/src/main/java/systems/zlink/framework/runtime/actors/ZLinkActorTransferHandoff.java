package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.internal.spots.SpotTransportAddress;

/** Owns the transfer-only backlog and bounded source-retirement state. */
final class ZLinkActorTransferHandoff implements AutoCloseable {
    static final int MAX_FORWARDING_MESSAGES = 1024;
    static final long MAX_FORWARDING_BYTES = 16L * 1024L * 1024L;
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
        retain(actorId, sourceActorRef, targetActorRef, null, window, retirement);
    }

    synchronized void retain(
        String actorId,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        SpotTransportAddress targetAddress,
        Duration window,
        Consumer<ForwardingSource> retirement) {
        requireOpen();
        ForwardingSource source = new ForwardingSource(
            sourceActorRef, targetActorRef, targetAddress,
            forwardingToken.incrementAndGet());
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

    <T> CompletionStage<T> forward(
        String actorId,
        long objectGeneration,
        long payloadBytes,
        java.util.function.Supplier<CompletionStage<T>> submission) {
        ForwardingSource source = forwardingSources.get(actorId);
        if (source == null
            || source.sourceActorRef().generation() != objectGeneration
            || source.targetActorRef().generation() != objectGeneration) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException(
                    "committed forwarding mapping is unavailable or stale"));
        }
        if (!source.tryAcquire(payloadBytes)) {
            return java.util.concurrent.CompletableFuture.failedFuture(
                new IllegalStateException(
                    "committed forwarding mapping queue exceeds 1024 messages or 16 MiB"));
        }
        CompletionStage<T> submitted;
        try {
            submitted = java.util.Objects.requireNonNull(
                submission.get(), "forwarding submission returned null");
        } catch (RuntimeException failure) {
            source.release(payloadBytes);
            return java.util.concurrent.CompletableFuture.failedFuture(failure);
        }
        return submitted.whenComplete(
            (ignored, failure) -> source.release(payloadBytes));
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

    static final class ForwardingSource {
        private final ZLinkBackendActorRef sourceActorRef;
        private final ZLinkBackendActorRef targetActorRef;
        private final SpotTransportAddress targetAddress;
        private final long token;
        private int pendingMessages;
        private long pendingBytes;

        private ForwardingSource(
            ZLinkBackendActorRef sourceActorRef,
            ZLinkBackendActorRef targetActorRef,
            SpotTransportAddress targetAddress,
            long token) {
            this.sourceActorRef = java.util.Objects.requireNonNull(
                sourceActorRef, "sourceActorRef");
            this.targetActorRef = java.util.Objects.requireNonNull(
                targetActorRef, "targetActorRef");
            this.targetAddress = targetAddress;
            this.token = token;
        }

        ZLinkBackendActorRef sourceActorRef() { return sourceActorRef; }
        ZLinkBackendActorRef targetActorRef() { return targetActorRef; }
        SpotTransportAddress targetAddress() { return targetAddress; }
        long token() { return token; }

        private synchronized boolean tryAcquire(long bytes) {
            if (bytes < 0 || bytes > MAX_FORWARDING_BYTES
                || pendingMessages >= MAX_FORWARDING_MESSAGES
                || pendingBytes + bytes > MAX_FORWARDING_BYTES) {
                return false;
            }
            pendingMessages++;
            pendingBytes += bytes;
            return true;
        }

        private synchronized void release(long bytes) {
            pendingMessages--;
            pendingBytes -= bytes;
        }

        synchronized int pendingMessages() { return pendingMessages; }
        synchronized long pendingBytes() { return pendingBytes; }
    }
}
