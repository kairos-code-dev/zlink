package systems.zlink.e2e.resiliencelifecycle.client;

import java.util.List;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ClientOptions;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ResilienceProcessManager;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        List<String> startOrder = parseStartOrder(args);
        ClientOptions options = ClientOptions.fromEnv();
        try (ResilienceProcessManager processes = new ResilienceProcessManager(options)) {
            new ResilienceLifecycleSuite(options, processes, startOrder)
                .run(Env.get("ZLINK_JAVA_E2E_SCENARIO", "all"));
        }
        System.out.println("resilience-lifecycle e2e result=passed");
    }

    private static List<String> parseStartOrder(String[] args) {
        if (args.length != 2 || !"--start-order".equals(args[0])) {
            throw new IllegalArgumentException("Usage: Client --start-order <api-a,api-b>");
        }
        List<String> roles = List.of(args[1].split(",", -1));
        if (roles.size() != 2 || !roles.contains("api-a") || !roles.contains("api-b")) {
            throw new IllegalArgumentException("start order must contain api-a and api-b exactly once");
        }
        return roles;
    }
}
