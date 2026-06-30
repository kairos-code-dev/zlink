package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.LeaveGameMsg;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorLeaveGameHandler {
    @ZLinkSpotActorSend
    public void leaveGame(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorSendContext context,
        LeaveGameMsg request,
        CancellationToken cancellationToken) {
        spot.leaveGame(actor, request.roomId());
    }
}
