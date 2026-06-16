package systems.zlink.samples.supportchat.client.configuration;

public final class SampleTopology {
    public static final String StreamEndpoint =
        property("streamEndpoint", "tcp://127.0.0.1:47214");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.supportchat." + name, defaultValue);
    }
}
