using Bingo.Server.Play.Actors;
using Bingo.Server.Play.BingoRoomSpots.Handlers;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.BingoRoomSpots;

internal sealed class BingoRoomSpot(
    IZLinkSpotContext context,
    BingoNotificationPublisher notifications,
    ILogger<BingoRoomSpotCreatedHandler> createdLogger) : IZLinkSpot
{
    private static readonly BingoRoomSettings DefaultSettings = BingoRoomSettings.Create("four-player", 0);

    private readonly BingoRoomSpotCreatedHandler _createdHandler = new(createdLogger);
    private readonly List<RoomPlayer> _players = [];
    private readonly Queue<int> _drawDeck = new(Enumerable.Range(1, DefaultSettings.MaxDrawNumber));
    private readonly List<int> _drawnNumbers = [];
    private readonly List<string> _winners = [];
    private BingoRoomSettings _settings = DefaultSettings;
    private IZLinkTimer? _timer;

    public IZLinkSpotContext Context { get; } = context;

    public string Status { get; private set; } = BingoRoomStatus.WaitingForPlayers;

    public string RoomName => _settings.RoomName;

    public string Mode => _settings.Mode;

    public int RequiredPlayers => _settings.RequiredPlayers;

    public void Configure()
    {
        Context.AddActorJoin<BingoRoomJoinHandler, PlayerActor, BingoRoomJoinReq, BingoRoomJoinRes>();
        Context.AddActorPacket<StartBingoGameHandler, PlayerActor>();
        Context.AddHandler<BingoRoomActorJoinedHandler>();
        Context.AddHandler<BingoRoomActorLeftHandler>();
    }

    public async ValueTask OnCreateAsync(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        await _createdHandler.HandleAsync(this, createParts, cancellationToken);
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _timer = await Context.AddTimer<BingoRoomTimerHandler>(
                "bingo-draw",
                _settings.DrawPeriod,
                cancellationToken: cancellationToken)
            ;
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        if (_timer is not null)
        {
            await _timer.CancelAsync(cancellationToken);
        }
    }

    public async ValueTask<BingoRoomJoinRes> JoinAsync(
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        var existing = _players.FirstOrDefault(player => player.Actor.ActorId == actor.ActorId);
        if (existing is not null)
        {
            return new BingoRoomJoinRes(Snapshot());
        }

        if (Status != BingoRoomStatus.WaitingForPlayers || _players.Count >= _settings.RequiredPlayers)
        {
            throw new InvalidOperationException($"Room {request.RoomId} cannot accept more players.");
        }

        actor.SetDisplayName(request.DisplayName);
        actor.JoinRoom(request.RoomId);
        var player = new RoomPlayer(actor, _players.Count, BingoCard.Create());
        _players.Add(player);
        await Context.JoinActorAsync(actor, cancellationToken);

        var state = Snapshot();
        await notifications.PublishAsync(PlayerJoinedEvents(player, state), cancellationToken)
            ;
        return new BingoRoomJoinRes(state);
    }

    public async ValueTask<StartBingoGameRes> StartAsync(
        PlayerActor actor,
        StartBingoGameReq request,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(request.RoomId, Context.SpotRid.ToHex(), StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Start request room id does not match actor room.");
        }

        if (_players.Count == 0 || !string.Equals(_players[0].Actor.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Only the host actor can start the bingo room.");
        }

        if (_players.Count != _settings.RequiredPlayers)
        {
            throw new InvalidOperationException(
                $"Bingo room requires exactly {_settings.RequiredPlayers} players before start.");
        }

        if (Status == BingoRoomStatus.WaitingForPlayers)
        {
            Status = BingoRoomStatus.Running;
            var state = Snapshot();
            await notifications.PublishAsync(EventsForAll(BingoRoomEventKind.GameStarted, state), cancellationToken)
                ;
        }

        return new StartBingoGameRes(Snapshot());
    }

    public async ValueTask TickAsync(CancellationToken cancellationToken)
    {
        if (Status != BingoRoomStatus.Running || _drawDeck.Count == 0)
        {
            return;
        }

        var number = _drawDeck.Dequeue();
        _drawnNumbers.Add(number);
        var newlyCompleted = new List<string>();
        foreach (var player in _players)
        {
            var before = player.Card.CompletedLines;
            player.Card.MarkDrawnNumber(number);
            if (player.Card.CompletedLines > before)
            {
                newlyCompleted.Add(player.Actor.ActorId);
            }
        }

        if (newlyCompleted.Count > 0)
        {
            Status = BingoRoomStatus.Finished;
            _winners.AddRange(newlyCompleted);
        }

        var state = Snapshot();
        await notifications.PublishAsync(NumberDrawnEvents(state, number), cancellationToken)
            ;
        var kind = Status == BingoRoomStatus.Finished
            ? BingoRoomEventKind.GameEnded
            : BingoRoomEventKind.State;
        await notifications.PublishAsync(EventsForAll(kind, state), cancellationToken)
            ;
    }

    private BingoRoomState Snapshot()
    {
        var hostActorId = _players.Count == 0 ? string.Empty : _players[0].Actor.ActorId;
        return new BingoRoomState(
            Context.SpotRid.ToHex(),
            Status,
            hostActorId,
            Status == BingoRoomStatus.WaitingForPlayers && _players.Count == _settings.RequiredPlayers,
            _drawnNumbers.Count,
            _drawnNumbers.Count == 0 ? null : _drawnNumbers[^1],
            _drawnNumbers.ToArray(),
            _players.Select(player => player.ToState(hostActorId)).ToArray(),
            _winners.ToArray());
    }

    private IReadOnlyList<BingoRoomEvent> PlayerJoinedEvents(
        RoomPlayer joined,
        BingoRoomState state)
    {
        return _players
            .Select(player => new BingoRoomEvent(
                BingoRoomEventKind.PlayerJoined,
                player.Actor,
                state,
                joined.Actor.ActorId,
                joined.Actor.DisplayName,
                joined.Seat,
                joined.Seat == 0))
            .ToArray();
    }

    private IReadOnlyList<BingoRoomEvent> NumberDrawnEvents(
        BingoRoomState state,
        int number)
    {
        return _players
            .Select(player => new BingoRoomEvent(
                BingoRoomEventKind.NumberDrawn,
                player.Actor,
                state,
                DrawnNumber: number))
            .ToArray();
    }

    private IReadOnlyList<BingoRoomEvent> EventsForAll(
        BingoRoomEventKind kind,
        BingoRoomState state)
    {
        return _players
            .Select(player => new BingoRoomEvent(kind, player.Actor, state))
            .ToArray();
    }

    public void ApplySettings(BingoRoomSettings settings)
    {
        if (settings.RequiredPlayers <= 0)
        {
            throw new InvalidOperationException("Bingo room requires at least one player.");
        }

        if (settings.MaxDrawNumber <= 0)
        {
            throw new InvalidOperationException("Bingo room requires at least one draw number.");
        }

        if (settings.DrawPeriod <= TimeSpan.Zero)
        {
            throw new InvalidOperationException("Bingo room draw period must be positive.");
        }

        _settings = settings;
        _drawDeck.Clear();
        foreach (var number in Enumerable.Range(1, settings.MaxDrawNumber))
        {
            _drawDeck.Enqueue(number);
        }
    }

    private sealed record RoomPlayer(
        PlayerActor Actor,
        int Seat,
        BingoCard Card)
    {
        public BingoPlayerState ToState(string hostActorId)
        {
            return new BingoPlayerState(
                Actor.ActorId,
                Actor.DisplayName,
                Seat,
                string.Equals(Actor.ActorId, hostActorId, StringComparison.Ordinal),
                Card.NumbersSnapshot(),
                Card.MarksSnapshot(),
                Card.CompletedLines);
        }
    }
}
