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

public class TestPubInvertMatchingPortedTest {
    @Test
    public void testPubInvertMatching() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub1 = new Socket(ctx, SocketType.SUB);
             Socket sub2 = new Socket(ctx, SocketType.SUB)) {
            String endpoint = TestSupport.inprocEndpoint("invert-matching");
            pub.bind(endpoint);
            sub1.connect(endpoint);
            sub2.connect(endpoint);

            byte[] prefix1 = "prefix1".getBytes(StandardCharsets.UTF_8);
            byte[] prefix2 = "p2".getBytes(StandardCharsets.UTF_8);

            sub1.setSockOpt(SocketOption.SUBSCRIBE, prefix1);
            sub2.setSockOpt(SocketOption.SUBSCRIBE, prefix2);
            TestSupport.sleepMs(80);

            pub.send(prefix1, SendFlag.NONE);
            assertEquals("prefix1", new String(TestSupport.recvWithTimeout(sub1,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(sub2, 64, 250);

            pub.send(prefix2, SendFlag.NONE);
            assertEquals("p2", new String(TestSupport.recvWithTimeout(sub2, 64,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(sub1, 64, 250);

            pub.setSockOpt(SocketOption.INVERT_MATCHING, 1);
            sub1.setSockOpt(SocketOption.INVERT_MATCHING, 1);
            sub2.setSockOpt(SocketOption.INVERT_MATCHING, 1);
            TestSupport.sleepMs(60);

            pub.send(prefix1, SendFlag.NONE);
            assertEquals("prefix1", new String(TestSupport.recvWithTimeout(sub2,
                64, TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(sub1, 64, 250);

            pub.send(prefix2, SendFlag.NONE);
            assertEquals("p2", new String(TestSupport.recvWithTimeout(sub1, 64,
                TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8));
            TestSupport.assertNoMessage(sub2, 64, 250);
        }
    }
}
