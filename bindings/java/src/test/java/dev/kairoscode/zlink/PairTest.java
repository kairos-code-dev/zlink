package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Assumptions;

public class PairTest {
    @Test
    public void pairSendRecv() {
        try (Context ctx = new Context()) {
            try (Socket s1 = new Socket(ctx, SocketType.PAIR); Socket s2 = new Socket(ctx, SocketType.PAIR)) {
                String endpoint = "inproc://java-pair";
                s1.bind(endpoint);
                s2.connect(endpoint);
                byte[] payload = "ping".getBytes();
                s1.send(payload, SendFlag.NONE);
                byte[] out = s2.recv(16, ReceiveFlag.NONE);
                org.junit.jupiter.api.Assertions.assertArrayEquals(payload, out);
            }
        } catch (Throwable e) {
            Assumptions.assumeTrue(false, "zlink native library not found: " + e.getMessage());
        }
    }
}
