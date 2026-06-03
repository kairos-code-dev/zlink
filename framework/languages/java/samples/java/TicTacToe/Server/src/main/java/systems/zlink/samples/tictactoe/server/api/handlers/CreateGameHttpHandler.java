package systems.zlink.samples.tictactoe.server.api.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpExchange;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

public final class CreateGameHttpHandler {
    private CreateGameHttpHandler() {
    }

    public static void handle(
        HttpExchange exchange,
        ZLinkClient client,
        ObjectMapper json) throws IOException {
        if (!exchange.getRequestMethod().equals("POST")) {
            write(exchange, 405, "");
            return;
        }

        try {
            CreateGameHttpReq request = json.readValue(
                exchange.getRequestBody(),
                CreateGameHttpReq.class);
            CreateGameRes game = await(client.requestToChannel(
                    SampleNames.PlayChannel,
                    new CreateGameReq(gameName(request)))
                .packetName("CreateGameReq")
                .submitAsync(CreateGameRes.class));
            write(exchange, 200, json.writeValueAsString(new CreateGameHttpRes(
                game.gameId(),
                game.playEndpoint(),
                game.gameName())));
        } catch (RuntimeException ex) {
            write(exchange, 500, ex.getMessage() == null ? "" : ex.getMessage());
        }
    }

    private static String gameName(CreateGameHttpReq request) {
        return request.gameName() == null || request.gameName().isBlank()
            ? "tictactoe-game"
            : request.gameName();
    }

    private static <T> T await(java.util.concurrent.CompletionStage<T> stage) {
        CompletableFuture<T> done = new CompletableFuture<>();
        CountDownLatch latch = new CountDownLatch(1);
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
            latch.countDown();
        });
        try {
            latch.await();
            return done.get();
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Interrupted while waiting for API reply.", ex);
        } catch (java.util.concurrent.ExecutionException ex) {
            throw new IllegalStateException("Create game request failed.", ex.getCause());
        }
    }

    private static void write(HttpExchange exchange, int statusCode, String body) throws IOException {
        byte[] bytes = body.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().set("Content-Type", "application/json");
        exchange.sendResponseHeaders(statusCode, bytes.length);
        exchange.getResponseBody().write(bytes);
        exchange.close();
    }
}
