package systems.zlink.samples.gamequest.server.configuration;

public final class SampleNames {
    public static final String StreamNode = "gamequest-stream";
    public static final String QuestOwnerChannelPrefix = "gamequest.quest-owner.";
    public static final String CompletedMarker = "gamequest=completed";
    public static final String ServerEvidenceMarker = "gamequest-server-evidence=completed";
    public static final String RehydrateMarker = "gamequest-rehydrate=completed";

    private SampleNames() {
    }

    public static String questOwnerChannelFor(String instanceId) {
        return QuestOwnerChannelPrefix + instanceId;
    }
}
