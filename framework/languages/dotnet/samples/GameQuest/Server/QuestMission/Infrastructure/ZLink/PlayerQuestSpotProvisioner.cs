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
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spotHandles)
{
    public async ValueTask ApplyGameplayEventAsync(
        GameplayMsg gameplayEvent,
        CancellationToken cancellationToken)
    {
        var address = await EnsureAddressAsync(gameplayEvent.PlayerId, cancellationToken);
        await routes.SendToSpot(address, gameplayEvent).Async(cancellationToken);
    }

    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        var address = await EnsureAddressAsync(request.PlayerId, cancellationToken);
        return await routes
            .RequestToSpot(address, request)
            .Async<SyncQuestProgressRes>(cancellationToken);
    }

    private async ValueTask<SpotHandle> EnsureAddressAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var spotRid = RoutingId.From(Encoding.UTF8.GetBytes($"player:{playerId}"));
        await spots.GetOrCreateAsync<PlayerQuestSpot, PlayerQuestSpotCreateReq>(
            spotRid,
            new PlayerQuestSpotCreateReq(playerId),
            cancellationToken);
        return await spotHandles.ResolveSpotHandleAsync(
                   SampleNames.MeshName,
                   spotRid,
                   cancellationToken)
               ?? throw new InvalidOperationException($"Player quest spot '{spotRid}' was not found.");
    }
}
