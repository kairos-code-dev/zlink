package systems.zlink.e2e.spotservice;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        switch (Env.get("ZLINK_JAVA_E2E_ROLE", "client")) {
            case "registry" -> RegistryApplication.run(args);
            case "play" -> PlayApplication.run(args);
            case "publisher" -> PublisherApplication.run(args);
            case "client" -> ClientApplication.run(args);
            default -> throw new IllegalArgumentException(
                "unknown role " + Env.get("ZLINK_JAVA_E2E_ROLE"));
        }
    }
}
