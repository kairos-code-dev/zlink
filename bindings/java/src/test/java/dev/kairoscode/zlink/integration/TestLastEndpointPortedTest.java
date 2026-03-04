package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestLastEndpointPortedTest {
    @Test
    public void testLastEndpointAfterBind() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER)) {
            router.setSockOpt(SocketOption.LINGER, 0);

            String endpoint1 = TestSupport.tcpEndpoint();
            router.bind(endpoint1);
            assertEquals(endpoint1, TestSupport.bytesToString(
                router.getSockOptBytes(SocketOption.LAST_ENDPOINT, 512)));

            String endpoint2 = TestSupport.tcpEndpoint();
            router.bind(endpoint2);
            assertEquals(endpoint2, TestSupport.bytesToString(
                router.getSockOptBytes(SocketOption.LAST_ENDPOINT, 512)));
        }
    }
}
