using GameQuest.Server.Configuration;

namespace GameQuest.QuestMission.Application;

internal sealed class QuestOwnerRouter(QuestMissionInstanceTopology instance)
{
    public bool IsLocalOwner(string playerId)
    {
        return OwnerIndex(playerId) == instance.OwnerIndex;
    }

    public static int OwnerIndex(string playerId)
    {
        return GameQuestRouting.OwnerIndex(playerId);
    }
}