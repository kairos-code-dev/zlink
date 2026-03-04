package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestRouterMandatoryHwmPortedTest {
    @Test
    public void testRouterMandatoryHwmBackpressure() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            router.setSockOpt(SocketOption.ROUTER_MANDATORY, 1);
            router.setSockOpt(SocketOption.SNDHWM, 1);
            router.setSockOpt(SocketOption.LINGER, 1);

            dealer.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealer.setSockOpt(SocketOption.RCVHWM, 1);

            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);
            dealer.connect(endpoint);

            dealer.send("Hello".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("X", new String(TestSupport.recvWithTimeout(router, 32,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals("Hello", new String(TestSupport.recvWithTimeout(router,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            byte[] rid = "X".getBytes(StandardCharsets.UTF_8);
            byte[] payload = new byte[65536];
            Arrays.fill(payload, (byte) 7);

            int firstBurst = sendUntilBackpressure(router, rid, payload, 10000);
            assertTrue(firstBurst > 0);
            assertTrue(firstBurst < 512,
                "first burst too large: " + firstBurst);

            TestSupport.sleepMs(800);

            int secondBurst = sendUntilBackpressure(router, rid, payload, 10000);
            assertTrue(secondBurst < 512,
                "second burst too large: " + secondBurst);
        }
    }

    private static int sendUntilBackpressure(Socket router, byte[] rid,
                                             byte[] payload,
                                             int maxMessages) {
        int sent = 0;
        for (int i = 0; i < maxMessages; i++) {
            try (Message ridMsg = Message.fromBytes(rid);
                 Message bodyMsg = Message.fromBytes(payload)) {
                ridMsg.send(router, SendFlag.DONTWAIT_SNDMORE);
                bodyMsg.send(router, SendFlag.DONTWAIT);
                sent++;
            } catch (RuntimeException ex) {
                break;
            }
        }
        return sent;
    }
}
