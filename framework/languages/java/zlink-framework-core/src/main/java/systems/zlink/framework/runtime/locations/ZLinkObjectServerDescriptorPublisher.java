package systems.zlink.framework.runtime.locations;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicLong;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalMeshNode;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;

/**
 * Publishes Object Server placement descriptors under the host owner lease.
 */
public final class ZLinkObjectServerDescriptorPublisher {
    private final ZLinkLocationStore store;
    private final ZLinkLocationRuntime runtime;
    private final ZLinkFrameworkRegistration registration;
    private final java.util.Map<String, ZLinkInternalMeshNode> nodes;
    private final AtomicLong revision = new AtomicLong(1);
    private final java.util.concurrent.atomic.AtomicBoolean published =
        new java.util.concurrent.atomic.AtomicBoolean();

    public ZLinkObjectServerDescriptorPublisher(
        ZLinkLocationStore store,
        ZLinkLocationRuntime runtime,
        ZLinkFrameworkRegistration registration,
        java.util.Map<String, ZLinkInternalMeshNode> nodes) {
        this.store = store;
        this.runtime = runtime;
        this.registration = registration;
        this.nodes = java.util.Map.copyOf(nodes);
    }

    public CompletionStage<Void> publish(ZLinkFrameworkRuntimeState state) {
        ZLinkLocationOwnerToken owner = runtime.currentOwnerToken();
        ZLinkLocationWriteIntent intent = published.get()
            ? ZLinkLocationWriteIntent.RENEW
            : ZLinkLocationWriteIntent.NEW_CLAIM;
        List<CompletionStage<?>> writes = new ArrayList<>();
        for (MeshNodeRegistration configured : registration.meshNodes()) {
            if (!configured.objectServer()) {
                continue;
            }
            ZLinkInternalMeshNode node = nodes.get(configured.meshName());
            if (node == null) {
                continue;
            }
            writes.add(store.updateMeshNode(
                    descriptor(configured, node, owner, state),
                    intent)
                .thenAccept(result -> {
                    if (result.status() != ZLinkLocationWriteStatus.STORED) {
                        throw new systems.zlink.framework.errors
                            .ZLinkConfigurationException(
                                "Object Server descriptor publication failed"
                                    + " [mesh=" + configured.meshName()
                                    + ", status=" + result.status() + "]");
                    }
                }));
        }
        return all(writes).thenRun(() -> published.set(true));
    }

    public CompletionStage<Void> remove() {
        ZLinkLocationOwnerToken owner = runtime.currentOwnerToken();
        List<CompletionStage<?>> removals = new ArrayList<>();
        for (MeshNodeRegistration configured : registration.meshNodes()) {
            ZLinkInternalMeshNode node = nodes.get(configured.meshName());
            if (node != null && configured.objectServer()) {
                removals.add(store.removeMeshNode(
                    new ZLinkMeshNodeDescriptorKey(
                        configured.meshName(),
                        node.status().routingId()),
                    owner));
            }
        }
        return all(removals);
    }

    private ZLinkMeshNodeDescriptor descriptor(
        MeshNodeRegistration configured,
        ZLinkInternalMeshNode node,
        ZLinkLocationOwnerToken owner,
        ZLinkFrameworkRuntimeState state) {
        List<ZLinkObjectCapability> capabilities = new ArrayList<>();
        configured.relocatableSpotFactories().values().forEach(factory ->
            capabilities.add(capability(
                ZLinkPlacementObjectKind.USER_SPOT,
                factory.stableType(),
                factory.options().stableTypeLimit(),
                factory.relocationPolicy())));
        configured.relocatableInstanceSpotFactories().values().forEach(factory ->
            capabilities.add(capability(
                ZLinkPlacementObjectKind.INSTANCE_SPOT,
                factory.stableType(),
                factory.options().stableTypeLimit(),
                factory.relocationPolicy())));
        configured.relocatableActorFactories().values().forEach(factory ->
            capabilities.add(capability(
                ZLinkPlacementObjectKind.ACTOR,
                factory.stableType(),
                0,
                factory.relocationPolicy())));
        return new ZLinkMeshNodeDescriptor(
            configured.meshName(),
            node.status().routingId(),
            node.status().lifecycleGeneration(),
            revision.getAndIncrement(),
            configured.bindEndpoint(),
            node.channelWeights(),
            0,
            capabilities,
            ZLinkMeshNodeObjectRole.SERVER,
            Optional.of(configured.entrySpotId()),
            state == ZLinkFrameworkRuntimeState.SERVING
                ? node.placementWeight()
                : 0,
            new ZLinkPlacementCapacity(
                new ZLinkCapacityUsage(
                    0, 0, configured.actorCapacity()),
                new ZLinkCapacityUsage(
                    0, 0, configured.spotCapacity()),
                capabilities.stream()
                    .filter(capability ->
                        capability.objectKind()
                            != ZLinkPlacementObjectKind.ACTOR)
                    .map(capability -> new ZLinkSpotTypeCapacity(
                        capability.objectKind(),
                        capability.stableType(),
                        new ZLinkCapacityUsage(
                            0, 0, capability.spotLimit())))
                    .toList()),
            new ZLinkActivationConcurrency(
                0,
                configured.activationConcurrency()),
            Optional.empty(),
            state,
            node.status().routingId().toString(),
            owner.ownerId(),
            owner.leaseGeneration(),
            Instant.now());
    }

    private static ZLinkObjectCapability capability(
        ZLinkPlacementObjectKind kind,
        String stableType,
        int stableTypeLimit,
        ZLinkRelocationPolicy<?> policy) {
        return new ZLinkObjectCapability(
            kind,
            stableType,
            policy instanceof ZLinkRelocationPolicy.Snapshot<?>
                ? ZLinkObjectMaintenancePolicyKind.SNAPSHOT
                : policy instanceof ZLinkRelocationPolicy.Recreate<?>
                    ? ZLinkObjectMaintenancePolicyKind.RECREATE
                    : ZLinkObjectMaintenancePolicyKind.DISABLED,
            policy instanceof ZLinkRelocationPolicy.Snapshot<?>,
            kind == ZLinkPlacementObjectKind.ACTOR ? 0 : stableTypeLimit);
    }

    private static CompletionStage<Void> all(
        List<CompletionStage<?>> stages) {
        return CompletableFuture.allOf(stages.stream()
            .map(CompletionStage::toCompletableFuture)
            .toArray(CompletableFuture[]::new));
    }
}
