package systems.zlink.e2e.resiliencelifecycle.client;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ClientOptions;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ResilienceProcessManager;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        ClientOptions options = ClientOptions.fromEnv();
        try (ResilienceProcessManager processes = new ResilienceProcessManager(options)) {
            new ResilienceLifecycleSuite(options, processes)
                .run(Env.get("ZLINK_JAVA_E2E_SCENARIO", "all"));
        }
        System.out.println("resilience-lifecycle e2e result=passed");
    }
}
