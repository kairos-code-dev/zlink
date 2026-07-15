package systems.zlink.e2e.observabilityops.client;

import java.net.URI;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsA1FlowCorrelationScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsA2ErrorFlowScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsA3OffNodeFlowScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsA4FanoutTimerScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsB1ConnectionMetricsScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsB2SpotActorMetricsScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsB3FanoutLeaseMetricsScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsB4ReaderFreeScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsC1DrainingMarkerScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsC2ActorHandoffScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsC3SpotDrainPolicyScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsC4ForcedDrainScenario;
import systems.zlink.e2e.observabilityops.client.Scenarios.ObsC5RolloutScenario;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;

public final class Program {
    private Program() {
    }

    public static void main(String... args) throws Exception {
        if (args.length != 1) {
            throw new IllegalArgumentException("Usage: observability-ops-client <scenario>");
        }
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions.createDefault(
                URI.create(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))));
        try {
            connector.connect().submit().toCompletableFuture().join();
            run(connector, args[0]);
            System.out.println("observability-ops client scenario=" + args[0] + " result=passed");
        } finally {
            connector.close().submit().toCompletableFuture().join();
        }
    }

    private static void run(ZLinkStreamConnector connector, String scenario) throws Exception {
        switch (scenario) {
            case "OBS-A1" -> ObsA1FlowCorrelationScenario.run(connector);
            case "OBS-A2" -> ObsA2ErrorFlowScenario.run();
            case "OBS-A3" -> ObsA3OffNodeFlowScenario.run(connector);
            case "OBS-A4" -> ObsA4FanoutTimerScenario.run(connector);
            case "OBS-B1" -> ObsB1ConnectionMetricsScenario.run();
            case "OBS-B2" -> ObsB2SpotActorMetricsScenario.transfer(connector);
            case "OBS-B2-QUEUE" -> ObsB2SpotActorMetricsScenario.queue(connector);
            case "OBS-B3" -> ObsB3FanoutLeaseMetricsScenario.run(connector);
            case "OBS-B4" -> ObsB4ReaderFreeScenario.run();
            case "OBS-C1" -> ObsC1DrainingMarkerScenario.run(connector);
            case "OBS-C2" -> ObsC2ActorHandoffScenario.run(connector);
            case "OBS-C3-WRITE" -> ObsC3SpotDrainPolicyScenario.write(connector);
            case "OBS-C3-READ" -> ObsC3SpotDrainPolicyScenario.read(connector);
            case "OBS-C4" -> ObsC4ForcedDrainScenario.run();
            case "OBS-C5-BIND" -> ObsC5RolloutScenario.bind(connector);
            case "OBS-C5-PROBE" -> ObsC5RolloutScenario.probe(connector);
            default -> throw new IllegalArgumentException(
                "unknown ObservabilityOps client scenario: " + scenario);
        }
    }
}
