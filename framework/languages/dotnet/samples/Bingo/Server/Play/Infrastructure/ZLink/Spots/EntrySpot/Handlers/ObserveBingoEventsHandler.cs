using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;
using Bingo.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

[ZLinkSpotActorRequestHandler(nameof(ObserveBingoEventsReq))]
internal sealed class ObserveBingoEventsHandler(IZLinkSpotManager spots)
    : IZLinkEntrySpotActorRequestHandler<BingoEntrySpot, PlayerActor, ObserveBingoEventsReq, ObserveBingoEventsRes>
{
    public async ValueTask<ObserveBingoEventsRes> HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        ObserveBingoEventsReq message,
        CancellationToken cancellationToken)
    {
        var observerRid = ObserverRoomRid(message.RoomId, entrySpot.Context.NodeRid);
        var settings = BingoRoomSettings.CreateObserver(message.RoomId, entrySpot.Context.NodeRid.ToString());
        await spots.GetOrCreateAsync<BingoRoom, BingoRoomSettingsPayload>(
            observerRid,
            BingoRoomSettingsPayloadMapper.ToPayload(settings),
            cancellationToken);

        var joined = await actor.Context.JoinSpot(
                observerRid,
                new BingoRoomJoinReq
                {
                    RoomId = message.RoomId,
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                    ObserveOnly = true
                })
            .Async<BingoRoomJoinRes>(cancellationToken);

        if (!joined.Accepted)
            return new ObserveBingoEventsRes
            {
                Subscribed = false,
                ObserverNodeRid = entrySpot.Context.NodeRid.ToString()
            };

        return new ObserveBingoEventsRes
        {
            Subscribed = true,
            ObserverNodeRid = entrySpot.Context.NodeRid.ToString()
        };
    }

    private static RoutingId ObserverRoomRid(string roomId, RoutingId localNodeRid)
    {
        return RoutingId.From($"observe:{roomId}:{localNodeRid}");
    }
}
