package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.contracts.core.RoutingId;
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
    private final String playNodeRid;

    public MatchBingoActorHandler(SampleTopology topology) {
        playNodeRid = topology.selectedPlayNodeRid();
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.MatchBingoRes> handle(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.MatchBingoReq request) {
        return entrySpot.context().outbound().requestToChannel(
                SampleNames.ApiChannel,
                BingoMessages.matchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.getMode(),
                    playNodeRid))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.MatchBingoApiRes.class)
            .thenCompose(matched -> actor.context().joinSpot(
                    RoutingId.from(matched.getRoomId()),
                    BingoMessages.bingoRoomJoinReq(
                        matched.getRoomId(),
                        actor.actorId(),
                        actor.displayName(),
                        false))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.BingoRoomJoinRes.class)
                .thenApply(joined -> BingoMessages.matchBingoRes(
                    matched.getRoomId(),
                    joined.reply().getState(),
                    matched.getRoomOwnerNodeRid())));
    }
}
