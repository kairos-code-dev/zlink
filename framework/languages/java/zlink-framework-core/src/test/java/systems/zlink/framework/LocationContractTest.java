package systems.zlink.framework;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertIterableEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletionStage;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ActorRefSnapshot;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.actors.ZLinkActorJoinCompletion;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRequestCall;
import systems.zlink.framework.actors.ZLinkActorSendCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.spots.SpotRef;
import systems.zlink.framework.spots.ZLinkSpotRequestCall;
import systems.zlink.framework.spots.ZLinkSpotSendCall;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationChangeType;
import systems.zlink.framework.locations.ZLinkLocationChanged;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationKey;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationReadiness;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationTopologyState;
import systems.zlink.framework.locations.ZLinkLocationWatchFilter;
import systems.zlink.framework.locations.ZLinkLocationWatchStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpotCreateResult;
import systems.zlink.framework.spots.ZLinkSpotCreateCall;
import systems.zlink.framework.spots.ZLinkSpotGetOrCreateCall;
import systems.zlink.framework.spots.ZLinkSpotInfo;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.streams.ZLinkSessionActors;

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
            "draining",
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
            "spotId",
            "spotGeneration",
            "spotType",
            "nodeRid",
            "spotKind",
            "routeEndpoint",
            "ownerId",
            "generation",
            "updatedAt");
        assertEquals(String.class, componentType(ZLinkSpotLocation.class, "spotId"));

        assertRecordComponents(
            ZLinkActorLocation.class,
            "actorId",
            "actorType",
            "actorRef",
            "nodeRid",
            "locationKind",
            "spotMeshName",
            "spotId",
            "ownerId",
            "generation",
            "updatedAt");
        assertEquals(ActorRef.class, componentType(ZLinkActorLocation.class, "actorRef"));
        // dotnet 정본(Rows.cs)과 동일: actor row의 locationKind는 actor가 사는 spot 종류(ZLinkSpotKind)다.
        // ZLinkLocationKind(row 종류)와 혼동 금지.
        assertEquals(ZLinkSpotKind.class, componentType(ZLinkActorLocation.class, "locationKind"));

        assertRecordComponents(
            ZLinkRouteLocation.class,
            "routeKind",
            "routeKey",
            "ownerNodeRid",
            "ownerId",
            "generation",
            "value",
            "updatedAt");
        assertRecordComponents(
            systems.zlink.framework.locations
                .ZLinkMeshNodeDescriptor.class,
            "meshName",
            "rid",
            "lifecycleGeneration",
            "descriptorRevision",
            "endpoint",
            "channelWeights",
            "applicationVersion",
            "objectCapabilities",
            "objectRole",
            "entrySpotId",
            "placementWeight",
            "capacity",
            "activationConcurrency",
            "maintenanceWave",
            "state",
            "securityIdentity",
            "ownerId",
            "leaseGeneration",
            "updatedAt");
    }

    @Test
    void locationStoreAndResolversUseAsyncTypedContracts() throws Exception {
        Method updatePeer = ZLinkPeerLocationStore.class.getMethod(
            "updatePeer",
            ZLinkPeerLocation.class,
            systems.zlink.framework.locations.ZLinkLocationWriteIntent.class);
        Method listPeerLocations = ZLinkPeerLocationStore.class.getMethod(
            "listPeerLocations",
            ZLinkPeerLocationFilter.class);
        Method listLivePeers = ZLinkPeerLocationResolver.class.getMethod(
            "listLivePeers",
            ZLinkPeerLocationFilter.class);
        Method claimOwnerLease = ZLinkOwnerLeaseStore.class.getMethod(
            "claimOwnerLease",
            String.class,
            java.time.Duration.class);
        Method readOwnerLease = ZLinkOwnerLeaseStore.class.getMethod(
            "readOwnerLease",
            String.class);
        Method renewOwnerLease = ZLinkOwnerLeaseStore.class.getMethod(
            "renewOwnerLease",
            ZLinkLocationOwnerToken.class,
            java.time.Duration.class);
        Method releaseOwnerLease = ZLinkOwnerLeaseStore.class.getMethod(
            "releaseOwnerLease",
            ZLinkLocationOwnerToken.class);
        Method removeAllByOwner = ZLinkLocationStore.class.getMethod(
            "removeAllByOwner",
            ZLinkLocationOwnerToken.class);
        Method updateMeshNode =
            systems.zlink.framework.locations
                .ZLinkMeshNodeLocationStore.class.getMethod(
                    "updateMeshNode",
                    systems.zlink.framework.locations
                        .ZLinkMeshNodeDescriptor.class,
                    ZLinkLocationWriteIntent.class);
        Method removeMeshNode =
            systems.zlink.framework.locations
                .ZLinkMeshNodeLocationStore.class.getMethod(
                    "removeMeshNode",
                    systems.zlink.framework.locations
                        .ZLinkMeshNodeDescriptorKey.class,
                    ZLinkLocationOwnerToken.class);
        Method listMeshNodes =
            systems.zlink.framework.locations
                .ZLinkMeshNodeLocationStore.class.getMethod(
                    "listMeshNodes",
                    String.class,
                    ZLinkPageRequest.class);
        assertEquals(CompletionStage.class, updatePeer.getReturnType());
        assertEquals(CompletionStage.class, listPeerLocations.getReturnType());
        assertEquals(CompletionStage.class, listLivePeers.getReturnType());
        assertEquals(CompletionStage.class, claimOwnerLease.getReturnType());
        assertEquals(CompletionStage.class, readOwnerLease.getReturnType());
        assertEquals(CompletionStage.class, renewOwnerLease.getReturnType());
        assertEquals(CompletionStage.class, releaseOwnerLease.getReturnType());
        assertEquals(CompletionStage.class, removeAllByOwner.getReturnType());
        assertEquals(CompletionStage.class, updateMeshNode.getReturnType());
        assertEquals(CompletionStage.class, removeMeshNode.getReturnType());
        assertEquals(CompletionStage.class, listMeshNodes.getReturnType());
    }

    @Test
    void frameworkOptionsExposeLocationStoreRegistrationSurface() throws Exception {
        assertNoMethod("add" + "Peer" + "LocationStore");
        assertNoMethod("add" + "Spot" + "LocationStore");
        assertNoMethod("add" + "Actor" + "LocationStore");
        assertNoMethod("add" + "Route" + "LocationStore");
        assertNoMethod("add" + "OwnerLease" + "Store");
        assertNoMethod("useInMemoryLocationStores");
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
        assertEquals(ZLinkLocationStore.class, ZLinkLocationStore.class);
    }

    @Test
    void locationRuntimeQueryExposesPagedRuntimeViews() throws Exception {
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("getStatus")
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listPeerLocations", ZLinkPeerLocationFilter.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listSpotLocations", ZLinkSpotLocationFilter.class, ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listActorLocations", ZLinkActorLocationFilter.class, ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod("listRouteLocations", ZLinkRouteLocationFilter.class, ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod(
                "listTopology",
                systems.zlink.framework.locations.ZLinkLocationTopologyFilter.class,
                ZLinkPageRequest.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationRuntimeQuery.class
            .getMethod(
                "listServiceSummaries",
                systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter.class)
            .getReturnType());
        assertEquals(ZLinkLocationPage.class, ZLinkLocationPage.class);
        assertEquals(ZLinkOwnerLease.class, ZLinkOwnerLease.class);
    }

    @Test
    void s4ConvenienceContractsArePublicAndTyped() throws Exception {
        assertEquals(CompletionStage.class, ZLinkActorDirectory.class
            .getMethod("find", String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorDirectory.class
            .getMethod(
                "ensure",
                String.class,
                systems.zlink.framework.messaging.ZLinkMessage.class)
            .getReturnType());
        assertMissing("systems.zlink.framework.actors.ZLinkActorPlacement");
        assertRecordComponents(ActorRefSnapshot.class, "nodeRid", "actorId", "generation");
        assertEquals(ActorRef.class, ActorRefSnapshot.class
            .getMethod("toActorRef")
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationReadiness.class
            .getMethod("isPeerReady", String.class, ZLinkLocationRole.class, RoutingId.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkSessionActors.class
            .getMethod("bindOrGet", ActorRef.class)
            .getReturnType());
        assertMissing("systems.zlink.framework.channels.ZLinkRoute" + "RequestCall");
    }

    @Test
    void actorClientPinsL13CompletionStageSurface() throws Exception {
        assertEquals(ZLinkActorSendCall.class, ZLinkActorClient.class
            .getMethod("sendToActor", ActorRef.class, Object.class)
            .getReturnType());
        assertEquals(ZLinkActorRequestCall.class, ZLinkActorClient.class
            .getMethod("requestToActor", ActorRef.class, Object.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorSendCall.class
            .getMethod("submit")
            .getReturnType());
        assertNoPublicMethod(ZLinkActorSendCall.class, "await");
        assertEquals(ZLinkActorRequestCall.class, ZLinkActorRequestCall.class
            .getMethod("timeout", java.time.Duration.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorRequestCall.class
            .getMethod("submit", Class.class)
            .getReturnType());
        assertNoPublicMethod(ZLinkActorRequestCall.class, "await", Class.class);
        assertNoPublicMethod(ZLinkActorSendCall.class, "submit", Class.class);
        assertNoPublicMethod(ZLinkActorRequestCall.class, "submit");
    }

    @Test
    void enumValuesMatchDotnetContractTablesValueByValue() throws Exception {
        assertEnumValues(ZLinkFrameworkErrorKind.class, Map.ofEntries(
            Map.entry("ACTOR_ROUTE_NOT_FOUND", 0),
            Map.entry("ACTOR_CREATE_FAILED", 1),
            Map.entry("ACTOR_ALREADY_EXISTS", 2),
            Map.entry("ACTOR_TYPE_MISMATCH", 3),
            Map.entry("SPOT_CREATE_FAILED", 4),
            Map.entry("SPOT_ROUTE_NOT_FOUND", 5),
            Map.entry("SPOT_TYPE_MISMATCH", 6),
            Map.entry("ACTOR_SESSION_NOT_BOUND", 7),
            Map.entry("HANDLER_NOT_FOUND", 8),
            Map.entry("ROUTE_HANDLER_NOT_FOUND", 9),
            Map.entry("ACTOR_DISPATCH_HANDLER_NOT_FOUND", 10),
            Map.entry("PAYLOAD_DECODE_FAILED", 11),
            Map.entry("ROUTE_NOT_CONNECTED", 12),
            Map.entry("REQUEST_TARGET_NOT_FOUND", 13),
            Map.entry("REQUEST_REJECTED", 14),
            Map.entry("REQUEST_PROTOCOL_ERROR", 15),
            Map.entry("REQUEST_FAILED", 16),
            Map.entry("WORKER_QUEUE_FULL", 17),
            Map.entry("WORKER_TIMED_OUT", 18),
            Map.entry("WORKER_FAILED", 19),
            Map.entry("ACTOR_LOCATION_STALE", 20),
            Map.entry("ACTOR_CREATE_REJECTED", 21),
            Map.entry("OBJECT_CLIENT_NOT_CONFIGURED", 22),
            Map.entry("MESH_SELECTION_REQUIRED", 23),
            Map.entry("MESH_NOT_FOUND", 24),
            Map.entry("INVALID_CONFIGURATION", 25),
            Map.entry("ALREADY_SUBMITTED", 26),
            Map.entry("ACTOR_GENERATION_STALE", 27),
            Map.entry("ACTOR_MOVING", 28),
            Map.entry("DEADLINE_EXCEEDED", 29),
            Map.entry("PLACEMENT_CAPACITY_EXHAUSTED", 30),
            Map.entry("ROUTING_ID_CONFLICT", 31),
            Map.entry("SPOT_GENERATION_STALE", 32),
            Map.entry("SPOT_MOVING", 33),
            Map.entry("RELOCATION_DATA_LOST", 34),
            Map.entry("SPOT_ID_CONFLICT", 35),
            Map.entry("RUNTIME_SHUTDOWN", 36),
            Map.entry("RELOCATION_DISABLED", 37),
            Map.entry("RELOCATION_TARGET_UNAVAILABLE", 38),
            Map.entry("RELOCATION_FAILED", 39)));
        assertRetriableOnly(
            ZLinkFrameworkErrorKind.ROUTE_NOT_CONNECTED,
            ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE,
            ZLinkFrameworkErrorKind.ACTOR_MOVING,
            ZLinkFrameworkErrorKind.DEADLINE_EXCEEDED,
            ZLinkFrameworkErrorKind.PLACEMENT_CAPACITY_EXHAUSTED,
            ZLinkFrameworkErrorKind.SPOT_MOVING,
            ZLinkFrameworkErrorKind.RELOCATION_TARGET_UNAVAILABLE,
            ZLinkFrameworkErrorKind.RELOCATION_FAILED);

        assertEnumValues(ZLinkLocationAutoConnectType.class, Map.of(
            "INVALID", 0,
            "ROUTE_MESH", 1,
            "CLIENT_SERVER", 2,
            "DEALER_MESH", 3,
            "FANOUT", 4,
            "SPOT_MESH", 5));
        assertEnumValues(ZLinkLocationRole.class, Map.of(
            "INVALID", 0,
            "SPOT", 2,
            "ROUTER", 3,
            "DEALER", 4,
            "PUB", 5,
            "SUB", 6));
        assertEnumValues(ZLinkRouteKind.class, Map.of(
            "INVALID", 0,
            "ACTOR_SESSION", 1,
            "SPOT_NAME", 2,
            "FRAMEWORK_ROUTE", 3));
        assertEnumValues(ZLinkLocationKind.class, Map.of(
            "INVALID", 0,
            "PEER", 1,
            "SPOT", 2,
            "ACTOR", 3,
            "ROUTE", 4));
        assertEnumValues(ZLinkLocationWriteIntent.class, Map.of(
            "NEW_CLAIM", 1,
            "RENEW", 2,
            "TAKEOVER", 3));
        assertEnumValues(ZLinkLocationWriteStatus.class, Map.of(
            "STORED", 1,
            "IGNORED_STALE", 2,
            "REJECTED_CONFLICT", 3));
        assertEnumValues(ZLinkLocationChangeType.class, Map.of(
            "UPSERTED", 1,
            "REMOVED", 2,
            "EXPIRED", 3));
        assertEnumValues(ZLinkLocationTopologyState.class, Map.of(
            "DISCOVERED", 1,
            "CONNECTING", 2,
            "READY", 3,
            "LOST", 4,
            "ERROR", 5,
            "STOPPED", 6));
        assertEnumValues(ZLinkSpotKind.class, Map.of(
            "INVALID", 0,
            "ENTRY", 1,
            "USER", 2));
        assertEnumValues(ZLinkLocationRuntimeEventKind.class, Map.of(
            "STATUS_CHANGED", 0,
            "TOPOLOGY_CHANGED", 1,
            "SERVICE_SUMMARY_CHANGED", 2,
            "STORE_FAILURE", 3,
            "STORE_RECOVERED", 4));
    }

    @Test
    void typedLocationWatchKeysUseClosedRecordSet() throws Exception {
        assertIterableEquals(
            List.of(
                ZLinkLocationKey.Peer.class,
                ZLinkLocationKey.Spot.class,
                ZLinkLocationKey.Actor.class,
                ZLinkLocationKey.Route.class),
            List.of(ZLinkLocationKey.class.getPermittedSubclasses()));
        assertRecordComponents(ZLinkLocationKey.Peer.class, "key");
        assertRecordComponents(ZLinkLocationKey.Spot.class, "key");
        assertRecordComponents(ZLinkLocationKey.Actor.class, "key");
        assertRecordComponents(ZLinkLocationKey.Route.class, "key");
        assertEquals(ZLinkPeerLocationKey.class, componentType(ZLinkLocationKey.Peer.class, "key"));
        assertEquals(ZLinkSpotLocationKey.class, componentType(ZLinkLocationKey.Spot.class, "key"));
        assertEquals(ZLinkActorLocationKey.class, componentType(ZLinkLocationKey.Actor.class, "key"));
        assertEquals(ZLinkRouteLocationKey.class, componentType(ZLinkLocationKey.Route.class, "key"));

        assertRecordComponents(
            ZLinkPeerLocationKey.class,
            "autoConnectType",
            "meshName",
            "role",
            "nodeRid",
            "endpoint");
        assertRecordComponents(ZLinkSpotLocationKey.class, "spotId");
        assertRecordComponents(ZLinkActorLocationKey.class, "actorId");
        assertRecordComponents(ZLinkRouteLocationKey.class, "routeKind", "routeKey");
        assertRecordComponents(ZLinkLocationChanged.class, "kind", "key", "changeType", "generation", "updatedAt");
        assertEquals(ZLinkLocationKey.class, componentType(ZLinkLocationChanged.class, "key"));
        assertEquals(java.util.concurrent.Flow.Publisher.class, ZLinkLocationWatchStore.class
            .getMethod("watch", ZLinkLocationWatchFilter.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkLocationChangeStampStore.class
            .getMethod("getChangeStamp", ZLinkLocationChangeStampScope.class)
            .getReturnType());
    }

    @Test
    void actorJoinAndManagerSurfacesPinSharedCallShape() throws Exception {
        assertEquals(void.class, ZLinkActorJoinCall.class
            .getMethod("defer")
            .getReturnType());
        assertEquals(ZLinkActorJoinCall.class, ZLinkActorJoinCall.class
            .getMethod("timeout", java.time.Duration.class)
            .getReturnType());
        assertTrue(ZLinkActorJoinCompletion.class.isSealed());
        assertRecordComponents(
            ZLinkActorJoinCompletion.Accepted.class,
            "operationId", "actor", "reply");
        assertRecordComponents(
            ZLinkActorJoinCompletion.Rejected.class,
            "operationId", "reply");
        assertRecordComponents(
            ZLinkActorJoinCompletion.Failed.class,
            "operationId", "kind", "isRetriable");
        assertEquals(ActorRef.class,
            componentType(ZLinkActorJoinCompletion.Accepted.class, "actor"));

        assertEquals(CompletionStage.class, ZLinkActorManager.class
            .getMethod("create", String.class, String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorManager.class
            .getMethod("create", String.class, String.class, ZLinkMessage.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorManager.class
            .getMethod("find", String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorManager.class
            .getMethod("getOrCreate", String.class, String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkActorManager.class
            .getMethod("getOrCreate", String.class, String.class, ZLinkMessage.class)
            .getReturnType());
    }

    @Test
    void spotManagerPinsRelocatableSpotContract() throws Exception {
        assertEquals(ZLinkSpotCreateCall.class, ZLinkSpotManager.class
            .getMethod("create", String.class)
            .getReturnType());
        assertEquals(ZLinkSpotGetOrCreateCall.class, ZLinkSpotManager.class
            .getMethod("getOrCreate", String.class, String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkSpotManager.class
            .getMethod("find", String.class)
            .getReturnType());
        assertEquals(CompletionStage.class, ZLinkSpotManager.class
            .getMethod("close", SpotRef.class)
            .getReturnType());
        assertEquals(ZLinkSpotCreateResult.class, ZLinkSpotCreateResult.class);
        assertEquals(ZLinkSpotInfo.class, ZLinkSpotInfo.class);
        assertNoPublicMethod(ZLinkSpotManager.class, "create", Class.class, Object.class);
        assertNoPublicMethod(ZLinkSpotManager.class, "getOrCreate", Class.class, RoutingId.class, Object.class);
    }

    @Test
    void routeClientPinsToNodeNaming() throws Exception {
        assertEquals(ZLinkSendCall.class, ZLinkRouteClient.class
            .getMethod("sendToNode", String.class, RoutingId.class, Object.class)
            .getReturnType());
        assertEquals(ZLinkRequestCall.class, ZLinkRouteClient.class
            .getMethod("requestToNode", String.class, RoutingId.class, Object.class)
            .getReturnType());
        assertEquals(ZLinkSendCall.class, ZLinkRouteClient.class
            .getMethod(
                "sendToChannel",
                String.class,
                Object.class)
            .getReturnType());
        assertEquals(ZLinkRequestCall.class, ZLinkRouteClient.class
            .getMethod(
                "requestToChannel",
                String.class,
                Object.class)
            .getReturnType());
        assertEquals(ZLinkSpotSendCall.class, ZLinkRouteClient.class
            .getMethod("sendToSpot", String.class, Object.class)
            .getReturnType());
        assertEquals(ZLinkSpotRequestCall.class, ZLinkRouteClient.class
            .getMethod("requestToSpot", String.class, Object.class)
            .getReturnType());
        assertNoPublicMethod(ZLinkRouteClient.class, "send", String.class, RoutingId.class, Object.class);
        assertNoPublicMethod(ZLinkRouteClient.class, "request", String.class, RoutingId.class, Object.class);
        assertNoPublicMethod(
            ZLinkRouteClient.class,
            "sendToSpot",
            String.class,
            RoutingId.class,
            RoutingId.class,
            Object.class);
        assertNoPublicMethod(
            ZLinkRouteClient.class,
            "requestToSpot",
            String.class,
            RoutingId.class,
            RoutingId.class,
            Object.class);
    }

    @Test
    void oldPublicContractSymbolsDoNotReenterJavaFrameworkConsumers() throws Exception {
        Path javaRoot = Path.of(System.getProperty("user.dir")).getParent();
        List<Path> scanRoots = List.of(
            javaRoot.resolve("zlink-framework-core"),
            javaRoot.resolve("zlink-framework-kotlin"),
            javaRoot.resolve("zlink-framework-locations-redis"),
            javaRoot.resolve("zlink-framework-spring-boot-starter"),
            javaRoot.resolve("e2e"),
            javaRoot.resolve("e2e-kotlin"),
            javaRoot.resolve("samples"));
        List<String> forbidden = List.of(
            "addPeerLocationStore",
            "addSpotLocationStore",
            "addActorLocationStore",
            "addRouteLocationStore",
            "addOwnerLeaseStore",
            "ZLinkRouteLocationResolver",
            "ZLinkActorRefResolver",
            "ZLinkSpotLocationResolver",
            "ZLinkActorLocationResolver",
            "resolveActorRef",
            "ResolveActorRef",
            "ZLinkRouteRequestCall",
            "ActorIdConflict",
            "ACTOR_ID_CONFLICT");

        List<String> offenders = new ArrayList<>();
        for (Path root : scanRoots) {
            if (!Files.exists(root)) {
                continue;
            }
            try (Stream<Path> files = Files.walk(root)) {
                files
                    .filter(Files::isRegularFile)
                    .filter(LocationContractTest::isGuardedSourceFile)
                    .filter(path -> !isAllowedGuardException(javaRoot, path))
                    .forEach(path -> collectForbiddenSymbols(javaRoot, path, forbidden, offenders));
            }
        }

        assertTrue(
            offenders.isEmpty(),
            "old public contract symbols reintroduced; allowed exceptions are build output, markdown docs, stream connector, and this guard test: "
                + offenders);
    }

    private static void assertMissing(String className) {
        assertThrows(ClassNotFoundException.class, () -> Class.forName(className));
    }

    private static void assertNoMethod(String name) {
        assertFalse(Arrays.stream(ZLinkFrameworkOptions.class.getMethods())
            .anyMatch(method -> method.getName().equals(name)));
    }

    private static void assertNoPublicMethod(Class<?> type, String name, Class<?>... parameterTypes) {
        assertThrows(NoSuchMethodException.class, () -> type.getMethod(name, parameterTypes));
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

    private static <T extends Enum<T>> void assertEnumValues(Class<T> type, Map<String, Integer> expected)
        throws Exception {
        assertEquals(
            expected.keySet(),
            Arrays.stream(type.getEnumConstants())
                .map(Enum::name)
                .collect(Collectors.toCollection(java.util.LinkedHashSet::new)));
        Method value = type.getMethod("value");
        for (T constant : type.getEnumConstants()) {
            assertEquals(expected.get(constant.name()), value.invoke(constant), constant.name());
        }
    }

    private static void assertRetriableOnly(ZLinkFrameworkErrorKind... retriable) {
        Set<ZLinkFrameworkErrorKind> retriableSet = Set.of(retriable);
        for (ZLinkFrameworkErrorKind kind : ZLinkFrameworkErrorKind.values()) {
            assertEquals(retriableSet.contains(kind), kind.retriable(), kind.name());
        }
    }

    private static boolean isGuardedSourceFile(Path path) {
        String name = path.getFileName().toString();
        return name.endsWith(".java") || name.endsWith(".kt") || name.endsWith(".kts");
    }

    private static boolean isAllowedGuardException(Path javaRoot, Path path) {
        Path relative = javaRoot.relativize(path);
        String normalized = relative.toString().replace('\\', '/');
        return normalized.contains("/build/")
            || normalized.contains("/.gradle/")
            || normalized.startsWith("zlink-stream-connector/")
            || normalized.equals("zlink-framework-core/src/test/java/systems/zlink/framework/LocationContractTest.java");
    }

    private static void collectForbiddenSymbols(
        Path javaRoot,
        Path path,
        List<String> forbidden,
        List<String> offenders) {
        try {
            List<String> lines = Files.readAllLines(path);
            for (int i = 0; i < lines.size(); i++) {
                String line = lines.get(i);
                for (String needle : forbidden) {
                    if (line.contains(needle)) {
                        offenders.add(javaRoot.relativize(path) + ":" + (i + 1) + " contains " + needle);
                    }
                }
            }
        } catch (IOException ex) {
            throw new AssertionError("failed to scan " + path, ex);
        }
    }
}
