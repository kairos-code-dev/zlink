using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomSpotCreatedHandler(
    ILogger<BingoRoomSpotCreatedHandler> logger)
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var settings = DecodeSettings(createParts);
        spot.ApplySettings(settings);
        logger.LogInformation(
            "bingo room: created. room={RoomId}, roomName={RoomName}, mode={Mode}, requiredPlayers={RequiredPlayers}, maxDrawNumber={MaxDrawNumber}, drawPeriod={DrawPeriod}",
            spot.Context.SpotRid.ToHex(),
            settings.RoomName,
            settings.Mode,
            settings.RequiredPlayers,
            settings.MaxDrawNumber,
            settings.DrawPeriod);
        return ValueTask.CompletedTask;
    }

    private static BingoRoomSettings DecodeSettings(IReadOnlyList<Message> createParts)
    {
        if (createParts.Count == 0)
        {
            return BingoRoomSettings.Create("four-player", 0);
        }

        if (createParts.Count != 1)
        {
            throw new InvalidOperationException(
                $"Bingo room expects exactly one create payload part, but received {createParts.Count}.");
        }

        return createParts[0].FromJson<BingoRoomSettings>();
    }
}
