package systems.zlink.samples.tictactoe.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.ZLinkFramework;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        try (ZLinkFramework framework = ZLinkFramework.start(options -> {
            options.addClientServerChannel("tictactoe-api", channel ->
                channel.enableClient(client -> client.useManualConnections(
                    endpoints -> endpoints.connect(TicTacToeSampleDefaults.ApiEndpoint))));
        })) {
            TicTacToeClientResult result = awaitSample(new TicTacToeClient(framework.client())
                .run(new TicTacToeClientOptions(
                    "Morning game",
                    TicTacToeSampleDefaults.HostAccessToken,
                    TicTacToeSampleDefaults.GuestAccessToken,
                    TicTacToeSampleDefaults.ApiEndpoint,
                    TicTacToeSampleDefaults.PlayEndpoint)));
            result.writeTo(System.out);
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
