using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

internal sealed class PlayerQuestOwnerProvisioner(IZLinkSpotManager spots) : IPlayerQuestOwnerProvisioner
{
    public async ValueTask EnsureAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        using var payload = new PlayerQuestSpotCreateReq(playerId).ToJson();
        await spots.GetOrCreateAsync<PlayerQuestSpot>(
            RoutingId.From(System.Text.Encoding.UTF8.GetBytes($"player:{playerId}")),
            payload,
            cancellationToken);
    }
}
