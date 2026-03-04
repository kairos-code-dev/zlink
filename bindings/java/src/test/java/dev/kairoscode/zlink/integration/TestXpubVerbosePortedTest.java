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

public class TestXpubVerbosePortedTest {
    @Test
    public void testXpubVerboseDuplicateSubscriptions() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.inprocEndpoint("xpub-verbose");
            xpub.bind(endpoint);
            sub.connect(endpoint);

            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "A".getBytes(StandardCharsets.UTF_8));
            assertArrayEquals(new byte[]{1, 'A'},
                TestSupport.recvWithTimeout(xpub, 16,
                    TestSupport.DEFAULT_TIMEOUT_MS));

            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "B".getBytes(StandardCharsets.UTF_8));
            assertArrayEquals(new byte[]{1, 'B'},
                TestSupport.recvWithTimeout(xpub, 16,
                    TestSupport.DEFAULT_TIMEOUT_MS));

            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "A".getBytes(StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(xpub, 16, 200);

            xpub.setSockOpt(SocketOption.XPUB_VERBOSE, 1);
            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "A".getBytes(StandardCharsets.UTF_8));
            assertArrayEquals(new byte[]{1, 'A'},
                TestSupport.recvWithTimeout(xpub, 16,
                    TestSupport.DEFAULT_TIMEOUT_MS));

            xpub.send("A".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            xpub.send("B".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);

            assertEquals("A", new String(TestSupport.recvWithTimeout(sub, 16,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            assertEquals("B", new String(TestSupport.recvWithTimeout(sub, 16,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
