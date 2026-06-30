package systems.zlink.e2e.pubsub.client.Support;

public record ClientOptions(
    String mode,
    String publisherHttp,
    String sub1Http,
    String sub2Http,
    String sub3Http,
    String publisherReadyFile,
    String prelateContinueFile,
    String lateReadyFile,
    String lateContinueFile) {
    public static ClientOptions fromEnv() {
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "default"),
            Env.get("ZLINK_JAVA_E2E_PUBLISHER_HTTP"),
            Env.get("ZLINK_JAVA_E2E_SUB1_HTTP"),
            Env.get("ZLINK_JAVA_E2E_SUB2_HTTP"),
            Env.get("ZLINK_JAVA_E2E_SUB3_HTTP"),
            Env.get("ZLINK_JAVA_E2E_PUBLISHER_READY_FILE"),
            Env.get("ZLINK_JAVA_E2E_PRELATE_CONTINUE_FILE"),
            Env.get("ZLINK_JAVA_E2E_LATE_READY_FILE"),
            Env.get("ZLINK_JAVA_E2E_LATE_CONTINUE_FILE"));
    }

    public String subscriberHttp(String rid) {
        return switch (rid) {
            case "sub-1" -> sub1Http;
            case "sub-2" -> sub2Http;
            case "sub-3" -> sub3Http;
            default -> throw new IllegalArgumentException("unknown subscriber " + rid);
        };
    }
}
