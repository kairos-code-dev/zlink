package systems.zlink.samples.tictactoe;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.tictactoe.client.TicTacToeClient;
import systems.zlink.samples.tictactoe.client.TicTacToeClientOptions;
import systems.zlink.samples.tictactoe.client.TicTacToeClientResult;
import systems.zlink.samples.tictactoe.server.api.ApiHttpServer;
import systems.zlink.samples.tictactoe.server.api.ApiServer;
import systems.zlink.samples.tictactoe.server.configuration.SampleSettings;
import systems.zlink.samples.tictactoe.server.play.PlayServer;

public final class TicTacToeSample {
    private TicTacToeSample() {
    }

    public static void main(String[] args) throws Exception {
        SampleSettings settings = SampleSettings.fromArgs(args).withEphemeralDefaults();
        SampleSettings.setCurrent(settings);
        try (ZLinkFramework play = ZLinkFramework.start(options -> PlayServer.configure(options, settings));
             ZLinkFramework api = ZLinkFramework.start(options -> ApiServer.configure(options, settings));
             ApiHttpServer httpApi = ApiHttpServer.start(api.client(), settings)) {
            TicTacToeClient client = new TicTacToeClient();
            TicTacToeClientResult result = awaitSample(client.run(new TicTacToeClientOptions(
                settings.apiPublicUrl(),
                "tictactoe-game",
                "player-x",
                "player-o")));

            require("player-x".equals(result.winner()), "direct TicTacToe winner mismatch");
            require(result.pushes().contains("GameWon:player-x"),
                "room Spot did not publish winner push");
        }

        System.out.println("TicTacToe sample self-check passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private static <T> T awaitSample(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
    }
}
