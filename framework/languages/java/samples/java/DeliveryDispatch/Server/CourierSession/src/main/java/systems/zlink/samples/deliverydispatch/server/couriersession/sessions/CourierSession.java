package systems.zlink.samples.deliverydispatch.server.couriersession.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CourierSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final ZLinkRouteClient routes;

    public CourierSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        ZLinkRouteClient routes) {
        this.context = context;
        this.handlers = handlers;
        this.routes = routes;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public void onConnected() {
    }

    @Override
    public void onDisconnected() {
        context.actors().bound()
            .forEach(actor -> await(actor.notifyDisconnected()));
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        if ("BindCourierSession".equals(dispatch.packetName())) {
            handleBindCourierSession(payload);
            return;
        }
        boolean handled = await(handlers.tryHandleAsync(context, dispatch, payload));
        if (handled) {
            return;
        }
        Messages.CourierDecision decision = payload.decode(Messages.CourierDecision.class);
        ZLinkSessionActor actor = context.actors().find(decision.courierId())
            .orElseThrow(() -> new IllegalStateException(
                "Courier actor is not bound: " + decision.courierId()));
        await(actor.relay(payload));
    }

    private void handleBindCourierSession(ZLinkMessage payload) {
        Messages.BindCourierSession request = payload.decode(Messages.BindCourierSession.class);
        var actorRef = findOrEnsureActor(request.courierId());
        ZLinkSessionActor actor = context.actors().find(actorRef.actorId())
            .orElseGet(() -> await(context.actors().bind(actorRef.toActorRef())));
        await(actor.relay(ZLinkMessage.of(new Messages.BindCourierSession(
            request.courierId(),
            actorRef,
            context.sessionId()))));
        context.client()
            .reply(new Messages.BindCourierSessionAccepted(
                request.courierId(),
                actorRef.nodeRid(),
                context.sessionId()))
            .submit();
    }

    private Messages.ActorRefWire findOrEnsureActor(String courierId) {
        String placement = SampleTopology.courierPlacement(courierId);
        RoutingId nodeRid = RoutingId.from(placement);
        SpotRef address = new SpotRef(SampleNames.CourierSpotDiscovery, nodeRid, nodeRid);
        Messages.CourierActorFound found = routes
            .requestToSpot(SampleNames.CourierSpotDiscovery, address, new Messages.FindCourierActor(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CourierActorFound.class);
        if (found.actor() != null) {
            return found.actor();
        }
        return routes
            .requestToSpot(SampleNames.CourierSpotDiscovery, address, new Messages.EnsureCourierActor(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CourierActorEnsured.class)
            .actor();
    }
}
