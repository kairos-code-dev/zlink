using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Adapters.ZLink.Spots;

internal static class BingoRoomSettingsPayloadMapper
{
    public static BingoRoomSettings FromMessage(Message request, BingoRoomSettings defaultSettings)
    {
        if (request.Size == 0)
        {
            return defaultSettings;
        }

        var payload = request.FromProto<BingoRoomSettingsPayload>();
        return new BingoRoomSettings(
            payload.RoomName,
            payload.Mode,
            payload.RequiredPlayers,
            payload.MaxDrawNumber,
            string.IsNullOrWhiteSpace(payload.Purpose) ? BingoRoomSettings.GamePurpose : payload.Purpose,
            payload.HasObservedRoomId ? payload.ObservedRoomId : null);
    }

    public static BingoRoomSettingsPayload ToPayload(BingoRoomSettings settings)
    {
        var payload = new BingoRoomSettingsPayload
        {
            RoomName = settings.RoomName,
            Mode = settings.Mode,
            RequiredPlayers = settings.RequiredPlayers,
            MaxDrawNumber = settings.MaxDrawNumber,
            Purpose = settings.Purpose,
        };

        if (settings.ObservedRoomId is not null)
        {
            payload.ObservedRoomId = settings.ObservedRoomId;
        }

        return payload;
    }
}
