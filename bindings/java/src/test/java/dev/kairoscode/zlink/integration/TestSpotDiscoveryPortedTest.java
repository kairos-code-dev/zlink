package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestSpotDiscoveryPortedTest {
    @Test
    public void testSpotDeliveryViaDiscoveryAcrossContexts() {
        TestSupport.assumeNative();

        String regPub = TestSupport.tcpEndpoint();
        String regRouter = TestSupport.tcpEndpoint();
        String serverEndpoint = TestSupport.tcpEndpoint();
        String clientEndpoint = TestSupport.tcpEndpoint();

        try (Context registryCtx = new Context();
             Context serverCtx = new Context();
             Context clientCtx = new Context();
             Registry registry = new Registry(registryCtx);
             SpotNode serverNode = new SpotNode(serverCtx);
             SpotNode clientNode = new SpotNode(clientCtx);
             Discovery discovery = new Discovery(clientCtx, ServiceType.SPOT);
             Spot publisher = new Spot(serverNode);
             Spot subscriber = new Spot(clientNode);
             Poller poller = new Poller();
             Message payload = Message.fromBytes("pong".getBytes(
                 StandardCharsets.UTF_8));
             Spot.RecvContext recvContext = subscriber.createRecvContext()) {
            registry.setEndpoints(regPub, regRouter);
            registry.setBroadcastInterval(100);
            registry.start();

            serverNode.bind(serverEndpoint);
            serverNode.connectRegistry(regRouter);
            serverNode.register("perf-spot", serverEndpoint);

            clientNode.bind(clientEndpoint);
            discovery.connectRegistry(regPub);
            discovery.subscribe("perf-spot");
            clientNode.setDiscovery(discovery, "perf-spot");
            subscriber.subscribe("bench");
            clientNode.setDiscovery(discovery, "perf-spot");

            TestSupport.waitUntil(
                () -> discovery.serviceAvailable("perf-spot")
                    || discovery.receiverCount("perf-spot") > 0,
                TestSupport.DEFAULT_TIMEOUT_MS,
                "spot discovery never became available");
            TestSupport.waitUntil(() -> clientNode.subPeers().size() > 0,
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
