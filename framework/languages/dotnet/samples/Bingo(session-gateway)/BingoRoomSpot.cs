namespace Bingo.SessionGateway;

internal sealed class BingoRoomSpot
{
    public const int RequiredPlayers = 4;

    private readonly BingoRoomRoster _roster = new();
    private readonly List<int> _drawnNumbers = [];
    private readonly List<string> _winners = [];
    private readonly BingoDrawDeck _drawDeck = new();

    public BingoRoomSpot(string roomId)
    {
        RoomId = roomId;
    }

    public string RoomId { get; }

    public bool CanAcceptPlayer => _roster.CanAcceptPlayer && _winners.Count == 0;

    public BingoRoomJoinRes Join(PlayerActor actor)
    {
        var join = _roster.Join(actor, RoomId);
        if (join.IsNew)
        {
            Broadcast(new PlayerJoinedNotify(
                RoomId,
                actor.ActorId,
                actor.DisplayName,
                join.Seat,
                join.IsHost,
                Snapshot()));
        }

        return new BingoRoomJoinRes(Snapshot());
    }

    public async ValueTask<StartBingoGameRes> StartAsync(
        string actorId,
        TimeSpan drawInterval,
        CancellationToken cancellationToken)
    {
        if (_roster.Count == 0 || !string.Equals(_roster.HostActorId, actorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Only the host actor can start the bingo room.");
        }

        if (_roster.Count != RequiredPlayers)
        {
            throw new InvalidOperationException("Bingo room requires exactly four players before start.");
        }

        if (_winners.Count > 0)
        {
            return new StartBingoGameRes(Snapshot());
        }

        Status = BingoRoomStatus.Running;
        var started = Snapshot();
        Broadcast(new BingoGameStartedNotify(started));

        while (Status == BingoRoomStatus.Running && _drawDeck.HasNext)
        {
            await Task.Delay(drawInterval, cancellationToken).ConfigureAwait(false);
            DrawNextNumber();
        }

        return new StartBingoGameRes(Snapshot());
    }

    public string Status { get; private set; } = BingoRoomStatus.WaitingForPlayers;

    public BingoRoomState Snapshot()
    {
        return new BingoRoomState(
            RoomId,
            Status,
            _roster.HostActorId,
            Status == BingoRoomStatus.WaitingForPlayers && _roster.Count == RequiredPlayers,
            _drawnNumbers.Count,
            _drawnNumbers.Count == 0 ? null : _drawnNumbers[^1],
            _drawnNumbers.ToArray(),
            _roster.SnapshotPlayers(),
            _winners.ToArray());
    }

    private void DrawNextNumber()
    {
        var number = _drawDeck.Draw();
        _drawnNumbers.Add(number);
        var newlyCompleted = _roster.MarkDrawnNumber(number);

        if (newlyCompleted.Count > 0)
        {
            Status = BingoRoomStatus.Finished;
            _winners.AddRange(newlyCompleted);
        }

        var snapshot = Snapshot();
        Broadcast(new BingoNumberDrawnNotify(RoomId, snapshot.DrawSeq, number, snapshot));
        if (Status == BingoRoomStatus.Finished)
        {
            Broadcast(new BingoGameEndedNotify(snapshot));
        }
        else
        {
            Broadcast(new BingoStateNotify(snapshot));
        }
    }

    private void Broadcast(object notification)
    {
        _roster.Broadcast(notification);
    }
}
