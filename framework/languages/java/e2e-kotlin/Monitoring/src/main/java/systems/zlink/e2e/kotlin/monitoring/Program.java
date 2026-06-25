package systems.zlink.e2e.kotlin.monitoring;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) {
        switch (Env.get("ZLINK_KOTLIN_E2E_ROLE", "client")) {
            case "registry" -> RegistryApplication.run(args);
            case "service" -> ServiceApplication.run(args);
            case "client" -> ClientApplication.run(args);
            case "validation" -> MonitoringValidationApplication.run(args);
            default -> throw new IllegalArgumentException(
                "unknown role " + Env.get("ZLINK_KOTLIN_E2E_ROLE"));
        }
    }
}
