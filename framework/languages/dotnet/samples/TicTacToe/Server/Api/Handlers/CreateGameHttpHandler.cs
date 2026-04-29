namespace TicTacToe.Server.Api.Handlers;

internal static class CreateGameHttpHandler
{
    public static async Task<IResult> HandleAsync(
        CreateGameHttpReq request,
        IZLinkClient client,
        ILoggerFactory loggerFactory,
        CancellationToken cancellationToken)
    {
        var logger = loggerFactory.CreateLogger("Game.Api.CreateGame");
        var gameName = request.GameName ?? SampleDefaults.GameName;
        logger.LogInformation("client -> api: create game requested. game={GameName}", gameName);
        logger.LogInformation("api -> play: requesting CreateGameReq. game={GameName}", gameName);

        var reply = await client.Request(
                SampleChannels.Play,
                new CreateGameReq(gameName))
            .WithTimeout(SampleTimeouts.Request)
            .Async<CreateGameRes>(cancellationToken);

        logger.LogInformation(
            "play -> api: game created. gameId={GameId}, endpoint={Endpoint}, game={GameName}",
            reply.GameId,
            reply.PlayEndpoint,
            reply.GameName);
        logger.LogInformation(
            "api -> client: returning game info. gameId={GameId}, endpoint={Endpoint}",
            reply.GameId,
            reply.PlayEndpoint);

        return Results.Ok(new CreateGameHttpRes(
            reply.GameId,
            reply.PlayEndpoint,
            reply.GameName));
    }
}
