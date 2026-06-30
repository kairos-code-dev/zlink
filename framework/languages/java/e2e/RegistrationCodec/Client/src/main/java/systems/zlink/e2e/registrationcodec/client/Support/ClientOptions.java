package systems.zlink.e2e.registrationcodec.client.Support;

public record ClientOptions(
    String serverEndpoint,
    String httpEndpoint,
    String logDir) {
    public static ClientOptions fromEnv() {
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
