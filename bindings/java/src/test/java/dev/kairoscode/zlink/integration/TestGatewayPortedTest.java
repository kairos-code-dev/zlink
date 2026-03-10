package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import org.junit.jupiter.api.Test;

public class TestGatewayPortedTest {
    @Test
    public void testGatewayProviderSetSockOpt() {
        TestSupport.assumeNative();

        final int hwm = 1_000_000;
        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(ctx, discovery);
             Receiver receiver = new Receiver(ctx)) {
            gateway.setSockOpt(SocketOption.SNDHWM, hwm);
            gateway.setSockOpt(SocketOption.RCVHWM, hwm);
            receiver.setSockOpt(SocketOption.SNDHWM, hwm);
            receiver.setSockOpt(SocketOption.RCVHWM, hwm);
        }
    }
}
