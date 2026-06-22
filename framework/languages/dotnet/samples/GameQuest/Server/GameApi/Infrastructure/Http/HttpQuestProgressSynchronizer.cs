using GameQuest.GameApi.Application;
using GameQuest.Shared;
using GameQuest.Server.Configuration;
using Zlink.HttpClient;

namespace GameQuest.GameApi.Infrastructure.Http;

internal sealed class HttpQuestProgressSynchronizer(GameQuestTopology topology) : IQuestProgressSynchronizer
{
    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var missionBaseUrl = GameQuestRouting.OwnerIndex(playerId) == 1
            ? topology.MissionBHttpBaseUrl
            : topology.MissionAHttpBaseUrl;
        using var mission = ZLinkHttpClient.Create(missionBaseUrl).Json().Build();
        return (await mission.Post("/internal/sync")
            .Body(new SyncQuestProgressReq(playerId))
            .SubmitAsync<SyncQuestProgressRes>(cancellationToken)).Body;
    }
}
