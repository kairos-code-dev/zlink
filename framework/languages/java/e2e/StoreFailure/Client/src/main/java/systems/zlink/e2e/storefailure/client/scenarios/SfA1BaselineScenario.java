package systems.zlink.e2e.storefailure.client.scenarios;

import systems.zlink.e2e.storefailure.client.support.ClientContext;
import systems.zlink.e2e.storefailure.client.support.ClientScenario;
import systems.zlink.e2e.storefailure.client.support.DiscoveryApiResult;

public final class SfA1BaselineScenario implements ClientScenario {
    @Override
    public DiscoveryApiResult run(ClientContext context) {
        context.waitForLivePeerRows();
        context.waitForHealthyStatus();
        return context.requestUntilAnyProvider();
    }
}
