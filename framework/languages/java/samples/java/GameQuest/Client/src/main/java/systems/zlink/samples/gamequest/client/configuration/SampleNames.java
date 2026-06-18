package systems.zlink.samples.gamequest.client.configuration;

public final class SampleNames {
    public static final String ApiA = "api-a";
    public static final String ApiB = "api-b";

    private SampleNames() {
    }

    public static String gameApiActionChannel(String apiName) {
        return "gamequest.gameapi." + apiName;
    }
}
