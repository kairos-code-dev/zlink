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

public class TestPubSubFilterXpubPortedTest {
    @Test
    public void testPubSubTopicFilterAcrossCoreTransports() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket pub = new Socket(ctx, SocketType.PUB);
                     Socket sub = new Socket(ctx, SocketType.SUB)) {
                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "filter-xpub");
                    pub.bind(endpoint);
                    sub.connect(endpoint);
                    sub.setSockOpt(SocketOption.SUBSCRIBE,
                        "topicA".getBytes(StandardCharsets.UTF_8));
                    TestSupport.sleepMs(80);

                    pub.send("topicA hello".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    pub.send("topicB world".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    pub.send("topicA test".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);

                    assertEquals("topicA hello",
                        new String(TestSupport.recvWithTimeout(sub, 128,
                            TestSupport.DEFAULT_TIMEOUT_MS),
                            StandardCharsets.UTF_8));
                    assertEquals("topicA test",
                        new String(TestSupport.recvWithTimeout(sub, 128,
                            TestSupport.DEFAULT_TIMEOUT_MS),
                            StandardCharsets.UTF_8));
                    TestSupport.assertNoMessage(sub, 128, 250);
                }
            }
        }
    }

    @Test
    public void testXpubXsubForwardsPayloadAfterSubscription() {
        TestSupport.assumeNative();

        try (Context ctx = new Context()) {
            for (String transport : IntegrationSupport.coreTransports()) {
                try (Socket xpub = new Socket(ctx, SocketType.XPUB);
                     Socket xsub = new Socket(ctx, SocketType.XSUB)) {
                    String endpoint = IntegrationSupport.endpointFor(transport,
                        "filter-xpub-xsub");
                    xpub.bind(endpoint);
                    xsub.connect(endpoint);
                    TestSupport.sleepMs(60);

                    byte[] subscribeAll = new byte[]{1};
                    xsub.send(subscribeAll, SendFlag.NONE);
                    byte[] subFrame = TestSupport.recvWithTimeout(xpub, 16,
                        TestSupport.DEFAULT_TIMEOUT_MS);
                    assertTrue(subFrame.length > 0 && subFrame[0] == 1);

                    xpub.send("xpub_xsub_test".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    assertEquals("xpub_xsub_test", new String(
                        TestSupport.recvWithTimeout(xsub, 64,
                            TestSupport.DEFAULT_TIMEOUT_MS),
                        StandardCharsets.UTF_8));
                }
            }
        }
    }
}
