package systems.zlink.e2e.registrationcodec.codecrequester.Configuration;

public record CodecRequesterOptions(
    String serverEndpoint,
    String httpEndpoint,
    String logDir) {
    public static CodecRequesterOptions fromEnv() {
        return new CodecRequesterOptions(
            Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
