package systems.zlink.e2e.spotservice.client;

import systems.zlink.e2e.spotservice.client.Scenarios.ScenarioSuite;
import systems.zlink.e2e.spotservice.client.Scenarios.SpotServiceScenarioContext;
import systems.zlink.e2e.spotservice.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "state1");
        String scenario = Env.get("ZLINK_JAVA_E2E_SCENARIO_ID", mode);
        String endpoint = Env.get("ZLINK_JAVA_E2E_GATEWAY_HTTP_ENDPOINT");
        ScenarioSuite.run(scenario, new SpotServiceScenarioContext(endpoint));
        System.out.println("spot-service e2e mode=" + mode + " result=passed");
    }
}
