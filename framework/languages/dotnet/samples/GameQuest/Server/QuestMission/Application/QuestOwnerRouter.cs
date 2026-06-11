using GameQuest.Shared;
using GameQuest.Server.Configuration;

namespace GameQuest.QuestMission.Application;

internal sealed class QuestOwnerRouter(QuestMissionInstanceTopology instance)
{
    public bool IsLocalOwner(string playerId) => OwnerIndex(playerId) == instance.OwnerIndex;

    public static int OwnerIndex(string playerId) => GameQuestRouting.OwnerIndex(playerId);
}
