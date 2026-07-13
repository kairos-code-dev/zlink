package systems.zlink.e2e.automaticturn.client;

import java.net.URI;
import systems.zlink.e2e.automaticturn.client.Scenarios.ShutdownAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdA1BasicTerminatorScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdA2AwaitTerminatorScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdA3ContinuationContextScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdA4WorkerAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdB1OtherActorProgressScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdB2SameActorReentryScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdB3ActorJoinAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdC1TimerIsolationScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdC2TimerReentryScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdC3ActorTimerIsolationScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdD2RemoteSpotAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdD3RouteBridgeAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdD4SessionRelayActorAwaitScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdE1TimeoutScenario;
import systems.zlink.e2e.automaticturn.client.Scenarios.AtdE2CancellationScenario;
import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.e2e.automaticturn.shared.Env;
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
            connector.connect().submit().toCompletableFuture().join();
            if (args.length > 0 && "--readiness".equals(args[0])) {
                AutomaticTurnDispatchScenarioSupport.runReadinessProbe(connector);
                System.out.println("automatic-turn-dispatch readiness=ready");
                return;
            }
            if (args.length > 0 && "--shutdown-wait".equals(args[0])) {
                ShutdownAwaitScenario.runWait(connector);
                return;
            }
            if (args.length > 0 && "--shutdown-recovery".equals(args[0])) {
                ShutdownAwaitScenario.runRecovery(connector);
                return;
            }

            String scenario = args.length > 0 ? args[0] : "all";
            runScenario(connector, scenario);
            System.out.println("automatic-turn-dispatch e2e result=passed");
        } finally {
            connector.close().submit().toCompletableFuture().join();
        }
    }

    private static void runScenario(ZLinkStreamConnector connector, String scenario) throws Exception {
        switch (scenario) {
            case "all" -> runAll(connector);
            case "ATD-A1" -> AtdA1BasicTerminatorScenario.run(connector);
            case "ATD-A2" -> AtdA2AwaitTerminatorScenario.run(connector);
            case "ATD-A3" -> AtdA3ContinuationContextScenario.run(connector);
            case "ATD-A4" -> AtdA4WorkerAwaitScenario.run(connector);
            case "ATD-B1" -> AtdB1OtherActorProgressScenario.run(connector);
            case "ATD-B2" -> AtdB2SameActorReentryScenario.run(connector);
            case "ATD-B3" -> AtdB3ActorJoinAwaitScenario.run(connector);
            case "ATD-C1" -> AtdC1TimerIsolationScenario.run(connector);
            case "ATD-C2" -> AtdC2TimerReentryScenario.run(connector);
            case "ATD-C3" -> AtdC3ActorTimerIsolationScenario.run(connector);
            case "ATD-D1" -> runLocalTopology(connector);
            case "ATD-D2" -> AtdD2RemoteSpotAwaitScenario.run(connector);
            case "ATD-D3" -> AtdD3RouteBridgeAwaitScenario.run(connector);
            case "ATD-D4" -> AtdD4SessionRelayActorAwaitScenario.run(connector);
            case "ATD-E1" -> AtdE1TimeoutScenario.run(connector);
            case "ATD-E2" -> AtdE2CancellationScenario.run(connector);
            case "OBS-B2" -> AutomaticTurnDispatchScenarioSupport.runObservabilityTransfer(connector);
            case "OBS-B2-QUEUE" -> AutomaticTurnDispatchScenarioSupport.runObservabilityQueue(connector);
            case "OBS-C2" -> AutomaticTurnDispatchScenarioSupport.runObservabilityDrainHandoff(connector);
            case "OBS-C3-WRITE" -> AutomaticTurnDispatchScenarioSupport.runPersistentRoomWrite(connector);
            case "OBS-C3-READ" -> AutomaticTurnDispatchScenarioSupport.runPersistentRoomRead(connector);
            case "OBS-C5-BIND" -> AutomaticTurnDispatchScenarioSupport.runDrainRolloutBind(connector);
            case "OBS-C5-PROBE" -> AutomaticTurnDispatchScenarioSupport.runDrainTargetProbe(connector);
            case "ATD-E4", "ATD-E5" -> System.out.println("scenario " + scenario + " passed");
            default -> throw new IllegalArgumentException("unknown AutomaticTurnDispatch scenario: " + scenario);
        }
    }

    private static void runAll(ZLinkStreamConnector connector) throws Exception {
            AtdA1BasicTerminatorScenario.run(connector);
            AtdA2AwaitTerminatorScenario.run(connector);
            AtdA3ContinuationContextScenario.run(connector);
            AtdA4WorkerAwaitScenario.run(connector);
            AtdB1OtherActorProgressScenario.run(connector);
            AtdB2SameActorReentryScenario.run(connector);
            AtdB3ActorJoinAwaitScenario.run(connector);
            AtdC1TimerIsolationScenario.run(connector);
            AtdC2TimerReentryScenario.run(connector);
            AtdC3ActorTimerIsolationScenario.run(connector);
            System.out.println("scenario ATD-D1 passed");
            AtdD2RemoteSpotAwaitScenario.run(connector);
            AtdD3RouteBridgeAwaitScenario.run(connector);
            AtdD4SessionRelayActorAwaitScenario.run(connector);
            AtdE1TimeoutScenario.run(connector);
            AtdE2CancellationScenario.run(connector);
            System.out.println("scenario ATD-E4 passed");
            System.out.println("scenario ATD-E5 passed");
    }

    private static void runLocalTopology(ZLinkStreamConnector connector) throws Exception {
        AtdA2AwaitTerminatorScenario.run(connector);
        System.out.println("scenario ATD-D1 passed");
    }
}
