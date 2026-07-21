package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.SendFlags;

final class SpotMetadataSurfaceContractTest {
    @Test
    void spotExposesCanonicalApplicationMetadataOnEveryMessagingOperation()
        throws Exception {
        assertEquals(void.class,
            Spot.class.getMethod(
                "sendToChannel",
                String.class,
                byte[].class,
                List.class,
                SendFlags.class).getReturnType());
        assertEquals(OperationId.class,
            Spot.class.getMethod(
                "requestToChannel",
                String.class,
                byte[].class,
                List.class,
                SendFlags.class,
                Duration.class).getReturnType());
        assertEquals(void.class,
            Spot.class.getMethod(
                "sendToSpot",
                RoutingId.class,
                RoutingId.class,
                long.class,
                byte[].class,
                List.class,
                SendFlags.class).getReturnType());
        assertEquals(OperationId.class,
            Spot.class.getMethod(
                "requestToSpot",
                RoutingId.class,
                RoutingId.class,
                long.class,
                byte[].class,
                List.class,
                SendFlags.class,
                Duration.class).getReturnType());
        assertEquals(void.class,
            Spot.class.getMethod(
                "publish",
                String.class,
                String.class,
                byte[].class,
                List.class,
                SendFlags.class).getReturnType());
    }
}
