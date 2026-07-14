package systems.zlink.e2e.storefailure.client;

import systems.zlink.e2e.storefailure.client.scenarios.SfA1BaselineScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfA2PollingFallbackScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfB1FailStaticScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfB2GraceExceededScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfC1CrashLeaseExpiryScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfC2GracefulRemovalScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfD1ShortOutageRecoveryScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfD2LongOutageRecoveryScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfD3StatusTransitionScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfE1StoreDelayNonBlockingScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfRecoveredScenario;
import systems.zlink.e2e.storefailure.client.scenarios.SfRecoveredWithPeersScenario;
import systems.zlink.e2e.storefailure.client.support.ClientContext;
import systems.zlink.e2e.storefailure.client.support.ClientOptions;
import systems.zlink.e2e.storefailure.client.support.ClientScenario;
import systems.zlink.e2e.storefailure.client.support.DiscoveryApiResult;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ClientOptions options = ClientOptions.fromEnv();
        ClientScenario scenario = scenario(options.scenario());
        try (ClientContext context = new ClientContext(options)) {
            DiscoveryApiResult result = scenario.run(context);
            System.out.println("scenario " + options.scenario() + " passed providers=" + result.providers());
        }
    }

    private static ClientScenario scenario(String name) {
        return switch (name) {
            case "SF-A1" -> new SfA1BaselineScenario();
            case "SF-A2" -> new SfA2PollingFallbackScenario();
            case "SF-B1" -> new SfB1FailStaticScenario();
            case "SF-B1-RECOVERED" -> new SfRecoveredScenario("SF-B1");
            case "SF-B2" -> new SfB2GraceExceededScenario();
            case "SF-B2-RECOVERED" -> new SfRecoveredWithPeersScenario("SF-B2");
            case "SF-C1" -> new SfC1CrashLeaseExpiryScenario();
            case "SF-C2" -> new SfC2GracefulRemovalScenario();
            case "SF-D1" -> new SfD1ShortOutageRecoveryScenario();
            case "SF-D1-RECOVERED" -> new SfRecoveredWithPeersScenario("SF-D1");
            case "SF-D2" -> new SfD2LongOutageRecoveryScenario();
            case "SF-D3-HEALTHY" -> new SfD3StatusTransitionScenario(
                SfD3StatusTransitionScenario.Stage.HEALTHY);
            case "SF-D3-OUTAGE" -> new SfD3StatusTransitionScenario(
                SfD3StatusTransitionScenario.Stage.OUTAGE);
            case "SF-D3-RECOVERED" -> new SfD3StatusTransitionScenario(
                SfD3StatusTransitionScenario.Stage.RECOVERED);
            case "SF-E1" -> new SfE1StoreDelayNonBlockingScenario();
            default -> throw new IllegalArgumentException("unknown scenario " + name);
        };
    }
}
