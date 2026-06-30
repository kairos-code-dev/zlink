package systems.zlink.e2e.kotlin.yielddispatch;

public final class ClientApplication {
    private ClientApplication() {
    }

    public static void run(String... args) {
        String mode = Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "");
        if ("d2".equals(mode)) {
            ClientScenario.runD2();
        } else {
            ClientScenario.run();
        }
        System.out.println("yield-dispatch kotlin e2e result=passed");
        System.exit(0);
    }
}
