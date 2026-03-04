package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestMsgFlagsPortedTest {
    @Test
    public void testMultipartMoreFlag() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER);
             Message msg = new Message()) {
            String endpoint = TestSupport.inprocEndpoint("msg-flags");
            router.bind(endpoint);
            dealer.connect(endpoint);

            dealer.send("A".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            dealer.send("B".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            msg.recv(router, ReceiveFlag.NONE);
            assertTrue(msg.more());

            msg.recv(router, ReceiveFlag.NONE);
            assertTrue(msg.more());
            assertEquals("A", new String(msg.data(), StandardCharsets.UTF_8));

            msg.recv(router, ReceiveFlag.NONE);
            assertFalse(msg.more());
            assertEquals("B", new String(msg.data(), StandardCharsets.UTF_8));
        }
    }

    @Test
    public void testRcvMoreSockOptAcrossMultipartFrames() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            String endpoint = TestSupport.inprocEndpoint("msg-flags-rcvmore");
            router.bind(endpoint);
            dealer.connect(endpoint);

            dealer.send("X".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            dealer.send("Y".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            router.recv(64, ReceiveFlag.NONE);
            assertEquals(1, router.getSockOptInt(SocketOption.RCVMORE));

            assertEquals("X", new String(router.recv(64, ReceiveFlag.NONE),
                StandardCharsets.UTF_8));
            assertEquals(1, router.getSockOptInt(SocketOption.RCVMORE));

            assertEquals("Y", new String(router.recv(64, ReceiveFlag.NONE),
                StandardCharsets.UTF_8));
            assertEquals(0, router.getSockOptInt(SocketOption.RCVMORE));
        }
    }
}
