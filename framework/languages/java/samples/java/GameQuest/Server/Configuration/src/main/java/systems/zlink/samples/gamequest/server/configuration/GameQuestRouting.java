package systems.zlink.samples.gamequest.server.configuration;

import java.nio.charset.StandardCharsets;

/** Player owner routing: a QuestMission instance owns a player by {@code playerId} hash. */
public final class GameQuestRouting {
    public static int ownerIndex(String playerId) {
        int sum = 0;
        for (byte value : playerId.getBytes(StandardCharsets.UTF_8)) {
            sum += value & 0xFF;
        }
        return sum % 2;
    }

    private GameQuestRouting() {
    }
}
