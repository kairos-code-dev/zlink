package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestXpubNodropPortedTest {
    @Test
    public void testXpubNodropDeliversAllAcceptedMessages() {
        TestSupport.assumeNative();

        final int hwm = 200;
        try (Context ctx = new Context();
             Socket xpub = new Socket(ctx, SocketType.XPUB);
             Socket sub = new Socket(ctx, SocketType.SUB)) {
            xpub.setSockOpt(SocketOption.SNDHWM, hwm);
            xpub.setSockOpt(SocketOption.XPUB_NODROP, 1);
            xpub.setSockOpt(SocketOption.SNDTIMEO, 0);

            String endpoint = TestSupport.inprocEndpoint("xpub-nodrop");
            xpub.bind(endpoint);

            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);
            sub.connect(endpoint);

            TestSupport.recvWithTimeout(xpub, 16, TestSupport.DEFAULT_TIMEOUT_MS);

            int sendCount = 0;
            while (true) {
                try {
                    xpub.send(new byte[0], SendFlag.NONE);
                    sendCount++;
                } catch (RuntimeException ex) {
                    break;
                }
            }
            assertTrue(sendCount > 0);

            sub.setSockOpt(SocketOption.RCVTIMEO, 250);

            int recvCount = 0;
            while (true) {
                try {
                    sub.recv(1, ReceiveFlag.NONE);
                    recvCount++;
                } catch (RuntimeException ex) {
                    break;
                }
            }

            assertEquals(sendCount, recvCount);
        }
    }
}
