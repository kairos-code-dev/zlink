package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestSpotPubsubScenarioPortedTest {
    @Test
    public void testSpotPeerTcp() {
        runSpotPeerTransport("tcp");
    }

    @Test
    public void testSpotPeerWs() {
        IntegrationSupport.requireCapability("ws");
        runSpotPeerTransport("ws");
    }

    private static void runSpotPeerTransport(String transport) {
        TestSupport.assumeNative();

        String topic = transport + ":test";
        String payload = transport + "-msg";
        String endpointA = switch (transport) {
            case "tcp" -> TestSupport.tcpEndpoint();
            case "ws" -> TestSupport.wsEndpoint();
            default -> throw new IllegalArgumentException("unsupported: " + transport);
        };
        String endpointB = switch (transport) {
            case "tcp" -> TestSupport.tcpEndpoint();
            case "ws" -> TestSupport.wsEndpoint();
            default -> throw new IllegalArgumentException("unsupported: " + transport);
        };

        try (Context ctx = new Context();
             SpotNode nodeA = new SpotNode(ctx);
             SpotNode nodeB = new SpotNode(ctx)) {
            nodeA.bind(endpointA);
            nodeB.bind(endpointB);
            nodeB.connectPeerPub(endpointA);

            try (Spot spotB = new Spot(nodeB)) {
                spotB.subscribe(topic);
                TestSupport.sleepMs(80);

                try (Spot spotA = new Spot(nodeA);
                     Message part = Message.fromBytes(
                       payload.getBytes(StandardCharsets.UTF_8))) {
                    spotA.publish(topic, part, SendFlag.NONE);
                }

                Spot.SpotMessage received = TestSupport.spotRecvWithTimeout(spotB,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertEquals(topic, received.topicId());
                assertEquals(payload, new String(received.parts()[0].data(),
                    StandardCharsets.UTF_8));
            }
        }
    }
}
