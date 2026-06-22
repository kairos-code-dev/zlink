package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinReq;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    public JoinGameRes joinGame(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        JoinGameReq request,
        CancellationToken cancellationToken) {
        System.out.println("actor: JoinGameReq received. actor=" + actor.actorId()
            + " roomId=" + request.roomId());
        TicTacToeGameJoinRes result = actor.context()
            .joinSpot(RoutingId.from(request.roomId()),
                new TicTacToeGameJoinReq(request.roomId(), actor.requirePlayer()))
            .await(TicTacToeGameJoinRes.class)
            .reply();
        actor.joinGame(request.roomId());
        System.out.println("actor: JoinGameReq completed. actor=" + actor.actorId());
        return new JoinGameRes(result.state());
    }
}
