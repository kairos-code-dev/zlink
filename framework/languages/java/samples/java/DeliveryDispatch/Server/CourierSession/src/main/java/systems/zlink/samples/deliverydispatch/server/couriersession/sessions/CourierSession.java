package systems.zlink.samples.deliverydispatch.server.couriersession.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CourierSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;
    private final ZLinkClient channels;

    public CourierSession(
        ZLinkSessionContext context,
        ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers,
        ZLinkClient channels) {
        this.context = context;
        this.handlers = handlers;
        this.channels = channels;
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
        Messages.CourierBound bound = channels
            .requestToChannel(
                SampleNames.CourierChannel,
                new Messages.BindCourier(request.courierId(), context.sessionId()))
            .await(Messages.CourierBound.class);
        ZLinkSessionActor actor = context.actors().find(bound.actor().actorId())
            .orElseGet(() -> await(context.actors().bind(bound.actor().toActorRef())));
        await(actor.relay(ZLinkMessage.of(new Messages.BindCourierSession(
            bound.courierId(),
            bound.actor(),
            bound.sessionRoute()))));
        context.client()
            .reply(new Messages.BindCourierSessionAccepted(
                bound.courierId(),
                bound.actor(),
                bound.sessionRoute()))
            .submit();
    }
}
