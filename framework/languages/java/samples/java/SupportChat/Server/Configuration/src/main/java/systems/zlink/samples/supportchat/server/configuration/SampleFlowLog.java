package systems.zlink.samples.supportchat.server.configuration;

public final class SampleFlowLog {
    private SampleFlowLog() {
    }

    public static String path(String role) {
        return System.getenv().getOrDefault("SUPPORTCHAT_LOG_DIR", "logs") + "/flow-" + role + ".log";
    }
}
