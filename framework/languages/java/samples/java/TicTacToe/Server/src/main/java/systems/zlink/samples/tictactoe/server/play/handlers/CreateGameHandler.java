package systems.zlink.samples.tictactoe.server.play.handlers;

import java.util.concurrent.CompletionStage;
import org.springframework.beans.factory.ObjectProvider;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

@ZLinkHandlerGroup(SampleNames.PlayChannel)
public final class CreateGameHandler {
    private final ObjectProvider<ZLinkSpotManager> spots;
    private final SampleSettings settings;

    public CreateGameHandler(ObjectProvider<ZLinkSpotManager> spots, SampleSettings settings) {
        this.spots = spots;
        this.settings = settings;
    }

    @ZLinkRequest(packetName = "CreateGameReq")
    public CompletionStage<CreateGameRes> createAsync(CreateGameReq request) {
        return spots.getObject().createAsync(TicTacToeGame.class)
            .thenApply(created -> new CreateGameRes(
                created.spotRid().toHex(),
                settings.playEndpoint(),
                request.gameName()));
    }
}
