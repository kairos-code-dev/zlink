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
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
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
                BingoMessages.matchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.getMode(),
                    SampleTopology.selectedPlayNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.MatchBingoApiRes.class);

        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        ZLinkActorJoinResult<Messages.BingoRoomJoinRes> joined = actor.context()
            .joinSpot(
                RoutingId.from(matched.getRoomId()),
                BingoMessages.bingoRoomJoinReq(
                    matched.getRoomId(),
                    actor.actorId(),
                    actor.displayName(),
                    false))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.BingoRoomJoinRes.class);
        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        return BingoMessages.matchBingoRes(
            matched.getRoomId(),
            joined.reply().getState(),
            matched.getRoomOwnerNodeRid());
    }
}
