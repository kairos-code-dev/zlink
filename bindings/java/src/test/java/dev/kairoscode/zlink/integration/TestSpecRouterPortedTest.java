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

public class TestSpecRouterPortedTest {
    @Test
    public void testFairQueueInTcp() {
        runFairQueue("tcp");
    }

    @Test
    public void testFairQueueInInproc() {
        runFairQueue("inproc");
    }

    private static void runFairQueue(String transport) {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket receiver = new Socket(ctx, SocketType.ROUTER)) {
            String endpoint = IntegrationSupport.endpointFor(transport,
                "spec-router");
            receiver.bind(endpoint);

            Socket[] senders = new Socket[5];
            try {
                for (int i = 0; i < senders.length; i++) {
                    senders[i] = new Socket(ctx, SocketType.DEALER);
                    byte[] rid = new byte[]{(byte) ('A' + i)};
                    senders[i].setSockOpt(SocketOption.ROUTING_ID, rid);
                    senders[i].connect(endpoint);
                }

                TestSupport.sleepMs(60);

                senders[0].send("M".getBytes(StandardCharsets.UTF_8),
                    SendFlag.NONE);
                assertEquals("A", recvFrame(receiver));
                assertEquals("M", recvFrame(receiver));

                senders[0].send("M".getBytes(StandardCharsets.UTF_8),
                    SendFlag.NONE);
                assertEquals("A", recvFrame(receiver));
                assertEquals("M", recvFrame(receiver));

                int expected = 0;
                for (int i = 0; i < senders.length; i++) {
                    senders[i].send("M".getBytes(StandardCharsets.UTF_8),
                        SendFlag.NONE);
                    expected += 'A' + i;
                }

                int actual = 0;
                for (int i = 0; i < senders.length; i++) {
                    String rid = recvFrame(receiver);
                    actual += rid.charAt(0);
                    assertEquals("M", recvFrame(receiver));
                }
                assertEquals(expected, actual);
            } finally {
                for (Socket sender : senders) {
                    if (sender != null) {
                        try {
                            sender.setSockOpt(SocketOption.LINGER, 0);
                            sender.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
            }
        }
    }

    private static String recvFrame(Socket socket) {
        return new String(TestSupport.recvWithTimeout(socket, 64,
            TestSupport.DEFAULT_TIMEOUT_MS), StandardCharsets.UTF_8);
    }
}
