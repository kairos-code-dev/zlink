package systems.zlink.samples.deliverydispatch.client.configuration;

public final class SampleTopology {
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47391");
    public static final String ApiChannelEndpoint =
        property("apiChannelEndpoint", "tcp://127.0.0.1:47392");
    public static final String SessionStreamEndpoint =
        property("sessionStreamEndpoint", "tcp://127.0.0.1:47400");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.deliverydispatch." + name, defaultValue);
    }
}
