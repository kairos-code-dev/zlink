package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.actors.ZLinkActorRelocationAdapter;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.configuration.ZLinkMeshNodeBuilder;
import systems.zlink.framework.configuration.ZLinkMeshObjectRoleBuilder;
import systems.zlink.framework.configuration.ZLinkMeshObjectServerBuilder;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkRelocationDeleteResult;
import systems.zlink.framework.locations.ZLinkRelocationFound;
import systems.zlink.framework.locations.ZLinkRelocationMissing;
import systems.zlink.framework.locations.ZLinkRelocationReadResult;
import systems.zlink.framework.locations.ZLinkRelocationRenewed;
import systems.zlink.framework.locations.ZLinkRelocationRenewMissing;
import systems.zlink.framework.locations.ZLinkRelocationRenewResult;
import systems.zlink.framework.locations.ZLinkRelocationStore;
import systems.zlink.framework.locations.ZLinkRelocationStored;
import systems.zlink.framework.locations.ZLinkStoreCancellation;
import systems.zlink.framework.spots.ZLinkInstanceSpot;
import systems.zlink.framework.spots.ZLinkSpotCloseReason;
import systems.zlink.framework.spots.ZLinkSpotClosingContext;
import systems.zlink.framework.spots.ZLinkSpotRelocationAdapter;

final class RelocationPublicContractTest {
    @Test
    void exposesSeparatedRelocationStoreRegistration() throws Exception {
        assertEquals(
            void.class,
            ZLinkFrameworkOptions.class
                .getMethod("addRelocationStore", ZLinkRelocationStore.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRelocationStore.class
                .getMethod(
                    "put",
                    byte[].class,
                    Duration.class,
                    ZLinkStoreCancellation.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRelocationStore.class
                .getMethod(
                    "get",
                    String.class,
                    ZLinkStoreCancellation.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRelocationStore.class
                .getMethod(
                    "renew",
                    String.class,
                    Duration.class,
                    ZLinkStoreCancellation.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkRelocationStore.class
                .getMethod(
                    "delete",
                    String.class,
                    ZLinkStoreCancellation.class)
                .getReturnType());
        assertEquals(
            List.of("reference", "checksumCrc32c", "expiresAt", "storeNow"),
            Arrays.stream(ZLinkRelocationStored.class.getRecordComponents())
                .map(component -> component.getName())
                .toList());
        assertTrue(ZLinkRelocationReadResult.class.isSealed());
        assertEquals(
            Set.of(
                ZLinkRelocationFound.class,
                ZLinkRelocationMissing.class),
            Set.of(ZLinkRelocationReadResult.class.getPermittedSubclasses()));
        assertEquals(2, ZLinkRelocationDeleteResult.values().length);
        assertEquals(
            Set.of(
                ZLinkRelocationRenewed.class,
                ZLinkRelocationRenewMissing.class),
            Set.of(ZLinkRelocationRenewResult.class.getPermittedSubclasses()));
    }

    @Test
    void exposesRelocationAdaptersAndInstanceSpotLifecycle()
        throws Exception {
        assertEquals(
            CompletionStage.class,
            ZLinkActorRelocationAdapter.class
                .getMethod(
                    "capture",
                    systems.zlink.framework.actors.ZLinkActor.class,
                    ZLinkRelocationCancellation.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkSpotRelocationAdapter.class
                .getMethod(
                    "restore",
                    Object.class,
                    byte[].class,
                    ZLinkRelocationCancellation.class)
                .getReturnType());
        assertEquals(
            CompletionStage.class,
            ZLinkInstanceSpot.class
                .getMethod("onClosing", ZLinkSpotClosingContext.class)
                .getReturnType());
        assertEquals(2, ZLinkSpotCloseReason.RELOCATION_OUT.value());
        assertEquals(
            ZLinkRelocationPolicy.Snapshot.class,
            ZLinkRelocationPolicy.snapshot(Object.class).getClass());
        assertEquals(
            ZLinkMeshObjectRoleBuilder.class,
            ZLinkMeshNodeBuilder.class.getMethod("objects").getReturnType());
        assertEquals(
            ZLinkMeshObjectServerBuilder.class,
            ZLinkMeshObjectRoleBuilder.class.getMethod("server").getReturnType());
        assertEquals(ZLinkLocationStore.class, ZLinkLocationStore.class);
    }

    @Test
    void relocationPayloadReadResultKeepsItsOwnSnapshot() {
        byte[] source = new byte[] {1, 2, 3};
        ZLinkRelocationFound found = new ZLinkRelocationFound(source);
        source[0] = 9;
        byte[] first = found.payload();
        first[1] = 8;
        assertArrayEquals(new byte[] {1, 2, 3}, found.payload());
    }
}
