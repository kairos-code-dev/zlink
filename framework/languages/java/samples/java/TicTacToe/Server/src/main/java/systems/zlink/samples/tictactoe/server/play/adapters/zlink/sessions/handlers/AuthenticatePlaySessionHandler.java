package systems.zlink.samples.tictactoe.server.play.adapters.zlink.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.msgpack.ZLinkStreamMessagePack;

public final class AuthenticatePlaySessionHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkActorManager actors;
    private final ZLinkClient channels;

    public AuthenticatePlaySessionHandler(
        ZLinkActorManager actors,
        ZLinkClient channels) {
        this.actors = actors;
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
        AuthenticateReq request = ZLinkStreamMessagePack.decode(
            new ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header)),
            AuthenticateReq.class);
        if (request.accessToken() == null || request.accessToken().isBlank()) {
            return CompletableFuture.failedFuture(new IllegalArgumentException("access token is required"));
        }
        return channels
            .requestToChannel(
                SampleNames.ApiChannel,
                new AuthenticatePlayerReq(request.accessToken()))
            .timeout(SampleNames.RequestTimeout)
            .submit(AuthenticatePlayerRes.class)
            .thenCompose(authenticated -> actors.getOrCreateAsync(
                authenticated.actorId(),
                SampleNames.PlayActor))
            .thenCompose(context.actors()::bindAsync)
            .thenCompose(bound -> context.client()
                .reply(new AuthenticateRes(bound.actorId()))
                .submit());
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
