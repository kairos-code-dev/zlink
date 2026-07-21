using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Infrastructure.ZLink;

internal sealed class ZLinkQuestProgressSynchronizer(
    GameQuestTopology topology,
    IZLinkRouteClient channels) : IQuestProgressSynchronizer
{
    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        return await channels.RequestToChannel(SampleNames.MeshName, topology.QuestOwnerChannel(playerId),
                new SyncQuestProgressReq(playerId))
            .Async<SyncQuestProgressRes>(cancellationToken);
    }
}
