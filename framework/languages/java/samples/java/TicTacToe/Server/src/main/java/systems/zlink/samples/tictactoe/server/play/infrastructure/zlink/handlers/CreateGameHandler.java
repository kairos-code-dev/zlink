package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.handlers;

import systems.zlink.contracts.core.RoutingId;
import org.springframework.beans.factory.ObjectProvider;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

@ZLinkHandlerGroup(SampleNames.PlayChannel)
public final class CreateGameHandler {
    private final ObjectProvider<ZLinkSpotManager> spots;
    private final SampleSettings settings;
    private final TicTacToeGameCreator gameCreator;

    public CreateGameHandler(
        ObjectProvider<ZLinkSpotManager> spots,
        SampleSettings settings,
        TicTacToeGameCreator gameCreator) {
        this.spots = spots;
        this.settings = settings;
        this.gameCreator = gameCreator;
    }

    @ZLinkRequest
    public java.util.concurrent.CompletionStage<CreateGameRes> create(CreateGameReq request) {
        TicTacToeGameCreator.GameRoom room = gameCreator.nextRoom(request.gameName());
        return spots.getObject().create(TicTacToeGame.class, RoutingId.from(room.roomId()))
            .thenApply(ignored -> gameCreator.created(room));
    }
}
