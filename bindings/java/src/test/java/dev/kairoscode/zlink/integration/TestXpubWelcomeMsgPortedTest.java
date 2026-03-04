package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestXpubWelcomeMsgPortedTest {
    @Test
    public void testXpubWelcomeMessageDeliveredOnSubscribe() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.inprocEndpoint("xpub-welcome");

            xpub.bind(endpoint);
            xpub.setSockOpt(SocketOption.XPUB_WELCOME_MSG,
                "W".getBytes(StandardCharsets.UTF_8));

            sub.setSockOpt(SocketOption.SUBSCRIBE,
                "W".getBytes(StandardCharsets.UTF_8));
            sub.connect(endpoint);

            assertArrayEquals(new byte[]{1, 'W'}, TestSupport.recvWithTimeout(xpub,
                8, TestSupport.DEFAULT_TIMEOUT_MS));
            assertEquals("W", new String(TestSupport.recvWithTimeout(sub, 8,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
        }
    }
}
