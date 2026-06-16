package systems.zlink.samples.shoppingmallcheckout.client.configuration;

public final class SampleTopology {
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47491");
    public static final String CommerceApiAEndpoint =
        property("commerceApiAEndpoint", "tcp://127.0.0.1:47492");
    public static final String CommerceApiBEndpoint =
        property("commerceApiBEndpoint", "tcp://127.0.0.1:47493");

    private SampleTopology() {
    }

    public static String commerceApiEndpoint(String instanceId) {
        return SampleNames.ApiInstanceB.equals(instanceId)
            ? CommerceApiBEndpoint
            : CommerceApiAEndpoint;
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.shoppingmallcheckout." + name, defaultValue);
    }
}
