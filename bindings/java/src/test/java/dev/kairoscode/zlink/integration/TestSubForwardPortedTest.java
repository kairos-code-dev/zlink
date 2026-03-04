package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestSubForwardPortedTest {
    @Test
    public void testSubscriptionAndDataForwardingThroughXpubXsubDevice() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket xsub = new Socket(ctx, SocketType.XSUB);
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            String endpointFrontend = TestSupport.tcpEndpoint();
            String endpointBackend = TestSupport.tcpEndpoint();

            xpub.bind(endpointFrontend);
            xsub.bind(endpointBackend);

            pub.connect(endpointBackend);
            sub.connect(endpointFrontend);
            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);

            byte[] subFrame = TestSupport.recvWithTimeout(xpub, 32,
                TestSupport.DEFAULT_TIMEOUT_MS);
            xsub.send(subFrame, SendFlag.NONE);

            TestSupport.sleepMs(80);

            pub.send(new byte[0], SendFlag.NONE);

            byte[] forwarded = TestSupport.recvWithTimeout(xsub, 8,
                TestSupport.DEFAULT_TIMEOUT_MS);
            xpub.send(forwarded, SendFlag.NONE);

            byte[] result = TestSupport.recvWithTimeout(sub, 8,
                TestSupport.DEFAULT_TIMEOUT_MS);
            assertEquals(0, result.length);
        }
    }
}
