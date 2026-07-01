package systems.zlink.e2e.registrationcodec.client.Support;

public record ClientOptions(
    String serverEndpoint,
    String httpEndpoint,
    String codecRequesterHttpEndpoint,
    String invalidServerEndpoint,
    String buildDir,
    String logDir) {
    public static ClientOptions fromEnv() {
        return new ClientOptions(
            Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_CODEC_REQUESTER_HTTP_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_INVALID_SERVER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_BUILD_DIR",
                System.getProperty("user.home") + "/.cache/zlink/java-e2e/RegistrationCodec"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
