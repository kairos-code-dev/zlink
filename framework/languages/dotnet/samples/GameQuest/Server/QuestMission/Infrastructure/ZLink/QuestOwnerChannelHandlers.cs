using GameQuest.QuestMission.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Systems.Zlink;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

[ZLinkHandlerGroup(SampleNames.QuestOwnerHandlerGroup)]
internal sealed class ApplyGameplayEventRouteHandler(
    PlayerQuestOwnerProvisioner owners)
    : IZLinkSendHandler<GameplayMsg>
{
    public async ValueTask HandleAsync(
        GameplayMsg message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        await owners.ApplyGameplayEventAsync(message, cancellationToken);
    }
}

[ZLinkHandlerGroup(SampleNames.QuestOwnerHandlerGroup)]
internal sealed class SyncQuestProgressRouteHandler(
    PlayerQuestOwnerProvisioner owners)
    : IZLinkRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes>
{
    public async ValueTask<SyncQuestProgressRes> HandleAsync(
        SyncQuestProgressReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        return await owners.SyncAsync(request, cancellationToken);
    }
}
