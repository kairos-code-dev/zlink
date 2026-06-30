package systems.zlink.e2e.registrationcodec.invalidduplicate.Configuration;

public record ServerOptions(String serverEndpoint) {
    public static ServerOptions fromEnv() {
        return new ServerOptions(Env.get("ZLINK_JAVA_E2E_SERVER_ENDPOINT"));
    }
}
