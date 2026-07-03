package systems.zlink.e2e.discoveryregistryha.client.scenarios;

import systems.zlink.e2e.discoveryregistryha.client.support.ClientContext;
import systems.zlink.e2e.discoveryregistryha.client.support.ClientScenario;
import systems.zlink.e2e.discoveryregistryha.client.support.DiscoveryApiResult;

public final class SfD2LongOutageRecoveryScenario implements ClientScenario {
    @Override
    public DiscoveryApiResult run(ClientContext context) {
        return context.runStoreFailureLongOutageRecovery();
    }
}
