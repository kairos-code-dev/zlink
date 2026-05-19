namespace Bingo.SessionGateway;

internal sealed class BingoRoomRoster
{
    private readonly List<RoomPlayer> _players = [];

    public int Count => _players.Count;

    public bool CanAcceptPlayer => _players.Count < BingoRoomSpot.RequiredPlayers;

    public string HostActorId => _players.Count == 0 ? string.Empty : _players[0].Actor.ActorId;

    public RoomJoin Join(PlayerActor actor, string roomId)
    {
        var existing = _players.FirstOrDefault(player => player.Actor.ActorId == actor.ActorId);
        if (existing is not null)
        {
            return new RoomJoin(existing.Seat, existing.Seat == 0, false);
        }

        if (!CanAcceptPlayer)
        {
            throw new InvalidOperationException($"Room {roomId} cannot accept actor {actor.ActorId}.");
        }

        var seat = _players.Count;
        _players.Add(new RoomPlayer(actor, seat, BingoCard.CreateForSeat(seat)));
        actor.JoinRoom(roomId);
        return new RoomJoin(seat, seat == 0, true);
    }

    public IReadOnlyList<string> MarkDrawnNumber(int number)
    {
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

        return newlyCompleted;
    }

    public void Broadcast(object notification)
    {
        foreach (var player in _players)
        {
            player.Actor.Push(notification);
        }
    }

    public IReadOnlyList<BingoPlayerState> SnapshotPlayers()
    {
        var hostActorId = HostActorId;
        return _players
            .Select(player => player.ToState(hostActorId))
            .ToArray();
    }

    public sealed record RoomJoin(
        int Seat,
        bool IsHost,
        bool IsNew);

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
