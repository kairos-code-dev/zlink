using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.HttpClient;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class GameplayEventOwnerDispatcher(
    GameQuestTopology topology) : IGameplayEventOwnerDispatcher
{
    public async ValueTask<string> DispatchAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        var owner = topology.OwnerRouteRid(gameplayEvent.PlayerId);
        var missionBaseUrl = GameQuestRouting.OwnerIndex(gameplayEvent.PlayerId) == 1
            ? topology.MissionBHttpBaseUrl
            : topology.MissionAHttpBaseUrl;
        using var mission = ZLinkHttpClient.Create(missionBaseUrl).Build();
        _ = await mission.Post("/internal/apply")
            .Body(new ApplyGameplayEventReq(gameplayEvent))
            .SubmitAsync<ApplyGameplayEventRes>(cancellationToken);
        return owner.ToString();
    }
}
