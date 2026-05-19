namespace Bingo.SessionGateway;

internal sealed class ApiServer(PlayServer playServer)
{
    public AuthenticatePlayerRes AuthenticatePlayer(AuthenticatePlayerReq request)
    {
        if (!request.AccessToken.StartsWith("player-", StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Access token must identify a sample player.");
        }

        var actorId = request.AccessToken;
        var displayName = request.AccessToken.Replace("player-", "Player ", StringComparison.Ordinal);
        return new AuthenticatePlayerRes(actorId, displayName);
    }

    public AllocateBingoRoomRes Match(AllocateBingoRoomReq request)
    {
        if (!string.Equals(request.Mode, "four-player", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unsupported bingo mode. mode={request.Mode}");
        }

        return playServer.AllocateRoom(request);
    }
}
