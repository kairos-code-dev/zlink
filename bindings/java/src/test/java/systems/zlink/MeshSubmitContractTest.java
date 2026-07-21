package systems.zlink;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.sockets.SendFlags;

class MeshSubmitContractTest {
    @Test
    void meshRequestsReturnOperationIdsForPullCompletion() throws Exception {
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "requestToNode",
                RoutingId.class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "requestToActor",
                ActorRef.class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
    }
}
