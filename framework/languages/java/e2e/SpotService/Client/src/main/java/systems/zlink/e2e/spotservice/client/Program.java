package systems.zlink.e2e.spotservice.client;

import systems.zlink.e2e.spotservice.client.Support.GatewayScenarioClient;
import systems.zlink.e2e.spotservice.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "state1");
        String endpoint = Env.get("ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT");
        new GatewayScenarioClient(endpoint).runMode(mode);
    }
}
