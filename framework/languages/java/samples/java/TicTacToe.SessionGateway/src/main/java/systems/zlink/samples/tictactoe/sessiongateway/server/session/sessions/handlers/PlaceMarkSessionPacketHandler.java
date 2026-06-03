package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class PlaceMarkSessionPacketHandler
    implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
    private final ZLinkClient channels;

    public PlaceMarkSessionPacketHandler(ZLinkClient channels) {
        this.channels = channels;
    }

    @Override
    public String packetName() {
        return "PlaceMarkReq";
    }

    @Override
    public CompletionStage<Void> handleAsync(
        ZLinkSessionContext context,
        ZLinkStreamHeader header,
        Message payload) {
        ZLinkSessionActor actor = CreateMatchSessionPacketHandler.requireSingleBoundActor(context);
        return channels.requestToChannel(
                SampleNames.PlayChannel,
                payload.toUtf8String() + "|" + actor.actorId())
            .packetName("PlaceMarkReq")
            .submitAsync(String.class)
            .thenCompose(reply -> {
                String packet = reply.contains(",Won,")
                    ? SampleNames.GameEndedPacket
                    : SampleNames.TurnChangedPacket;
                return context.client()
                    .send(reply)
                    .packetName(packet)
                    .submitAsync()
                    .thenCompose(ignored -> context.client()
                        .reply(reply)
                        .submitAsync());
            });
    }
}
