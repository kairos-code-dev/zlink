package systems.zlink.samples.tictactoe.server.api.handlers;

import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

@RestController
public final class CreateGameHttpHandler {
    private final ZLinkClient client;

    public CreateGameHttpHandler(ZLinkClient client) {
        this.client = client;
    }

    @PostMapping("/games")
    public CreateGameHttpRes handle(@RequestBody CreateGameHttpReq request) {
        CreateGameRes game = client.requestToChannel(
                    SampleNames.PlayChannel,
                    new CreateGameReq(gameName(request)))
                .timeout(SampleNames.RequestTimeout)
            .await(CreateGameRes.class);
        return new CreateGameHttpRes(
            game.roomId(),
            game.playEndpoint(),
            game.gameName());
    }

    private static String gameName(CreateGameHttpReq request) {
        return request.gameName() == null || request.gameName().isBlank()
            ? "tictactoe-game"
            : request.gameName();
    }
}
