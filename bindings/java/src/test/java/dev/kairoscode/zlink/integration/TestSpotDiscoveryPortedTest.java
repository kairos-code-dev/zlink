package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestSpotDiscoveryPortedTest {
    @Test
    public void testSpotDeliveryViaDiscoveryAcrossContexts() {
        TestSupport.assumeNative();

        String serverEndpoint = TestSupport.tcpEndpoint();
        String clientEndpoint = TestSupport.tcpEndpoint();

        try (Context serverCtx = new Context();
             Context clientCtx = new Context();
             SpotNode serverNode = new SpotNode(serverCtx);
             SpotNode clientNode = new SpotNode(clientCtx);
             Spot publisher = new Spot(serverNode);
             Spot subscriber = new Spot(clientNode);
             Poller poller = new Poller();
             Message payload = Message.fromBytes("pong".getBytes(
                 StandardCharsets.UTF_8));
             Spot.RecvContext recvContext = subscriber.createRecvContext()) {
            serverNode.bind(serverEndpoint);
            clientNode.bind(clientEndpoint);
            clientNode.connectPeerPub(serverEndpoint);
            subscriber.subscribe("bench");

            TestSupport.waitUntil(() -> !clientNode.subPeers().isEmpty(),
              TestSupport.DEFAULT_TIMEOUT_MS,
              "spot client never observed publisher peer");

            poller.addSpotSub(subscriber, PollEventType.POLLIN);
            publisher.publish("bench", payload, SendFlag.NONE);

            TestSupport.waitUntil(() -> poller.pollCount(50) > 0,
              TestSupport.DEFAULT_TIMEOUT_MS,
              "spot subscriber never became readable");

            Spot.SpotRawBorrowed raw = subscriber.recvRawBorrowed(
              ReceiveFlag.DONTWAIT, recvContext);
            assertEquals(1, raw.parts().length);
            assertEquals("pong", new String(raw.parts()[0].data(),
              StandardCharsets.UTF_8));
        }
    }
}
