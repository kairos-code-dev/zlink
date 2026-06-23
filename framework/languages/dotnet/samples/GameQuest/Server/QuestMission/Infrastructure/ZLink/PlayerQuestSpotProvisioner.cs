using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Infrastructure.ZLink;

internal sealed class PlayerQuestOwnerProvisioner(IZLinkSpotManager spots) : IPlayerQuestOwnerProvisioner
{
    public async ValueTask EnsureAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        await spots.GetOrCreateAsync<PlayerQuestSpot>(
            RoutingId.From(System.Text.Encoding.UTF8.GetBytes($"player:{playerId}")),
            new PlayerQuestSpotCreateReq(playerId),
            cancellationToken);
    }
}
