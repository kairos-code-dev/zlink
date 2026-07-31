package systems.zlink.framework.runtime.binding;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.Socket;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorBindOperation;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendActorUnbindOperation;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendStreamErrorHandler;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendStreamPacketHandler;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;

final class ZLinkJavaStreamSocket implements ZLinkBackendStreamSocket, ZLinkJavaSocketBacked {
    private final StreamSocket socket;
    private final ZLinkJavaRawMeshNode meshNode;
    private final BoundSessionSink boundSessionSink;
    private final BoundSessionLifecycle boundSessionLifecycle;
    private final Runnable nativeClose;
    private final Map<BindingKey, SessionBinding> bindings =
        new ConcurrentHashMap<>();
    private final AtomicLong nextBoundSessionSequence =
        new AtomicLong(1);
    private final AtomicBoolean closed = new AtomicBoolean();
    private SocketMonitor monitor;
    private boolean sessionServiceStarted;

    ZLinkJavaStreamSocket(StreamSocket socket, ZLinkJavaRawMeshNode meshNode) {
        this(socket, meshNode, null, null, null);
    }

    ZLinkJavaStreamSocket(
        StreamSocket socket,
        ZLinkJavaRawMeshNode meshNode,
        BoundSessionSink boundSessionSink) {
        this(socket, meshNode, boundSessionSink, null, null);
    }

    ZLinkJavaStreamSocket(
        StreamSocket socket,
        ZLinkJavaRawMeshNode meshNode,
        BoundSessionSink boundSessionSink,
        BoundSessionLifecycle boundSessionLifecycle) {
        this(
            socket,
            meshNode,
            boundSessionSink,
            boundSessionLifecycle,
            null);
    }

    ZLinkJavaStreamSocket(
        StreamSocket socket,
        ZLinkJavaRawMeshNode meshNode,
        BoundSessionSink boundSessionSink,
        BoundSessionLifecycle boundSessionLifecycle,
        Runnable nativeClose) {
        this.socket = socket;
        this.meshNode = meshNode;
        this.boundSessionSink = boundSessionSink;
        this.boundSessionLifecycle = boundSessionLifecycle;
        this.nativeClose = nativeClose == null
            ? socket::close
            : nativeClose;
    }

