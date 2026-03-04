package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.gateway.Gateway;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;

public class CoreGatewayApiSurfacePortedTest {
    @Test
    public void testHighLevelGatewayApiSurface() {
        Class<Gateway> cls = Gateway.class;

        assertDoesNotThrow(() -> cls.getMethod("sendTo",
            String.class, Message.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("sendTo",
            String.class, Message[].class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("sendTo",
            String.class, String.class, Message.class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("sendTo",
            String.class, String.class, Message[].class, SendFlag.class));
        assertDoesNotThrow(() -> cls.getMethod("recvMessages",
            ReceiveFlag.class));
    }
}
