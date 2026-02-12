package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.*;
import org.junit.jupiter.api.Test;

import java.lang.foreign.ValueLayout;
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

                        try (Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY)) {
                            discovery.connectRegistry(regPub);
                            discovery.subscribe("svc");

                            try (Receiver receiver = new Receiver(ctx)) {
                                String serviceEp =
                                  TestTransports.endpointFor(tc.name, tc.endpoint, "-svc");
                                receiver.bind(serviceEp);
                                try (Socket providerRouter = receiver.routerSocket();
                                     Gateway gateway = new Gateway(ctx, discovery)) {
                                    receiver.connectRegistry(regRouter);
                                    receiver.register("svc", serviceEp, 1);
                                    sleep(100);
                                    int status = -1;
                                    for (int i = 0; i < 20; i++) {
                                        Receiver.ReceiverResult res = receiver.registerResult("svc");
                                        status = res.status();
                                        if (res.status() == 0)
                                            break;
                                        sleep(50);
                                    }
                                    assertEquals(0, status);
                                    assertTrue(TestTransports.waitUntil(
                                      () -> discovery.receiverCount("svc") > 0, 5000));
                                    try (Gateway.PreparedService preparedSvc =
                                           gateway.prepareService("svc")) {
                                        assertTrue(TestTransports.waitUntil(
                                          () -> gateway.connectionCount(preparedSvc) > 0, 5000));
                                    TestTransports.gatewaySendWithRetry(
                                      gateway, "svc", "hello".getBytes(), SendFlag.NONE, 5000);

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

                                    assertTrue(rid.length > 0);

                                    try (Message moveMsg = Message.fromBytes(
                                      "hello-move".getBytes(StandardCharsets.UTF_8))) {
                                        gateway.sendMove(preparedSvc,
                                          new Message[]{moveMsg}, SendFlag.NONE);
                                    }

                                    byte[] ridMove =
                                      TestTransports.recvWithTimeout(providerRouter, 256, 2000);
                                    byte[] movePayload = new byte[0];
                                    for (int i = 0; i < 3; i++) {
                                        movePayload =
                                          TestTransports.recvWithTimeout(providerRouter, 256, 2000);
                                        if (movePayload.length == 0)
                                            continue;
                                        if ("hello-move".equals(new String(movePayload,
                                          StandardCharsets.UTF_8).trim())) {
                                            break;
                                        }
                                    }
                                    assertEquals("hello-move",
                                      new String(movePayload, StandardCharsets.UTF_8).trim());
                                    assertTrue(ridMove.length > 0);
                                    }
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
                                    try (Spot.PreparedTopic preparedTopic =
                                           spot.prepareTopic("topic")) {
                                        spot.subscribe(preparedTopic);
                                        spot.publish(preparedTopic,
                                            new Message[]{Message.fromBytes("spot-msg".getBytes())}, SendFlag.NONE);
                                    Spot.SpotMessage spotMsg =
                                      TestTransports.spotReceiveWithTimeout(spot, 5000);
                                    assertEquals("topic", spotMsg.topicId());
                                    assertTrue(spotMsg.parts().length == 1);
                                    assertEquals("spot-msg",
                                      new String(spotMsg.parts()[0], StandardCharsets.UTF_8).trim());

                                    try (Message moveMsg = Message.fromBytes(
                                      "spot-move".getBytes(StandardCharsets.UTF_8))) {
                                        spot.publishMove(preparedTopic,
                                          new Message[]{moveMsg}, SendFlag.NONE);
                                    }
                                    try (Spot.SpotMessages spotMoveMsg =
                                           TestTransports.spotReceiveMessagesWithTimeout(
                                             spot, 5000)) {
                                        assertEquals("topic", spotMoveMsg.topicId());
                                        assertEquals(1, spotMoveMsg.parts().length);
                                        assertEquals("spot-move",
                                          new String(spotMoveMsg.parts()[0].data(),
                                            StandardCharsets.UTF_8).trim());
                                    }

                                    try (Spot.RecvContext recvContext =
                                           spot.createRecvContext()) {
                                        try (Message rawMsg = Message.fromBytes(
                                          "spot-raw-1".getBytes(StandardCharsets.UTF_8))) {
                                            spot.publishMove(preparedTopic,
                                              new Message[]{rawMsg}, SendFlag.NONE);
                                        }
                                        Spot.SpotRawMessage rawMessage1 =
                                          TestTransports.spotReceiveRawWithTimeout(
                                            spot, recvContext, 5000);
                                        assertEquals("topic",
                                          new String(rawMessage1.topicId().toArray(
                                            ValueLayout.JAVA_BYTE),
                                            StandardCharsets.UTF_8));
                                        assertEquals(1, rawMessage1.parts().length);
                                        assertEquals("spot-raw-1",
                                          new String(rawMessage1.parts()[0].data(),
                                            StandardCharsets.UTF_8).trim());
                                        Message reusedPart = rawMessage1.parts()[0];

                                        try (Message rawMsg = Message.fromBytes(
                                          "spot-raw-2".getBytes(StandardCharsets.UTF_8))) {
                                            spot.publishMove(preparedTopic,
                                              new Message[]{rawMsg}, SendFlag.NONE);
                                        }
                                        Spot.SpotRawMessage rawMessage2 =
                                          TestTransports.spotReceiveRawWithTimeout(
                                            spot, recvContext, 5000);
                                        assertEquals(1, rawMessage2.parts().length);
                                        assertTrue(reusedPart == rawMessage2.parts()[0]);
                                        assertEquals("spot-raw-2",
                                          new String(rawMessage2.parts()[0].data(),
                                            StandardCharsets.UTF_8).trim());

                                        try (Message rawMsg = Message.fromBytes(
                                          "spot-borrowed-1".getBytes(
                                            StandardCharsets.UTF_8))) {
                                            spot.publishMove(preparedTopic,
                                              new Message[]{rawMsg}, SendFlag.NONE);
                                        }
                                        Spot.SpotRawBorrowed borrowed1 =
                                          spot.recvRawBorrowed(ReceiveFlag.NONE,
                                            recvContext);
                                        assertEquals("topic",
                                          new String(borrowed1.topicId().toArray(
                                            ValueLayout.JAVA_BYTE),
                                            StandardCharsets.UTF_8));
                                        assertEquals("spot-borrowed-1",
                                          new String(borrowed1.parts()[0].data(),
                                            StandardCharsets.UTF_8).trim());

                                        try (Message rawMsg = Message.fromBytes(
                                          "spot-borrowed-2".getBytes(
                                            StandardCharsets.UTF_8))) {
                                            spot.publishMove(preparedTopic,
                                              new Message[]{rawMsg}, SendFlag.NONE);
                                        }
                                        Spot.SpotRawBorrowed borrowed2 =
                                          spot.recvRawBorrowed(ReceiveFlag.NONE,
                                            recvContext);
                                        assertTrue(borrowed1 == borrowed2);
                                        assertEquals("spot-borrowed-2",
                                          new String(borrowed2.parts()[0].data(),
                                            StandardCharsets.UTF_8).trim());
                                    }
                                    }
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
