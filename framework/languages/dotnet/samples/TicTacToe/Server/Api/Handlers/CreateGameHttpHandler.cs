using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace TicTacToe.Server.Api.Handlers;

internal static class CreateGameHttpHandler
{
    public static async Task<IResult> HandleAsync(
        CreateGameHttpReq request,
        IZLinkRouteClient client,
        SampleSettings settings,
        ILoggerFactory loggerFactory,
        CancellationToken cancellationToken)
    {
        var logger = loggerFactory.CreateLogger("Game.Api.CreateGame");
        var gameName = !string.IsNullOrWhiteSpace(request.GameName)
            ? request.GameName
            : SampleDefaults.GameName;
        logger.LogInformation("client -> api: create game requested. game={GameName}", gameName);
        logger.LogInformation(
            "api -> play: requesting CreateGameReq. game={GameName}",
            gameName);

        var reply = await client.RequestToChannel(
                SampleNodes.Mesh,
                SampleChannels.Play,
                new CreateGameReq(gameName))
            .Async<CreateGameRes>(cancellationToken);

        logger.LogInformation(
            "play -> api: game created. roomId={RoomId}, endpoint={Endpoint}, game={GameName}",
            reply.RoomId,
            reply.OwnerPlayEndpoint,
            reply.GameName);
        logger.LogInformation(
            "api -> client: returning game info. roomId={RoomId}, endpoint={Endpoint}",
            reply.RoomId,
            reply.OwnerPlayEndpoint);

        return Results.Ok(new CreateGameHttpRes(
            reply.RoomId,
            reply.OwnerPlayEndpoint,
            reply.PlayEndpoints,
            reply.PlayNodes,
            reply.GameName,
            reply.RequiredLevel));
    }
}
