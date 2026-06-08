using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using TicTacToe.Server.Play.Actors;
using TicTacToe.Server.Play.GameSpots.Handlers;

namespace TicTacToe.Server.Play.GameSpots;

sealed class TicTacToeGame(
    IZLinkSpotContext context,
    ILogger<TicTacToeGame> logger) : IZLinkSpot<PlayActor>
{
    private static readonly TimeSpan GameTickPeriod = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan TurnTimeout = TimeSpan.FromSeconds(15);

    private readonly Dictionary<string, PlayerSlot> _players = new(StringComparer.Ordinal);
    private readonly char[] _board = Enumerable.Repeat('.', 9).ToArray();
    private string _nextTurn = "X";
    private string _status = "WaitingForPlayers";
    private string? _winner;
    private string? _lastMoveActorId;
    private int? _lastMoveCell;
    private DateTimeOffset? _turnDeadline;
    private IZLinkTimer? _gameTick;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlayActorPlaceMarkHandler>();
    }

    public ValueTask OnPostActorJoinedAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayActor player,
        Message request,
        CancellationToken cancellationToken)
    {
        var joinRequest = request.Decode<TicTacToeGameJoinReq>();
        var reply = await JoinPlayerAsync(player, joinRequest.GameId, cancellationToken);
        logger.LogInformation(
            "TicTacToeGame: actor join accepted. actor={ActorId}, gameId={GameId}, mark={Mark}",
            player.ActorId,
            joinRequest.GameId,
            reply.State.XActorId == player.ActorId ? "X" : "O");

        return ZLinkSpotActorJoinResult.Accept(reply.Encode());
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. gameId={GameId}, createPayloadBytes={CreatePayloadBytes}",
            Context.SpotRid.ToHex(),
            request.Size);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _gameTick = await Context.AddTimer<TicTacToeGameTimerHandler>(
            "game-tick",
            GameTickPeriod,
            cancellationToken: cancellationToken);
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        if (_gameTick is not null)
        {
            await _gameTick.CancelAsync();
        }
    }

    public async ValueTask<TicTacToeGameJoinRes> JoinPlayerAsync(
        PlayActor actor,
        string gameId,
        CancellationToken cancellationToken)
    {
        var (slot, isNewPlayer) = GetOrAddPlayer(actor);
        actor.JoinGame(gameId);
        UpdateWaitingStatus();

        var state = Snapshot();
        if (isNewPlayer)
        {
            await NotifyPlayerJoinedAsync(actor, slot, state, cancellationToken);
        }

        await BroadcastAsync(state, actor.ActorId, cancellationToken);
        return new TicTacToeGameJoinRes(state);
    }

    public async ValueTask<PlaceMarkRes> PlaceMarkAsync(
        PlayActor actor,
        int cell,
        CancellationToken cancellationToken)
    {
        if (!_players.TryGetValue(actor.ActorId, out var slot))
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
        _lastMoveActorId = actor.ActorId;
        _lastMoveCell = cell;
        AdvanceAfterMove(slot);

        var state = Snapshot();
        await BroadcastAsync(state, actor.ActorId, cancellationToken);
        return new PlaceMarkRes(state);
    }

    internal async ValueTask TickAsync(CancellationToken cancellationToken)
    {
        if (_status != "InProgress"
            || _turnDeadline is not { } deadline
            || DateTimeOffset.UtcNow < deadline)
        {
            return;
        }

        var timedOut = _players.Values.FirstOrDefault(player => player.Mark == _nextTurn);
        var winner = _players.Values.FirstOrDefault(player => player.Mark != _nextTurn);

        _status = "TurnTimedOut";
        _winner = winner?.Actor.ActorId;
        _nextTurn = string.Empty;
        _lastMoveActorId = timedOut?.Actor.ActorId;
        _lastMoveCell = null;
        _turnDeadline = null;

        await BroadcastAsync(Snapshot(), null, cancellationToken);
    }

    private (PlayerSlot Slot, bool IsNewPlayer) GetOrAddPlayer(PlayActor actor)
    {
        if (_players.TryGetValue(actor.ActorId, out var existing))
        {
            existing.Actor = actor;
            return (existing, false);
        }

        var mark = _players.Count switch
        {
            0 => "X",
            1 => "O",
            _ => throw new InvalidOperationException("Tic-tac-toe game already has two players.")
        };

        var slot = new PlayerSlot(actor, mark);
        _players.Add(actor.ActorId, slot);
        return (slot, true);
    }

    private void UpdateWaitingStatus()
    {
        if (_status == "WaitingForPlayers" && _players.Count == 2)
        {
            _status = "InProgress";
            ResetTurnDeadline();
        }
    }

    private void AdvanceAfterMove(PlayerSlot slot)
    {
        if (HasWon(slot.Mark[0]))
        {
            _status = "Won";
            _winner = slot.Actor.ActorId;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        if (_board.All(static cell => cell != '.'))
        {
            _status = "Draw";
            _winner = null;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        _nextTurn = slot.Mark == "X" ? "O" : "X";
        ResetTurnDeadline();
    }

    private void ResetTurnDeadline()
    {
        _turnDeadline = DateTimeOffset.UtcNow.Add(TurnTimeout);
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

    private GameState Snapshot()
    {
        var x = _players.Values.FirstOrDefault(static player => player.Mark == "X");
        var o = _players.Values.FirstOrDefault(static player => player.Mark == "O");
        return new GameState(
            Context.SpotRid.ToHex(),
            new string(_board),
            _status,
            _winner,
            _nextTurn,
            x?.Actor.ActorId,
            o?.Actor.ActorId,
            _lastMoveActorId,
            _lastMoveCell);
    }

    private ValueTask BroadcastAsync(
        GameState state,
        string? excludedActorId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var message = new GameStateNotify(state);
        var recipients = _players.Values
            .Select(static player => player.Actor)
            .Where(actor => !string.Equals(actor.ActorId, excludedActorId, StringComparison.Ordinal))
            .ToArray();
        return SendSessionPushAsync(
            recipients,
            actor => actor.Context.BoundSession.Send(message).Submit(cancellationToken));
    }

    private ValueTask NotifyPlayerJoinedAsync(
        PlayActor joinedActor,
        PlayerSlot joinedSlot,
        GameState state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var message = new PlayerJoinedNotify(
            state.GameId,
            joinedActor.ActorId,
            joinedSlot.Mark,
            state);

        var recipients = _players.Values
            .Select(static player => player.Actor)
            .Where(actor => !string.Equals(actor.ActorId, joinedActor.ActorId, StringComparison.Ordinal))
            .ToArray();
        return SendSessionPushAsync(
            recipients,
            actor => actor.Context.BoundSession.Send(message).Submit(cancellationToken));
    }

    private static async ValueTask SendSessionPushAsync(
        IReadOnlyList<PlayActor> recipients,
        Func<PlayActor, ValueTask> sendAsync)
    {
        foreach (var recipient in recipients)
        {
            await sendAsync(recipient);
        }
    }

    private sealed class PlayerSlot(PlayActor actor, string mark)
    {
        public PlayActor Actor { get; set; } = actor;

        public string Mark { get; } = mark;
    }
}
