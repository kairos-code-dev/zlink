using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    IZLinkRouteClient routes,
    GameQuestTopology topology) : IGameplayEventOwnerDispatcher
{
    public async ValueTask<string> DispatchAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        var owner = topology.OwnerRouteRid(gameplayEvent.PlayerId);
        await routes
            .Request(SampleNames.QuestOwnerRouteChannel, owner, new ApplyGameplayEventReq(gameplayEvent))
            .Async<ApplyGameplayEventRes>(cancellationToken);
        return owner.ToString();
    }
}
