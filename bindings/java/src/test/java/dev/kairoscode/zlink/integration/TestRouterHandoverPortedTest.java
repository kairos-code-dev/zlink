package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestRouterHandoverPortedTest {
    @Test
    public void testWithRouterHandover() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealerOne = new Socket(ctx, SocketType.DEALER);
             Socket dealerTwo = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);
            router.setSockOpt(SocketOption.ROUTER_HANDOVER, 1);

            dealerOne.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealerOne.connect(endpoint);
            dealerOne.send("hello-1".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);

            dealerTwo.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealerTwo.connect(endpoint);
            dealerTwo.send("hello-2".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);

            router.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router.send("to-x".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            TestSupport.assertNoMessage(dealerOne, 64, 300);
            assertEquals("to-x", new String(TestSupport.recvWithTimeout(dealerTwo,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testWithoutRouterHandover() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealerOne = new Socket(ctx, SocketType.DEALER);
             Socket dealerTwo = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);

            dealerOne.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealerOne.connect(endpoint);
            dealerOne.send("hello-1".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);
            TestSupport.recvWithTimeout(router, 64, TestSupport.DEFAULT_TIMEOUT_MS);

            dealerTwo.setSockOpt(SocketOption.ROUTING_ID,
                "X".getBytes(StandardCharsets.UTF_8));
            dealerTwo.connect(endpoint);
            dealerTwo.send("hello-2".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);
            TestSupport.sleepMs(100);

            router.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            router.send("to-x".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            assertEquals("to-x", new String(TestSupport.recvWithTimeout(dealerOne,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(dealerTwo, 64, 300);
        }
    }
}
