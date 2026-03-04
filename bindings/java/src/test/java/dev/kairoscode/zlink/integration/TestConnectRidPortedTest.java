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
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestConnectRidPortedTest {
    @Test
    public void testRouterToRouterUnnamed() {
        runRouterToRouter(false);
    }

    @Test
    public void testRouterToRouterNamed() {
        runRouterToRouter(true);
    }

    private void runRouterToRouter(boolean named) {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket bindRouter = new Socket(ctx, SocketType.ROUTER);
             Socket connRouter = new Socket(ctx, SocketType.ROUTER)) {
            String endpoint = TestSupport.tcpEndpoint();
            bindRouter.bind(endpoint);

            if (named) {
                bindRouter.setSockOpt(SocketOption.ROUTING_ID,
                    "X".getBytes(StandardCharsets.UTF_8));
                connRouter.setSockOpt(SocketOption.ROUTING_ID,
                    "Y".getBytes(StandardCharsets.UTF_8));
            }

            connRouter.setSockOpt(SocketOption.CONNECT_ROUTING_ID,
                "conn1".getBytes(StandardCharsets.UTF_8));
            connRouter.connect(endpoint);

            connRouter.send("conn1".getBytes(StandardCharsets.UTF_8),
                SendFlag.SNDMORE);
            connRouter.send("hi 1".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);

            byte[] routingFrame = TestSupport.recvWithTimeout(bindRouter, 256,
                TestSupport.DEFAULT_TIMEOUT_MS);
            if (named)
                assertEquals("Y", new String(routingFrame,
                    StandardCharsets.UTF_8));
            else
                assertEquals(16, routingFrame.length);

            assertEquals("hi 1", new String(TestSupport.recvWithTimeout(bindRouter,
                256, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));

            bindRouter.send(routingFrame, SendFlag.SNDMORE);
            bindRouter.send("ok".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            String first = new String(TestSupport.recvWithTimeout(connRouter, 256,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8);
            assertTrue(first.equals("conn1") || first.equals("X"),
                "unexpected first frame: " + first);
            assertEquals("ok", new String(TestSupport.recvWithTimeout(connRouter,
                256, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
