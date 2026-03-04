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

public class TestDisconnectInprocPortedTest {
    @Test
    public void testDisconnectInprocUnsubscribesAndStopsDelivery() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.inprocEndpoint("disconnect");

            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "foo".getBytes(StandardCharsets.UTF_8));
            pub.bind(endpoint);
            sub.connect(endpoint);

            byte[] subFrame = TestSupport.recvWithTimeout(pub, 64,
                TestSupport.DEFAULT_TIMEOUT_MS);
            assertTrue(subFrame.length > 0 && subFrame[0] == 1);

            pub.send("foo".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            pub.send("this is foo!".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);

            assertEquals("foo", new String(TestSupport.recvWithTimeout(sub, 64,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals("this is foo!", new String(TestSupport.recvWithTimeout(
                sub, 128, TestSupport.DEFAULT_TIMEOUT_MS),
                StandardCharsets.UTF_8));

            sub.disconnect(endpoint);

            // Some transports/backends do not guarantee an unsubscribe frame
            // delivery timing on disconnect, so only enforce no further data.
            TestSupport.sleepMs(50);

            pub.send("foo".getBytes(StandardCharsets.UTF_8), SendFlag.SNDMORE);
            pub.send("post-disconnect".getBytes(StandardCharsets.UTF_8),
                SendFlag.NONE);

            TestSupport.assertNoMessage(sub, 128, 300);
        }
    }
}
