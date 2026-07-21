package systems.zlink.contract;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.StreamSessionService;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;

class MeshServiceContractTest {
    @Test
    void contextCreatesNamedMeshNode() throws Exception {
        assertEquals(MeshNode.class,
            Context.class.getMethod("createMeshNode", MeshNodeOptions.class)
                .getReturnType());
        assertFalse(Arrays.stream(Context.class.getMethods())
            .anyMatch(method -> method.getName().equals("createSpotNode")));
    }

    @Test
    void pullDispatchBatchesAreExplicitReusableResources() throws Exception {
        assertTrue(AutoCloseable.class.isAssignableFrom(ReadyBatch.class));
        assertTrue(AutoCloseable.class.isAssignableFrom(ReceiveBatch.class));
        assertEquals(systems.zlink.contracts.service.spot.DrainResult.class,
            MeshNode.class.getMethod(
                "drainReady", int.class, ReadyBatch.class, RecvFlags.class)
                .getReturnType());
    }

    @Test
    void spotAndActorMessagingUseTypedMeshOperations() throws Exception {
        assertEquals(OperationId.class,
            Spot.class.getMethod(
                "requestToSpot",
                RoutingId.class,
                RoutingId.class,
                long.class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
        assertEquals(ActorRef.class,
            Actor.class.getMethod("ref").getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod(
                "sendToActor", ActorRef.class, List.class, SendFlags.class)
                .getReturnType());
    }

    @Test
    void streamActorBindingLivesOnStreamSessionService() throws Exception {
        assertEquals(OperationId.class,
            StreamSessionService.class.getMethod(
                "bindActor", RoutingId.class, ActorRef.class, Duration.class)
                .getReturnType());
        assertFalse(Arrays.stream(StreamSocket.class.getMethods())
            .anyMatch(method -> method.getName().equals("bindActor")));
    }
}
