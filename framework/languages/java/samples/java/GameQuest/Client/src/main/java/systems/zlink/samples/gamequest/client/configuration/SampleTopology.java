package systems.zlink.samples.gamequest.client.configuration;

public final class SampleTopology {
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47591");
    public static final String GameApiAActionEndpoint =
        property("gameApiAActionEndpoint", "tcp://127.0.0.1:47594");
    public static final String GameApiBActionEndpoint =
        property("gameApiBActionEndpoint", "tcp://127.0.0.1:47595");
    public static final String GameApiAStreamEndpoint =
        property("gameApiAStreamEndpoint", "tcp://127.0.0.1:47596");
    public static final String GameApiBStreamEndpoint =
        property("gameApiBStreamEndpoint", "tcp://127.0.0.1:47597");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.gamequest." + name, defaultValue);
    }
}
