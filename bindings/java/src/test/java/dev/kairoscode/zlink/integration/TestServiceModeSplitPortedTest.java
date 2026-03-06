package dev.kairoscode.zlink.integration;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ErrorCode;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import org.junit.jupiter.api.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class TestServiceModeSplitPortedTest {
    @Test
    public void testGatewayRouterModeRejectsFacadeSend() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(ctx, discovery);
             Socket router = gateway.routerSocket();
             Message msg = Message.fromBytes("x".getBytes(StandardCharsets.UTF_8))) {
            ZlinkException ex = assertThrows(ZlinkException.class,
                () -> gateway.sendTo("svc", msg, SendFlag.NONE));
            assertEquals(ErrorCode.EFSM, ex.errorCode());
        }
    }

    @Test
    public void testSpotPubSocketModeRejectsFacadePublish() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx)) {
            node.bind(TestSupport.inprocEndpoint("spot-mode-pub"));
            try (Socket socket = node.pubSocket();
                 Spot spot = new Spot(node);
                 Message msg = Message.fromBytes("x".getBytes(StandardCharsets.UTF_8))) {
                ZlinkException ex = assertThrows(ZlinkException.class,
                    () -> spot.publish("mode:pub", msg, SendFlag.NONE));
                assertEquals(ErrorCode.EFSM, ex.errorCode());
            }
        }
    }
}
