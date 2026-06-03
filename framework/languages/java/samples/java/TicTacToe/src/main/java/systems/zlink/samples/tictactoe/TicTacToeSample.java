package systems.zlink.samples.tictactoe;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.tictactoe.client.TicTacToeClient;
import systems.zlink.samples.tictactoe.client.TicTacToeClientOptions;
import systems.zlink.samples.tictactoe.client.TicTacToeClientResult;
import systems.zlink.samples.tictactoe.server.api.ApiServer;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.server.play.PlayServer;

public final class TicTacToeSample {
    private TicTacToeSample() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework play = ZLinkFramework.start(PlayServer::configure);
             ZLinkFramework api = ZLinkFramework.start(ApiServer::configure);
             ZLinkFramework clientFramework = ZLinkFramework.start(options ->
                 options.addClientServerChannel("tictactoe-api", channel ->
                     channel.enableClient(client -> client.useManualConnections(
                         endpoints -> endpoints.connect(SampleTopology.ApiEndpoint)))))) {
            TicTacToeClient client = new TicTacToeClient(clientFramework.client());
            TicTacToeClientResult result = awaitSample(client.run(new TicTacToeClientOptions(
                "Morning game",
                "alice-token",
                "bob-token",
                SampleTopology.ApiEndpoint,
                SampleTopology.PlayStreamEndpoint)));

            require("alice".equals(result.winner()), "direct TicTacToe winner mismatch");
            require(result.pushes().contains("GameWon:alice"),
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
