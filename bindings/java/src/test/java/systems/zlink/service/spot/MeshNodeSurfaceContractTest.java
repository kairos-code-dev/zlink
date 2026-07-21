package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.DrainResult;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshReadyHandler;
import systems.zlink.contracts.service.spot.OperationId;
import systems.zlink.contracts.service.spot.PeerChannels;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.SendFlags;

class MeshNodeSurfaceContractTest {
    @Test
    void meshNodeExposesPullDispatchAndTypedMessagingSurface() throws Exception {
        assertEquals(void.class,
            MeshNode.class.getMethod("setReadyHandler", MeshReadyHandler.class)
                .getReturnType());
        assertEquals(DrainResult.class,
            MeshNode.class.getMethod(
                "drainReady", int.class, ReadyBatch.class, RecvFlags.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "requestToActor",
                ActorRef.class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "joinActorSpot",
                ActorRef.class,
                RoutingId.class,
                RoutingId.class,
                long.class,
                List.class,
                Duration.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "joinActorEntrySpot",
                ActorRef.class,
                RoutingId.class,
                List.class,
                Duration.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "leaveActor",
                ActorRef.class,
                long.class,
                Duration.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "closeActorBoundSession",
                ActorRef.class,
                long.class,
                Duration.class)
                .getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod(
                "sendToNode",
                RoutingId.class,
                List.class,
                SendFlags.class)
                .getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod(
                "sendToNode",
                RoutingId.class,
                byte[].class,
                List.class,
                SendFlags.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "requestToNode",
                RoutingId.class,
                byte[].class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod(
                "sendToChannel",
                String.class,
                byte[].class,
                List.class,
                SendFlags.class)
                .getReturnType());
        assertEquals(OperationId.class,
            MeshNode.class.getMethod(
                "requestToChannel",
                String.class,
                byte[].class,
                List.class,
                SendFlags.class,
                Duration.class)
                .getReturnType());
        assertEquals(long.class,
            MeshNode.class.getMethod("maxMessageSize").getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod("setMaxMessageSize", long.class)
                .getReturnType());
        assertEquals(PeerChannels.class,
            MeshNode.class.getMethod("peerChannels", RoutingId.class, long.class)
                .getReturnType());
        assertEquals(ReceiveBatch.class,
            ReceiveBatch.class.getMethod(
                "create", int.class, int.class, int.class)
                .getReturnType());
    }
}
