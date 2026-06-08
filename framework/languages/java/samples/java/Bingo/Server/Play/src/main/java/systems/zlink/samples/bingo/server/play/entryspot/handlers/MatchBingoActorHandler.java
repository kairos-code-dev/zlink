package systems.zlink.samples.bingo.server.play.entryspot.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class MatchBingoActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        BingoEntrySpot,
        PlayerActor,
        Messages.MatchBingoReq,
        Messages.MatchBingoRes> {
    @Override
    public CompletionStage<Messages.MatchBingoRes> handleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.MatchBingoReq request,
        CancellationToken cancellationToken) {
        return entrySpot.context().outbound().requestToChannel(
                SampleNames.ApiChannel,
                new Messages.MatchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.mode()))
            .timeout(SampleTimings.RequestTimeout)
            .submitAsync(Messages.MatchBingoApiRes.class)
            .thenCompose(matched -> {
                if (cancellationToken.isCancellationRequested()) {
                    return java.util.concurrent.CompletableFuture.failedFuture(
                        new IllegalStateException("MatchBingoReq was cancelled"));
                }
                return actor.context()
                    .<Messages.BingoRoomJoinRes>joinSpot(
                        RoutingId.from(matched.roomId()),
                        new Messages.BingoRoomJoinReq(
                            matched.roomId(),
                            actor.actorId(),
                            actor.displayName()))
                    .timeout(SampleTimings.RequestTimeout)
                    .submitAsync(Messages.BingoRoomJoinRes.class)
                    .thenApply(joined -> {
                        if (cancellationToken.isCancellationRequested()) {
                            throw new IllegalStateException("MatchBingoReq was cancelled");
                        }
                        return new Messages.MatchBingoRes(
                            matched.roomId(),
                            joined.reply().state());
                    });
            });
    }
}
