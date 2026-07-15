package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.concurrent.atomic.AtomicInteger;
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
    private final systems.zlink.samples.tictactoe.server.configuration.ApiSettings settings;
    private final AtomicInteger nextOwnerIndex = new AtomicInteger();

    public CreateGameHttpHandler(
        ZLinkClient client,
        systems.zlink.samples.tictactoe.server.configuration.ApiSettings settings) {
        this.client = client;
        this.settings = settings;
    }

    @PostMapping("/games")
    public java.util.concurrent.CompletionStage<CreateGameHttpRes> handle(@RequestBody CreateGameHttpReq request) {
        return client.requestToChannel(
                    SampleNames.playChannel(selectOwner()),
                    new CreateGameReq(gameName(request)))
                .timeout(SampleNames.RequestTimeout)
            .submit(CreateGameRes.class)
            .thenApply(game -> new CreateGameHttpRes(
                game.roomId(), game.gameName(), game.ownerPlayEndpoint(), game.playEndpoints(),
                game.playNodes(), game.requiredLevel()));
    }

    private static String gameName(CreateGameHttpReq request) {
        return request.gameName() == null || request.gameName().isBlank()
            ? "tictactoe-game"
            : request.gameName();
    }

    private int selectOwner() {
        int playNodeCount = settings.playChannelEndpoints().size();
        if (playNodeCount == 0) {
            throw new IllegalStateException("At least one Play endpoint is required.");
        }
        return Math.floorMod(nextOwnerIndex.getAndIncrement(), playNodeCount);
    }
}
