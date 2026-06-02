package systems.zlink.samples.tictactoe.client;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;

public final class TicTacToeClient {
    private final ZLinkClient frameworkClient;

    public TicTacToeClient(ZLinkClient frameworkClient) {
        this.frameworkClient = frameworkClient;
    }

    public CompletionStage<TicTacToeClientResult> run(TicTacToeClientOptions options) {
        return authenticate(options.hostAccessToken())
            .thenCombine(authenticate(options.guestAccessToken()), (host, guest) -> new Players(host, guest))
            .thenCompose(players -> createGame(options.gameName())
                .thenApply(game -> playScenario(game, players)));
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
                "inproc://zlink-java-sample-tictactoe-stream",
                gameName));
    }

    private TicTacToeClientResult playScenario(CreateGameRes game, Players players) {
        var room = TicTacToeGameDirectory.get(game.gameId());
        GameState joined = room.join(players.host().actorId()).state();
        joined = room.join(players.guest().actorId()).state();
        joined = room.placeMark(players.host().actorId(), 0).state();
        joined = room.placeMark(players.guest().actorId(), 4).state();
        joined = room.placeMark(players.host().actorId(), 1).state();
        joined = room.placeMark(players.guest().actorId(), 8).state();
        joined = room.placeMark(players.host().actorId(), 2).state();
        return new TicTacToeClientResult(joined.winner(), room.pushes());
    }

    private record Players(AuthenticateRes host, AuthenticateRes guest) {
    }
}
