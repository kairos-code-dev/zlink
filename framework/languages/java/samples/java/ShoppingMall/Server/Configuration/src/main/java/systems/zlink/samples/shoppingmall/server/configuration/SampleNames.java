package systems.zlink.samples.shoppingmall.server.configuration;

public final class SampleNames {
    public static final String OrderSpotDiscovery = "shoppingmall.orders";
    public static final String CompletedMarker = "shoppingmall=completed";
    public static final String ServerEvidenceMarker = "shoppingmall-server-evidence=completed";

    private SampleNames() {
    }

    public static String orderWorkflowChannelFor(String workflowName) {
        return "shoppingmall.order-workflow." + workflowName;
    }
}
