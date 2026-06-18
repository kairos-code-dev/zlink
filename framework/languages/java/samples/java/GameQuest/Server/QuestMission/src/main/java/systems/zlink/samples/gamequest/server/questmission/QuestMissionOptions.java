package systems.zlink.samples.gamequest.server.questmission;

/** Parsed {@code --mission} selector for a QuestMission process ({@code mission-a}/{@code mission-b}). */
public record QuestMissionOptions(String missionName) {
    public static QuestMissionOptions fromArgs(String[] args) {
        String missionName = "mission-a";
        for (int i = 0; i < args.length - 1; i++) {
            if ("--mission".equals(args[i])) {
                missionName = args[i + 1];
            }
        }
        return new QuestMissionOptions(missionName);
    }
}
