namespace Bingo.SessionGateway;

internal sealed class SessionServer(
    ApiServer apiServer,
    PlayServer playServer)
{
    public AuthenticateRes Authenticate(BingoClientConnector connector, AuthenticateReq request)
    {
        var player = apiServer.AuthenticatePlayer(new AuthenticatePlayerReq(request.AccessToken));
        var actor = playServer.GetOrCreateActor(player.ActorId, player.DisplayName);
        actor.BindSession(connector);
        connector.BindActor(actor);
        return new AuthenticateRes(player.ActorId, player.DisplayName);
    }

    public MatchBingoRes Match(BingoClientConnector connector, MatchBingoReq request)
    {
        var actor = connector.RequireActor();
        var allocated = apiServer.Match(new AllocateBingoRoomReq(
            actor.ActorId,
            actor.DisplayName,
            request.Mode));
        return new MatchBingoRes(allocated.RoomId, allocated.State);
    }

    public ValueTask<StartBingoGameRes> StartAsync(
        BingoClientConnector connector,
        StartBingoGameReq request,
        CancellationToken cancellationToken)
    {
        var actor = connector.RequireActor();
        return playServer.StartRoomAsync(actor.ActorId, request, cancellationToken);
    }
}
