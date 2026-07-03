package systems.zlink.e2e.discoveryregistryha.client.scenarios;

import systems.zlink.e2e.discoveryregistryha.client.support.ClientContext;
import systems.zlink.e2e.discoveryregistryha.client.support.ClientScenario;
import systems.zlink.e2e.discoveryregistryha.client.support.DiscoveryApiResult;

public final class SfRecoveredWithPeersScenario implements ClientScenario {
    private final String scenarioName;

    public SfRecoveredWithPeersScenario(String scenarioName) {
        this.scenarioName = scenarioName;
    }

    @Override
    public DiscoveryApiResult run(ClientContext context) {
        return context.runStoreFailureRecoveredWithPeers(scenarioName);
    }
}
