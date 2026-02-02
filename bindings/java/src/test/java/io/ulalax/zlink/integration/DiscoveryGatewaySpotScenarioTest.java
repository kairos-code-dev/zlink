package io.ulalax.zlink.integration;

import io.ulalax.zlink.*;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.UUID;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class DiscoveryGatewaySpotScenarioTest {
    @Test
    public void discoveryGatewaySpotFlow() {
        Context ctx = new Context();
        try {
            for (TestTransports.TransportCase tc : TestTransports.transports("discovery")) {
                TestTransports.tryTransport(tc.name, () -> {
                    String regPub = "inproc://reg-pub-" + UUID.randomUUID();
                    String regRouter = "inproc://reg-router-" + UUID.randomUUID();
                    try (Registry registry = new Registry(ctx)) {
                        registry.setEndpoints(regPub, regRouter);
                        registry.start();

                        try (Discovery discovery = new Discovery(ctx)) {
                            discovery.connectRegistry(regPub);
                            discovery.subscribe("svc");

                            try (Provider provider = new Provider(ctx)) {
                                String serviceEp =
                                  TestTransports.endpointFor(tc.name, tc.endpoint, "-svc");
                                provider.bind(serviceEp);
                                provider.connectRegistry(regRouter);
                                provider.register("svc", serviceEp, 1);
                                sleep(100);
                                for (int i = 0; i < 20; i++) {
                                    Provider.ProviderResult res = provider.registerResult("svc");
                                    if (res.status() == 0)
                                        break;
                                    sleep(50);
                                }
                                for (int i = 0; i < 50; i++) {
                                    if (discovery.providerCount("svc") > 0)
                                        break;
                                    sleep(50);
                                }

                                try (Socket providerRouter = provider.routerSocket();
                                     Gateway gateway = new Gateway(ctx, discovery)) {
                                    for (int i = 0; i < 50; i++) {
                                        if (gateway.connectionCount("svc") > 0)
                                            break;
                                        sleep(50);
                                    }
                                    gateway.send("svc",
                                        new Message[]{Message.fromBytes("hello".getBytes())}, 0);

                                    byte[] rid = TestTransports.recvWithTimeout(providerRouter, 256, 2000);
                                    byte[] payload = new byte[0];
                                    for (int i = 0; i < 3; i++) {
                                        payload = TestTransports.recvWithTimeout(providerRouter, 256, 2000);
                                        if (payload.length == 0)
                                            continue;
                                        if ("hello".equals(new String(payload, StandardCharsets.UTF_8).trim())) {
                                            break;
                                        }
                                    }
                                    assertEquals("hello", new String(payload, StandardCharsets.UTF_8).trim());

                                    providerRouter.send(rid, TestTransports.ZLINK_SNDMORE);
                                    TestTransports.sendWithRetry(providerRouter, "world".getBytes(), 0, 2000);

                                    Gateway.GatewayMessage gwMsg =
                                      TestTransports.gatewayReceiveWithTimeout(gateway, 5000);
                                    assertEquals("svc", gwMsg.serviceName());
                                    assertEquals(1, gwMsg.parts().length);
                                    assertEquals("world", new String(gwMsg.parts()[0], StandardCharsets.UTF_8).trim());
                                }
                            }

                            try (SpotNode node = new SpotNode(ctx)) {
                                String spotEp =
                                  TestTransports.endpointFor(tc.name, tc.endpoint, "-spot");
                                node.bind(spotEp);
                                node.connectRegistry(regRouter);
                                node.register("spot", spotEp);
                                sleep(100);

                                try (SpotNode peerNode = new SpotNode(ctx);
                                     Spot spot = new Spot(peerNode)) {
                                    peerNode.connectRegistry(regRouter);
                                    peerNode.connectPeerPub(spotEp);
                                    sleep(100);
                                    spot.subscribe("topic");
                                    spot.publish("topic",
                                        new Message[]{Message.fromBytes("spot-msg".getBytes())}, 0);
                                    Spot.SpotMessage spotMsg =
                                      TestTransports.spotReceiveWithTimeout(spot, 5000);
                                    assertEquals("topic", spotMsg.topicId());
                                    assertTrue(spotMsg.parts().length == 1);
                                    assertEquals("spot-msg",
                                      new String(spotMsg.parts()[0], StandardCharsets.UTF_8).trim());
                                }
                            }
                        }
                    }
                });
            }
        } finally {
            TestTransports.closeContext(ctx);
        }
    }

    private static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }
}
