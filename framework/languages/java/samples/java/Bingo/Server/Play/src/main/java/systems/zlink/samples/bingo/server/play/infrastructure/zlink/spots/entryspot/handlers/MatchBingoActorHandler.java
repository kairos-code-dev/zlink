package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class MatchBingoActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        BingoEntrySpot,
        PlayerActor,
        Messages.MatchBingoReq,
        Messages.MatchBingoRes> {
    @Override
    public Messages.MatchBingoRes handle(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.MatchBingoReq request,
        CancellationToken cancellationToken) {
        Messages.MatchBingoApiRes matched = entrySpot.context().outbound().requestToChannel(
                SampleNames.ApiChannel,
                new Messages.MatchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.mode(),
                    SampleTopology.selectedPlayNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .yieldAwait(Messages.MatchBingoApiRes.class);

        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        ZLinkActorJoinResult<Messages.BingoRoomJoinRes> joined = actor.context()
            .joinSpot(
                RoutingId.from(matched.roomId()),
                new Messages.BingoRoomJoinReq(
                    matched.roomId(),
                    actor.actorId(),
                    actor.displayName(),
                    false))
            .timeout(SampleTimings.RequestTimeout)
            .yieldAwait(Messages.BingoRoomJoinRes.class);
        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        return new Messages.MatchBingoRes(
            matched.roomId(),
            joined.reply().state(),
            matched.roomOwnerNodeRid());
    }
}
