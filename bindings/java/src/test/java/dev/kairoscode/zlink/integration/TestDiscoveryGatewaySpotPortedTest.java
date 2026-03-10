package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.receiver.ReceiverInfo;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
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

                discovery.connectRegistry(regPub);
                discovery.subscribe("svc");

                String serviceEp = TestSupport.tcpEndpoint();
                receiver.bind(serviceEp);

                try (Socket providerRouter = receiver.routerSocket();
                     Gateway gateway = new Gateway(ctx, discovery)) {
                    receiver.connectRegistry(regRouter);

                    String advertise = TestSupport.bytesToString(
                        providerRouter.getSockOptBytes(
                            dev.kairoscode.zlink.SocketOption.LAST_ENDPOINT,
                            256));
                    receiver.register("svc", advertise, 1);

                    int status = -1;
                    for (int i = 0; i < 40; i++) {
                        status = receiver.registerResult("svc").status();
                        if (status == 0)
                            break;
                        TestSupport.sleepMs(50);
                    }
                    assertEquals(0, status);

                    TestSupport.waitUntil(
                        () -> discovery.receiverCount("svc") > 0,
                        TestSupport.DEFAULT_TIMEOUT_MS,
                        "discovery did not resolve service");
                    List<ReceiverInfo> providerInfos = discovery.getReceivers("svc");
                    assertTrue(providerInfos.size() > 0);
                    byte[] providerRoutingId = providerInfos.get(0).routingId();
                    assertTrue(providerRoutingId.length > 0);
                    assertEquals(receiverRoutingId, new String(providerRoutingId,
                        StandardCharsets.UTF_8));

                    TestSupport.waitUntil(
                        () -> gateway.connectionCount("svc") > 0,
                        TestSupport.DEFAULT_TIMEOUT_MS,
                        "gateway did not establish provider connection");

                    try (Message msg = Message.fromBytes(
                        "hello".getBytes(StandardCharsets.UTF_8))) {
                        gateway.sendTo("svc", msg, SendFlag.NONE);
                    }
                    ProviderFrame helloFrame = recvProviderFrame(
                        providerRouter);
                    assertEquals("hello", helloFrame.payloadText());

                    try (Message directRid = Message.fromBytes(
                        "hello-rid".getBytes(StandardCharsets.UTF_8))) {
                        gateway.sendTo("svc", receiverRoutingId,
                            directRid, SendFlag.NONE);
                    }
                    ProviderFrame ridFrame = recvProviderFrame(providerRouter);
                    assertEquals("hello-rid", ridFrame.payloadText());

                    try (Message ridMulti1 = Message.fromBytes(
                        "hello-rid-list-a".getBytes(StandardCharsets.UTF_8));
                         Message ridMulti2 = Message.fromBytes(
                             "hello-rid-list-b".getBytes(
                                 StandardCharsets.UTF_8))) {
                        gateway.sendTo("svc", receiverRoutingId,
                            new Message[] {ridMulti1, ridMulti2},
                            SendFlag.NONE);
                    }
                    ProviderFrame ridListFrame = recvProviderFrame(providerRouter);
                    assertEquals("hello-rid-list-b", ridListFrame.payloadText());

                    try (Message list1 = Message.fromBytes(
                        "hello-list-a".getBytes(StandardCharsets.UTF_8));
                         Message list2 = Message.fromBytes(
                             "hello-list-b".getBytes(StandardCharsets.UTF_8))) {
                        gateway.sendTo("svc", new Message[] {list1, list2},
                            SendFlag.NONE);
                    }
                    ProviderFrame listFrame = recvProviderFrame(providerRouter);
                    assertEquals("hello-list-b", listFrame.payloadText());

                    try (Arena arena = Arena.ofShared()) {
                        MemorySegment constPayload = arena.allocateFrom(
                            "hello-const", StandardCharsets.UTF_8);
                        try (Message constMsg = Message.fromMemorySegment(
                            constPayload, 0, "hello-const".length())) {
                            gateway.sendTo("svc", constMsg, SendFlag.NONE);
                        }
                    }
                    ProviderFrame constFrame = recvProviderFrame(
                        providerRouter);
                    assertEquals("hello-const", constFrame.payloadText());

                    // Explicitly deregister before teardown to avoid carrying
                    // transient routing state into following integration tests.
                    receiver.unregister("svc");
                }

                runSpotScenario(ctx, regRouter);
            }
        }
    }

    private static void runSpotScenario(Context ctx, String regRouter) {
        try (SpotNode node = new SpotNode(ctx)) {
            String spotEp = TestSupport.tcpEndpoint();
            node.bind(spotEp);
            node.register("spot", spotEp);
            TestSupport.sleepMs(100);

            try (SpotNode peerNode = new SpotNode(ctx);
                Spot spot = new Spot(peerNode);
                Spot.PreparedTopic topic = spot.prepareTopic("topic");
                Spot.PublishContext publishContext =
                  spot.createPublishContext()) {
                peerNode.connectPeerPub(spotEp);
                TestSupport.sleepMs(120);

                spot.subscribe(topic);

                try (Message m = Message.fromBytes(
                    "spot-msg".getBytes(StandardCharsets.UTF_8))) {
                    spot.publish(topic, new Message[] {m}, SendFlag.NONE);
                }
                Spot.SpotMessage msg = TestSupport.spotRecvWithTimeout(spot,
                    TestSupport.DEFAULT_TIMEOUT_MS);
                assertEquals("topic", msg.topicId());
                assertEquals(1, msg.parts().length);
                assertEquals("spot-msg",
                    new String(msg.parts()[0].data(), StandardCharsets.UTF_8));

                try (Message move = Message.fromBytes(
                    "spot-move".getBytes(StandardCharsets.UTF_8))) {
                    spot.publishMove(topic, new Message[] {move}, SendFlag.NONE);
                }
                try (Spot.SpotMessage moved = TestSupport.waitFor(
                    () -> spot.recv(ReceiveFlag.DONTWAIT),
                    TestSupport.DEFAULT_TIMEOUT_MS)) {
                    assertEquals("topic", moved.topicId());
                    assertEquals(1, moved.parts().length);
                    assertEquals("spot-move", new String(moved.parts()[0].data(),
                        StandardCharsets.UTF_8));
                }

                try (Message move = Message.fromBytes(
                    "spot-move-ctx".getBytes(StandardCharsets.UTF_8))) {
                    spot.publishMove(topic, move, SendFlag.NONE, publishContext);
                }
                try (Spot.SpotMessage movedCtx = TestSupport.waitFor(
                    () -> spot.recv(ReceiveFlag.DONTWAIT),
                    TestSupport.DEFAULT_TIMEOUT_MS)) {
                    assertEquals("spot-move-ctx", new String(
                        movedCtx.parts()[0].data(), StandardCharsets.UTF_8));
                }

                try (Arena arena = Arena.ofShared()) {
                    MemorySegment constPayload = arena.allocateFrom(
                        "spot-const-ctx", StandardCharsets.UTF_8);
                    try (Message constMsg = Message.fromMemorySegment(constPayload, 0,
                        "spot-const-ctx".length())) {
                        spot.publish(topic, new Message[] {constMsg}, SendFlag.NONE,
                            publishContext);
                    }
                }
                try (Spot.SpotMessage constMsg = TestSupport.waitFor(
                    () -> spot.recv(ReceiveFlag.DONTWAIT),
                    TestSupport.DEFAULT_TIMEOUT_MS)) {
                    assertEquals("spot-const-ctx", new String(
                        constMsg.parts()[0].data(), StandardCharsets.UTF_8));
                }

                try (Spot.RecvContext recvContext = spot.createRecvContext()) {
                    try (Message raw = Message.fromBytes(
                        "spot-raw-1".getBytes(StandardCharsets.UTF_8))) {
                        spot.publishMove(topic, new Message[] {raw},
                            SendFlag.NONE);
                    }
                    Spot.SpotRawMessage raw1 = TestSupport.waitFor(
                        () -> spot.recvRaw(ReceiveFlag.DONTWAIT, recvContext),
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("topic", new String(raw1.topicId().toArray(
                        ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8));
                    assertEquals("spot-raw-1", new String(raw1.parts()[0].data(),
                        StandardCharsets.UTF_8));
                    Message reused = raw1.parts()[0];

                    try (Message raw = Message.fromBytes(
                        "spot-raw-2".getBytes(StandardCharsets.UTF_8))) {
                        spot.publishMove(topic, new Message[] {raw},
                            SendFlag.NONE);
                    }
                    Spot.SpotRawMessage raw2 = TestSupport.waitFor(
                        () -> spot.recvRaw(ReceiveFlag.DONTWAIT, recvContext),
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertSame(reused, raw2.parts()[0]);
                    assertEquals("spot-raw-2", new String(raw2.parts()[0].data(),
                        StandardCharsets.UTF_8));

                    try (Message raw = Message.fromBytes(
                        "spot-borrowed-1".getBytes(StandardCharsets.UTF_8))) {
                        spot.publishMove(topic, new Message[] {raw},
                            SendFlag.NONE);
                    }
                    Spot.SpotRawBorrowed borrowed1 = TestSupport.waitFor(
                        () -> spot.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                            recvContext), TestSupport.DEFAULT_TIMEOUT_MS);
                    assertEquals("spot-borrowed-1", new String(
                        borrowed1.parts()[0].data(), StandardCharsets.UTF_8));
                    long topicBufferAddr = borrowed1.topicIdBuffer().address();

                    try (Message raw = Message.fromBytes(
                        "spot-borrowed-2".getBytes(StandardCharsets.UTF_8))) {
                        spot.publishMove(topic, new Message[] {raw},
                            SendFlag.NONE);
                    }
                    Spot.SpotRawBorrowed borrowed2 = TestSupport.waitFor(
                        () -> spot.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                            recvContext), TestSupport.DEFAULT_TIMEOUT_MS);
                    assertSame(borrowed1, borrowed2);
                    assertEquals(topicBufferAddr,
                        borrowed2.topicIdBuffer().address());
                    assertEquals("spot-borrowed-2", new String(
                        borrowed2.parts()[0].data(), StandardCharsets.UTF_8));
                }

                peerNode.disconnectPeerPub(spotEp);
                node.unregister("spot");
            }
        }
    }

    private static ProviderFrame recvProviderFrame(Socket providerRouter) {
        byte[] rid = TestSupport.recvWithTimeout(providerRouter, 256,
            TestSupport.DEFAULT_TIMEOUT_MS);
        byte[] payload = TestSupport.recvWithTimeout(providerRouter, 256,
            TestSupport.DEFAULT_TIMEOUT_MS);
        while (providerRouter.getSockOptInt(SocketOption.RCVMORE) != 0) {
            payload = TestSupport.recvWithTimeout(providerRouter, 256,
                TestSupport.DEFAULT_TIMEOUT_MS);
        }
        return new ProviderFrame(rid, payload);
    }

    private record ProviderFrame(byte[] routingId, byte[] payload) {
        String payloadText() {
            return new String(payload, StandardCharsets.UTF_8);
        }
    }
}
