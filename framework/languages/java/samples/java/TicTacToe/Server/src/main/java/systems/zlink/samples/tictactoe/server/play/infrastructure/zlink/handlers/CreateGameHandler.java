package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.handlers;

import systems.zlink.contracts.core.RoutingId;
import org.springframework.beans.factory.ObjectProvider;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

public final class CreateGameHandler implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {
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

    @Override
    public java.util.concurrent.CompletionStage<CreateGameRes> handle(
        CreateGameReq request,
        ZLinkRequestContext context) {
        TicTacToeGameCreator.GameRoom room = gameCreator.nextRoom(request.gameName());
        return spots.getObject().create(TicTacToeGame.class, RoutingId.from(room.roomId()))
            .thenApply(ignored -> gameCreator.created(room));
    }
}
