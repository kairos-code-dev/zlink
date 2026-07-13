using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.Store;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Systems.Zlink;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

[ZLinkHandlerGroup("quest-owner")]
internal sealed class ApplyGameplayEventRouteHandler(
    PlayerQuestOwnerProvisioner owners,
    QuestOwnerRouter ownerRouter)
    : IZLinkRequestHandler<ApplyGameplayEventReq, ApplyGameplayEventRes>
{
    public async ValueTask<ApplyGameplayEventRes> HandleAsync(
        ApplyGameplayEventReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(request.Event.PlayerId)) return new ApplyGameplayEventRes(false);
        return await owners.ApplyGameplayEventAsync(request.Event, cancellationToken);
    }
}

[ZLinkHandlerGroup("quest-owner")]
internal sealed class SyncQuestProgressRouteHandler(
    PlayerQuestOwnerProvisioner owners,
    QuestOwnerRouter ownerRouter,
    QuestStore store)
    : IZLinkRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes>
{
    public async ValueTask<SyncQuestProgressRes> HandleAsync(
        SyncQuestProgressReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(request.PlayerId))
            return new SyncQuestProgressRes(await store.ReadProjectionAsync(request.PlayerId, cancellationToken));
        return await owners.SyncAsync(request, cancellationToken);
    }
}

[ZLinkHandlerGroup("quest-owner")]
internal sealed class ClosePlayerQuestOwnerRouteHandler(
    IZLinkSpotManager spots,
    QuestOwnerRouter ownerRouter)
    : IZLinkRequestHandler<ClosePlayerQuestOwnerReq, ClosePlayerQuestOwnerRes>
{
    public async ValueTask<ClosePlayerQuestOwnerRes> HandleAsync(
        ClosePlayerQuestOwnerReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(request.PlayerId)) return new ClosePlayerQuestOwnerRes(false);
        var spotRid = RoutingId.From(System.Text.Encoding.UTF8.GetBytes($"player:{request.PlayerId}"));
        return new ClosePlayerQuestOwnerRes(await spots.CloseAsync(spotRid, cancellationToken));
    }
}
