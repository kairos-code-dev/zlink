package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;

/** Owns the transfer-only backlog and bounded source-retirement state. */
final class ZLinkActorTransferHandoff {
    private static final ScheduledExecutorService RETIREMENTS =
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
    private final AtomicLong forwardingToken = new AtomicLong();

    void begin(String actorId) {
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

    void retain(
        String actorId,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        Duration window,
        Consumer<ForwardingSource> retirement) {
        ForwardingSource source = new ForwardingSource(
            sourceActorRef, targetActorRef, forwardingToken.incrementAndGet());
        forwardingSources.put(actorId, source);
        RETIREMENTS.schedule(
            () -> {
                forwardingSources.remove(actorId, source);
                retirement.accept(source);
            },
            window.toMillis(),
            TimeUnit.MILLISECONDS);
    }

    Optional<ForwardingSource> forwardingSource(String actorId) {
        return Optional.ofNullable(forwardingSources.get(actorId));
    }

    int forwardingSourceCount() {
        return forwardingSources.size();
    }

    record ForwardingSource(
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        long token) {
    }
}
