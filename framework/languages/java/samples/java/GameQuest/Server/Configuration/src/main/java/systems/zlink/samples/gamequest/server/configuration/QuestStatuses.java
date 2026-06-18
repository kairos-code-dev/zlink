package systems.zlink.samples.gamequest.server.configuration;

/** Quest aggregate status values carried by {@code QuestProgress.status}. */
public final class QuestStatuses {
    public static final String Active = "Active";
    public static final String Completed = "Completed";
    public static final String RewardGranted = "RewardGranted";

    private QuestStatuses() {
    }
}
