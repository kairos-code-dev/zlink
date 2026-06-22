using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;

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
        _ = context;
        var observerRid = ObserverRoomRid(message.RoomId, entrySpot.Context.NodeRid);
        var settings = BingoRoomSettings.CreateObserver(message.RoomId, entrySpot.Context.NodeRid.ToString());
        using var settingsPart = BingoRoomSettingsPayloadMapper.ToPayload(settings).ToProto();
        var created = await spots.GetOrCreateAsync<BingoRoom>(observerRid, settingsPart, cancellationToken);
        if (created.State == ZLinkSpotCreateState.Rejected)
        {
            throw new InvalidOperationException("Observer BingoRoom creation was rejected.");
        }

        var joined = await actor.Context.JoinSpot(
                observerRid,
                new BingoRoomJoinReq
                {
                    RoomId = message.RoomId,
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                    ObserveOnly = true,
                }.ToProto())
            .Async(cancellationToken);

        if (!joined.Accepted)
        {
            return new ObserveBingoEventsRes
            {
                Subscribed = false,
                ObserverNodeRid = entrySpot.Context.NodeRid.ToString(),
            };
        }

        return new ObserveBingoEventsRes
        {
            Subscribed = true,
            ObserverNodeRid = entrySpot.Context.NodeRid.ToString(),
        };
    }

    private static RoutingId ObserverRoomRid(string roomId, RoutingId localNodeRid)
    {
        return RoutingId.From($"observe:{roomId}:{localNodeRid}");
    }
}
