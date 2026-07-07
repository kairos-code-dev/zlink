using System.Text;
using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

internal sealed class PlayerQuestOwnerProvisioner(
    IZLinkSpotManager spots,
    IZLinkRouteClient routes,
    QuestMissionInstanceTopology instance)
{
    public async ValueTask<ApplyGameplayEventRes> ApplyGameplayEventAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        var address = await EnsureAddressAsync(gameplayEvent.PlayerId, cancellationToken);
        return await routes
            .RequestToSpot(SampleNames.QuestSpotDiscovery, address, new ApplyGameplayEventReq(gameplayEvent))
            .Async<ApplyGameplayEventRes>(cancellationToken);
    }

    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        var address = await EnsureAddressAsync(request.PlayerId, cancellationToken);
        return await routes
            .RequestToSpot(SampleNames.QuestSpotDiscovery, address, request)
            .Async<SyncQuestProgressRes>(cancellationToken);
    }

    private async ValueTask<SpotRef> EnsureAddressAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var spotRid = RoutingId.From(Encoding.UTF8.GetBytes($"player:{playerId}"));
        await spots.GetOrCreateAsync<PlayerQuestSpot, PlayerQuestSpotCreateReq>(
            spotRid,
            new PlayerQuestSpotCreateReq(playerId),
            cancellationToken);
        return new SpotRef(instance.SpotRid, spotRid);
    }
}
