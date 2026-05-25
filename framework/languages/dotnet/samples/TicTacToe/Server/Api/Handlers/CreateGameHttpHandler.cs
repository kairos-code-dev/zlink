using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Server.Api;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Server.Play.EntrySpot;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Server.Play.Sessions;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Zlink.Framework.Runtime.Core;

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
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync<CreateGameRes>(cancellationToken);

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
