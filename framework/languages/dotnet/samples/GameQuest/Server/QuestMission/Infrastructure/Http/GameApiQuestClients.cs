using System.Text.Json;
using GameQuest.QuestMission.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace GameQuest.QuestMission.Infrastructure.Http;

internal sealed class HttpGameApiSnapshotClient(GameQuestTopology topology) : IGameApiSnapshotClient
{
    public async ValueTask<GetGameplaySnapshotRes> ReadSnapshotAsync(
        GetGameplaySnapshotReq request,
        CancellationToken cancellationToken)
    {
        using var gameApi = ZLinkHttpClient.Create(topology.GameApiAHttpBaseUrl).Json().Build();
        return (await gameApi.Post("/internal/snapshot")
            .Body(request)
            .SubmitAsync<GetGameplaySnapshotRes>(cancellationToken)).Body;
    }
}

internal sealed class HttpQuestProgressNotifier(GameQuestTopology topology) : IQuestProgressNotifier
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public async ValueTask<QuestProgressNotifyResult> NotifyAsync(
        string sourceApi,
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        var baseUrl = string.Equals(sourceApi, "api-b", StringComparison.Ordinal)
            ? topology.GameApiBHttpBaseUrl
            : topology.GameApiAHttpBaseUrl;
        try
        {
            using var gameApi = ZLinkHttpClient.Create(baseUrl).Json().Build();
            var response = await gameApi.Post("/internal/notify")
                .Body(request)
                .SubmitRawAsync(cancellationToken);
            if (response.Status is < 200 or >= 300) return new QuestProgressNotifyResult(false, response.Status, null);

            var delivered = JsonSerializer.Deserialize<NotifyQuestProgressRes>(response.Body, JsonOptions);
            return new QuestProgressNotifyResult(delivered?.Delivered ?? false, null, null);
        }
        catch (ZLinkFrameworkException error)
        {
            return new QuestProgressNotifyResult(false, null, error);
        }
    }
}