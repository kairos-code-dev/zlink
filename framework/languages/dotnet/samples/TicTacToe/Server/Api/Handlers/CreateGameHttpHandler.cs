using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace TicTacToe.Server.Api.Handlers;

internal static class CreateGameHttpHandler
{
    public static async Task<IResult> HandleAsync(
        CreateGameHttpReq request,
        IZLinkChannelClient client,
        ILoggerFactory loggerFactory,
        CancellationToken cancellationToken)
    {
        var logger = loggerFactory.CreateLogger("Game.Api.CreateGame");
        var gameName = request.GameName ?? SampleDefaults.GameName;
        logger.LogInformation("client -> api: create game requested. game={GameName}", gameName);
        logger.LogInformation("api -> play: requesting CreateGameReq. game={GameName}", gameName);

        var reply = await client.RequestToChannel(
                SampleChannels.Play,
                new CreateGameReq(gameName))
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync<CreateGameRes>(cancellationToken);

        logger.LogInformation(
            "play -> api: game created. roomId={RoomId}, endpoint={Endpoint}, game={GameName}",
            reply.RoomId,
            reply.PlayEndpoint,
            reply.GameName);
        logger.LogInformation(
            "api -> client: returning game info. roomId={RoomId}, endpoint={Endpoint}",
            reply.RoomId,
            reply.PlayEndpoint);

        return Results.Ok(new CreateGameHttpRes(
            reply.RoomId,
            reply.PlayEndpoint,
            reply.GameName));
    }
}
