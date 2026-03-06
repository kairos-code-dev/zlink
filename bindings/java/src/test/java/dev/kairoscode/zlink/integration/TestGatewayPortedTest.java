package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.discovery.DiscoverySocketRole;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.receiver.ReceiverSocketRole;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.RegistrySocketRole;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class TestGatewayPortedTest {
    @Test
    public void testGatewayProviderSetSockOpt() {
        TestSupport.assumeNative();

        final int hwm = 1_000_000;
        final int linger = 0;

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
             Registry registry = new Registry(ctx);
             Gateway gateway = new Gateway(ctx, discovery);
             Receiver receiver = new Receiver(ctx)) {
            discovery.setSockOpt(DiscoverySocketRole.SUB, SocketOption.LINGER,
                linger);
            registry.setSockOpt(RegistrySocketRole.PUB, SocketOption.LINGER,
                linger);
            registry.setSockOpt(RegistrySocketRole.ROUTER, SocketOption.LINGER,
                linger);

            gateway.setSockOpt(SocketOption.SNDHWM, hwm);
            gateway.setSockOpt(SocketOption.RCVHWM, hwm);
            receiver.setSockOpt(ReceiverSocketRole.ROUTER, SocketOption.SNDHWM,
                hwm);
            receiver.setSockOpt(ReceiverSocketRole.ROUTER, SocketOption.RCVHWM,
                hwm);

            try (Socket gatewayRouter = gateway.routerSocket();
                 Socket receiverRouter = receiver.routerSocket()) {
                assertEquals(hwm, gatewayRouter.getSockOptInt(SocketOption.SNDHWM));
                assertEquals(hwm, gatewayRouter.getSockOptInt(SocketOption.RCVHWM));
                assertEquals(hwm, receiverRouter.getSockOptInt(SocketOption.SNDHWM));
                assertEquals(hwm, receiverRouter.getSockOptInt(SocketOption.RCVHWM));
            }
        }
    }
}
