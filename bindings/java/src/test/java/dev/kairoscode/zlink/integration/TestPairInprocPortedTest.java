package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestPairInprocPortedTest {
    @Test
    public void testRoundTripAndSend() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket left = new Socket(ctx, SocketType.PAIR);
             Socket right = new Socket(ctx, SocketType.PAIR)) {
            String endpoint = TestSupport.inprocEndpoint("pair-inproc");
            left.bind(endpoint);
            right.connect(endpoint);
            TestSupport.sleepMs(30);

            byte[] ping = "ping".getBytes(StandardCharsets.UTF_8);
            right.send(ping, SendFlag.NONE);
            byte[] out = TestSupport.recvWithTimeout(left, 16,
                TestSupport.SHORT_TIMEOUT_MS);
            assertArrayEquals(ping, out);

            ByteBuffer direct = ByteBuffer.allocateDirect(8);
            direct.put("pong".getBytes(StandardCharsets.UTF_8));
            direct.flip();
            assertEquals(4, left.send(direct, SendFlag.NONE));
            assertEquals(4, direct.position());

            byte[] out2 = right.recv(16, ReceiveFlag.NONE);
            assertEquals("pong", new String(out2, StandardCharsets.UTF_8));
        }
    }
}
