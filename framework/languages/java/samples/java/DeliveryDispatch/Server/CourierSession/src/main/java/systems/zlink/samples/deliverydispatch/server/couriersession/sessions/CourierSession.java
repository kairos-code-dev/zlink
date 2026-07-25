package systems.zlink.samples.deliverydispatch.server.couriersession.sessions;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionMessageContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class CourierSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;
    private final SampleTopology topology;

    public CourierSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles,
        SampleTopology topology) {
        this.context = context;
        this.handlers = handlers;
        this.routes = routes;
        this.spotHandles = spotHandles;
        this.topology = topology;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onConnected() {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDisconnected() {
        CompletableFuture<?>[] notifications = context.actors().bound().stream()
            .map(actor -> actor.notifyDisconnected().toCompletableFuture())
            .toArray(CompletableFuture[]::new);
        return CompletableFuture.allOf(notifications);
    }

    @Override
    public CompletionStage<Void> onError(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onDispatch(
        ZLinkSessionMessageContext dispatch,
        ZLinkMessage payload) {
        if ("BindCourierSessionReq".equals(dispatch.packetName())) {
            return handleBindCourierSessionReq(dispatch, payload);
        }
        return handlers.tryHandle(context, dispatch, payload).thenCompose(handled -> {
            if (handled) {
                return CompletableFuture.completedFuture(null);
            }
            Messages.CourierDecision decision = payload.decode(Messages.CourierDecision.class);
            ZLinkSessionActor actor = context.actors().find(decision.courierId())
                .orElseThrow(() -> new IllegalStateException(
                    "Courier actor is not bound: " + decision.courierId()));
            return actor.relay(payload).thenApply(ignored -> null);
        });
    }

    private CompletionStage<Void> handleBindCourierSessionReq(
        ZLinkSessionMessageContext dispatch,
        ZLinkMessage payload) {
        Messages.BindCourierSessionReq request = payload.decode(Messages.BindCourierSessionReq.class);
        return findOrEnsureActor(request.courierId()).thenCompose(actorRef -> {
            ZLinkSessionActor bound = context.actors().find(actorRef.actorId()).orElse(null);
            CompletionStage<ZLinkSessionActor> actorStage = bound == null
                ? context.actors().bind(actorRef.toActorRef())
                : CompletableFuture.completedFuture(bound);
            Messages.BindCourierSessionReq relayed = new Messages.BindCourierSessionReq(
                request.courierId(), actorRef, context.sessionId());
            return actorStage.thenCompose(actor ->
                actor.relay(dispatch, ZLinkMessage.of(relayed)).thenApply(ignored -> null));
        });
    }

    private CompletionStage<systems.zlink.framework.actors.ActorRefSnapshot> findOrEnsureActor(
        String courierId) {
        String placement = topology.courierPlacement(courierId);
        return resolveSpot(placement).thenCompose(address -> routes
                .requestToSpot(
                    address,
                    new Messages.FindCourierActorReq(courierId))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.FindCourierActorRes.class)
                .thenCompose(found -> found.actor() != null
                    ? CompletableFuture.completedFuture(found.actor())
                    : routes.requestToSpot(
                            address,
                            new Messages.EnsureCourierActorReq(courierId))
                        .timeout(SampleTimings.RequestTimeout)
                        .submit(Messages.EnsureCourierActorRes.class)
                        .thenApply(Messages.EnsureCourierActorRes::actor)));
    }

    private CompletionStage<SpotHandle> resolveSpot(String placement) {
        return spotHandles.resolveSpotHandle(RoutingId.from(placement))
            .thenApply(found -> found.orElseThrow(() ->
                new IllegalStateException("courier Spot is not registered: " + placement)));
    }
}
