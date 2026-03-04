package dev.kairoscode.zlink;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

public class CoreGatewayApiSurfacePortedTest {
    @Test
    public void testHighLevelGatewayApiSurface() {
        Class<Gateway> cls = Gateway.class;

        assertDoesNotThrow(() -> cls.getMethod("send",
            String.class, Message.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("send",
            String.class, List.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("sendToRoutingId",
            String.class, String.class, Message.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("sendToRoutingId",
            String.class, String.class, List.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("recvMany", ReceiveFlag.class));
    }
}
