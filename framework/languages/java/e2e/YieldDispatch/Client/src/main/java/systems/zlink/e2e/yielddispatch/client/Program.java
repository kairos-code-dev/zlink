package systems.zlink.e2e.yielddispatch.client;

import java.net.URI;
import systems.zlink.e2e.yielddispatch.client.Scenarios.ShutdownYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdA1BasicTerminatorScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdA2YieldTerminatorScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdA3ContinuationContextScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdA4WorkerYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdB1OtherActorProgressScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdB2SameActorReentryScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdB3ActorJoinYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdC1TimerIsolationScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdC2TimerReentryScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdC3ActorTimerIsolationScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdD2RemoteSpotYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdD3RouteBridgeYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdD4SessionRelayActorYieldScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdE1TimeoutScenario;
import systems.zlink.e2e.yielddispatch.client.Scenarios.YdE2CancellationScenario;
import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.e2e.yielddispatch.shared.Env;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;

public final class Program {
    private Program() {
    }

    public static void main(String... args) throws Exception {
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions.createDefault(URI.create(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))));
        try {
            connector.connect().await();
            if (args.length > 0 && "--readiness".equals(args[0])) {
                YieldDispatchScenarioSupport.runReadinessProbe(connector);
                System.out.println("yield-dispatch readiness=ready");
                return;
            }
            if (args.length > 0 && "--shutdown-wait".equals(args[0])) {
                ShutdownYieldScenario.runWait(connector);
                return;
            }
            if (args.length > 0 && "--shutdown-recovery".equals(args[0])) {
                ShutdownYieldScenario.runRecovery(connector);
                return;
            }

            String scenario = args.length > 0 ? args[0] : "all";
            runScenario(connector, scenario);
            System.out.println("yield-dispatch e2e result=passed");
        } finally {
            connector.close().await();
        }
    }

    private static void runScenario(ZLinkStreamConnector connector, String scenario) throws Exception {
        switch (scenario) {
            case "all" -> runAll(connector);
            case "YD-A1" -> YdA1BasicTerminatorScenario.run(connector);
            case "YD-A2" -> YdA2YieldTerminatorScenario.run(connector);
            case "YD-A3" -> YdA3ContinuationContextScenario.run(connector);
            case "YD-A4" -> YdA4WorkerYieldScenario.run(connector);
            case "YD-B1" -> YdB1OtherActorProgressScenario.run(connector);
            case "YD-B2" -> YdB2SameActorReentryScenario.run(connector);
            case "YD-B3" -> YdB3ActorJoinYieldScenario.run(connector);
            case "YD-C1" -> YdC1TimerIsolationScenario.run(connector);
            case "YD-C2" -> YdC2TimerReentryScenario.run(connector);
            case "YD-C3" -> YdC3ActorTimerIsolationScenario.run(connector);
            case "YD-D2" -> YdD2RemoteSpotYieldScenario.run(connector);
            case "YD-D3" -> YdD3RouteBridgeYieldScenario.run(connector);
            case "YD-D4" -> YdD4SessionRelayActorYieldScenario.run(connector);
            case "YD-E1" -> YdE1TimeoutScenario.run(connector);
            case "YD-E2" -> YdE2CancellationScenario.run(connector);
            default -> throw new IllegalArgumentException("unknown YieldDispatch scenario: " + scenario);
        }
    }

    private static void runAll(ZLinkStreamConnector connector) throws Exception {
            YdA1BasicTerminatorScenario.run(connector);
            YdA2YieldTerminatorScenario.run(connector);
            YdA3ContinuationContextScenario.run(connector);
            YdA4WorkerYieldScenario.run(connector);
            YdB1OtherActorProgressScenario.run(connector);
            YdB2SameActorReentryScenario.run(connector);
            YdB3ActorJoinYieldScenario.run(connector);
            YdC1TimerIsolationScenario.run(connector);
            YdC2TimerReentryScenario.run(connector);
            YdC3ActorTimerIsolationScenario.run(connector);
            YdD2RemoteSpotYieldScenario.run(connector);
            YdD3RouteBridgeYieldScenario.run(connector);
            YdD4SessionRelayActorYieldScenario.run(connector);
            YdE1TimeoutScenario.run(connector);
            YdE2CancellationScenario.run(connector);
    }
}
