package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.LeaveGameReq;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorLeaveGameHandler {
    @ZLinkSpotActorSend
    public void leaveGame(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorSendContext context,
        LeaveGameReq request,
        CancellationToken cancellationToken) {
        spot.leaveGame(actor, request.roomId());
    }
}
