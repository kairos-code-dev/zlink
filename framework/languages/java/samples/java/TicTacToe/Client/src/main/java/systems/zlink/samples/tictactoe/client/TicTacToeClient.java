package systems.zlink.samples.tictactoe.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
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
        ZLinkStreamConnector host = playerConnector(game.playEndpoint());
        ZLinkStreamConnector guest = playerConnector(game.playEndpoint());
        Queue<GameStateNotify> stateNotifications = new ConcurrentLinkedQueue<>();
        Queue<PlayerJoinedNotify> playerJoinedNotifications = new ConcurrentLinkedQueue<>();
        CompletableFuture<PlayerJoinedNotify> hostSawGuestJoin = new CompletableFuture<>();
        CompletableFuture<GameStateNotify> guestSawHostMove = new CompletableFuture<>();
        CompletableFuture<GameStateNotify> hostSawGuestMove = new CompletableFuture<>();
        List<AutoCloseable> handlers = new ArrayList<>();
        handlers.add(ZLinkStreamJson.on(host, GameStateNotify.class, message -> {
            GameStateNotify notify = message.payload();
            stateNotifications.add(notify);
            if (options.oActorId().equals(notify.state().lastMoveActorId())) {
                hostSawGuestMove.complete(notify);
            }
            return CompletableFuture.completedFuture(null);
        }));
        handlers.add(ZLinkStreamJson.on(guest, GameStateNotify.class, message -> {
            GameStateNotify notify = message.payload();
            stateNotifications.add(notify);
            if (options.xActorId().equals(notify.state().lastMoveActorId())) {
                guestSawHostMove.complete(notify);
            }
            return CompletableFuture.completedFuture(null);
        }));
        handlers.add(ZLinkStreamJson.on(host, PlayerJoinedNotify.class, message -> {
            PlayerJoinedNotify notify = message.payload();
            playerJoinedNotifications.add(notify);
            if (options.oActorId().equals(notify.actorId())) {
                hostSawGuestJoin.complete(notify);
            }
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
                request(host, new JoinGameReq(game.roomId()), JoinGameRes.class))
                .thenApply(reply -> {
                    require(reply.state().roomId().equals(game.roomId()), "host joined unexpected room");
                    require("WaitingForPlayers".equals(reply.state().status()), "first join must wait");
                    hostJoin.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "guest JoinGameReq",
                request(guest, new JoinGameReq(game.roomId()), JoinGameRes.class))
                .thenApply(reply -> {
                    require(reply.state().roomId().equals(game.roomId()), "guest joined unexpected room");
                    require("InProgress".equals(reply.state().status()), "second join must start game");
                    guestJoin.set(reply);
                    return reply;
                }))
            .thenCompose(ignored -> requestStep(
                "host PlayerJoinedNotify",
                hostSawGuestJoin.orTimeout(3, TimeUnit.SECONDS)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(0)",
                request(host, new PlaceMarkReq(0), PlaceMarkRes.class))
                .thenApply(reply -> {
                    require(options.xActorId().equals(reply.state().lastMoveActorId()),
                        "host move response must include host move");
                    return addMove(moves, reply);
                }))
            .thenCompose(ignored -> requestStep(
                "guest GameStateNotify(host move)",
                guestSawHostMove.orTimeout(3, TimeUnit.SECONDS)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(3)",
                request(guest, new PlaceMarkReq(3), PlaceMarkRes.class))
                .thenApply(reply -> {
                    require(options.oActorId().equals(reply.state().lastMoveActorId()),
                        "guest move response must include guest move");
                    return addMove(moves, reply);
                }))
            .thenCompose(ignored -> requestStep(
                "host GameStateNotify(guest move)",
                hostSawGuestMove.orTimeout(3, TimeUnit.SECONDS)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(1)",
                request(host, new PlaceMarkReq(1), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(4)",
                request(guest, new PlaceMarkReq(4), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(2)",
                request(host, new PlaceMarkReq(2), PlaceMarkRes.class))
                .thenApply(reply -> addMove(moves, reply)))
            .thenApply(finalMove -> {
                validateFinalState(options, moves);
                return new TicTacToeClientResult(
                    game,
                    hostAuth.get(),
                    guestAuth.get(),
                    hostJoin.get(),
                    guestJoin.get(),
                    List.copyOf(moves),
                    List.copyOf(stateNotifications),
                    List.copyOf(playerJoinedNotifications));
            })
            .whenComplete((ignored, error) -> {
                closeHandlers(handlers);
                host.close();
                guest.close();
            });
    }

    private static ZLinkStreamConnector playerConnector(String endpoint) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            ZLinkStreamDispatchMode.AUTO,
            TicTacToeSampleDefaults.RequestTimeout,
            2,
            java.time.Duration.ofSeconds(5),
            64 * 1024,
            false,
            java.time.Duration.ofSeconds(1),
            TicTacToeSampleDefaults.RequestTimeout.plusSeconds(5),
            true,
            java.time.Duration.ofMillis(250),
            java.time.Duration.ofSeconds(5),
            2.0));
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

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    private static void validateFinalState(
        TicTacToeClientOptions options,
        List<PlaceMarkRes> moves) {
        if (moves.isEmpty()) {
            throw new IllegalStateException("TicTacToe sample did not complete any moves.");
        }
        var finalState = moves.get(moves.size() - 1).state();
        if (!"Won".equals(finalState.status()) || !options.xActorId().equals(finalState.winner())) {
            throw new IllegalStateException(
                "Unexpected final game state: board=" + finalState.board()
                    + ", status=" + finalState.status()
                    + ", winner=" + (finalState.winner() == null ? "-" : finalState.winner()));
        }
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
