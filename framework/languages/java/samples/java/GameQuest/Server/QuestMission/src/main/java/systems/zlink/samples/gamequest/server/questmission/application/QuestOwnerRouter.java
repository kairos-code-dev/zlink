package systems.zlink.samples.gamequest.server.questmission.application;

import org.springframework.stereotype.Component;
import systems.zlink.samples.gamequest.server.configuration.GameQuestRouting;
import systems.zlink.samples.gamequest.server.configuration.QuestMissionInstanceTopology;

/** Decides whether this QuestMission instance owns a given player. Mirrors the .NET {@code QuestOwnerRouter}. */
@Component
public final class QuestOwnerRouter {
    private final int ownerIndex;

    public QuestOwnerRouter(QuestMissionInstanceTopology instance) {
        this.ownerIndex = instance.ownerIndex();
    }

    public boolean isLocalOwner(String playerId) {
        return GameQuestRouting.ownerIndex(playerId) == ownerIndex;
    }
}
