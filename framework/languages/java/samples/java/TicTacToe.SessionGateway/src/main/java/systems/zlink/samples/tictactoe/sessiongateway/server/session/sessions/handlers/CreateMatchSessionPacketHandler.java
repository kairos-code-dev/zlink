package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class CreateMatchSessionPacketHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
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
        return context.client()
            .reply("match-" + actor.actorId())
            .submitAsync();
    }

    private static ZLinkSessionActor requireSingleBoundActor(ZLinkSessionContext context) {
        return switch (context.actors().bound().size()) {
            case 1 -> context.actors().bound().get(0);
            case 0 -> throw new IllegalStateException(
                "Client must authenticate before creating a match");
            default -> throw new IllegalStateException(
                "Exactly one actor must be bound before creating a match");
        };
    }
}
