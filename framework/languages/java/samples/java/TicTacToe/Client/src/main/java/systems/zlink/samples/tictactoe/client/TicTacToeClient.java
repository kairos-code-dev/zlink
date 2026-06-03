package systems.zlink.samples.tictactoe.client;

import java.net.URI;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.json.ZLinkStreamJson;

public final class TicTacToeClient {
    private final ZLinkClient frameworkClient;

    public TicTacToeClient(ZLinkClient frameworkClient) {
        this.frameworkClient = frameworkClient;
    }

    public CompletionStage<TicTacToeClientResult> run(TicTacToeClientOptions options) {
        return createGame(options.gameName())
            .thenCompose(game -> playScenario(options, game));
    }

    private CompletionStage<CreateGameRes> createGame(String gameName) {
        return frameworkClient
            .requestToChannel("tictactoe-api", new CreateGameReq(gameName))
            .packetName("CreateGame")
            .submitAsync(CreateGameRes.class);
    }

    private CompletionStage<TicTacToeClientResult> playScenario(
        TicTacToeClientOptions options,
        CreateGameRes game) {
        ZLinkStreamConnector host = playerConnector(game.playEndpoint(), options.hostAccessToken());
        ZLinkStreamConnector guest = playerConnector(game.playEndpoint(), options.guestAccessToken());
        return host.connectAsync()
            .thenCompose(ignored -> guest.connectAsync())
            .thenCompose(ignored -> requestStep(
                "host AuthenticateReq",
                request(host, new AuthenticateReq(options.hostAccessToken()), AuthenticateRes.class)))
            .thenCompose(ignored -> requestStep(
                "guest AuthenticateReq",
                request(guest, new AuthenticateReq(options.guestAccessToken()), AuthenticateRes.class)))
            .thenCompose(ignored -> requestStep(
                "host JoinGameReq",
                request(host, new JoinGameReq(game.gameId()), JoinGameRes.class)))
            .thenCompose(ignored -> requestStep(
                "guest JoinGameReq",
                request(guest, new JoinGameReq(game.gameId()), JoinGameRes.class)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(0)",
                request(host, new PlaceMarkReq(0), PlaceMarkRes.class)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(4)",
                request(guest, new PlaceMarkReq(4), PlaceMarkRes.class)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(1)",
                request(host, new PlaceMarkReq(1), PlaceMarkRes.class)))
            .thenCompose(ignored -> requestStep(
                "guest PlaceMarkReq(8)",
                request(guest, new PlaceMarkReq(8), PlaceMarkRes.class)))
            .thenCompose(ignored -> requestStep(
                "host PlaceMarkReq(2)",
                request(host, new PlaceMarkReq(2), PlaceMarkRes.class)))
            .thenApply(TicTacToeClient::resultFromReply)
            .whenComplete((ignored, error) -> {
                host.close();
                guest.close();
            });
    }

    private static ZLinkStreamConnector playerConnector(String endpoint, String actorId) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint + "/" + actorId),
            ZLinkStreamDispatchMode.MANUAL,
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

    private static TicTacToeClientResult resultFromReply(PlaceMarkRes reply) {
        String winner = reply.state().winner();
        return new TicTacToeClientResult(
            winner,
            winner == null
                ? java.util.List.of()
                : java.util.List.of("GameWon:" + winner));
    }

}
