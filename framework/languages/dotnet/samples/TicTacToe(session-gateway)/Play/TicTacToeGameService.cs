using TicTacToe.SessionGateway.Contracts;

namespace TicTacToe.SessionGateway.Play;

internal sealed class TicTacToeGameService
{
    private readonly object _gate = new();
    private readonly Dictionary<string, GameRoom> _games = new(StringComparer.Ordinal);

    public JoinResult Join(
        string gameId,
        string playerId)
    {
        lock (_gate)
        {
            var game = GetOrCreateGame(gameId);
            var (slot, isNewPlayer) = game.GetOrAddPlayer(playerId);
            game.UpdateWaitingStatus();
            var state = game.Snapshot();
            return new JoinResult(
                new JoinGameRes(state),
                BuildJoinedNotifications(game, playerId, slot.Mark, state, isNewPlayer),
                BuildStateNotifications(game, state));
        }
    }

    public MoveResult PlaceMark(
        string gameId,
        string playerId,
        int cell)
    {
        lock (_gate)
        {
            var game = _games.TryGetValue(gameId, out var existing)
                ? existing
                : throw new InvalidOperationException($"Game '{gameId}' does not exist.");
            var state = game.PlaceMark(playerId, cell);
            return new MoveResult(new PlaceMarkRes(state), BuildStateNotifications(game, state));
        }
    }

    private GameRoom GetOrCreateGame(string gameId)
    {
        if (!_games.TryGetValue(gameId, out var game))
        {
            game = new GameRoom(gameId);
            _games.Add(gameId, game);
        }

        return game;
    }

    private IReadOnlyList<GameNotification<PlayerJoinedNotify>> BuildJoinedNotifications(
        GameRoom game,
        string joinedPlayerId,
        string mark,
        GameState state,
        bool isNewPlayer)
    {
        if (!isNewPlayer)
        {
            return [];
        }

        var notification = new PlayerJoinedNotify(state.GameId, joinedPlayerId, mark, state);
        return game.PlayerIds
            .Where(playerId => !string.Equals(playerId, joinedPlayerId, StringComparison.Ordinal))
            .Select(playerId => new GameNotification<PlayerJoinedNotify>(playerId, notification))
            .ToArray();
    }

    private IReadOnlyList<GameNotification<GameStateNotify>> BuildStateNotifications(
        GameRoom game,
        GameState state)
    {
        var notification = new GameStateNotify(state);
        return game.PlayerIds
            .Select(playerId => new GameNotification<GameStateNotify>(playerId, notification))
            .ToArray();
    }

    private sealed class GameRoom(string gameId)
    {
        private readonly Dictionary<string, PlayerSlot> _players = new(StringComparer.Ordinal);
        private readonly char[] _board = Enumerable.Repeat('.', 9).ToArray();
        private string _nextTurn = "X";
        private string _status = "WaitingForPlayers";
        private string? _winner;
        private string? _lastMovePlayerId;
        private int? _lastMoveCell;

        public IReadOnlyCollection<string> PlayerIds => _players.Keys.ToArray();

        public bool ContainsPlayer(string playerId) => _players.ContainsKey(playerId);

        public (PlayerSlot Slot, bool IsNewPlayer) GetOrAddPlayer(string playerId)
        {
            if (_players.TryGetValue(playerId, out var existing))
            {
                return (existing, false);
            }

            var mark = _players.Count switch
            {
                0 => "X",
                1 => "O",
                _ => throw new InvalidOperationException("Tic-tac-toe game already has two players.")
            };

            var slot = new PlayerSlot(playerId, mark);
            _players.Add(playerId, slot);
            return (slot, true);
        }

        public void UpdateWaitingStatus()
        {
            if (_status == "WaitingForPlayers" && _players.Count == 2)
            {
                _status = "InProgress";
            }
        }

        public GameState PlaceMark(string playerId, int cell)
        {
            if (!_players.TryGetValue(playerId, out var slot))
            {
                throw new InvalidOperationException("Player has not joined this game.");
            }

            if (_status != "InProgress")
            {
                throw new InvalidOperationException($"Game is not in progress. status={_status}");
            }

            if (!string.Equals(slot.Mark, _nextTurn, StringComparison.Ordinal))
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

            _board[cell] = slot.Mark[0];
            _lastMovePlayerId = playerId;
            _lastMoveCell = cell;
            AdvanceAfterMove(slot);
            return Snapshot();
        }

        public GameState Snapshot()
        {
            var x = _players.Values.FirstOrDefault(static player => player.Mark == "X");
            var o = _players.Values.FirstOrDefault(static player => player.Mark == "O");
            return new GameState(
                gameId,
                new string(_board),
                _status,
                _winner,
                _nextTurn,
                x?.PlayerId,
                o?.PlayerId,
                _lastMovePlayerId,
                _lastMoveCell);
        }

        private void AdvanceAfterMove(PlayerSlot slot)
        {
            if (HasWon(slot.Mark[0]))
            {
                _status = "Won";
                _winner = slot.PlayerId;
                _nextTurn = string.Empty;
                return;
            }

            if (_board.All(static cell => cell != '.'))
            {
                _status = "Draw";
                _winner = null;
                _nextTurn = string.Empty;
                return;
            }

            _nextTurn = slot.Mark == "X" ? "O" : "X";
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

    internal sealed record PlayerSlot(string PlayerId, string Mark);
}

internal sealed record GameNotification<TMessage>(
    string ActorId,
    TMessage Message);

internal sealed record JoinResult(
    JoinGameRes Reply,
    IReadOnlyList<GameNotification<PlayerJoinedNotify>> PlayerJoinedNotifications,
    IReadOnlyList<GameNotification<GameStateNotify>> StateNotifications);

internal sealed record MoveResult(
    PlaceMarkRes Reply,
    IReadOnlyList<GameNotification<GameStateNotify>> StateNotifications);
