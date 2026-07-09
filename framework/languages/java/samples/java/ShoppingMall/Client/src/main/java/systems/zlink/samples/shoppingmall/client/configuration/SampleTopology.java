package systems.zlink.samples.shoppingmall.client.configuration;

public final class SampleTopology {
    public static final String ApiAHttpUrl = property("apiAHttpUrl", "http://127.0.0.1:49101");
    public static final String ApiBHttpUrl = property("apiBHttpUrl", "http://127.0.0.1:49102");

    private SampleTopology() {
    }

    private static String property(String name, String fallback) {
        return System.getProperty("zlink.samples.shoppingmall." + name, fallback);
    }
}
