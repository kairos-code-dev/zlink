package systems.zlink.framework.runtime.spots;

import systems.zlink.framework.runtime.backend.*;

import systems.zlink.framework.ZLinkAwait;
import java.lang.reflect.Method;
import java.lang.reflect.InvocationTargetException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Supplier;
import java.util.logging.Logger;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkCloseException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkHandlerContext;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.runtime.actors.ZLinkActorSpotRoutePackets;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerMethodInvoker;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerKind;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.locations.ZLinkLocationLifecycle;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.runtime.messaging.ZLinkPacketNames;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkWorkerCall;
import systems.zlink.framework.spots.ZLinkWorkerTask;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotHandlerRegistry;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class SpotActivation
    extends SpotActivationBase<DefaultSpotContext> {
    private final ZLinkSpot<?> spot;

    SpotActivation(
        ZLinkSpotRuntime host,
        ZLinkSpotHandlerInvoker spotHandlerInvoker,
        ZLinkSpot<?> spot,
        ZLinkBackendSpot backendSpot,
        DefaultSpotContext context) {
        super(host, spotHandlerInvoker, spot, backendSpot, context);
        this.spot = spot;
    }

    ZLinkSpot<?> spot() {
        return spot;
    }

    @Override
    CompletionStage<Void> appendSpotHandler(
        CompletionStage<Void> tail,
        Supplier<CompletionStage<Void>> operation) {
        return tail.thenCompose(ignored -> operation.get());
    }

    @Override
    CompletionStage<Void> appendActorLifecycle(
        CompletionStage<Void> tail,
        ZLinkBackendActorLifecycleEvent event,
        ZLinkBackendActorRef actorRef,
        ZLinkActor actor) {
        return tail.thenCompose(ignored -> {
            if (host.isClosing()) {
                return systems.zlink.framework.ZLinkSubmitStage.completed();
            }
            Supplier<CompletionStage<Void>> transition = host.actorLifecycleTransition(
                spot,
                event,
                actorRef,
                actor,
                context.spotRid());
                return transition == null
                    ? systems.zlink.framework.ZLinkSubmitStage.completed()
                    : host.actorSessions().dispatch(actor, transition);
        });
    }

    void handleDispatchEvent(ZLinkBackendSpotDispatchInfo info) {
        if (host.isClosing()) {
            return;
        }
        if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
            drainRoutesForDispatch();
            return;
        }
        context.enqueueDispatch(() -> dispatchEventAsync(info)
            .whenComplete((ignored, error) -> {
                for (ZLinkBackendActorReceived actorMessage : info.actorMessages()) {
                    actorMessage.close();
                }
            }));
    }

    private CompletionStage<Void> dispatchEventAsync(ZLinkBackendSpotDispatchInfo info) {
        if (info.event() == ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
            return drainRoutesAsync();
        }
        if (info.event() == ZLinkBackendSpotDispatchEvent.SUBSCRIBE_READABLE) {
            return drainSubscriptionsAsync();
        }
        if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
            return drainUnhandledActorJoinsAsync();
        }
        if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
            return dispatchActorMessages(info.actorMessages());
        }
        if (info.event() == ZLinkBackendSpotDispatchEvent.ACTOR_LIFECYCLE_READABLE) {
            return drainActorLifecycleEvents();
        }
        return systems.zlink.framework.ZLinkSubmitStage.completed();
    }

    private void drainRoutesForDispatch() {
        List<ZLinkBackendReceived> routes = new ArrayList<>();
        while (true) {
            ZLinkBackendReceived received =
                backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
            if (received == null) {
                break;
            }
            if (host.dispatchSpotRouteBridgePacket(received)) {
                received.close();
                continue;
            }
            routes.add(received);
        }
        if (routes.isEmpty()) {
            return;
        }
        context.enqueueDispatch(() -> dispatchRoutesAsync(routes));
    }

    private CompletionStage<Void> dispatchRoutesAsync(List<ZLinkBackendReceived> routes) {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (ZLinkBackendReceived received : routes) {
            tail = tail.thenCompose(ignored -> dispatchRouteAsync(received));
        }
        return tail;
    }

    void drainPolledDispatchQueues() {
        context.enqueueDispatch(() -> drainRoutesAsync()
            .thenCompose(ignored -> drainUnhandledActorJoinsAsync())
            .thenCompose(ignored -> drainActorLifecycleEvents()));
    }

    private CompletionStage<Void> drainRoutesAsync() {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        while (true) {
            ZLinkBackendReceived received =
                backendSpot.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
            if (received == null) {
                return tail;
            }
            ZLinkSpotRuntime.traceSpotRouteInbound("spot-recv", backendSpot, received);
            if (host.dispatchSpotRouteBridgePacket(received)) {
                received.close();
                continue;
            }
            tail = tail.thenCompose(ignored -> dispatchRouteAsync(received));
        }
    }

    private CompletionStage<Void> dispatchRouteAsync(ZLinkBackendReceived received) {
        trackRouteReceived(received);
        if (ZLinkSpotRuntime.isProbeFrame(received.parts())) {
            closeRouteReceived(received);
            return systems.zlink.framework.ZLinkSubmitStage.completed();
        }
        ParsedPacket packet = ZLinkSpotRuntime.parsePacket(received.parts());
        ZLinkSpotRuntime.traceSpotRouteDispatch("spot-dispatch", backendSpot, received, packet);
        host.traceMessageFlow(
            ZLinkMessageFlowOutcome.RECEIVED,
            ZLinkDispatchErrorSurface.SPOT_ROUTE,
            received.requestSeq().isPresent()
                ? ZLinkDispatchMessageKind.REQUEST
                : ZLinkDispatchMessageKind.SEND,
            packet.packetName(),
            null,
            null,
            received.requestSeq().map(String::valueOf).orElse(null),
            null,
            backendSpot.routingId().toString(),
            null);
        if (ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(packet.packetName())) {
            return dispatchRoutedActorJoinAsync(received, packet);
        }
        if (ZLinkActorSpotRoutePackets.BOUND_SESSION_SEND_PACKET_NAME.equals(packet.packetName())) {
            CompletionStage<?> stage = received.requestSeq().isPresent()
                ? handleRoutedBoundSessionSendRequestParts(received.parts())
                    .thenAccept(received::reply)
                : handleRoutedBoundSessionSendParts(received.parts());
            return stage
                .thenApply(ignored -> (Void) null)
                .whenComplete((ignored, error) -> closeRouteReceived(received));
        }
        if (ZLinkActorSpotRoutePackets.ACTOR_PACKET_NAME.equals(packet.packetName())) {
            return handleRoutedActorPacketParts(received.parts())
                .thenAccept(reply -> reply.ifPresent(message -> received.reply(List.of(message))))
                .thenApply(ignored -> (Void) null)
                .whenComplete((ignored, error) -> closeRouteReceived(received));
        }
        return dispatchSpotRouteHandler(received, packet);
    }

    private CompletionStage<Void> drainSubscriptionsAsync() {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        while (true) {
            ZLinkBackendTopicMessage received =
                backendSpot.subscribe(ZLinkBackendRecvMode.DONT_WAIT);
            if (received == null) {
                return tail;
            }
            tail = tail.thenCompose(ignored -> dispatchSpotSubscription(received));
        }
    }

    private CompletionStage<Void> drainUnhandledActorJoinsAsync() {
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        while (true) {
            ZLinkBackendActorJoinRequest request =
                backendSpot.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
            if (request == null) {
                return tail;
            }
            tail = tail.thenCompose(ignored -> dispatchActorJoinAsync(request));
        }
    }

    private CompletionStage<Void> dispatchActorJoinAsync(ZLinkBackendActorJoinRequest request) {
        Message payloadCopy = actorJoinPayload(request.parts());
        request.parts().forEach(Message::close);
        return host.runWithOutbound(context.dispatchOutbound(), () ->
            invokeActorJoinCallback(request, payloadCopy))
            .handle((response, error) -> {
                if (error != null) {
                    try (Message emptyReply = Message.from(new byte[0])) {
                        backendSpot.replyActorJoin(request, 1, List.of(emptyReply));
                    }
                    return null;
                }
                ZLinkSpotActorJoinResponse effective =
                    response == null ? ZLinkSpotActorJoinResponse.reject() : response;
                Message reply = effective.reply() == null
                    ? Message.from(new byte[0])
                    : ZLinkMessagePayloads.message(effective.reply(), host.serializerForSpot());
                try {
                    backendSpot.replyActorJoin(
                        request,
                        effective.accepted() ? 0 : 1,
                        List.of(reply));
                } finally {
                    reply.close();
                }
                return null;
            })
            .thenApply(ignored -> (Void) null)
            .whenComplete((ignored, error) -> payloadCopy.close());
    }

    private Message actorJoinPayload(List<Message> parts) {
        if (parts.isEmpty()) {
            return Message.from(new byte[0]);
        }
        if (parts.size() >= 3
            && ZLinkActorSpotRoutePackets.JOIN_SPOT_PACKET_NAME.equals(parts.get(0).toUtf8String())) {
            return Message.from(parts.get(2).toByteArray());
        }
        return Message.from(parts.get(0).toByteArray());
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<Void> dispatchRoutedActorJoinAsync(
        ZLinkBackendReceived received,
        ParsedPacket packet) {
        return handleRoutedActorJoinParts(null, null, received.parts())
            .thenAccept(received::reply)
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    host.replySpotRouteDispatchError(
                        received,
                        packet.packetName(),
                        backendSpot.routingId(),
                        ZLinkDispatchErrorReason.HANDLER_EXCEPTION,
                        error);
                } else {
                    host.traceMessageFlow(
                        ZLinkMessageFlowOutcome.REPLIED,
                        ZLinkDispatchErrorSurface.SPOT_ROUTE,
                        ZLinkDispatchMessageKind.REQUEST,
                        packet.packetName(),
                        null,
                        null,
                        received.requestSeq().map(String::valueOf).orElse(null),
                        null,
                        backendSpot.routingId().toString(),
                        null);
                }
                closeRouteReceived(received);
            });
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<List<Message>> handleRoutedActorJoinParts(
        String routeChannelName,
        RoutingId sourcePeerRid,
        List<Message> parts) {
        ParsedPacket packet = ZLinkSpotRuntime.parsePacket(parts);
        ZLinkActorSpotRoutePackets.JoinRequest joinRequest =
            ZLinkActorSpotRoutePackets.decodeJoinRequest(packet.payload());
        Message joinPayload = parts.size() > 2
            ? Message.from(parts.get(2).toByteArray())
            : Message.from(new byte[0]);
        return host.actorAdmissions().admitRoutedActor(
                joinRequest,
                routeChannelName,
                sourcePeerRid,
                host.primaryNode(),
                backendSpot.routingId(),
                host.spotFor(backendSpot.routingId()),
                actor -> host.runWithOutbound(context.dispatchOutbound(), () ->
                    ZLinkHandlerStages.fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(
                        actor,
                        ZLinkMessage.fromEncoded(
                            ZLinkMessagePayloads.encoded(joinPayload),
                            host.serializerForSpot()),
                        ZLinkSpotRuntime.noneCancellation()))),
                actor -> host.notifySpotActorLifecycleAndSuppressBackendEvent(
                    spot,
                    actor,
                    backendSpot.routingId(),
                    true))
            .thenApply(join -> encodeRoutedJoinReply(join.actorRef(), join.response()))
            .handle((reply, error) -> {
                try {
                    if (error != null) {
                        throw new CompletionException(error);
                    }
                    try (reply) {
                        return List.of(Message.from(reply.toByteArray()));
                    }
                } finally {
                    joinPayload.close();
                }
            });
    }

    private Message encodeRoutedJoinReply(
        ZLinkBackendActorRef actorRef,
        ZLinkSpotActorJoinResponse response) {
        Message reply = response.reply() == null
            ? Message.from(new byte[0])
            : ZLinkMessagePayloads.message(response.reply(), host.serializerForSpot());
        try {
            return ZLinkActorSpotRoutePackets.encodeJoinReply(
                response.accepted(),
                actorRef,
                reply);
        } finally {
            reply.close();
        }
    }

    @SuppressWarnings({"rawtypes", "unchecked"})
    private CompletionStage<ZLinkSpotActorJoinResponse> invokeActorJoinCallback(
        ZLinkBackendActorJoinRequest request,
        Message payload) {
        return host.actorAdmissions().admitSpotActor(
            request,
            backendSpot.routingId(),
            host.spotFor(backendSpot.routingId()),
            actor -> ZLinkHandlerStages.fromSupplier(() -> ((ZLinkSpot) spot).onActorJoin(
                actor,
                ZLinkMessage.fromEncoded(
                    ZLinkMessagePayloads.encoded(payload),
                    host.serializerForSpot()),
                ZLinkSpotRuntime.noneCancellation())),
            actor -> host.notifySpotActorLifecycleAndSuppressBackendEvent(
                spot,
                actor,
                backendSpot.routingId(),
                true));
    }

    @Override
    public void close() {
        if (spot == null) {
            closeResources();
            return;
        }
        try {
            host.awaitClosing(host.runWithOutbound(context.dispatchOutbound(), () ->
                ZLinkHandlerStages.fromRunnable(spot::onClosing)));
        } finally {
            closeResources();
        }
    }

    private void closeResources() {
        closePendingActorMessage();
        closeActiveRouteReceives();
        context.closeTimers();
        backendSpot.close();
    }

}
