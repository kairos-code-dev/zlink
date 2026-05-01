using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

public sealed record TicTacToeClientResult(
    CreateGameHttpRes Game,
    AuthenticateRes XAuthentication,
    AuthenticateRes OAuthentication,
    JoinGameRes XJoin,
    JoinGameRes OJoin,
    IReadOnlyList<PlaceMarkRes> Moves,
    IReadOnlyList<GameStateNotify> StateNotifications,
    IReadOnlyList<PlayerJoinedNotify> PlayerJoinedNotifications)
{
    public GameState FinalState => Moves.Count > 0 ? Moves[^1].State : OJoin.State;

    public void WriteTo(TextWriter writer)
    {
        writer.WriteLine($"api: created game {Game.GameId} at {Game.PlayEndpoint}");
        writer.WriteLine($"client-x: {nameof(AuthenticateRes)}|{XAuthentication.ActorId}");
        writer.WriteLine($"client-o: {nameof(AuthenticateRes)}|{OAuthentication.ActorId}");
        writer.WriteLine($"client-x: {nameof(JoinGameRes)}|{XJoin.State.GameId}|X");
        writer.WriteLine($"client-o: {nameof(JoinGameRes)}|{OJoin.State.GameId}|O");

        foreach (var move in Moves)
        {
            writer.WriteLine(
                $"move: {nameof(PlaceMarkRes)}|board={move.State.Board}|status={move.State.Status}|next={Format(move.State.NextTurn)}|winner={move.State.Winner ?? "-"}");
        }

        foreach (var notify in PlayerJoinedNotifications)
        {
            writer.WriteLine(
                $"notify: {nameof(PlayerJoinedNotify)}|player={notify.ActorId}|mark={notify.Mark}|game={notify.GameId}");
        }

        writer.WriteLine($"notifications: {StateNotifications.Count} {nameof(GameStateNotify)} packets received");
        writer.WriteLine($"notifications: {PlayerJoinedNotifications.Count} {nameof(PlayerJoinedNotify)} packets received");
        writer.WriteLine($"final: board={FinalState.Board}|status={FinalState.Status}|winner={FinalState.Winner ?? "-"}");
    }

    private static string Format(string value)
        => string.IsNullOrEmpty(value) ? "-" : value;
}
