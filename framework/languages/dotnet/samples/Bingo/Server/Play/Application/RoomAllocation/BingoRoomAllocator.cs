using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Systems.Zlink;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed class BingoRoomAllocator(
    IZLinkSpotManager spots,
    IBingoMatchQueue matchQueue)
{
    private int _roomSeq;

    public async ValueTask<BingoMatchReservation> AllocateAsync(
        string mode,
        string actorId,
        string preferredOwnerNodeRid,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id is required for room allocation.");
        }

        if (string.IsNullOrWhiteSpace(preferredOwnerNodeRid))
        {
            throw new InvalidOperationException("Preferred owner node rid is required for room allocation.");
        }

        var settings = BingoRoomSettings.Create(mode, Interlocked.Increment(ref _roomSeq));
        var roomId = $"bingo-room-{Guid.NewGuid():N}";
        var roomRid = RoutingId.From(roomId);
        var reservation = await matchQueue.ReserveAsync(
            mode,
            actorId,
            preferredOwnerNodeRid,
            roomId,
            settings.RequiredPlayers,
            cancellationToken);

        if (string.Equals(reservation.OwnerPlayNodeRid, preferredOwnerNodeRid, StringComparison.Ordinal)
            && string.Equals(reservation.RoomId, roomId, StringComparison.Ordinal))
        {
            using var settingsPart = BingoRoomSettingsPayloadMapper.ToPayload(settings).ToProto();
            await spots.GetOrCreateAsync<BingoRoom>(roomRid, settingsPart, cancellationToken);
        }

        return reservation;
    }
}
