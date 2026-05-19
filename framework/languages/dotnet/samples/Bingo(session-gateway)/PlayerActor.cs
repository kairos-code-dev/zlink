namespace Bingo.SessionGateway;

internal sealed class PlayerActor
{
    private readonly List<object> _notifications = [];

    public PlayerActor(string actorId, string displayName)
    {
        ActorId = actorId;
        DisplayName = displayName;
    }

    public string ActorId { get; }

    public string DisplayName { get; }

    public string? JoinedRoomId { get; private set; }

    public IReadOnlyList<object> Notifications => _notifications;

    public void BindSession(BingoClientConnector session)
    {
        Session = session;
    }

    public BingoClientConnector? Session { get; private set; }

    public void JoinRoom(string roomId)
    {
        JoinedRoomId = roomId;
    }

    public void Push(object notification)
    {
        _notifications.Add(notification);
        Session?.ReceivePush(notification);
    }
}
