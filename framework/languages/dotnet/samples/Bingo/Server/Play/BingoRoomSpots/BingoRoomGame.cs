using Bingo.Server.Play;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots;

internal sealed class BingoRoomGame(string roomId, BingoRoomSettings settings)
{
    private readonly List<BingoRoomPlayer> _players = [];
    private readonly Queue<int> _drawDeck = [];
    private readonly List<int> _drawnNumbers = [];
    private readonly List<string> _winners = [];
    private BingoRoomSettings _settings = settings;

    public string Status { get; private set; } = BingoRoomStatus.WaitingForPlayers;

    public string RoomName => _settings.RoomName;

    public string Mode => _settings.Mode;

    public int RequiredPlayers => _settings.RequiredPlayers;

    public bool IsReadyToDraw => Status == BingoRoomStatus.Running
                                 && _players.Count == _settings.RequiredPlayers
                                 && _players.All(static player => player.HasCard);

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
        _drawDeck.Clear();
        foreach (var number in Enumerable.Range(1, newSettings.MaxDrawNumber))
        {
            _drawDeck.Enqueue(number);
        }
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
            events.AddRange(EventsForAll(BingoRoomEventKind.GameStarted, Snapshot()));
        }

        return new BingoGameChange(Snapshot(), events);
    }

    public BingoGameChange SubmitCard(string actorId, BingoCard card)
    {
        if (Status != BingoRoomStatus.Running)
        {
            throw new InvalidOperationException($"Room is not accepting cards. status={Status}");
        }

        RequirePlayer(actorId).SubmitCard(card);
        return new BingoGameChange(
            Snapshot(),
            [],
            ShouldStartDrawTimer: IsReadyToDraw);
    }

    public BingoGameChange DrawNextNumber()
    {
        if (Status != BingoRoomStatus.Running)
        {
            return new BingoGameChange(Snapshot(), [], ShouldStopDrawTimer: true);
        }

        if (!IsReadyToDraw)
        {
            return new BingoGameChange(Snapshot(), []);
        }

        if (_drawDeck.Count == 0)
        {
            Status = BingoRoomStatus.Finished;
            return new BingoGameChange(
                Snapshot(),
                EventsForAll(BingoRoomEventKind.GameEnded, Snapshot()),
                ShouldStopDrawTimer: true);
        }

        var number = _drawDeck.Dequeue();
        _drawnNumbers.Add(number);

        var newlyCompleted = new List<string>();
        foreach (var player in _players)
        {
            var card = player.RequireCard();
            var before = card.CompletedLines;
            card.MarkDrawnNumber(number);
            if (card.CompletedLines > before)
            {
                newlyCompleted.Add(player.ActorId);
            }
        }

        if (newlyCompleted.Count > 0)
        {
            Status = BingoRoomStatus.Finished;
            _winners.AddRange(newlyCompleted);
        }
        else if (_drawDeck.Count == 0)
        {
            Status = BingoRoomStatus.Finished;
        }

        var state = Snapshot();
        var events = new List<BingoGameEvent>();
        events.AddRange(NumberDrawnEvents(state, number));
        var finished = Status == BingoRoomStatus.Finished;
        if (finished)
        {
            events.AddRange(EventsForAll(BingoRoomEventKind.GameEnded, state));
        }

        return new BingoGameChange(
            state,
            events,
            ShouldStopDrawTimer: finished);
    }

    public BingoRoomState Snapshot()
    {
        var hostActorId = _players.Count == 0 ? string.Empty : _players[0].ActorId;
        return new BingoRoomState(
            roomId,
            Status,
            hostActorId,
            Status == BingoRoomStatus.WaitingForPlayers && _players.Count == _settings.RequiredPlayers,
            _drawnNumbers.Count,
            _drawnNumbers.Count == 0 ? null : _drawnNumbers[^1],
            _drawnNumbers.ToArray(),
            _players.Select(player => player.ToState(hostActorId)).ToArray(),
            _winners.ToArray());
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

    private BingoRoomPlayer RequirePlayer(string actorId)
    {
        return _players.FirstOrDefault(player => player.ActorId == actorId)
               ?? throw new InvalidOperationException("Player has not joined this room.");
    }

    private sealed class BingoRoomPlayer(
        string actorId,
        string displayName,
        int seat)
    {
        private BingoCard? _card;

        public string ActorId { get; } = actorId;

        public string DisplayName { get; } = displayName;

        public int Seat { get; } = seat;

        public bool HasCard => _card is not null;

        public void SubmitCard(BingoCard card)
        {
            if (_card is not null)
            {
                throw new InvalidOperationException("Bingo card has already been submitted.");
            }

            _card = card;
        }

        public BingoCard RequireCard()
        {
            return _card ?? throw new InvalidOperationException("Bingo card has not been submitted.");
        }

        public BingoPlayerState ToState(string hostActorId)
        {
            var card = _card;
            return new BingoPlayerState(
                ActorId,
                DisplayName,
                Seat,
                string.Equals(ActorId, hostActorId, StringComparison.Ordinal),
                card?.NumbersSnapshot() ?? [],
                card?.MarksSnapshot() ?? [],
                card?.CompletedLines ?? 0);
        }
    }
}
