package systems.zlink.samples.shoppingmall.client.configuration;

public final class SampleNames {
    public static final String CommerceApiChannel = "shoppingmall.commerce.api";
    public static final String ApiInstanceA = "api-a";
    public static final String ApiInstanceB = "api-b";

    private SampleNames() {
    }

    public static String commerceApiChannel(String instanceId) {
        return CommerceApiChannel + "." + instanceId;
    }
}
