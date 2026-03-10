package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestServiceModeSplitPortedTest {
    @Test
    public void testGatewayRouterHandleNotExposed() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(ctx, discovery)) {
            assertThrows(UnsupportedOperationException.class,
              gateway::routerSocket);
        }
    }

    @Test
    public void testSpotDefaultFacadeHandlesExist() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Socket pub = node.pubSocket();
             Socket sub = node.subSocket()) {
            assertNotNull(pub);
            assertNotNull(sub);
        }
    }
}
