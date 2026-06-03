package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class CreateMatchSessionPacketHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;

    public CreateMatchSessionPacketHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "CreateMatchReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        ZLinkSessionActor actor = requireSingleBoundActor(context);
        return channels.requestToChannel(SampleNames.PlayChannel, actor.actorId())
            .packetName("CreateMatchRoom")
            .submitAsync(String.class)
            .thenCompose(reply -> context.client()
                .reply(reply)
                .submitAsync());
    }

    static ZLinkSessionActor requireSingleBoundActor(ZLinkSessionContext context) {
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "Client must authenticate before using the session gateway");
            default -> throw new IllegalStateException(
                "Exactly one actor must be bound before using the session gateway");
        };
    }
}
