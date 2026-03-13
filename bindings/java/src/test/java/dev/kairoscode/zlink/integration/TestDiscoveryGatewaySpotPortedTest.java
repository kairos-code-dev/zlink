package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.receiver.ReceiverInfo;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestDiscoveryGatewaySpotPortedTest {
    @Test
    public void testDiscoveryGatewaySpotFlow() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            String regPub = TestSupport.inprocEndpoint("reg-pub-flow");
            String regRouter = TestSupport.inprocEndpoint("reg-router-flow");
            String receiverRoutingId = "receiver-rid-flow";

            try (Registry registry = new Registry(ctx);
                 Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
                 Receiver receiver = new Receiver(ctx, receiverRoutingId)) {
                registry.setEndpoints(regPub, regRouter);
                registry.start();

                discovery.connectRegistry(regRouter);

                receiver.bind(TestSupport.tcpEndpoint());
                receiver.connectRegistry(regRouter);
                String advertise = receiver.lastEndpoint();
                receiver.register("svc", advertise, 1);

                TestSupport.waitUntil(
                  () -> receiver.registerResult("svc").status() == 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "receiver register did not succeed");
                TestSupport.waitUntil(
                  () -> discovery.receiverCount("svc") > 0,
                  TestSupport.DEFAULT_TIMEOUT_MS,
                  "discovery did not resolve service");

                List<ReceiverInfo> providerInfos = discovery.getReceivers("svc");
                assertTrue(providerInfos.size() > 0);
                assertEquals(receiverRoutingId, new String(
                  providerInfos.get(0).routingId(), StandardCharsets.UTF_8));

                try (Gateway gateway = new Gateway(ctx, discovery)) {
                    TestSupport.waitUntil(
                      () -> gateway.connectionCount("svc") > 0,
                      TestSupport.DEFAULT_TIMEOUT_MS,
                      "gateway did not establish provider connection");
                    try (Message msg = Message.fromBytes(
                      "hello".getBytes(StandardCharsets.UTF_8))) {
                        gateway.sendTo("svc", msg, SendFlag.NONE);
                    }
                    try (Receiver.ReceiverMessages received = TestSupport.receiverRecvWithTimeout(
                      receiver, TestSupport.DEFAULT_TIMEOUT_MS)) {
                        Message[] parts = received.parts();
                        assertEquals("hello", new String(parts[parts.length - 1].data(),
                          StandardCharsets.UTF_8));
                    }
                }

                receiver.unregister("svc");
                runSpotScenario(ctx);
            }
        }
    }

    private static void runSpotScenario(Context ctx) {
        try (SpotNode serverNode = new SpotNode(ctx);
             SpotNode peerNode = new SpotNode(ctx);
             Spot publisher = new Spot(serverNode);
             Spot subscriber = new Spot(peerNode);
             Spot.RecvContext recvContext = subscriber.createRecvContext()) {
            String spotEp = TestSupport.tcpEndpoint();
            serverNode.bind(spotEp);
            peerNode.bind(TestSupport.tcpEndpoint());
            peerNode.connectPeerPub(spotEp);
            subscriber.subscribe("topic");
            TestSupport.waitUntil(() -> !peerNode.subPeers().isEmpty(),
              TestSupport.DEFAULT_TIMEOUT_MS,
              "spot client never observed publisher peer");

            try (Message m = Message.fromBytes("spot-msg".getBytes(
              StandardCharsets.UTF_8))) {
                publisher.publish("topic", new Message[] {m}, SendFlag.NONE);
            }
            Spot.SpotRawBorrowed raw = TestSupport.waitFor(
              () -> subscriber.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                recvContext),
              TestSupport.DEFAULT_TIMEOUT_MS);
            assertEquals(1, raw.parts().length);
            assertEquals("spot-msg",
              new String(raw.parts()[0].data(), StandardCharsets.UTF_8));
        }
    }
}
