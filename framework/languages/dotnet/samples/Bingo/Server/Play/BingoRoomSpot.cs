using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play;

internal sealed class BingoRoomSpot(
    IZLinkSpotContext context,
    BingoNotificationPublisher notifications) : IZLinkSpot
{
    public const int RequiredPlayers = 4;

    private readonly List<RoomPlayer> _players = [];
    private readonly Queue<int> _drawDeck = new(Enumerable.Range(1, 75));
    private readonly List<int> _drawnNumbers = [];
    private readonly List<string> _winners = [];
    private IZLinkTimer? _timer;

    public IZLinkSpotContext Context { get; } = context;

    public string Status { get; private set; } = BingoRoomStatus.WaitingForPlayers;

    public void Configure()
    {
        Context.AddActorJoin<BingoRoomJoinHandler, PlayerActor, BingoRoomJoinReq, BingoRoomJoinRes>();
        Context.AddActorPacket<StartBingoGameHandler, PlayerActor>();
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _timer = await Context.AddTimer<BingoRoomTimerHandler>(
                "bingo-draw",
                SampleTimings.DrawPeriod,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        if (_timer is not null)
        {
            await _timer.CancelAsync(cancellationToken).ConfigureAwait(false);
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

        if (Status != BingoRoomStatus.WaitingForPlayers || _players.Count >= RequiredPlayers)
        {
            throw new InvalidOperationException($"Room {request.RoomId} cannot accept more players.");
        }

        actor.SetDisplayName(request.DisplayName);
        actor.JoinRoom(request.RoomId);
        var player = new RoomPlayer(actor, _players.Count, BingoCard.Create());
        _players.Add(player);
        await Context.JoinActorAsync(actor, cancellationToken).ConfigureAwait(false);

        var state = Snapshot();
        await notifications.PublishAsync(PlayerJoinedEvents(player, state), cancellationToken)
            .ConfigureAwait(false);
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

        if (_players.Count != RequiredPlayers)
        {
            throw new InvalidOperationException("Bingo room requires exactly four players before start.");
        }

        if (Status == BingoRoomStatus.WaitingForPlayers)
        {
            Status = BingoRoomStatus.Running;
            var state = Snapshot();
            await notifications.PublishAsync(EventsForAll(BingoRoomEventKind.GameStarted, state), cancellationToken)
                .ConfigureAwait(false);
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
            .ConfigureAwait(false);
        var kind = Status == BingoRoomStatus.Finished
            ? BingoRoomEventKind.GameEnded
            : BingoRoomEventKind.State;
        await notifications.PublishAsync(EventsForAll(kind, state), cancellationToken)
            .ConfigureAwait(false);
    }

    private BingoRoomState Snapshot()
    {
        var hostActorId = _players.Count == 0 ? string.Empty : _players[0].Actor.ActorId;
        return new BingoRoomState(
            Context.SpotRid.ToHex(),
            Status,
            hostActorId,
            Status == BingoRoomStatus.WaitingForPlayers && _players.Count == RequiredPlayers,
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
                player.Actor.ActorId,
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
                player.Actor.ActorId,
                state,
                DrawnNumber: number))
            .ToArray();
    }

    private IReadOnlyList<BingoRoomEvent> EventsForAll(
        BingoRoomEventKind kind,
        BingoRoomState state)
    {
        return _players
            .Select(player => new BingoRoomEvent(kind, player.Actor.ActorId, state))
            .ToArray();
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
