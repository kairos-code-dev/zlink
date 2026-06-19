using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Domain.Bingo;

internal sealed class BingoRoomGame(string roomId, BingoRoomSettings settings)
{
    private readonly List<BingoRoomPlayer> _players = [];
    private BingoRoomSettings _settings = settings;
    private BingoGame? _game;

    public string Status { get; private set; } = BingoRoomStatus.WaitingForPlayers;

    public bool IsReadyToDraw => _game?.IsReadyToDraw == true
                                 && Status == BingoRoomStatus.Running;

    public void ApplySettings(BingoRoomSettings newSettings)
    {
        if (newSettings.RequiredPlayers <= 0)
        {
            throw new InvalidOperationException("Bingo room requires at least one player.");
        }

        if (newSettings.MaxDrawNumber <= 0)
        {
            throw new InvalidOperationException("Bingo room requires at least one draw number.");
        }

        _settings = newSettings;
        _game = null;
    }

    public BingoGameChange JoinPlayer(string actorId, string displayName)
    {
        var existing = _players.FirstOrDefault(player => player.ActorId == actorId);
        if (existing is not null)
        {
            return new BingoGameChange(Snapshot(), []);
        }

        if (Status != BingoRoomStatus.WaitingForPlayers || _players.Count >= _settings.RequiredPlayers)
        {
            throw new InvalidOperationException($"Room {roomId} cannot accept more players.");
        }

        var player = new BingoRoomPlayer(actorId, displayName, _players.Count);
        _players.Add(player);

        var events = new List<BingoGameEvent>();
        var joinedState = Snapshot();
        events.AddRange(PlayerJoinedEvents(player, joinedState));

        if (_players.Count == _settings.RequiredPlayers)
        {
            Status = BingoRoomStatus.Running;
            _game = new BingoGame(_settings, _players.Select(static roomPlayer => roomPlayer.ActorId).ToArray());
            events.AddRange(EventsForExistingPlayers(
                BingoRoomEventKind.GameStarted,
                Snapshot(),
                player.ActorId));
        }

        return new BingoGameChange(Snapshot(), events);
    }

    public BingoGameChange SubmitCard(string actorId, BingoCard card)
    {
        if (Status != BingoRoomStatus.Running)
        {
            throw new InvalidOperationException($"Room is not accepting cards. status={Status}");
        }

        RequireGame().SubmitCard(actorId, card);
        return new BingoGameChange(Snapshot(), []);
    }

    public BingoGameChange DrawNextNumber()
    {
        if (Status != BingoRoomStatus.Running)
        {
            return new BingoGameChange(Snapshot(), [], ShouldStopDrawTimer: true);
        }

        var result = RequireGame().DrawNextNumber();
        if (result.IsFinished)
        {
            Status = BingoRoomStatus.Finished;
        }

        var state = Snapshot();
        var events = new List<BingoGameEvent>();
        if (result.Number is { } number)
        {
            events.AddRange(NumberDrawnEvents(state, number));
        }

        if (Status == BingoRoomStatus.Finished)
        {
            events.AddRange(EventsForAll(BingoRoomEventKind.GameEnded, state));
        }

        return new BingoGameChange(
            state,
            events,
            ShouldStopDrawTimer: Status == BingoRoomStatus.Finished);
    }

    public BingoRoomState Snapshot()
    {
        var hostActorId = _players.Count == 0 ? string.Empty : _players[0].ActorId;
        var game = _game?.Snapshot() ?? new BingoGameSnapshot(0, null, [], []);
        var state = new BingoRoomState
        {
            RoomId = roomId,
            Status = Status,
            HostActorId = hostActorId,
            CanStart = Status == BingoRoomStatus.WaitingForPlayers && _players.Count == _settings.RequiredPlayers,
            DrawSeq = game.DrawSeq,
            DrawnNumbers = { game.DrawnNumbers },
            Players = { _players.Select(player => player.ToState(hostActorId, _game)) },
            Winners = { game.Winners },
        };
        if (game.LastDrawnNumber is { } lastDrawnNumber)
        {
            state.LastDrawnNumber = lastDrawnNumber;
        }

        return state;
    }

    private IReadOnlyList<BingoGameEvent> PlayerJoinedEvents(
        BingoRoomPlayer joined,
        BingoRoomState state)
    {
        return _players
            .Where(player => !string.Equals(player.ActorId, joined.ActorId, StringComparison.Ordinal))
            .Select(player => new BingoGameEvent(
                BingoRoomEventKind.PlayerJoined,
                player.ActorId,
                state,
                joined.ActorId,
                joined.DisplayName,
                joined.Seat,
                joined.Seat == 0))
            .ToArray();
    }

    private IReadOnlyList<BingoGameEvent> NumberDrawnEvents(
        BingoRoomState state,
        int number)
    {
        return _players
            .Select(player => new BingoGameEvent(
                BingoRoomEventKind.NumberDrawn,
                player.ActorId,
                state,
                DrawnNumber: number))
            .ToArray();
    }

    private IReadOnlyList<BingoGameEvent> EventsForAll(
        BingoRoomEventKind kind,
        BingoRoomState state)
    {
        return _players
            .Select(player => new BingoGameEvent(kind, player.ActorId, state))
            .ToArray();
    }

    private IReadOnlyList<BingoGameEvent> EventsForExistingPlayers(
        BingoRoomEventKind kind,
        BingoRoomState state,
        string joinedActorId)
    {
        return _players
            .Where(player => !string.Equals(player.ActorId, joinedActorId, StringComparison.Ordinal))
            .Select(player => new BingoGameEvent(kind, player.ActorId, state))
            .ToArray();
    }

    private BingoGame RequireGame()
    {
        return _game ?? throw new InvalidOperationException("Bingo game has not started.");
    }

    private sealed record BingoRoomPlayer(
        string ActorId,
        string DisplayName,
        int Seat)
    {
        public BingoPlayerState ToState(string hostActorId, BingoGame? game)
        {
            var card = game?.CardSnapshot(ActorId) ?? BingoCardSnapshot.Empty;
            return new BingoPlayerState
            {
                ActorId = ActorId,
                DisplayName = DisplayName,
                Seat = Seat,
                IsHost = string.Equals(ActorId, hostActorId, StringComparison.Ordinal),
                Card = { card.Numbers },
                Marks = { card.Marks },
                CompletedLines = card.CompletedLines,
            };
        }
    }
}
