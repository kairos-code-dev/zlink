package systems.zlink.samples.tictactoe.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpRes;
import systems.zlink.samples.tictactoe.shared.contracts.GameStateNotify;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerJoinedNotify;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class TicTacToeClient {
    private final HttpClient http;
    private final ObjectMapper json;

    public TicTacToeClient() {
        this(HttpClient.newHttpClient(), new ObjectMapper());
    }

    TicTacToeClient(HttpClient http, ObjectMapper json) {
        this.http = http;
        this.json = json;
    }

    public CompletionStage<TicTacToeClientResult> run(TicTacToeClientOptions options) {
        return createGame(options.apiUrl(), options.gameName())
            .thenCompose(game -> playScenario(options, game));
    }

    private CompletionStage<CreateGameHttpRes> createGame(String apiUrl, String gameName) {
        try {
            HttpRequest request = HttpRequest.newBuilder(URI.create(apiUrl).resolve("/games"))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(
                    json.writeValueAsString(new CreateGameHttpReq(gameName))))
                .build();
            return http.sendAsync(request, HttpResponse.BodyHandlers.ofString())
                .thenApply(response -> {
                    if (response.statusCode() / 100 != 2) {
                        throw new IllegalStateException(
                            "API returned HTTP " + response.statusCode() + ": " + response.body());
                    }
                    try {
                        return json.readValue(response.body(), CreateGameHttpRes.class);
                    } catch (java.io.IOException ex) {
                        throw new IllegalStateException("API returned an invalid game response.", ex);
                    }
                });
        } catch (java.io.IOException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    private CompletionStage<TicTacToeClientResult> playScenario(
        TicTacToeClientOptions options,
        CreateGameHttpRes game) {
        ZLinkStreamConnector host = playerConnector(game.playEndpoint(), options.xActorId());
        ZLinkStreamConnector guest = playerConnector(game.playEndpoint(), options.oActorId());
        Queue<GameStateNotify> stateNotifications = new ConcurrentLinkedQueue<>();
        Queue<PlayerJoinedNotify> playerJoinedNotifications = new ConcurrentLinkedQueue<>();
        List<AutoCloseable> handlers = new ArrayList<>();
        handlers.add(ZLinkStreamJson.on(host, GameStateNotify.class, message -> {
            stateNotifications.add(message.payload());
            return CompletableFuture.completedFuture(null);
        }));
        handlers.add(ZLinkStreamJson.on(guest, GameStateNotify.class, message -> {
            stateNotifications.add(message.payload());
            return CompletableFuture.completedFuture(null);
        }));
        handlers.add(ZLinkStreamJson.on(host, PlayerJoinedNotify.class, message -> {
            playerJoinedNotifications.add(message.payload());
            return CompletableFuture.completedFuture(null);
        }));
        handlers.add(ZLinkStreamJson.on(guest, PlayerJoinedNotify.class, message -> {
            playerJoinedNotifications.add(message.payload());
            return CompletableFuture.completedFuture(null);
        }));

        java.util.concurrent.atomic.AtomicReference<AuthenticateRes> hostAuth =
            new java.util.concurrent.atomic.AtomicReference<>();
        java.util.concurrent.atomic.AtomicReference<AuthenticateRes> guestAuth =
            new java.util.concurrent.atomic.AtomicReference<>();
        java.util.concurrent.atomic.AtomicReference<JoinGameRes> hostJoin =
            new java.util.concurrent.atomic.AtomicReference<>();
        java.util.concurrent.atomic.AtomicReference<JoinGameRes> guestJoin =
            new java.util.concurrent.atomic.AtomicReference<>();
        List<PlaceMarkRes> moves = new ArrayList<>();

        return host.connectAsync()
            .thenCompose(ignored -> guest.connectAsync())
            .thenCompose(ignored -> requestStep(
                "host AuthenticateReq",
                request(host, new AuthenticateReq(options.xActorId()), AuthenticateRes.class))
                .thenApply(reply -> {
                    hostAuth.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "guest AuthenticateReq",
                request(guest, new AuthenticateReq(options.oActorId()), AuthenticateRes.class))
                .thenApply(reply -> {
                    guestAuth.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "host JoinGameReq",
                request(host, new JoinGameReq(game.gameId()), JoinGameRes.class))
                .thenApply(reply -> {
                    hostJoin.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "guest JoinGameReq",
                request(guest, new JoinGameReq(game.gameId()), JoinGameRes.class))
                .thenApply(reply -> {
                    guestJoin.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(0)",
                request(host, new PlaceMarkReq(0), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(4)",
                request(guest, new PlaceMarkReq(4), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(1)",
                request(host, new PlaceMarkReq(1), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(8)",
                request(guest, new PlaceMarkReq(8), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(2)",
                request(host, new PlaceMarkReq(2), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(finalMove -> CompletableFuture.supplyAsync(
                () -> new TicTacToeClientResult(
                    game,
                    hostAuth.get(),
                    guestAuth.get(),
                    hostJoin.get(),
                    guestJoin.get(),
                    List.copyOf(moves),
                    List.copyOf(stateNotifications),
                    List.copyOf(playerJoinedNotifications)),
                CompletableFuture.delayedExecutor(250, TimeUnit.MILLISECONDS)))
            .whenComplete((ignored, error) -> {
                closeHandlers(handlers);
                host.close();
                guest.close();
            });
    }

    private static ZLinkStreamConnector playerConnector(String endpoint, String actorId) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint + "/" + actorId),
            ZLinkStreamDispatchMode.AUTO,
            Duration.ofSeconds(3),
            2));
    }

    private static <TReply> CompletionStage<TReply> request(
        ZLinkStreamConnector connector,
        Object request,
        Class<TReply> replyType) {
        return ZLinkStreamJson.request(connector, request)
            .submitAsync()
            .thenApply(reply -> ZLinkStreamJson.decode(reply, replyType));
    }

    private static <TReply> CompletionStage<TReply> requestStep(
        String step,
        CompletionStage<TReply> request) {
        return request.exceptionallyCompose(error ->
            CompletableFuture.failedFuture(new IllegalStateException(
                "TicTacToe sample step failed: " + step,
                error)));
    }

    private static PlaceMarkRes addMove(List<PlaceMarkRes> moves, PlaceMarkRes reply) {
        moves.add(reply);
        return reply;
    }

    private static void closeHandlers(List<AutoCloseable> handlers) {
        for (AutoCloseable handler : handlers) {
            try {
                handler.close();
            } catch (Exception ignored) {
                // Best-effort cleanup for sample event subscriptions.
            }
        }
    }

}
