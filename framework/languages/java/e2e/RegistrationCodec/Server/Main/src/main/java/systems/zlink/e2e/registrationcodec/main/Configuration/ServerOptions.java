package systems.zlink.e2e.registrationcodec.main.Configuration;

public record ServerOptions(
    String serverEndpoint,
    String httpEndpoint,
    String logDir) {
    public static ServerOptions fromEnv() {
        return new ServerOptions(
            Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
