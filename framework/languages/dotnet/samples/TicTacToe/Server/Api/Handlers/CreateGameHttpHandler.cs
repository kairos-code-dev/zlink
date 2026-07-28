using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Api.Handlers;

internal static class CreateGameHttpHandler
{
    public static async Task<IResult> HandleAsync(
        CreateGameHttpReq request,
        IZLinkSpotManager spots,
        SampleSettings settings,
        ILoggerFactory loggerFactory,
        CancellationToken cancellationToken)
    {
        var logger = loggerFactory.CreateLogger("Game.Api.CreateGame");
        var gameName = !string.IsNullOrWhiteSpace(request.GameName)
            ? request.GameName
            : SampleDefaults.GameName;
        logger.LogInformation("client -> api: create game requested. game={GameName}", gameName);
        var created = await spots
            .Create(SampleTypes.GameSpot)
            .InMesh(SampleNodes.Mesh)
            .Request(new TicTacToeGameCreateReq(
                gameName,
                SampleDefaults.RequiredLevel))
            .Async(cancellationToken);

        logger.LogInformation(
            "api: game Spot ready. roomId={RoomId}, state={State}, game={GameName}",
            created.Spot.SpotId,
            created.State,
            gameName);

        return Results.Ok(new CreateGameHttpRes(
            created.Spot.SpotId,
            settings.PlayEndpoints,
            settings.PlayNodes,
            gameName,
            SampleDefaults.RequiredLevel));
    }
}
