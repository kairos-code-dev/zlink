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
import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestIssue566PortedTest {
    @Test
    public void testIssue566RouterDealerDifferentContexts() {
        TestSupport.assumeNative();

        try (Context ctxRouter = new Context();
             Context ctxDealer = new Context();
             Socket router = new Socket(ctxRouter, SocketType.ROUTER)) {
            router.setSockOpt(SocketOption.ROUTER_MANDATORY, 1);
            String endpoint = TestSupport.tcpEndpoint();
            router.bind(endpoint);

            for (int cycle = 0; cycle < 100; cycle++) {
                try (Socket dealer = new Socket(ctxDealer, SocketType.DEALER)) {
                    String routingId = String.format("%09d", cycle);
                    dealer.setSockOpt(SocketOption.ROUTING_ID,
                        routingId.getBytes(StandardCharsets.UTF_8));
                    dealer.setSockOpt(SocketOption.RCVTIMEO, 1000);
                    dealer.setSockOpt(SocketOption.LINGER, 0);
                    dealer.connect(endpoint);

                    boolean sent = false;
                    for (int attempt = 0; attempt < 500; attempt++) {
                        TestSupport.sleepMs(2);
                        try {
                            router.send(routingId.getBytes(StandardCharsets.UTF_8),
                                SendFlag.SNDMORE);
                            router.send("HELLO".getBytes(StandardCharsets.UTF_8),
                                SendFlag.NONE);
                            sent = true;
                            break;
                        } catch (RuntimeException ex) {
                            // Router may report host-unreachable until pipe is ready.
                        }
                    }

                    assertTrue(sent, "router did not reach dealer: " + routingId);
                    byte[] payload = dealer.recv(32, ReceiveFlag.NONE);
                    assertEquals("HELLO", new String(payload,
                        StandardCharsets.UTF_8));
                }
            }
        }
    }
}
