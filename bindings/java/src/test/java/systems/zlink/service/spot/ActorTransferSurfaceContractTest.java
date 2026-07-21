package systems.zlink.service.spot;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshRecordPayload;

class ActorTransferSurfaceContractTest {
    @Test
    void meshNodeProjectsTheFormalCoreActorTransferLifecycle() throws Exception {
        Class<?> prepare = Class.forName(
            "systems.zlink.contracts.service.spot.ActorTransferPrepare");
        Class<?> prepared = Class.forName(
            "systems.zlink.contracts.service.spot.PrepareActorTransferResult");
        Class<?> token = Class.forName(
            "systems.zlink.contracts.service.spot.ActorTransferToken");

        assertEquals(prepared,
            MeshNode.class.getMethod(
                "prepareActorTransfer", prepare, Duration.class).getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod(
                "commitActorTransfer", token, long.class).getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod("activateActorTransfer", token).getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod("abortActorTransfer", token).getReturnType());
    }

    @Test
    void transferControlIsATypedReceivePayload() throws Exception {
        Class<?> control = Class.forName(
            "systems.zlink.contracts.service.spot.ActorTransferControl");
        assertTrue(MeshRecordPayload.class.isAssignableFrom(control));
    }
}
