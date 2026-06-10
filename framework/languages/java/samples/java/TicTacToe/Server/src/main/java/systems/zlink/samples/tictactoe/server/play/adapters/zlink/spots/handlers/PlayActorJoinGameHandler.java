package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.PlayEntrySpot;
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
            .joinSpot(RoutingId.from(request.roomId()),
                new TicTacToeGameJoinReq(request.roomId(), actor.actorId()))
            .submit(TicTacToeGameJoinRes.class)
            .thenApply(result -> {
                actor.joinGame(request.roomId());
                return new JoinGameRes(result.reply().state());
            });
    }
}
