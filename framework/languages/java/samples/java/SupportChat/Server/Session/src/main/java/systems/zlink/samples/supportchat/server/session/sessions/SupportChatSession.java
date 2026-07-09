package systems.zlink.samples.supportchat.server.session.sessions;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SupportChatSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkClient channels;
    private String actorId;
    private String displayName;
    private String role;

    public SupportChatSession(
        ZLinkSessionContext context,
        ZLinkClient channels) {
        this.context = context;
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
    }

    @Override
    public void onError(ZLinkStreamError error) {
    }

    @Override
    public void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload) {
        switch (dispatch.packetName()) {
            case "AuthenticateReq" -> authenticate(payload.decode(Messages.AuthenticateReq.class));
            case "SetAgentAvailableReq",
                 "OpenConversationReq",
                 "JoinConversationReq",
                 "SendChatMessageReq",
                 "SetTypingReq",
                 "CloseConversationReq" -> await(requireActor(dispatch).relay(payload));
            default -> throw new IllegalArgumentException("Unsupported packet: " + dispatch.packetName());
        }
    }

    private void authenticate(Messages.AuthenticateReq request) {
        Messages.AuthenticateUserRes authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, new Messages.AuthenticateUserReq(request.accessToken()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AuthenticateUserRes.class);
        if (!authenticated.accepted()) {
            throw new IllegalArgumentException(authenticated.reason());
        }
        actorId = authenticated.actorId();
        displayName = authenticated.displayName();
        role = authenticated.role();
        Messages.EnsureSupportUserActorRes ensured = channels
            .requestToChannel(
                SampleNames.SupportChannel,
                new Messages.EnsureSupportUserActorReq(actorId, displayName, role))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.EnsureSupportUserActorRes.class);
        await(context.actors().bind(new ActorRef(
            RoutingId.from(ensured.actor().nodeRid()),
            ensured.actor().actorId(),
            ensured.actor().generation())));
        context.client()
            .reply(new Messages.AuthenticateRes(actorId, displayName, role))
            .await();
    }

    private ZLinkSessionActor requireActor(ZLinkSessionDispatchContext dispatch) {
        if (actorId == null) {
            throw new IllegalStateException("Authenticate first");
        }
        return context.actors().find(actorId)
            .orElseThrow(() -> new IllegalStateException("actor is not bound: " + actorId));
    }
}
