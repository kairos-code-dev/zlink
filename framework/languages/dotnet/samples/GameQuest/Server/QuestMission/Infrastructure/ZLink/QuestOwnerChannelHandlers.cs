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
    : IZLinkSendHandler<GameplayMsg>
{
    public async ValueTask HandleAsync(
        GameplayMsg message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(message.PlayerId)) return;
        await owners.ApplyGameplayEventAsync(message, cancellationToken);
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
            return new SyncQuestProgressRes(
                (await store.ReadProjectionAsync(request.PlayerId, cancellationToken))
                .Select(QuestContractMapper.ToContract)
                .ToArray());
        return await owners.SyncAsync(request, cancellationToken);
    }
}
