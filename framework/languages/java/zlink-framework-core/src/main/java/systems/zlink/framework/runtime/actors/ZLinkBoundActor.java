package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeoutException;
import java.util.function.Predicate;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendStreamSocket;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkBoundActor implements ZLinkSessionActor {
    private final ZLinkBackendStreamSocket stream;
    private final RoutingId sessionRid;
    private volatile ZLinkBackendActorRef ref;
    private final Optional<ZLinkActor> managedActor;
    private final ZLinkActorRuntime actors;
    private final ZLinkMessageSerializer serializer;
    private final long bindingToken;
    private final Predicate<RoutingId> routeReady;
    private final ZLinkSessionActorsRuntime.LocalActorDispatcher localActorDispatcher;
    private final boolean nativeSessionRelayAttached;
    private final ZLinkStreamCodec defaultCodec;
    private final ZLinkSessionRelayHeaders relayHeaders;
    private final ZLinkMessageFlowTracer flow;
    private final ZLinkActorDirectory actorDirectory;
    private final ZLinkRelayMetadataPolicy metadataPolicy;
    private volatile boolean nativeRebound;
    private CompletionStage<Void> bindingRefresh = CompletableFuture.completedFuture(null);

    ZLinkBoundActor(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkBackendActorRef ref,
        Optional<ZLinkActor> managedActor,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer,
        long bindingToken,
        Predicate<RoutingId> routeReady,
        ZLinkSessionActorsRuntime.LocalActorDispatcher localActorDispatcher,
        boolean nativeSessionRelayAttached,
        ZLinkStreamCodec defaultCodec,
        ZLinkSessionRelayHeaders relayHeaders,
        ZLinkMessageFlowTracer flow,
        ZLinkActorDirectory actorDirectory,
        ZLinkRelayMetadataPolicy metadataPolicy) {
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.ref = ref;
        this.managedActor = managedActor;
        this.actors = actors;
        this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
        this.bindingToken = bindingToken;
        this.routeReady = routeReady == null ? ignored -> true : routeReady;
        this.localActorDispatcher = localActorDispatcher;
        this.nativeSessionRelayAttached = nativeSessionRelayAttached;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
        this.relayHeaders = relayHeaders;
        this.flow = flow;
        this.actorDirectory = actorDirectory;
        this.metadataPolicy =
            metadataPolicy == null ? ZLinkRelayMetadataPolicy.EMPTY : metadataPolicy;
    }

    @Override
    public String actorId() {
        return ref.actorId();
    }

    @Override
    public ActorRef ref() {
        ZLinkBackendActorRef current = ref;
        return new ActorRef(current.nodeRid(), current.actorId(), current.generation());
    }

    void rebindNativeActor(ZLinkBackendActorRef targetActor) {
        if (!ref.actorId().equals(targetActor.actorId())) {
            throw new ZLinkConfigurationException(
                "bound session actor id mismatch: " + targetActor.actorId());
        }
        ref = targetActor;
        nativeRebound = true;
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> relay(
        ZLinkMessage payload) {
        return relay(relayHeaders.current(), payload);
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> relay(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        return relay(relayHeaders.find(dispatch).or(relayHeaders::current), payload);
    }

    private CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> relay(
        Optional<ZLinkStreamHeader> currentHeader,
        ZLinkMessage payload) {
        if (payload == null) {
            return CompletableFuture.failedFuture(new IllegalArgumentException(
                "payload is required"));
        }
        if (currentHeader.isEmpty()) {
            return CompletableFuture.failedFuture(new IllegalStateException(
                "Session actor relay requires an active stream dispatch."));
        }
        ZLinkStreamHeader header = metadataPolicy.sessionToActor(currentHeader.get());
        traceRelay(header);
        Message message = ZLinkMessagePayloads.message(payload, serializer);
        byte[] payloadBytes = message.toByteArray();
        message.close();
        if (managedActor.isPresent() && localActorDispatcher != null && !nativeRebound) {
            return ZLinkSubmitResults.fromVoidStage(relayLocal(header, payloadBytes));
        }
        return ZLinkSubmitResults.fromVoidStage(refreshNativeBinding()
            .thenCompose(ignored -> ensureNativeBinding())
            .thenCompose(ignored -> relayWithRetry(header, payloadBytes)));
    }

    private synchronized CompletionStage<Void> refreshNativeBinding() {
        if (actorDirectory == null || !nativeSessionRelayAttached) {
            return CompletableFuture.completedFuture(null);
        }
        bindingRefresh = bindingRefresh.handle((ignored, error) -> null)
            .thenCompose(ignored -> actorDirectory.find(ref.actorId()))
            .thenCompose(current -> {
                if (current.isEmpty()) {
                    return CompletableFuture.completedFuture(null);
                }
                ActorRef located = current.get();
                ZLinkBackendActorRef target = new ZLinkBackendActorRef(
                    located.nodeRid(), located.actorId(), located.generation());
                ZLinkBackendActorRef previous = ref;
                if (previous.nodeRid().equals(target.nodeRid())
                    && previous.generation() == target.generation()) {
                    return CompletableFuture.completedFuture(null);
                }
                return ZLinkBoundSessionRuntime.ignoreMissingBinding(
                        stream.unbindActor(sessionRid, previous.actorId())
                            .submit(ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT))
                    .thenCompose(unbound -> awaitRouteReady(target))
                    .thenCompose(ready -> ZLinkBoundSessionRuntime.bindActorWithRetry(
                        stream,
                        sessionRid,
                        target,
                        ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT))
                    .thenRun(() -> rebindNativeActor(target));
            });
        return bindingRefresh;
    }

    private CompletionStage<Void> awaitRouteReady(ZLinkBackendActorRef target) {
        return ZLinkActorRetryScheduler.waitUntilRelay(
            ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT,
            () -> routeReady.test(target.nodeRid()),
            () -> {},
            () -> new TimeoutException(
                "remote bound session route was not ready before timeout: "
                    + target.actorId()));
    }

    private void traceRelay(ZLinkStreamHeader header) {
        if (flow == null
            || !flow.enabled(systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.SENT)) {
            return;
        }
        flow.trace(new systems.zlink.framework.configuration.ZLinkMessageFlowEvent(
            systems.zlink.framework.configuration.ZLinkMessageFlowOutcome.SENT,
            systems.zlink.framework.configuration.ZLinkDispatchErrorSurface.SPOT_ACTOR,
            header.requestSequence().isPresent()
                ? systems.zlink.framework.configuration.ZLinkDispatchMessageKind.ACTOR_REQUEST
                : systems.zlink.framework.configuration.ZLinkDispatchMessageKind.ACTOR_SEND,
            header.packetName(),
            null,
            null,
            header.correlationId().orElse(null),
            null,
            null,
            ref.actorId(),
            null,
            null, null, null, null,
            header.flowId().orElse(null),
            header.flowOrigin().orElse(null)));
    }

    private CompletionStage<Void> relayLocal(
        ZLinkStreamHeader header,
        byte[] payloadBytes) {
        if (localActorDispatcher == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "local actor dispatch requires a Spot runtime"));
        }
        Message payload = Message.from(payloadBytes);
        return localActorDispatcher.dispatch(ref, header, payload)
            .thenCompose(reply -> {
                if (reply.isEmpty()) {
                    return CompletableFuture.completedFuture(null);
                }
                return replyLocal(header, reply.get());
            })
            .whenComplete((ignored, error) -> payload.close());
    }

    private CompletionStage<Void> replyLocal(
        ZLinkStreamHeader header,
        Message reply) {
        try {
            if (header.requestSequence().isEmpty()) {
                reply.close();
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "actor reply requires a stream request sequence: "
                        + header.packetName()));
            }
            ZLinkStreamHeader replyHeader = ZLinkStreamHeader.createResponse(
                header,
                header.codec(),
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                header.packetName(),
                Map.of());
            byte[] replyBytes = reply.toByteArray();
            return ZLinkActorRetryScheduler.submitRelayUntilAccepted(
                ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT,
                () -> {
                    try (Message attemptReply = Message.from(replyBytes)) {
                        return stream.reply(
                            sessionRid,
                            replyHeader,
                            List.of(attemptReply),
                            SendFlags.DONT_WAIT);
                    }
                },
                () -> new TimeoutException(
                    "local actor session reply was not ready before timeout: "
                        + ref.actorId()));
        } finally {
            reply.close();
        }
    }

    private CompletionStage<Void> awaitRouteReady() {
        return ZLinkActorRetryScheduler.waitUntilRelayOrContinue(
            ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT,
            () -> routeReady.test(ref.nodeRid()));
    }

    private CompletionStage<Void> relayWithRetry(
        ZLinkStreamHeader header,
        byte[] payloadBytes) {
        return ZLinkActorRetryScheduler.submitRelayUntilAcceptedAfterRetry(
            ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT,
            () -> {
                try (Message payloadPart = Message.from(payloadBytes)) {
                    return stream.relayBoundActor(
                        sessionRid,
                        ref.actorId(),
                        header,
                        List.of(payloadPart),
                        SendFlags.DONT_WAIT);
                }
            },
            this::retryAfterNativeBinding,
            () -> new TimeoutException(
                "session relay route was not ready before timeout: "
                    + ref.actorId()));
    }

    private void retryAfterNativeBinding(Runnable attempt) {
        stream.bindActor(sessionRid, ref)
            .submit(Duration.ofSeconds(2))
            .whenComplete((ignored, error) ->
                ZLinkActorRetryScheduler.scheduleRelay(attempt));
    }

    private CompletionStage<Void> ensureNativeBinding() {
        return ZLinkActorRetryScheduler.bindRelayUntilAccepted(
            ZLinkSessionActorsRuntime.RELAY_SUBMIT_TIMEOUT,
            () -> stream.bindActor(sessionRid, ref)
                .submit(Duration.ofSeconds(2)),
            ZLinkActorSubmitFaults::alreadyBound,
            ZLinkActorSubmitFaults::retryableSessionActorBindFailure);
    }

    @Override
    public CompletionStage<Void> notifyDisconnected() {
        CompletionStage<Void> notification = managedActor.isPresent() && !nativeRebound
            ? (actors.clearSessionBinding(managedActor.get(), bindingToken)
                ? actors.notifyDisconnected(managedActor.get())
                : CompletableFuture.completedFuture(null))
            : notifyRemoteDisconnected();
        return notification.thenCompose(ignored -> stream.unbindActor(sessionRid, ref.actorId())
            .submit(Duration.ofSeconds(30)));
    }

    private CompletionStage<Void> notifyRemoteDisconnected() {
        if (!nativeSessionRelayAttached) {
            return CompletableFuture.completedFuture(null);
        }
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.SEND,
            defaultCodec,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.empty(),
            ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME,
            Map.of());
        return ensureNativeBinding()
            .thenCompose(ignored -> relayWithRetry(header, new byte[0]));
    }

    CompletionStage<Void> notifyRemoteBoundSession() {
        if (!nativeSessionRelayAttached || managedActor.isPresent()) {
            return CompletableFuture.completedFuture(null);
        }
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.SEND,
            ZLinkStreamCodec.RAW,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.empty(),
            ZLinkBoundSessionRuntime.REMOTE_BOUND_SESSION_BIND_PACKET_NAME,
            Map.of());
        return ensureNativeBinding()
            .thenCompose(ignored -> relayWithRetry(header, new byte[0]));
    }
}
