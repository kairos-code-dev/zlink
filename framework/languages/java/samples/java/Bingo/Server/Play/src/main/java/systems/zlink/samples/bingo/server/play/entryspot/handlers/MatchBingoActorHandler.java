package systems.zlink.samples.bingo.server.play.entryspot.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class MatchBingoActorHandler {
    private final ZLinkSpotOutbound outbound;

    public MatchBingoActorHandler(ZLinkSpotOutbound outbound) {
        this.outbound = outbound;
    }

    @ZLinkSpotActorRequest
    public CompletionStage<Messages.MatchBingoRes> handleAsync(
        PlayerActor actor,
        Messages.MatchBingoReq request) {
        return outbound.requestToChannel(
                SampleNames.ApiChannel,
                new Messages.MatchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.mode()))
            .packetName("MatchBingoApiReq")
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(Messages.MatchBingoApiRes.class)
            .thenCompose(matched -> actor.context()
                .<Messages.BingoRoomJoinRes>joinSpot(
                    RoutingId.fromHex(matched.roomId()),
                    new Messages.BingoRoomJoinReq(
                        matched.roomId(),
                        actor.actorId(),
                        actor.displayName()))
                .timeout(SampleTimings.RequestTimeout)
                .submitAsync(Messages.BingoRoomJoinRes.class)
                .thenApply(joined -> new Messages.MatchBingoRes(
                    matched.roomId(),
                    joined.reply().state())));
    }
}
