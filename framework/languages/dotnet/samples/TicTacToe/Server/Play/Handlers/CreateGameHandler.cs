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

namespace TicTacToe.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
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

        var created = await spots.CreateAsync<TicTacToeGame>(cancellationToken);
        logger.LogInformation(
            "play: TicTacToeGame spot created. gameId={GameId}, endpoint={Endpoint}",
            created.SpotRid.ToHex(),
            settings.PlayEndpoint);

        return new CreateGameRes(
            created.SpotRid.ToHex(),
            settings.PlayEndpoint,
            request.GameName);
    }
}
