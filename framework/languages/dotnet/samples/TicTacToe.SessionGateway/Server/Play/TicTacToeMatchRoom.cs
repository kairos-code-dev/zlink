using TicTacToe.SessionActorDispatch.Play;

namespace TicTacToe.SessionGateway.Play;

internal sealed class TicTacToeMatchRoom(string matchId)
{
    private readonly Dictionary<string, ActorSlot> _actors = new(StringComparer.Ordinal);
    private readonly char[] _board = Enumerable.Repeat('.', 9).ToArray();
    private TicTacToeMark _nextTurn = TicTacToeMark.X;
    private TicTacToeGameStatus _status = TicTacToeGameStatus.WaitingForActors;
    private string? _winnerActorId;
    private string? _lastMoveActorId;
    private int? _lastMoveCell;

    public IReadOnlyCollection<string> ActorIds => _actors.Keys.ToArray();

    public IReadOnlyCollection<PlayerActor> Actors
        => _actors.Values.Select(static slot => slot.Actor).ToArray();

    public string MatchId => matchId;

    public (ActorSlot Slot, bool IsNewActor) GetOrAddActor(PlayerActor actor)
    {
        var actorId = actor.ActorId;
        if (_actors.TryGetValue(actorId, out var existing))
        {
            return (existing, false);
        }

        var mark = _actors.Count switch
        {
            0 => TicTacToeMark.X,
            1 => TicTacToeMark.O,
            _ => throw new InvalidOperationException("Tic-tac-toe match already has two actors.")
        };

        var slot = new ActorSlot(actor, mark);
        _actors.Add(actorId, slot);
        return (slot, true);
    }

    public void StartWhenReady()
    {
        if (_status == TicTacToeGameStatus.WaitingForActors && _actors.Count == 2)
        {
            _status = TicTacToeGameStatus.InProgress;
        }
    }

    public TicTacToeGameSnapshot PlaceMark(string actorId, int cell)
    {
        if (!_actors.TryGetValue(actorId, out var slot))
        {
            throw new InvalidOperationException("Actor has not joined this match.");
        }

        if (_status != TicTacToeGameStatus.InProgress)
        {
            throw new InvalidOperationException($"Match is not in progress. status={_status}");
        }

        if (slot.Mark != _nextTurn)
        {
            throw new InvalidOperationException($"It is {_nextTurn}'s turn.");
        }

        if ((uint)cell >= _board.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(cell), "Cell must be between 0 and 8.");
        }

        if (_board[cell] != '.')
        {
            throw new InvalidOperationException($"Cell {cell} is already occupied.");
        }

        _board[cell] = slot.Mark.ToBoardCell();
        _lastMoveActorId = actorId;
        _lastMoveCell = cell;
        AdvanceAfterMove(slot);
        return Snapshot();
    }

    public TicTacToeGameSnapshot Snapshot()
    {
        var x = _actors.Values.FirstOrDefault(static actor => actor.Mark == TicTacToeMark.X);
        var o = _actors.Values.FirstOrDefault(static actor => actor.Mark == TicTacToeMark.O);
        var turnActorId = _nextTurn switch
        {
            TicTacToeMark.X => x?.ActorId ?? string.Empty,
            TicTacToeMark.O => o?.ActorId ?? string.Empty,
            _ => string.Empty
        };
        return new TicTacToeGameSnapshot(
            matchId,
            new string(_board),
            _status,
            turnActorId,
            _winnerActorId,
            x?.ActorId,
            o?.ActorId,
            _lastMoveActorId,
            _lastMoveCell);
    }

    private void AdvanceAfterMove(ActorSlot slot)
    {
        if (HasWon(slot.Mark.ToBoardCell()))
        {
            _status = TicTacToeGameStatus.Won;
            _winnerActorId = slot.ActorId;
            return;
        }

        if (_board.All(static cell => cell != '.'))
        {
            _status = TicTacToeGameStatus.Draw;
            _winnerActorId = null;
            return;
        }

        _nextTurn = slot.Mark.Other();
    }

    private bool HasWon(char mark)
    {
        ReadOnlySpan<int> lines =
        [
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            0, 3, 6,
            1, 4, 7,
            2, 5, 8,
            0, 4, 8,
            2, 4, 6
        ];

        for (var i = 0; i < lines.Length; i += 3)
        {
            if (_board[lines[i]] == mark
                && _board[lines[i + 1]] == mark
                && _board[lines[i + 2]] == mark)
            {
                return true;
            }
        }

        return false;
    }
}
