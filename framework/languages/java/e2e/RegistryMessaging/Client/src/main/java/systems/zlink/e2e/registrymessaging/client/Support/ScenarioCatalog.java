package systems.zlink.e2e.registrymessaging.client.Support;

import systems.zlink.e2e.registrymessaging.client.Scenarios.RmA1DiscoveryRequestScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmA2ManualEndpointScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmA4SameRidFailoverScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmA6MultipleChannelsScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmB1ScaleOutScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmB2ScaleInScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC1RequestSendScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC2TargetedRouteScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC3MultiProviderDistributionScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC4TimeoutIsolationScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC5MissingPacketScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC7WeightedProviderScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC8PayloadRoundTripScenario;
import systems.zlink.e2e.registrymessaging.client.Scenarios.RmC9BackpressureScenario;

public final class ScenarioCatalog {
    private final RegistryMessagingHttp http;

    public ScenarioCatalog(RegistryMessagingHttp http) {
        this.http = http;
    }

    public void run() {
        switch (ClientOptions.get("ZLINK_JAVA_E2E_SCENARIO", "common")) {
            case "common" -> runCommon();
            case "scale-out" -> RmB1ScaleOutScenario.run();
            case "scale-in" -> RmB2ScaleInScenario.run();
            case "failover" -> RmA4SameRidFailoverScenario.run();
            case "weighted" -> RmC7WeightedProviderScenario.run(http.directConsumer());
            default -> throw new IllegalArgumentException(
                "unknown scenario " + ClientOptions.get("ZLINK_JAVA_E2E_SCENARIO"));
        }
    }

    private void runCommon() {
        RmA1DiscoveryRequestScenario.run(http.providerA(), http.providerB(), http.registry());
        RmC1RequestSendScenario.run(http.providerA(), http.providerB());
        RmC4TimeoutIsolationScenario.run(http.discoveryConsumer());
        RmC5MissingPacketScenario.run(http.discoveryConsumer(), http.providerA(), http.providerB());
        RmA2ManualEndpointScenario.run(http.providerA());
        RmC3MultiProviderDistributionScenario.run(http.directConsumer(), "RM-C3", "multi-", 80, false);
        RmA6MultipleChannelsScenario.run(http.providerA(), http.providerB(), http.workflow());
        RmC8PayloadRoundTripScenario.run(http.singleConsumer(), http.providerA());
        RmC9BackpressureScenario.run(http.backpressureConsumer(), http.providerA());
        RmC2TargetedRouteScenario.run(http.providerA());
    }
}
