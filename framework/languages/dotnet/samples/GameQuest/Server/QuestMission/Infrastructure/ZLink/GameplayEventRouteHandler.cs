using GameQuest.QuestMission.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

internal sealed class GameplayEventRouteHandler(
    PlayerQuestOwnerProvisioner playerQuestOwners,
    QuestOwnerRouter ownerRouter)
    : IZLinkRouteRequestHandler<ApplyGameplayEventReq, ApplyGameplayEventRes>
{
    public async ValueTask<ApplyGameplayEventRes> HandleAsync(
        ApplyGameplayEventReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(request.Event.PlayerId))
            return new ApplyGameplayEventRes(false);

        return await playerQuestOwners.ApplyGameplayEventAsync(request.Event, cancellationToken);
    }
}
