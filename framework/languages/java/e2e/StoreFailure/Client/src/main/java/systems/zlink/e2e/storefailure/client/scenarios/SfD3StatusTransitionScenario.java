package systems.zlink.e2e.storefailure.client.scenarios;

import systems.zlink.e2e.storefailure.client.support.ClientContext;
import systems.zlink.e2e.storefailure.client.support.ClientScenario;
import systems.zlink.e2e.storefailure.client.support.DiscoveryApiResult;

public final class SfD3StatusTransitionScenario implements ClientScenario {
    private final Stage stage;

    public SfD3StatusTransitionScenario(Stage stage) {
        this.stage = stage;
    }

    @Override
    public DiscoveryApiResult run(ClientContext context) {
        return switch (stage) {
            case HEALTHY -> context.runStoreFailureStatusHealthy();
            case OUTAGE -> context.runStoreFailureStatusOutage();
            case RECOVERED -> context.runStoreFailureStatusRecovered();
        };
    }

    public enum Stage {
        HEALTHY,
        OUTAGE,
        RECOVERED
    }
}
