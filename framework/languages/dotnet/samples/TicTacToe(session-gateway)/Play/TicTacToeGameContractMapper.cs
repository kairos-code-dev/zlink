using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Play;

namespace TicTacToe.SessionGateway.Play;

internal static class TicTacToeGameContractMapper
{
    public static TicTacToeState ToContract(this TicTacToeGameSnapshot snapshot)
    {
        return new TicTacToeState(
            snapshot.MatchId,
            snapshot.Board,
            snapshot.Status.ToContract(),
            snapshot.TurnActorId,
            snapshot.WinnerActorId,
            snapshot.Status == TicTacToeGameStatus.Draw,
            snapshot.XActorId,
            snapshot.OActorId,
            snapshot.LastMoveActorId,
            snapshot.LastMoveCell);
    }

    public static string ToContract(this TicTacToeMark mark)
    {
        return mark switch
        {
            TicTacToeMark.X => "X",
            TicTacToeMark.O => "O",
            _ => throw new ArgumentOutOfRangeException(nameof(mark), mark, "Unknown mark.")
        };
    }

    public static char ToBoardCell(this TicTacToeMark mark)
    {
        return mark switch
        {
            TicTacToeMark.X => 'X',
            TicTacToeMark.O => 'O',
            _ => throw new ArgumentOutOfRangeException(nameof(mark), mark, "Unknown mark.")
        };
    }

    public static TicTacToeMark Other(this TicTacToeMark mark)
    {
        return mark switch
        {
            TicTacToeMark.X => TicTacToeMark.O,
            TicTacToeMark.O => TicTacToeMark.X,
            _ => throw new ArgumentOutOfRangeException(nameof(mark), mark, "Unknown mark.")
        };
    }

    private static string ToContract(this TicTacToeGameStatus status)
    {
        return status switch
        {
            TicTacToeGameStatus.WaitingForActors => "WaitingForActors",
            TicTacToeGameStatus.InProgress => "InProgress",
            TicTacToeGameStatus.Won => "Won",
            TicTacToeGameStatus.Draw => "Draw",
            _ => throw new ArgumentOutOfRangeException(nameof(status), status, "Unknown game status.")
        };
    }
}
