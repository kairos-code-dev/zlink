package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestDiffservPortedTest {
    @Test
    public void testDiffservTosOption() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket server = new Socket(ctx, SocketType.PAIR);
             Socket client = new Socket(ctx, SocketType.PAIR)) {
            int serverTos = 0x28;
            int clientTos = 0x58;

            server.setSockOpt(SocketOption.TOS, serverTos);
            String endpoint = TestSupport.tcpEndpoint();
            server.bind(endpoint);
            assertEquals(serverTos, server.getSockOptInt(SocketOption.TOS));

            client.setSockOpt(SocketOption.TOS, clientTos);
            client.connect(endpoint);
            assertEquals(clientTos, client.getSockOptInt(SocketOption.TOS));

            TestSupport.sleepMs(80);

            client.send("c2s".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("c2s", new String(server.recv(64, ReceiveFlag.NONE),
                StandardCharsets.UTF_8));

            server.send("s2c".getBytes(StandardCharsets.UTF_8), SendFlag.NONE);
            assertEquals("s2c", new String(client.recv(64, ReceiveFlag.NONE),
                StandardCharsets.UTF_8));
        }
    }
}