    @Override public Socket nativeSocket() { return socket; }
    @Override public String name() { return "stream"; }
    @Override public void setTlsServer(
        String certificatePath,
        String keyPath,
        boolean requireClientCertificate) {
        socket.setTlsServer(certificatePath, keyPath, requireClientCertificate);
    }
    @Override public void bind(String endpoint) { socket.bind(endpoint); }
    @Override public void onPacket(ZLinkBackendStreamPacketHandler handler) {
        socket.options().notify(true);
        socket.onPacket(handler::handle);
    }
    @Override public void onTransportError(ZLinkBackendStreamErrorHandler handler) {
        closeMonitor();
        monitor = socket.monitorOpen(MonitorEventType.DISCONNECTED);
        monitor.onEvent(event -> event.routingId().ifPresent(routingId ->
            handler.handle(routingId, 0, event.event().name())));
    }
    @Override public void startSessionService() {
        if (meshNode != null && !sessionServiceStarted) {
            sessionServiceStarted = true;
        }
    }
    @Override public boolean send(RoutingId routingId, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 1, null, null, parts, flags);
    }
    boolean sendBoundSessionPush(
        RoutingId routingId,
        List<Message> parts,
        SendFlags flags) {
        if (boundSessionSink != null) {
            return boundSessionSink.send(routingId, parts, flags);
        }
        if (parts == null || parts.size() != 1) {
            throw new IllegalArgumentException(
                "bound Session push requires one encoded STREAM frame");
        }
        return socket.send(routingId)
            .message(parts.getFirst())
            .flags(flags)
            .submit();
    }
    @Override public boolean send(RoutingId routingId, String packetName, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 1, null, packetName, parts, flags);
    }
    @Override public boolean send(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), header, parts, flags);
    }
    @Override public boolean reply(RoutingId routingId, long requestSeq, String packetName, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), 3, requestSeq, packetName, parts, flags);
    }
    @Override public boolean reply(RoutingId routingId, ZLinkStreamHeader header, List<Message> parts, SendFlags flags) {
        return ZLinkJavaStreamFraming.submit(socket.send(routingId), header, parts, flags);
    }
    @Override public ZLinkBackendActorBindOperation bindActor(RoutingId sessionRid, ZLinkBackendActorRef actor) {
        return timeout -> {
            requireSessionRuntime();
            BindingKey key = new BindingKey(sessionRid, actor.actorId());
            long generation =
                rawSpotNode().allocateStreamBindingGeneration();
            SessionBinding candidate =
                new SessionBinding(actor, generation);
            CompletionStage<Void> bound = boundSessionLifecycle == null
                ? rawSpotNode().bindStreamSession(
                    sessionRid,
                    actor,
                    generation,
                    this,
                    timeout)
                : boundSessionLifecycle.bind(
                    sessionRid,
                    actor,
                    generation,
                    timeout);
            return bound.thenRun(() ->
                bindings.compute(key, (ignored, current) ->
                    current == null
                        || current.generation() < candidate.generation()
                        ? candidate
                        : current));
        };
    }
    @Override public ZLinkBackendActorUnbindOperation unbindActor(RoutingId sessionRid, String actorId) {
        return timeout -> {
            BindingKey key = new BindingKey(sessionRid, actorId);
            SessionBinding binding = requireBinding(sessionRid, actorId);
            CompletionStage<Void> unbound = unbindBinding(
                sessionRid,
                binding,
                timeout);
            return unbound.thenRun(() ->
                bindings.remove(key, binding));
        };
    }
    @Override public boolean sendBoundActor(RoutingId sessionRid, String actorId, List<Message> parts, SendFlags flags) {
        SessionBinding binding =
            requireBinding(sessionRid, actorId);
        return rawSpotNode().forwardBoundStreamSession(
            binding.actor(),
            sessionRid,
            binding.generation(),
            allocateBoundSessionSequence(),
            this,
            parts);
    }
    @Override public boolean relayBoundActor(
        RoutingId sessionRid,
        String actorId,
        ZLinkStreamHeader streamHeader,
        List<Message> parts,
        SendFlags flags) {
        Message header = Message.from(ZLinkStreamHeaderCodec.encode(streamHeader));
        try {
            SessionBinding binding =
                requireBinding(sessionRid, actorId);
            return rawSpotNode().forwardBoundStreamSession(
                binding.actor(),
                sessionRid,
                binding.generation(),
                allocateBoundSessionSequence(),
                this,
                streamHeader,
                prepend(header, parts));
        } finally {
            header.close();
        }
    }
    @Override public void close() {
        if (!closed.compareAndSet(false, true)) {
            return;
        }
        notifyAdmissionShutdown();
        closeMonitor();
        Throwable cleanupFailure = null;
        if (meshNode != null) {
            Map<BindingKey, SessionBinding> closingBindings =
                Map.copyOf(bindings);
            List<CompletableFuture<Void>> cleanup =
                new ArrayList<>(closingBindings.size());
            closingBindings.forEach((key, binding) -> {
                try {
                    cleanup.add(unbindBinding(
                        key.sessionRid(),
                        binding,
                        Duration.ofMillis(250))
                        .toCompletableFuture());
                } catch (RuntimeException | Error failure) {
                    cleanup.add(
                        CompletableFuture.failedFuture(failure));
                }
            });
            try {
                CompletableFuture.allOf(
                    cleanup.toArray(CompletableFuture[]::new))
                    .get(500, TimeUnit.MILLISECONDS);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                cleanupFailure = interrupted;
            } catch (ExecutionException | TimeoutException failure) {
                cleanupFailure = failure instanceof ExecutionException
                    && failure.getCause() != null
                    ? failure.getCause()
                    : failure;
            } finally {
                closingBindings.forEach((key, binding) ->
                    rawSpotNode().discardStreamSession(
                        key.sessionRid(),
                        binding.actor(),
                        binding.generation(),
                        this));
                bindings.clear();
            }
        }
        Throwable nativeCloseFailure = null;
        try {
            nativeClose.run();
        } catch (RuntimeException | Error failure) {
            nativeCloseFailure = failure;
        }
        if (cleanupFailure != null) {
            IllegalStateException failure = new IllegalStateException(
                "STREAM binding cleanup did not complete",
                cleanupFailure);
            if (nativeCloseFailure != null) {
                failure.addSuppressed(nativeCloseFailure);
            }
            throw failure;
        }
        if (nativeCloseFailure instanceof RuntimeException failure) {
            throw failure;
        }
        if (nativeCloseFailure instanceof Error failure) {
            throw failure;
        }
    }

    private void closeMonitor() {
        if (monitor != null) {
            monitor.close();
            monitor = null;
        }
    }

    private static List<Message> prepend(Message first, List<Message> rest) {
        ArrayList<Message> result = new ArrayList<>(rest.size() + 1);
        result.add(first);
        result.addAll(rest);
        return result;
    }

    private SessionBinding requireBinding(
        RoutingId sessionRid,
        String actorId) {
        requireSessionRuntime();
        SessionBinding binding =
            bindings.get(new BindingKey(sessionRid, actorId));
        if (binding == null) {
            throw new IllegalStateException(
                "STREAM session is not bound to actor: " + actorId);
        }
        return binding;
    }

    private ZLinkJavaRawSpotNode rawSpotNode() {
        return (ZLinkJavaRawSpotNode) meshNode.spotNode();
    }

    private long allocateBoundSessionSequence() {
        return nextBoundSessionSequence.getAndUpdate(
            current -> current == Long.MAX_VALUE ? 1 : current + 1);
    }

    private CompletionStage<Void> unbindBinding(
        RoutingId sessionRid,
        SessionBinding binding,
        Duration timeout) {
        return boundSessionLifecycle == null
            ? rawSpotNode().unbindStreamSession(
                sessionRid,
                binding.actor(),
                binding.generation(),
                this,
                timeout)
            : boundSessionLifecycle.unbind(
                sessionRid,
                binding.actor(),
                binding.generation(),
                timeout);
    }

    private void requireSessionRuntime() {
        if (meshNode == null || !sessionServiceStarted) {
            throw new IllegalStateException(
                "STREAM actor dispatch is not enabled for this stream node");
        }
    }

    private record BindingKey(RoutingId sessionRid, String actorId) {
    }

    private record SessionBinding(
        ZLinkBackendActorRef actor,
        long generation) {
    }

    @FunctionalInterface
    interface BoundSessionSink {
        boolean send(
            RoutingId sessionRid,
            List<Message> parts,
            SendFlags flags);
    }

    interface BoundSessionLifecycle {
        CompletionStage<Void> bind(
            RoutingId sessionRid,
            ZLinkBackendActorRef actor,
            long bindingGeneration,
            Duration timeout);

        CompletionStage<Void> unbind(
            RoutingId sessionRid,
            ZLinkBackendActorRef actor,
            long bindingGeneration,
            Duration timeout);
    }
}
