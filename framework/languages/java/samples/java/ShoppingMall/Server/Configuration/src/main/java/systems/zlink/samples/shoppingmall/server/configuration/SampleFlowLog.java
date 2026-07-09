package systems.zlink.samples.shoppingmall.server.configuration;

public final class SampleFlowLog {
    private SampleFlowLog() {
    }

    public static String path(String role) {
        return System.getenv().getOrDefault("SHOPPINGMALL_LOG_DIR", "logs")
            + "/flow-" + role + ".log";
    }
}
