package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestSpotServicePollerPortedTest {
    @Test
    public void testSpotSubCanBePolledViaServiceInstance() {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        try (Context ctx = new Context();
             SpotNode pubNode = new SpotNode(ctx);
             SpotNode subNode = new SpotNode(ctx);
             Spot publisher = new Spot(pubNode);
             Spot subscriber = new Spot(subNode);
             Poller poller = new Poller();
             Message payload = Message.fromBytes("pong".getBytes(
                 StandardCharsets.UTF_8));
             Spot.RecvContext recvContext = subscriber.createRecvContext()) {
            pubNode.bind(endpoint);
            subNode.connectPeerPub(endpoint);
            subscriber.subscribe("mode:sub");
            TestSupport.sleepMs(100);

            poller.addSpotSub(subscriber, PollEventType.POLLIN);
            publisher.publish("mode:sub", payload, SendFlag.NONE);

            TestSupport.waitUntil(() -> poller.pollCount(50) > 0,
                TestSupport.DEFAULT_TIMEOUT_MS, "spot sub poller never became ready");

            Spot.SpotRawBorrowed raw = subscriber.recvRawBorrowed(
                ReceiveFlag.DONTWAIT, recvContext);
            assertEquals("mode:sub", toUtf8(raw.topicId()));
            assertEquals(1, raw.parts().length);
            assertEquals("pong", new String(raw.parts()[0].data(),
                StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testSpotPubCanBePolledViaServiceInstance() {
        TestSupport.assumeNative();

        String endpoint = TestSupport.tcpEndpoint();
        try (Context ctx = new Context();
             SpotNode pubNode = new SpotNode(ctx);
             Spot publisher = new Spot(pubNode);
             Socket rawSub = new Socket(ctx, SocketType.SUB);
             Poller poller = new Poller();
             Message payload = Message.fromBytes("pong".getBytes(
                 StandardCharsets.UTF_8))) {
            pubNode.bind(endpoint);
            rawSub.setSockOpt(dev.kairoscode.zlink.SocketOption.SUBSCRIBE,
                "mode:pub".getBytes(StandardCharsets.UTF_8));
            rawSub.connect(endpoint);
            TestSupport.sleepMs(100);

            poller.addSpotPub(publisher, PollEventType.POLLOUT);
            TestSupport.waitUntil(() -> poller.pollCount(50) > 0,
                TestSupport.DEFAULT_TIMEOUT_MS, "spot pub poller never became writable");

            publisher.publish("mode:pub", payload, SendFlag.NONE);

            byte[] topic = TestSupport.recvWithTimeout(rawSub, 64,
                TestSupport.DEFAULT_TIMEOUT_MS);
            byte[] body = TestSupport.recvWithTimeout(rawSub, 64,
                TestSupport.DEFAULT_TIMEOUT_MS);
            assertEquals("mode:pub", new String(topic, StandardCharsets.UTF_8));
            assertEquals("pong", new String(body, StandardCharsets.UTF_8));
            assertTrue(rawSub.getSockOptInt(
                dev.kairoscode.zlink.SocketOption.RCVMORE) == 0);
        }
    }

    private static String toUtf8(MemorySegment topic) {
        byte[] bytes = topic.toArray(ValueLayout.JAVA_BYTE);
        return new String(bytes, StandardCharsets.UTF_8);
    }
}
