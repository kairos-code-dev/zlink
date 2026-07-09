package systems.zlink.e2e.kotlin.yielddispatch;

import java.util.ArrayList;
import java.time.Duration;
import java.util.List;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA1BasicTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA2YieldTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA3ContinuationContextScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA4WorkerYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB1OtherActorProgressScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB2SameActorReentryScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB3ActorJoinYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdC1TimerIsolationScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdC2TimerReentryScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdC3ActorTimerIsolationScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD1LocalTopologyScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD2RemoteSpotYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD3RouteBridgeYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD4SessionRelayActorYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdE1TimeoutScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdE2CancellationScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdE3ShutdownRecoveryScenario;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ClientScenario {
    private ClientScenario() {
    }

    public static void run(String scenario) {
        if ("YD-C1".equals(scenario) || "YD-C2".equals(scenario) || "YD-E1".equals(scenario)) {
            runSingleConnectorScenario(scenario);
            return;
        }
        ZLinkStreamConnector roomA = ClientStreamSupport.createConnector();
        ZLinkStreamConnector roomB = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> roomA.connect().await());
            ClientStreamSupport.awaitLifecycle(() -> roomB.connect().await());
            YdB1OtherActorProgressScenario.JoinedActors actors =
                YdB1OtherActorProgressScenario.run(roomA, roomB);
            if ("YD-B1".equals(scenario)) {
                return;
            }
            runScenario(scenario, roomA, roomB, actors);
        } finally {
            ScenarioAssert.lifecycle(() -> roomA.close().await());
            ScenarioAssert.lifecycle(() -> roomB.close().await());
        }
    }

    private static void runSingleConnectorScenario(String scenario) {
        ZLinkStreamConnector connector = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> connector.connect().await());
            switch (scenario) {
                case "YD-C1" -> YdC1TimerIsolationScenario.run(connector);
                case "YD-C2" -> YdC2TimerReentryScenario.run(connector);
                case "YD-E1" -> YdE1TimeoutScenario.run(connector);
                default -> throw new IllegalArgumentException("unknown YieldDispatch single-connector scenario: " + scenario);
            }
        } finally {
            ScenarioAssert.lifecycle(() -> connector.close().await());
        }
    }

    private static void runScenario(
        String scenario,
        ZLinkStreamConnector roomA,
        ZLinkStreamConnector roomB,
        YdB1OtherActorProgressScenario.JoinedActors actors) {
        switch (scenario) {
            case "all" -> runAll(roomA, roomB, actors);
            case "YD-A1" -> YdA1BasicTerminatorScenario.run(roomA, actors.actorA());
            case "YD-A2" -> {
                YdA1BasicTerminatorScenario.Result a1 = YdA1BasicTerminatorScenario.run(roomA, actors.actorA());
                YdA2YieldTerminatorScenario.run(roomA, actors.actorA(), a1);
            }
            case "YD-A3" -> YdA3ContinuationContextScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
            case "YD-A4" -> YdA4WorkerYieldScenario.run(roomA, actors.actorA());
            case "YD-B2" -> YdB2SameActorReentryScenario.run(roomA, actors.actorA());
            case "YD-B3" -> YdB3ActorJoinYieldScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
            case "YD-C1" -> YdC1TimerIsolationScenario.run(roomA);
            case "YD-C2" -> YdC2TimerReentryScenario.run(roomA);
            case "YD-C3" -> YdC3ActorTimerIsolationScenario.run(roomA);
            case "YD-D1" -> {
                List<String> completed = runLocalTopologyScenarios(roomA, roomB, actors);
                YdD1LocalTopologyScenario.run(roomA, completed);
            }
            case "YD-D4" -> runD4(roomA, actors.actorA());
            case "YD-E1" -> YdE1TimeoutScenario.run(roomA);
            case "YD-E2" -> YdE2CancellationScenario.run(roomA);
            case "YD-E3" -> runE3Scenario(roomA);
            default -> throw new IllegalArgumentException("unknown YieldDispatch scenario: " + scenario);
        }
    }

    private static void runAll(
        ZLinkStreamConnector roomA,
        ZLinkStreamConnector roomB,
        YdB1OtherActorProgressScenario.JoinedActors actors) {
        List<String> completed = runLocalTopologyScenarios(roomA, roomB, actors);
        YdE2CancellationScenario.run(roomA);
        YdD1LocalTopologyScenario.run(roomA, completed);
    }

    private static List<String> runLocalTopologyScenarios(
        ZLinkStreamConnector roomA,
        ZLinkStreamConnector roomB,
        YdB1OtherActorProgressScenario.JoinedActors actors) {
        List<String> completed = new ArrayList<>();
        completed.add("YD-B1");
        YdA1BasicTerminatorScenario.Result a1 = YdA1BasicTerminatorScenario.run(roomA, actors.actorA());
        completed.add("YD-A1");
        YdA2YieldTerminatorScenario.run(roomA, actors.actorA(), a1);
        completed.add("YD-A2");
        YdA3ContinuationContextScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
        completed.add("YD-A3");
        YdA4WorkerYieldScenario.run(roomA, actors.actorA());
        completed.add("YD-A4");
        YdB2SameActorReentryScenario.run(roomA, actors.actorA());
        completed.add("YD-B2");
        YdB3ActorJoinYieldScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
        completed.add("YD-B3");
        YdC1TimerIsolationScenario.run(roomA);
        completed.add("YD-C1");
        YdC2TimerReentryScenario.run(roomA);
        completed.add("YD-C2");
        YdC3ActorTimerIsolationScenario.run(roomA);
        completed.add("YD-C3");
        runD4(roomA, actors.actorA());
        completed.add("YD-D4");
        YdE1TimeoutScenario.run(roomA);
        completed.add("YD-E1");
        return completed;
    }

    private static void runD4(
        ZLinkStreamConnector roomA,
        String actorId) {
        ZLinkStreamConnector unbound = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> unbound.connect().await());
            YdD4SessionRelayActorYieldScenario.run(roomA, actorId, unbound);
        } catch (Exception error) {
            throw new IllegalStateException("YD-D4 scenario failed", error);
        } finally {
            ScenarioAssert.lifecycle(() -> unbound.close().await());
        }
    }

    public static void runD2(String scenario) {
        ZLinkStreamConnector connector = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> connector.connect().await());
            switch (scenario) {
                case "d2" -> {
                    YdD2RemoteSpotYieldScenario.run(connector);
                    YdD3RouteBridgeYieldScenario.run(connector);
                }
                case "YD-D2" -> YdD2RemoteSpotYieldScenario.run(connector);
                case "YD-D3" -> YdD3RouteBridgeYieldScenario.run(connector);
                default -> throw new IllegalArgumentException("unknown YieldDispatch scenario: " + scenario);
            }
        } finally {
            ScenarioAssert.lifecycle(() -> connector.close().await());
        }
    }

    public static void runE3() {
        ZLinkStreamConnector connector = ClientStreamSupport.createConnector();
        ZLinkStreamConnector recovery = ClientStreamSupport.createConnector(Duration.ofSeconds(120));
        try {
            ClientStreamSupport.awaitLifecycle(() -> connector.connect().await());
            ClientStreamSupport.awaitLifecycle(() -> recovery.connect().await());
            YdE3ShutdownRecoveryScenario.run(connector, recovery);
        } finally {
            ScenarioAssert.lifecycle(() -> recovery.close().await());
            ScenarioAssert.lifecycle(() -> connector.close().await());
        }
    }

    private static void runE3Scenario(ZLinkStreamConnector connector) {
        ZLinkStreamConnector recovery = ClientStreamSupport.createConnector(Duration.ofSeconds(120));
        try {
            ClientStreamSupport.awaitLifecycle(() -> recovery.connect().await());
            YdE3ShutdownRecoveryScenario.run(connector, recovery);
        } finally {
            ScenarioAssert.lifecycle(() -> recovery.close().await());
        }
    }
}
