package systems.zlink.samples.tictactoe.client;

import java.io.PrintStream;
import java.util.List;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticateRes;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameHttpRes;
import systems.zlink.samples.tictactoe.shared.contracts.GameState;
import systems.zlink.samples.tictactoe.shared.contracts.GameStateNotify;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerJoinedNotify;

public record TicTacToeClientResult(
    CreateGameHttpRes game,
    AuthenticateRes xAuthentication,
    AuthenticateRes oAuthentication,
    JoinGameRes xJoin,
    JoinGameRes oJoin,
    List<PlaceMarkRes> moves,
    List<GameStateNotify> stateNotifications,
    List<PlayerJoinedNotify> playerJoinedNotifications) {
    public GameState finalState() {
        return moves.isEmpty() ? oJoin.state() : moves.get(moves.size() - 1).state();
    }

    public void writeTo(PrintStream output) {
        output.println("api: created game " + game.gameId() + " at " + game.playEndpoint());
        output.println("client-x: AuthenticateRes|" + xAuthentication.actorId());
        output.println("client-o: AuthenticateRes|" + oAuthentication.actorId());
        output.println("client-x: JoinGameRes|" + xJoin.state().gameId() + "|X");
        output.println("client-o: JoinGameRes|" + oJoin.state().gameId() + "|O");
        for (PlaceMarkRes move : moves) {
            output.println("move: PlaceMarkRes|board=" + move.state().board()
                + "|status=" + move.state().status()
                + "|next=" + format(move.state().nextTurn())
                + "|winner=" + format(move.state().winner()));
        }
        for (PlayerJoinedNotify notify : playerJoinedNotifications) {
            output.println("notify: PlayerJoinedNotify|player=" + notify.actorId()
                + "|mark=" + notify.mark()
                + "|game=" + notify.gameId());
        }
        output.println("notifications: " + stateNotifications.size()
            + " GameStateNotify packets received");
        output.println("notifications: " + playerJoinedNotifications.size()
            + " PlayerJoinedNotify packets received");
        output.println("final: board=" + finalState().board()
            + "|status=" + finalState().status()
            + "|winner=" + format(finalState().winner()));
    }

    private static String format(String value) {
        return value == null || value.isEmpty() ? "-" : value;
    }
}
