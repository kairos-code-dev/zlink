package systems.zlink.e2e.monitoring;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        switch (Env.get("ZLINK_JAVA_E2E_ROLE", "client")) {
            case "registry" -> RegistryApplication.run(args);
            case "service" -> ServiceApplication.run(args);
            case "client" -> ClientApplication.run(args);
            default -> throw new IllegalArgumentException(
                "unknown role " + Env.get("ZLINK_JAVA_E2E_ROLE"));
        }
    }
}
