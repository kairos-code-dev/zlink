namespace Bingo.SessionGateway;

internal sealed class PlayServer
{
    private readonly BingoEntrySpot _entrySpot = new();
    private readonly Dictionary<string, PlayerActor> _actors = new(StringComparer.Ordinal);
    private readonly List<BingoRoomSpot> _rooms = [];

    public PlayerActor GetOrCreateActor(string actorId, string displayName)
    {
        if (_actors.TryGetValue(actorId, out var actor))
        {
            return actor;
        }

        actor = new PlayerActor(actorId, displayName);
        _actors.Add(actorId, actor);
        return actor;
    }

    public AllocateBingoRoomRes AllocateRoom(AllocateBingoRoomReq request)
    {
        var actor = GetOrCreateActor(request.ActorId, request.DisplayName);
        var room = FindOpenRoom() ?? CreateRoom();
        var joined = _entrySpot.JoinRoom(actor, room);
        return new AllocateBingoRoomRes(room.RoomId, joined.State);
    }

    public ValueTask<StartBingoGameRes> StartRoomAsync(
        string actorId,
        StartBingoGameReq request,
        CancellationToken cancellationToken)
    {
        return FindRoom(request.RoomId).StartAsync(
            actorId,
            TimeSpan.FromMilliseconds(1),
            cancellationToken);
    }

    public BingoRoomSpot FindRoom(string roomId)
    {
        return _rooms.FirstOrDefault(room => string.Equals(room.RoomId, roomId, StringComparison.Ordinal))
            ?? throw new InvalidOperationException($"Bingo room was not found. roomId={roomId}");
    }

    private BingoRoomSpot? FindOpenRoom()
    {
        return _rooms.FirstOrDefault(room => room.CanAcceptPlayer);
    }

    private BingoRoomSpot CreateRoom()
    {
        var room = new BingoRoomSpot($"bingo-room-{_rooms.Count + 1}");
        _rooms.Add(room);
        return room;
    }
}
