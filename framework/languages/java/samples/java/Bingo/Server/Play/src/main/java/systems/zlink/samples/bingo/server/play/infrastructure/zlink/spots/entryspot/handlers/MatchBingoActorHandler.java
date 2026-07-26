package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.ZLinkMessageContext;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class MatchBingoActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        BingoEntrySpot,
        PlayerActor,
        Messages.MatchBingoReq,
        Messages.MatchBingoRes> {
    @Override
    public java.util.concurrent.CompletionStage<Messages.MatchBingoRes> handle(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkMessageContext context,
        Messages.MatchBingoReq request) {
        return entrySpot.context().outbound().requestToChannel(
                SampleNames.ApiChannel,
                BingoMessages.matchBingoApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.getMode()))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.MatchBingoApiRes.class)
            .thenApply(matched -> {
                actor.context().joinSpot(
                    matched.getRoomId(),
                    BingoMessages.bingoRoomJoinReq(
                        matched.getRoomId(),
                        actor.actorId(),
                        actor.displayName(),
                        false))
                    .timeout(SampleTimings.RequestTimeout)
                    .defer();
                return BingoMessages.matchBingoRes(
                    matched.getRoomId(),
                    BingoMessages.bingoRoomState(
                        matched.getRoomId(),
                        "Waiting",
                        "",
                        false,
                        0,
                        null,
                        java.util.List.of(),
                        java.util.List.of(),
                        java.util.List.of()));
            });
    }
}
