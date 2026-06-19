package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoEntrySpot;
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
        Messages.MatchBingoApiRes matched;
        try {
            System.out.println("bingo match: api request actor=" + actor.actorId());
            matched = entrySpot.context().outbound().requestToChannel(
                    SampleNames.ApiChannel,
                    new Messages.MatchBingoApiReq(
                        actor.actorId(),
                        actor.displayName(),
                        request.mode(),
                        SampleTopology.selectedPlayNodeRid()))
                .timeout(SampleTimings.RequestTimeout)
                .await(Messages.MatchBingoApiRes.class);
            System.out.println("bingo match: allocated room=" + matched.roomId());
        } catch (RuntimeException ex) {
            ex.printStackTrace(System.out);
            throw ex;
        }

        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        ZLinkActorJoinResult<Messages.BingoRoomJoinRes> joined;
        try {
            System.out.println("bingo match: join room=" + matched.roomId() + " actor=" + actor.actorId());
            joined = actor.context()
                .joinSpot(
                    RoutingId.from(matched.roomId()),
                    new Messages.BingoRoomJoinReq(
                        matched.roomId(),
                        actor.actorId(),
                        actor.displayName(),
                        false))
                .timeout(SampleTimings.RequestTimeout)
                .await(Messages.BingoRoomJoinRes.class);
            System.out.println("bingo match: joined room=" + matched.roomId() + " actor=" + actor.actorId());
        } catch (RuntimeException ex) {
            ex.printStackTrace(System.out);
            throw ex;
        }
        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("MatchBingoReq was cancelled");
        }
        return new Messages.MatchBingoRes(
            matched.roomId(),
            joined.reply().state(),
            matched.roomOwnerNodeRid());
    }
}
