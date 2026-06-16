package systems.zlink.samples.supportchat.server.session.sessions.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.protobuf.ZLinkStreamProtobuf;

public final class AuthenticateSupportChatSessionHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;

    public AuthenticateSupportChatSessionHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        Messages.AuthenticateReq request = ZLinkStreamProtobuf.decode(
            new ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header)),
            Messages.AuthenticateReq.class);
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

    private static ZLinkStreamCodec connectorCodec(ZLinkStreamHeader header) {
        return switch (header.codec()) {
            case RAW -> ZLinkStreamCodec.RAW;
            case JSON -> ZLinkStreamCodec.JSON;
            case MESSAGE_PACK -> ZLinkStreamCodec.MESSAGE_PACK;
            case PROTOBUF -> ZLinkStreamCodec.PROTOBUF;
        };
    }
}
