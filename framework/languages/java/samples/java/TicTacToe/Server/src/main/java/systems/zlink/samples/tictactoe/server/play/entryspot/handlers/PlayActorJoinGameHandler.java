package systems.zlink.samples.tictactoe.server.play.entryspot.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinReq;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    public CompletionStage<JoinGameRes> joinGame(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        JoinGameReq request,
        CancellationToken cancellationToken) {
        return actor.context()
            .joinSpot(RoutingId.fromHex(request.gameId()),
                new TicTacToeGameJoinReq(request.gameId(), actor.actorId()))
            .submitAsync(TicTacToeGameJoinRes.class)
            .thenApply(result -> {
                actor.joinGame(request.gameId());
                return new JoinGameRes(result.reply().state());
            });
    }
}
