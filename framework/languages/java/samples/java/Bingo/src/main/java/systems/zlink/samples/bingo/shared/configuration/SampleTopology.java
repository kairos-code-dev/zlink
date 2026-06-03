package systems.zlink.samples.bingo.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint = "tcp://127.0.0.1:47101";
    public static final String RegistryRouterEndpoint = "tcp://127.0.0.1:47102";
    public static final String ApiChannelEndpoint = "tcp://127.0.0.1:47103";
    public static final String PlayChannelEndpoint = "tcp://127.0.0.1:47104";
    public static final String SessionSpotEndpoint = "tcp://127.0.0.1:47105";
    public static final String SessionRouterEndpoint = "tcp://127.0.0.1:47106";
    public static final String PlaySpotEndpoint = "tcp://127.0.0.1:47110";
    public static final String PlaySpotRouterEndpoint = "tcp://127.0.0.1:47111";
    public static final String StreamEndpoint = "tcp://127.0.0.1:47112";
    public static final int SessionPort = 29100;

    private SampleTopology() {
    }
}
