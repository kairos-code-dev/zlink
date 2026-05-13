namespace TicTacToe.Server.Play.Handlers;

sealed class CreateGameHandler(
    IZLinkSpotManager spots,
    SampleSettings settings,
    ILogger<CreateGameHandler> logger)
{
    [ZLinkRequest]
    public async ValueTask<CreateGameRes> CreateAsync(
        CreateGameReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation(
            "api -> play: CreateGameReq received. game={GameName}",
            request.GameName);

        var created = await spots.CreateAsync(SampleTypes.GameSpot, cancellationToken);
        logger.LogInformation(
            "play: TicTacToeGame spot created. gameId={GameId}, endpoint={Endpoint}",
            created.SpotId.Value,
            settings.PlayEndpoint);

        return new CreateGameRes(
            created.SpotId.Value,
            settings.PlayEndpoint,
            request.GameName);
    }
}
