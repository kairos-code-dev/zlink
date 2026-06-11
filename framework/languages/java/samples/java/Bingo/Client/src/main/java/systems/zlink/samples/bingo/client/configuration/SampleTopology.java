package systems.zlink.samples.bingo.client.configuration;

public final class SampleTopology {
    public static final String StreamEndpoint =
        property("streamEndpoint", "tcp://127.0.0.1:47114");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.bingo." + name, defaultValue);
    }
}
