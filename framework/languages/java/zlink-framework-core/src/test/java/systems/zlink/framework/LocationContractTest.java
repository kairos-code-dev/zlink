package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotAddress;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationResolver;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

final class LocationContractTest {
    @Test
    void frameworkRootDoesNotExposeDirectRuntimeStartFacades() {
        assertThrows(
            ClassNotFoundException.class,
            () -> Class.forName("systems.zlink.framework.ZLinkFramework"));
        assertThrows(
            ClassNotFoundException.class,
            () -> Class.forName("systems.zlink.framework.ZLinkRegistry"));
    }

    @Test
    void runtimeTypeDoesNotExposePublicDirectStartMembers() {
        assertEquals(0, ZLinkFrameworkRuntime.class.getConstructors().length);
        assertFalse(Arrays.stream(ZLinkFrameworkRuntime.class.getMethods())
            .anyMatch(method -> method.getName().equals("start")
                && Modifier.isPublic(method.getModifiers())));
    }

    @Test
    void legacyRegistryAndDiscoveryContractsAreNotPublicSurface() {
        assertMissing("systems.zlink.framework.registry.ZLinkRegistryQuery");
        assertMissing("systems.zlink.framework.registry.ZLinkRegistryQueryClient");
        assertMissing("systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime");
        assertMissing("systems.zlink.framework.runtime.backend.ZLinkBackendDiscovery");
        assertMissing("systems.zlink.framework.runtime.spots.SpotDiscoveryReconciler");
    }

    @Test
    void locationRecordsCarryDotnetPortedFields() {
        assertRecordComponents(
            ZLinkPeerLocation.class,
            "autoConnectType",
            "meshName",
            "nodeRid",
            "role",
            "endpoint",
            "weight",
            "value",
            "metadata",
            "capabilities",
            "ownerId",
            "generation",
            "updatedAt");
        assertEquals(ZLinkLocationAutoConnectType.class, componentType(ZLinkPeerLocation.class, "autoConnectType"));
        assertEquals(ZLinkLocationRole.class, componentType(ZLinkPeerLocation.class, "role"));

        assertRecordComponents(
            ZLinkSpotLocation.class,
            "meshName",
            "spotRid",
            "spotType",
            "nodeRid",
            "spotKind",
            "routeEndpoint",
            "ownerId",
            "generation",
            "updatedAt");
        assertEquals(RoutingId.class, componentType(ZLinkSpotLocation.class, "spotRid"));

        assertRecordComponents(
            ZLinkActorLocation.class,
            "actorType",
            "actorId",
            "actorRef",
            "nodeRid",
            "generation",
            "locationKind",
            "spotMeshName",
            "spotRid",
            "spotKind",
            "ownerId",
            "updatedAt");

        assertRecordComponents(
            ZLinkRouteLocation.class,
            "routeKind",
            "routeKey",
            "ownerNodeRid",
            "ownerId",
            "generation",
            "value",
            "updatedAt");
    }

    @Test
    void spotAddressUsesSpotAddressMessagingContractFields() {
        assertRecordComponents(ZLinkSpotAddress.class, "meshName", "nodeRid", "spotRid");
        assertEquals(RoutingId.class, componentType(ZLinkSpotAddress.class, "nodeRid"));
        assertEquals(RoutingId.class, componentType(ZLinkSpotAddress.class, "spotRid"));
    }

    @Test
    void locationStoreAndResolversUseAsyncTypedContracts() throws Exception {
        Method updatePeer = ZLinkPeerLocationStore.class.getMethod(
            "updatePeerAsync",
            ZLinkPeerLocation.class,
            systems.zlink.framework.locations.ZLinkLocationWriteIntent.class);
        Method listPeers = ZLinkPeerLocationStore.class.getMethod(
            "listPeersAsync",
            ZLinkPeerLocationFilter.class);
        Method renewOwnerLease = ZLinkOwnerLeaseStore.class.getMethod(
            "renewOwnerLeaseAsync",
            String.class,
            RoutingId.class,
            java.time.Duration.class);
        Method resolveSpotAddress = ZLinkSpotLocationResolver.class.getMethod(
            "resolveSpotAddressAsync",
            String.class,
            RoutingId.class);

        assertEquals(CompletionStage.class, updatePeer.getReturnType());
        assertEquals(CompletionStage.class, listPeers.getReturnType());
        assertEquals(CompletionStage.class, renewOwnerLease.getReturnType());
        assertEquals(CompletionStage.class, resolveSpotAddress.getReturnType());
    }

    @Test
    void frameworkOptionsExposeLocationStoreRegistrationSurface() throws Exception {
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addPeerLocationStore", Class.class)
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addSpotLocationStore", Class.class)
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addActorLocationStore", Class.class)
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addRouteLocationStore", Class.class)
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addOwnerLeaseStore", Class.class)
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("useInMemoryLocationStores")
            .getReturnType());
        assertEquals(void.class, ZLinkFrameworkOptions.class
            .getMethod("addLocationStore", ZLinkLocationStore.class)
            .getReturnType());
        assertEquals(ZLinkLocationOptions.class, ZLinkFrameworkOptions.class
            .getMethod("configureLocations")
            .getReturnType());
        assertEquals(void.class, ZLinkLocationOptions.class
            .getMethod("setSpotRouterChannel", String.class, String.class)
            .getReturnType());
        assertEquals(Map.class, ZLinkLocationOptions.class
            .getMethod("spotRouterChannels")
            .getReturnType());

        assertEquals(ZLinkPeerLocationStore.class, ZLinkPeerLocationStore.class);
        assertEquals(ZLinkSpotLocationStore.class, ZLinkSpotLocationStore.class);
        assertEquals(ZLinkActorLocationStore.class, ZLinkActorLocationStore.class);
        assertEquals(ZLinkRouteLocationStore.class, ZLinkRouteLocationStore.class);
        assertEquals(ZLinkOwnerLeaseStore.class, ZLinkOwnerLeaseStore.class);
    }

    @Test
    void locationRuntimeQueryExposesPagedRuntimeViews() throws Exception {
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("getStatusAsync")
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listPeersAsync", ZLinkPeerLocationFilter.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listSpotsAsync", ZLinkSpotLocationFilter.class, ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listActorsAsync", ZLinkActorLocationFilter.class, ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(ZLinkLocationPage.class, ZLinkLocationPage.class);
        assertEquals(ZLinkOwnerLease.class, ZLinkOwnerLease.class);
    }

    private static void assertMissing(String className) {
        assertThrows(ClassNotFoundException.class, () -> Class.forName(className));
    }

    private static void assertRecordComponents(Class<? extends Record> type, String... names) {
        assertEquals(
            List.of(names),
            Arrays.stream(type.getRecordComponents())
                .map(java.lang.reflect.RecordComponent::getName)
                .toList());
    }

    private static Class<?> componentType(Class<? extends Record> type, String name) {
        return Arrays.stream(type.getRecordComponents())
            .filter(component -> component.getName().equals(name))
            .findFirst()
            .orElseThrow()
            .getType();
    }
}
