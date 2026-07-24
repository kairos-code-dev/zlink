package systems.zlink.framework.runtime.spots;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.backend.*;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;

abstract class SpotActivationBase<C extends SpotDispatchLine> implements AutoCloseable {
    final ZLinkSpotRuntime host;
    final ZLinkSpotHandlerInvoker handlerInvoker;
    final Object spotSurface;
    final ZLinkBackendSpot backendSpot;
    final C context;
    private final Set<ZLinkBackendReceived> activeRouteReceives =
        java.util.Collections.synchronizedSet(
            java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>()));
    private ZLinkBackendActorReceived pendingActorHeader;

    SpotActivationBase(
        ZLinkSpotRuntime host,
        ZLinkSpotHandlerInvoker handlerInvoker,
        Object spotSurface,
        ZLinkBackendSpot backendSpot,
        C context) {
        this.host = host;
        this.handlerInvoker = handlerInvoker;
        this.spotSurface = spotSurface;
        this.backendSpot = backendSpot;
        this.context = context;
    }

    abstract CompletionStage<Void> appendSpotHandler(
        CompletionStage<Void> tail,
        Supplier<CompletionStage<Void>> operation);

    abstract CompletionStage<Void> appendActorLifecycle(
        CompletionStage<Void> tail,
        ZLinkBackendActorLifecycleEvent event,
        ZLinkBackendActorRef actorRef,
        ZLinkActor actor);

    final void trackRouteReceived(ZLinkBackendReceived received) {
        activeRouteReceives.add(received);
    }

    void dispatchResolvedActorPacket(
        ZLinkActor actor,
        ActorPacketFrames.Header packetHeader,
        ActorMessageRead read) {
        host.dispatchLocalActorPacket(
            context,
            spotSurface,
            actor,
            packetHeader,
            read.headerPart(),
            read.bodyPart(),
            read.fromPendingHeader());
    }

    final CompletionStage<Void> dispatchActorMessages(
        List<ZLinkBackendActorReceived> actorMessages) {
        int index = 0;
        while (index < actorMessages.size() || pendingActorHeader != null) {
            ActorMessageRead read = host.readActorMessage(
                actorMessages,
                index,
                pendingActorHeader);
            index = read.nextIndex();
            pendingActorHeader = read.nextPendingHeader();
            if (!read.complete()) {
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            }
            boolean pendingHeader = read.fromPendingHeader();
            ZLinkBackendActorReceived headerPart = read.headerPart();
            ZLinkBackendActorReceived bodyPart = read.bodyPart();
            ActorPacketFrames.Header packetHeader = ActorPacketFrames.decode(headerPart);
            if (!host.actorSessions().available()) {
                host.closePendingActorHeader(headerPart, pendingHeader);
                continue;
            }
            Optional<ZLinkActor> localActor =
                host.actorSessions().localActor(headerPart.actor().actorId());
            if (localActor.isEmpty()) {
                host.reportSpotActorHandlerMissing(
                    packetHeader,
                    context.spotId(),
                    headerPart.actor().actorId(),
                    headerPart.sourceNodeRid());
                if (packetHeader.requestSeq().isPresent()) {
                    ZLinkBackendActorReceived headerCopy = pendingHeader
                        ? headerPart
                        : host.copyActorReceived(headerPart);
                    host.replyActorDispatchError(
                        context,
                        packetHeader,
                        headerCopy,
                        headerPart.actor().actorId(),
                        new ZLinkConfigurationException(
                            "SPOT actor is not registered locally: "
                                + headerPart.actor().actorId()),
                        "actor missing error reply failed");
                } else {
                    host.closePendingActorHeader(headerPart, pendingHeader);
                }
                continue;
            }
            ZLinkActor actor = localActor.get();
            if (host.dispatchActorControlPacket(packetHeader, headerPart, actor, pendingHeader)) {
                continue;
            }
            dispatchResolvedActorPacket(actor, packetHeader, read);
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    final CompletionStage<Void> drainActorLifecycleEvents() {
        CompletionStage<Void> tail =
            java.util.concurrent.CompletableFuture.completedFuture(null);
        while (true) {
            if (host.isClosing()) {
                return tail;
            }
            ZLinkBackendActorLifecycleEvent event =
                backendSpot.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
            if (event == null) {
                return tail;
            }
            if (!host.actorSessions().available()) {
                continue;
            }
            ZLinkBackendActorRef actorRef = host.actorLifecycleRef(event);
            Optional<ZLinkActor> actor = host.actorSessions().localActor(actorRef.actorId());
            if (actor.isPresent()) {
                tail = appendActorLifecycle(tail, event, actorRef, actor.get());
            }
        }
    }

    final CompletionStage<Void> dispatchSpotRouteHandler(
        ZLinkBackendReceived received,
        ParsedPacket packet) {
        Map<String, String> metadata;
        try {
            metadata = ZLinkApplicationMetadata.decode(
                received.applicationMetadata());
        } catch (IllegalArgumentException error) {
            if (received.requestSeq().isPresent()) {
                host.replySpotRouteDispatchError(
                    received,
                    packet.packetName(),
                    context.spotId(),
                    ZLinkDispatchErrorReason.INVALID_FRAME,
                    error);
            } else {
                host.reportSpotRouteSendDropped(
                    received, packet.packetName(), context.spotId());
            }
            closeRouteReceived(received);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        SpotPacketHandlerRegistration handler =
            context.handlerCatalog().packetHandler(packet.packetName());
        if (handler == null) {
            if (received.requestSeq().isPresent()) {
                host.replySpotRouteDispatchError(
                    received,
                    packet.packetName(),
                    context.spotId(),
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    null);
            } else {
                host.reportSpotRouteSendDropped(received, packet.packetName(), context.spotId());
            }
            closeRouteReceived(received);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        if (received.requestSeq().isPresent()) {
            if (!handler.request()) {
                host.replySpotRouteDispatchError(
                    received,
                    packet.packetName(),
                    context.spotId(),
                    ZLinkDispatchErrorReason.HANDLER_MISSING,
                    null);
                closeRouteReceived(received);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            }
            Message payloadCopy = Message.from(packet.payload());
            return appendSpotHandler(
                java.util.concurrent.CompletableFuture.completedFuture(null),
                () ->
                systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext.propagate(
                    host.runWithOutbound(context.dispatchOutbound(), () ->
                        handlerInvoker.invokeRequest(
                            handler, spotSurface, payloadCopy, metadata))
                        .thenAccept(reply -> received.reply(List.of(reply))))
                    .whenComplete((ignored, error) -> {
                        if (error != null && !host.isClosing()) {
                            host.replySpotRouteDispatchError(
                                received,
                                packet.packetName(),
                                context.spotId(),
                                ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                                error);
                        }
                        payloadCopy.close();
                        closeRouteReceived(received);
                        if (error == null) {
                            host.traceMessageFlow(
                                ZLinkMessageFlowOutcome.REPLIED,
                                ZLinkDispatchErrorSurface.SPOT_ROUTE,
                                ZLinkDispatchMessageKind.REQUEST,
                                packet.packetName(),
                                null,
                                null,
                                received.requestSeq().map(String::valueOf).orElse(null),
                                null,
                                context.spotId().toString(),
                                null);
                        }
                    }));
        }
        if (handler.request()) {
            host.reportSpotRouteSendDropped(received, packet.packetName(), context.spotId());
            closeRouteReceived(received);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        }
        Message payloadCopy = Message.from(packet.payload());
        String packetName = packet.packetName();
        closeRouteReceived(received);
        return appendSpotHandler(
            java.util.concurrent.CompletableFuture.completedFuture(null),
            () ->
            host.runWithOutbound(context.dispatchOutbound(), () ->
                handlerInvoker.invokePacket(
                    handler, spotSurface, payloadCopy, metadata))
                .whenComplete((ignored, error) -> {
                    payloadCopy.close();
                    if (error == null) {
                        host.traceMessageFlow(
                            ZLinkMessageFlowOutcome.DISPATCHED,
                            ZLinkDispatchErrorSurface.SPOT_ROUTE,
                            ZLinkDispatchMessageKind.SEND,
                            packetName,
                            null,
                            null,
                            null,
                            null,
                            context.spotId().toString(),
                            null);
                    }
                }));
    }

    final CompletionStage<Void> handleRoutedBoundSessionSendParts(
        List<Message> parts) {
        ZLinkActorSpotRoutePackets.BoundSessionSend send =
            ZLinkActorSpotRoutePackets.decodeBoundSessionSend(parts);
        byte[] frameBytes = send.frame().toByteArray();
        CompletionStage<Void> sendStage = host.actorSessions().sendBoundSession(
            send.actorRef(),
            frameBytes,
            () -> host.sendActorBoundSessionWithRetry(
                host.primaryNode(),
                send.actorRef(),
                send.actorRef().actorId(),
                frameBytes,
                "routed actor bound session send failed"));
        return sendStage.whenComplete((ignored, error) -> send.close());
    }

    final CompletionStage<List<Message>> handleRoutedBoundSessionSendRequestParts(
        List<Message> parts) {
        return handleRoutedBoundSessionSendParts(parts)
            .thenApply(ignored -> List.of(Message.from(new byte[0])));
    }

    final CompletionStage<Optional<Message>> handleRoutedActorPacketParts(
        List<Message> parts) {
        ZLinkActorSpotRoutePackets.ActorPacket packet =
            ZLinkActorSpotRoutePackets.decodeActorPacket(parts);
        if (packet.handoffArrivalIndex() != null) {
            host.actorAdmissions().traceTransferMarker(
                "backlog_enqueued",
                packet.actorRef().actorId(),
                packet.handoffArrivalIndex());
        }
        return host.dispatchLocalSessionActor(packet.actorRef(), packet.header(), packet.payload())
            .thenApply(reply -> {
                Optional<Message> completed = host.replyTransferredRequestDirect(
                    packet.actorRef(), packet.header(), packet.replyRoute(), reply);
                return packet.replyRoute() == null
                    ? completed
                    : Optional.of(ZLinkActorSpotRoutePackets.createHandoffDirectReplyAck());
            })
            .whenComplete((ignored, error) -> packet.close());
    }

    final CompletionStage<Void> dispatchSpotSubscription(
        ZLinkBackendTopicMessage received) {
        try {
            if (host.isDraining()) {
                return CompletableFuture.completedFuture(null);
            }
            if (received.parts().isEmpty()) {
                host.reportSpotSubscriptionDropped(
                    received.topic(),
                    null,
                    context.spotId(),
                    ZLinkDispatchErrorReason.INVALID_FRAME);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            }
            ParsedPacket packet = host.parsePacket(received.parts());
            Map<String, String> metadata;
            try {
                metadata = ZLinkApplicationMetadata.decode(
                    received.applicationMetadata());
            } catch (IllegalArgumentException error) {
                host.reportSpotSubscriptionDropped(
                    received.topic(),
                    null,
                    context.spotId(),
                    ZLinkDispatchErrorReason.INVALID_FRAME);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            }
            host.traceMessageFlow(
                ZLinkMessageFlowOutcome.RECEIVED,
                ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                ZLinkDispatchMessageKind.PUBLISH,
                packet.packetName(),
                null,
                received.topic(),
                null,
                null,
                context.spotId().toString(),
                null);
            CompletionStage<Void> tail =
                java.util.concurrent.CompletableFuture.completedFuture(null);
            boolean dispatched = false;
            for (SpotSubscriptionHandlerRegistration handler :
                context.handlerCatalog().subscriptionHandlers(received.topic())) {
                if (!handler.packetName().equals(packet.packetName())) {
                    continue;
                }
                dispatched = true;
                Message payloadCopy = Message.from(packet.payload());
                tail = appendSpotHandler(
                    tail,
                    () -> host.runWithOutbound(context.dispatchOutbound(), () ->
                        handlerInvoker.invokeSubscription(
                            handler,
                            spotSurface,
                            received.channelName(),
                            received.topic(),
                            received.routingId().map(Object::toString),
                            payloadCopy,
                            metadata))
                        .whenComplete((ignored, error) -> payloadCopy.close()));
            }
            if (dispatched) {
                host.traceMessageFlow(
                    ZLinkMessageFlowOutcome.DISPATCHED,
                    ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
                    ZLinkDispatchMessageKind.PUBLISH,
                    packet.packetName(),
                    null,
                    received.topic(),
                    null,
                    null,
                    context.spotId().toString(),
                    null);
            } else {
                host.reportSpotSubscriptionDropped(
                    received.topic(),
                    packet.packetName(),
                    context.spotId(),
                    ZLinkDispatchErrorReason.HANDLER_MISSING);
            }
            return tail;
        } finally {
            received.parts().forEach(Message::close);
        }
    }

    final void closeActiveRouteReceives() {
        for (ZLinkBackendReceived received : List.copyOf(activeRouteReceives)) {
            closeRouteReceived(received);
        }
    }

    final void closeRouteReceived(ZLinkBackendReceived received) {
        if (activeRouteReceives.remove(received)) {
            received.close();
        }
    }

    final void closePendingActorMessage() {
        if (pendingActorHeader != null) {
            pendingActorHeader.close();
            pendingActorHeader = null;
        }
    }
}
