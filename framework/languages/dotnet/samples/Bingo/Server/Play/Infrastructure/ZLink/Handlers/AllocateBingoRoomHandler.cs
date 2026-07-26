using Bingo.Server.Configuration;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class AllocateBingoRoomHandler(
    BingoRoomAllocator allocator,
    IZLinkSpotManager spots,
    ILogger<AllocateBingoRoomHandler> logger)
    : IZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes>
{
    public async ValueTask<AllocateBingoRoomRes> HandleAsync(
        AllocateBingoRoomReq request,
        IZLinkMessageContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "play allocate: request. actor={ActorId}, mode={Mode}",
            request.ActorId,
            request.Mode);
        var reservation = await allocator.AllocateAsync(
            request.Mode,
            request.ActorId,
            cancellationToken);
        logger.LogInformation(
            "play allocate: allocated. actor={ActorId}, room={RoomId}",
            request.ActorId,
            reservation.RoomId);
        if (reservation.NewRoomSettings is not null)
            _ = await spots
                .GetOrCreate(reservation.RoomId, SampleNames.RoomSpotType)
                .InMesh(SampleNames.MeshName)
                .Request(BingoRoomSettingsPayloadMapper.ToPayload(
                    reservation.NewRoomSettings))
                .Async(cancellationToken);

        return new AllocateBingoRoomRes
        {
            RoomId = reservation.RoomId
        };
    }
}
