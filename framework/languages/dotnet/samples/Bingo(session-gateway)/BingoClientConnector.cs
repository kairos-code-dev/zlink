namespace Bingo.SessionGateway;

internal sealed class BingoClientConnector(SessionServer sessionServer)
{
    private readonly List<object> _pushes = [];
    private PlayerActor? _actor;

    public string ActorId => _actor?.ActorId ?? string.Empty;

    public IReadOnlyList<object> Pushes => _pushes;

    public AuthenticateRes Authenticate(string accessToken)
    {
        return sessionServer.Authenticate(this, new AuthenticateReq(accessToken));
    }

    public MatchBingoRes Match(string mode)
    {
        return sessionServer.Match(this, new MatchBingoReq(mode));
    }

    public ValueTask<StartBingoGameRes> StartAsync(
        string roomId,
        CancellationToken cancellationToken)
    {
        return sessionServer.StartAsync(this, new StartBingoGameReq(roomId), cancellationToken);
    }

    public void BindActor(PlayerActor actor)
    {
        _actor = actor;
    }

    public PlayerActor RequireActor()
    {
        return _actor ?? throw new InvalidOperationException("Client connector is not authenticated.");
    }

    public void ReceivePush(object notification)
    {
        _pushes.Add(notification);
    }
}
