package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class JoinMatchSessionPacketHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;
    private final PlayNotificationRelay relay;

    public JoinMatchSessionPacketHandler(
        ZLinkClient channels,
        PlayNotificationRelay relay) {
        this.channels = channels;
        this.relay = relay;
    }

    @Override
    public String packetName() {
        return "JoinMatchReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        ZLinkSessionActor actor = CreateMatchSessionPacketHandler.requireSingleBoundActor(context);
        return channels.requestToChannel(
                SampleNames.PlayChannel,
                payload.toUtf8String().trim() + "|" + actor.actorId())
            .packetName("JoinMatchReq")
            .submitAsync(String.class)
            .thenCompose(response -> relay.deliverAsync(response)
                .thenCompose(ignored -> context.client()
                    .reply(relay.reply(response))
                    .submitAsync()));
    }
}
