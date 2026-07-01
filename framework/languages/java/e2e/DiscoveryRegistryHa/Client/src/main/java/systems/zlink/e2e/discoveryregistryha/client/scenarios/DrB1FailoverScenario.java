package systems.zlink.e2e.discoveryregistryha.client.scenarios;

import systems.zlink.e2e.discoveryregistryha.client.support.ClientContext;
import systems.zlink.e2e.discoveryregistryha.client.support.ClientScenario;
import systems.zlink.e2e.discoveryregistryha.client.support.DiscoveryApiResult;

public final class DrB1FailoverScenario implements ClientScenario {
    @Override
    public DiscoveryApiResult run(ClientContext context) {
        return context.runStandard();
    }
}
