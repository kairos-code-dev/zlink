package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA1BasicTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA2YieldTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA3ContinuationContextScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA4WorkerYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB1OtherActorProgressScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB2SameActorReentryScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB3ActorJoinYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdC1TimerIsolationScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdC2TimerReentryScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD1LocalTopologyScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD2RemoteSpotYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdD3RouteBridgeYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdE1TimeoutScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdE2CancellationScenario;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ClientScenario {
    private ClientScenario() {
    }

    public static void run(String scenario) {
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
            case "YD-A4" -> YdA4WorkerYieldScenario.run(roomB, actors.actorB());
            case "YD-B2" -> YdB2SameActorReentryScenario.run(roomA, actors.actorA());
            case "YD-B3" -> YdB3ActorJoinYieldScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
            case "YD-C1" -> YdC1TimerIsolationScenario.run(roomA);
            case "YD-C2" -> YdC2TimerReentryScenario.run(roomA);
            case "YD-D1" -> YdD1LocalTopologyScenario.run(roomA);
            case "YD-E1" -> YdE1TimeoutScenario.run(roomA);
            case "YD-E2" -> YdE2CancellationScenario.run(roomA);
            default -> throw new IllegalArgumentException("unknown YieldDispatch scenario: " + scenario);
        }
    }

    private static void runAll(
        ZLinkStreamConnector roomA,
        ZLinkStreamConnector roomB,
        YdB1OtherActorProgressScenario.JoinedActors actors) {
        YdA1BasicTerminatorScenario.Result a1 = YdA1BasicTerminatorScenario.run(roomA, actors.actorA());
        YdA2YieldTerminatorScenario.run(roomA, actors.actorA(), a1);
        YdA3ContinuationContextScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
        YdA4WorkerYieldScenario.run(roomB, actors.actorB());
        YdB2SameActorReentryScenario.run(roomA, actors.actorA());
        YdB3ActorJoinYieldScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
        YdC1TimerIsolationScenario.run(roomA);
        YdC2TimerReentryScenario.run(roomA);
        YdE1TimeoutScenario.run(roomA);
        YdE2CancellationScenario.run(roomA);
        YdD1LocalTopologyScenario.run(roomA);
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
}
