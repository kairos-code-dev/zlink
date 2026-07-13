package systems.zlink.e2e.kotlin.automaticturn;

public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        String scenario = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "all");
        if (args.length > 0) {
            scenario = args[0];
        }
        if ("d2".equals(scenario) || "ATD-D2".equals(scenario) || "ATD-D3".equals(scenario)) {
            ClientScenario.runD2(scenario);
        } else if ("ATD-E3".equals(scenario)) {
            ClientScenario.runE3();
        } else {
            ClientScenario.run(scenario);
        }
        System.out.println("automatic-turn-dispatch kotlin e2e result=passed");
        System.exit(0);
    }
}
