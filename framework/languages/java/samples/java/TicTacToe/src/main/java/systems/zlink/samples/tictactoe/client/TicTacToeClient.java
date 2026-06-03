package systems.zlink.samples.tictactoe.client;

import java.net.URI;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public final class TicTacToeClient {
    private final ZLinkClient frameworkClient;

    public TicTacToeClient(ZLinkClient frameworkClient) {
        this.frameworkClient = frameworkClient;
    }

    public CompletionStage<TicTacToeClientResult> run(TicTacToeClientOptions options) {
        return authenticate(options.hostAccessToken())
            .thenCombine(authenticate(options.guestAccessToken()), (host, guest) -> new Players(host, guest))
            .thenCompose(players -> createGame(options.gameName())
                .thenCompose(game -> playScenario(options, game, players)));
    }

    private CompletionStage<AuthenticateRes> authenticate(String accessToken) {
        return frameworkClient
            .requestToChannel("tictactoe-api", accessToken)
            .packetName("AuthenticatePlayer")
            .submitAsync(String.class)
            .thenApply(AuthenticateRes::new);
    }

    private CompletionStage<CreateGameRes> createGame(String gameName) {
        return frameworkClient
            .requestToChannel("tictactoe-api", gameName)
            .packetName("CreateGame")
            .submitAsync(String.class)
            .thenApply(gameId -> new CreateGameRes(
                gameId,
                TicTacToeSampleDefaults.PlayEndpoint,
                gameName));
    }

    private CompletionStage<TicTacToeClientResult> playScenario(
        TicTacToeClientOptions options,
        CreateGameRes game,
        Players players) {
        ZLinkStreamConnector host = playerConnector(options.playEndpoint(), players.host().actorId());
        ZLinkStreamConnector guest = playerConnector(options.playEndpoint(), players.guest().actorId());
        return host.connectAsync()
            .thenCompose(ignored -> guest.connectAsync())
            .thenCompose(ignored -> request(host, "AuthenticateReq", players.host().actorId()))
                .thenCompose(ignored -> request(guest, "AuthenticateReq", players.guest().actorId()))
                .thenCompose(ignored -> request(host, "JoinGameReq", game.gameId() + "|" + players.host().actorId()))
                .thenCompose(ignored -> request(guest, "JoinGameReq", game.gameId() + "|" + players.guest().actorId()))
                .thenCompose(ignored -> request(host, "PlaceMarkReq", game.gameId() + "|" + players.host().actorId() + "|0"))
                .thenCompose(ignored -> request(guest, "PlaceMarkReq", game.gameId() + "|" + players.guest().actorId() + "|4"))
                .thenCompose(ignored -> request(host, "PlaceMarkReq", game.gameId() + "|" + players.host().actorId() + "|1"))
                .thenCompose(ignored -> request(guest, "PlaceMarkReq", game.gameId() + "|" + players.guest().actorId() + "|8"))
                .thenCompose(ignored -> request(host, "PlaceMarkReq", game.gameId() + "|" + players.host().actorId() + "|2"))
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

    private static CompletionStage<String> request(ZLinkStreamConnector connector, String packetName, String body) {
        return connector.request(new ZLinkStreamEncodedPayload(
                packetName,
                Message.from(body),
                Map.of()))
            .packetName(packetName)
            .submitAsync()
            .thenApply(reply -> reply.payload().toUtf8String());
    }

    private static TicTacToeClientResult resultFromReply(String reply) {
        String[] parts = reply.split("\\|", 2);
        String winner = parts[0].isBlank() ? null : parts[0];
        return new TicTacToeClientResult(
            winner,
            parts.length == 2 && !parts[1].isBlank()
                ? java.util.List.of(parts[1].split(","))
                : java.util.List.of());
    }

    private record Players(AuthenticateRes host, AuthenticateRes guest) {
    }
}
