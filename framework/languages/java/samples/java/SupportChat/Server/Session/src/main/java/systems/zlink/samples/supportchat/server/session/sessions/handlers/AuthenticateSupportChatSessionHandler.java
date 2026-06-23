package systems.zlink.samples.supportchat.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class AuthenticateSupportChatSessionHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Messages.AuthenticateReq> {
    private final ZLinkClient channels;

    public AuthenticateSupportChatSessionHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public Class<Messages.AuthenticateReq> messageType() {
        return Messages.AuthenticateReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Messages.AuthenticateReq request) {
        if (request.accessToken() == null || request.accessToken().isBlank()) {
            throw new IllegalArgumentException("access token is required");
        }
        Messages.AuthenticateUserRes authenticated = channels
            .requestToChannel(SampleNames.ApiChannel, new Messages.AuthenticateUserReq(request.accessToken()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.AuthenticateUserRes.class);
        if (!authenticated.accepted()
            || authenticated.actorId() == null
            || authenticated.actorId().isBlank()
            || authenticated.displayName() == null
            || authenticated.displayName().isBlank()
            || authenticated.role() == null
            || authenticated.role().isBlank()) {
            throw new IllegalStateException(
                authenticated.reason() == null
                    ? "SupportChat authentication failed."
                    : authenticated.reason());
        }
        Messages.EnsureSupportUserActorRes ensured = channels
            .requestToChannel(
                SampleNames.SupportChannel,
                new Messages.EnsureSupportUserActorReq(
                    authenticated.actorId(),
                    authenticated.displayName(),
                    authenticated.role()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.EnsureSupportUserActorRes.class);
        await(context.actors()
            .bind(new ZLinkActorRef(
                RoutingId.from(ensured.actor().nodeRid()),
                ensured.actor().actorId(),
                ensured.actor().generation())));
        context.client()
            .reply(new Messages.AuthenticateRes(
                authenticated.actorId(),
                authenticated.displayName(),
                authenticated.role()))
            .await();
    }
}
