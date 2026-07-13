package systems.zlink.e2e.storefailure.client.scenarios;

import systems.zlink.e2e.storefailure.client.support.ClientContext;
import systems.zlink.e2e.storefailure.client.support.ClientScenario;
import systems.zlink.e2e.storefailure.client.support.DiscoveryApiResult;

public final class SfC2GracefulRemovalScenario implements ClientScenario {
    @Override
    public DiscoveryApiResult run(ClientContext context) {
        return context.runStoreFailureGracefulRemoval();
    }
}
