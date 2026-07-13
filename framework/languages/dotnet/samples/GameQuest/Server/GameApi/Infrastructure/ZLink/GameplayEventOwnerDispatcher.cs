using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    GameQuestTopology topology,
    IZLinkChannelClient channels) : IGameplayEventOwnerDispatcher
{
    public async ValueTask<string> DispatchAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        var owner = topology.OwnerRouteRid(gameplayEvent.PlayerId);
        _ = await channels.RequestToChannel(
                topology.QuestOwnerChannel(gameplayEvent.PlayerId),
                new ApplyGameplayEventReq(gameplayEvent))
            .Async<ApplyGameplayEventRes>(cancellationToken);
        return owner.ToString();
    }
}
