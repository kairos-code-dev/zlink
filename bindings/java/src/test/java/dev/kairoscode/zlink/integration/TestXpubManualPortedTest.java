package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestXpubManualPortedTest {
    @Test
    public void testXpubManualBasicRouting() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket xsub = new Socket(ctx, SocketType.XSUB)) {
            String endpoint = TestSupport.inprocEndpoint("xpub-manual");

            xpub.setSockOpt(SocketOption.XPUB_MANUAL, 1);
            xpub.bind(endpoint);
            xsub.connect(endpoint);

            byte[] subscription = new byte[]{1, 'A'};
            xsub.send(subscription, SendFlag.NONE);
            assertArrayEquals(subscription, TestSupport.recvWithTimeout(xpub,
                16, TestSupport.DEFAULT_TIMEOUT_MS));

            xpub.setSockOpt(SocketOption.SUBSCRIBE,
                "B".getBytes(StandardCharsets.UTF_8));

            xpub.send("A".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            xpub.send("B".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            assertEquals("B", new String(TestSupport.recvWithTimeout(xsub, 8,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(xsub, 8, 200);
        }
    }

    @Test
    public void testXpubManualPassesUserMessageFrames() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket xsub = new Socket(ctx, SocketType.XSUB)) {
            String endpoint = TestSupport.inprocEndpoint("xpub-manual-user");
            xpub.bind(endpoint);
            xsub.connect(endpoint);

            byte[] userFrame = new byte[]{2, 'A'};
            xsub.send(userFrame, SendFlag.NONE);
            assertArrayEquals(userFrame, TestSupport.recvWithTimeout(xpub, 16,
                TestSupport.DEFAULT_TIMEOUT_MS));
        }
    }
}
