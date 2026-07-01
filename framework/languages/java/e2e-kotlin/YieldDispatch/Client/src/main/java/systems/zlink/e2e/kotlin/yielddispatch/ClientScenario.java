package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA1BasicTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA2YieldTerminatorScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA3ContinuationContextScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdA4WorkerYieldScenario;
import systems.zlink.e2e.kotlin.yielddispatch.scenarios.YdB1OtherActorProgressScenario;
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

    public static void run() {
        ZLinkStreamConnector roomA = ClientStreamSupport.createConnector();
        ZLinkStreamConnector roomB = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> roomA.connect().await());
            ClientStreamSupport.awaitLifecycle(() -> roomB.connect().await());
            YdB1OtherActorProgressScenario.JoinedActors actors =
                YdB1OtherActorProgressScenario.run(roomA, roomB);
            YdA1BasicTerminatorScenario.Result a1 = YdA1BasicTerminatorScenario.run(roomA, actors.actorA());
            YdA2YieldTerminatorScenario.run(roomA, actors.actorA(), a1);
            YdA3ContinuationContextScenario.run(roomA, actors.actorA(), roomB, actors.actorB());
            YdA4WorkerYieldScenario.run(roomB, actors.actorB());
            YdC1TimerIsolationScenario.run(roomA);
            YdC2TimerReentryScenario.run(roomA);
            YdE1TimeoutScenario.run(roomA);
            YdE2CancellationScenario.run(roomA);
            YdD1LocalTopologyScenario.run(roomA);
        } finally {
            ScenarioAssert.lifecycle(() -> roomA.close().await());
            ScenarioAssert.lifecycle(() -> roomB.close().await());
        }
    }

    public static void runD2() {
        ZLinkStreamConnector connector = ClientStreamSupport.createConnector();
        try {
            ClientStreamSupport.awaitLifecycle(() -> connector.connect().await());
            YdD2RemoteSpotYieldScenario.run(connector);
            YdD3RouteBridgeYieldScenario.run(connector);
        } finally {
            ScenarioAssert.lifecycle(() -> connector.close().await());
        }
    }
}
