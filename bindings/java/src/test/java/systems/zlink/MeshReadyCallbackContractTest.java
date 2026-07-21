package systems.zlink;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import java.util.Arrays;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshReadyHandler;
import systems.zlink.contracts.service.spot.ReadyDomain;

class MeshReadyCallbackContractTest {
    @Test
    void readyCallbackOnlyCarriesDomainMask() throws Exception {
        assertEquals(int.class,
            MeshReadyHandler.class.getMethod("onReady", int.class).getReturnType());
        assertEquals(void.class,
            MeshNode.class.getMethod("setReadyHandler", MeshReadyHandler.class)
                .getReturnType());
        assertEquals(ReadyDomain.ALL.value(),
            ReadyDomain.mask(ReadyDomain.APPLICATION, ReadyDomain.INFRASTRUCTURE));
    }

    @Test
    void removedPushDispatchCallbacksAreNotExposed() {
        assertFalse(Arrays.stream(MeshNode.class.getMethods())
            .anyMatch(method -> method.getName().equals("setDispatchHandler")));
    }
}
