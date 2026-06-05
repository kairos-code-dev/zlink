package systems.zlink.samples.bingo.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class AuthenticateSessionHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;

    public AuthenticateSessionHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "AuthenticateReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        Messages.AuthenticateReq request = ZLinkStreamJson.decode(
            new ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header)),
            Messages.AuthenticateReq.class);
        if (request.accessToken() == null || request.accessToken().isBlank()) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("access token is required"));
        }
        return channels
            .requestToChannel(SampleNames.ApiChannel, new Messages.AuthenticatePlayerReq(request.accessToken()))
            .packetName("AuthenticatePlayer")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(Messages.AuthenticatePlayerRes.class)
            .thenCompose(authenticated -> {
                if (!authenticated.accepted()
                    || authenticated.actorId() == null
                    || authenticated.actorId().isBlank()
                    || authenticated.displayName() == null
                    || authenticated.displayName().isBlank()) {
                    return CompletableFuture.failedFuture(new IllegalStateException(
                        authenticated.reason() == null
                            ? "Player authentication failed."
                            : authenticated.reason()));
                }
                return channels
                    .requestToChannel(
                        SampleNames.PlayChannel,
                        new Messages.EnsurePlayerActorReq(
                            authenticated.actorId(),
                            authenticated.displayName()))
                    .packetName("EnsurePlayerActor")
                    .timeout(SampleTimings.RequestTimeout)
                    .submitAsync(Messages.EnsurePlayerActorRes.class)
                    .thenCompose(ensured -> context.actors()
                        .bindAsync(new ZLinkActorRef(
                            RoutingId.from(ensured.actor().nodeRid()),
                            ensured.actor().actorId(),
                            ensured.actor().generation()))
                        .thenCompose(ignored -> context.client()
                            .reply(new Messages.AuthenticateRes(
                                ensured.actorId(),
                                authenticated.displayName()))
                            .submitAsync()));
            });
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
