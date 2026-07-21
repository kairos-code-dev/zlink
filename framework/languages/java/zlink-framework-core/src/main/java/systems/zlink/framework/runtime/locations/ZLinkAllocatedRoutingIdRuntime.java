package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkAllocatedRoutingId;
import systems.zlink.framework.locations.ZLinkAllocatedRoutingIdProvider;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireRequest;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquired;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocation;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationMember;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationStore;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupConfigurationMismatch;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupExhausted;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotIdentityModeConflict;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;

public final class ZLinkAllocatedRoutingIdRuntime
    implements ZLinkAllocatedRoutingIdProvider, AutoCloseable {
    private final ZLinkRoutingIdSlotAllocationStore store;
    private final ZLinkLocationRuntime locations;
    private final ZLinkLocationOptions options;
    private final List<Group> groups;
    private final Map<String, CompletableFuture<ZLinkAllocatedRoutingId>> ready;
    private final List<AcquiredGroup> acquired = new ArrayList<>();
    private boolean started;

    public ZLinkAllocatedRoutingIdRuntime(
        ZLinkFrameworkRegistration registration,
        ZLinkRoutingIdSlotAllocationStore store,
        ZLinkLocationRuntime locations,
        ZLinkLocationOptions options) {
        this.store = store;
        this.locations = locations;
        this.options = options;
        this.groups = buildGroups(registration);
        validateLeaseSafety(this.groups, options);
        this.ready = new LinkedHashMap<>();
        groups.forEach(group -> ready.put(group.name(), new CompletableFuture<>()));
    }

    public boolean enabled() {
        return !groups.isEmpty();
    }

    public synchronized void start() {
        if (!enabled() || started) {
            return;
        }
        started = true;
        try {
            while (acquired.size() < groups.size()) {
                releaseAcquiredForRetry();
                boolean retry = false;
                for (Group group : groups) {
                    try {
                        var result = store.acquireRoutingIdSlot(new ZLinkRoutingIdSlotAcquireRequest(
                                group.name(),
                                group.storeMembers(),
                                group.slotCount(),
                                locations.ownerId(),
                                options.ownerLeaseTtl()))
                            .toCompletableFuture()
                            .join();
                        if (result instanceof ZLinkRoutingIdSlotAcquired success) {
                            acquired.add(new AcquiredGroup(group, success.allocation()));
                        } else if (result instanceof ZLinkRoutingIdSlotGroupExhausted) {
                            retry = true;
                            break;
                        } else if (result instanceof ZLinkRoutingIdSlotGroupConfigurationMismatch mismatch) {
                            throw new ZLinkConfigurationException(
                                "routing ID allocation group configuration mismatch: "
                                    + group.name() + " expected slots="
                                    + mismatch.expectedSlotCount() + " actual slots="
                                    + mismatch.actualSlotCount());
                        } else if (result instanceof ZLinkRoutingIdSlotIdentityModeConflict) {
                            throw new ZLinkConfigurationException(
                                "routing ID allocation conflicts with fixed identity mode: "
                                    + group.name());
                        } else {
                            throw new ZLinkConfigurationException(
                                "unknown routing ID allocation result: " + result.getClass().getName());
                        }
                    } catch (ZLinkConfigurationException error) {
                        throw error;
                    } catch (RuntimeException storeUnavailable) {
                        retry = true;
                        break;
                    }
                }
                if (!retry && acquired.size() == groups.size()) {
                    applyAcquired();
                    return;
                }
                releaseAcquiredForRetry();
                sleep(options.pollingInterval());
            }
        } catch (RuntimeException error) {
            releaseAcquiredForRetry();
            started = false;
            ready.values().forEach(source -> source.completeExceptionally(error));
            throw error;
        }
    }

    public synchronized void markReady() {
        for (AcquiredGroup item : acquired) {
            Map<String, RoutingId> routingIds = new LinkedHashMap<>();
            for (Member member : item.group().members()) {
                routingIds.put(member.registration().meshName(), member.routingId(item.allocation().slot()));
            }
            ready.get(item.group().name()).complete(new ZLinkAllocatedRoutingId(
                item.group().name(), item.allocation().slot(), routingIds));
        }
    }

    @Override
    public CompletionStage<ZLinkAllocatedRoutingId> waitForReadyAllocation(String groupName) {
        CompletableFuture<ZLinkAllocatedRoutingId> source = ready.get(groupName);
        if (source == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "routing ID allocation group is not registered: " + groupName));
        }
        return source;
    }

    @Override
    public synchronized void close() {
        if (!started) {
            return;
        }
        RuntimeException failure = null;
        for (int index = acquired.size() - 1; index >= 0; index--) {
            AcquiredGroup item = acquired.get(index);
            try {
                store.releaseRoutingIdSlot(
                        item.group().name(),
                        item.allocation().slot(),
                        item.allocation().owner())
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException error) {
                if (failure == null) {
                    failure = error;
                } else {
                    failure.addSuppressed(error);
                }
            }
        }
        acquired.clear();
        started = false;
        if (failure != null) {
            throw failure;
        }
    }

    private void applyAcquired() {
        for (AcquiredGroup item : acquired) {
            for (Member member : item.group().members()) {
                member.registration().applyAllocatedRoutingId(
                    member.routingId(item.allocation().slot()));
            }
        }
    }

    private void releaseAcquiredForRetry() {
        for (int index = acquired.size() - 1; index >= 0; index--) {
            AcquiredGroup item = acquired.get(index);
            try {
                store.releaseRoutingIdSlot(
                        item.group().name(),
                        item.allocation().slot(),
                        item.allocation().owner())
                    .toCompletableFuture()
                    .join();
            } catch (RuntimeException ignored) {
                // The same owner retries idempotently. The shared owner lease fences any claim
                // that could not be released while the store was unavailable.
            }
        }
        acquired.clear();
    }

    private static List<Group> buildGroups(ZLinkFrameworkRegistration registration) {
        Map<String, List<Member>> grouped = new java.util.TreeMap<>();
        for (MeshNodeRegistration node : registration.meshNodes()) {
            if (node.allocationSlotCount() == null) {
                continue;
            }
            String prefix = node.allocationPrefix();
            RoutingId.from(prefix + node.allocationSlotCount());
            grouped.computeIfAbsent(node.allocationGroup(), ignored -> new ArrayList<>())
                .add(new Member(node, prefix, node.allocationSlotCount()));
        }
        List<Group> result = new ArrayList<>();
        for (var entry : grouped.entrySet()) {
            List<Member> members = entry.getValue().stream()
                .sorted(Comparator.comparing(member -> member.registration().meshName()))
                .toList();
            int slotCount = members.getFirst().slotCount();
            if (members.stream().anyMatch(member -> member.slotCount() != slotCount)) {
                throw new ZLinkConfigurationException(
                    "routing ID allocation group must use one slot count: " + entry.getKey());
            }
            if (members.stream().map(member -> member.registration().meshName()).distinct().count()
                != members.size()) {
                throw new ZLinkConfigurationException(
                    "routing ID allocation group contains duplicate mesh member: " + entry.getKey());
            }
            result.add(new Group(
                entry.getKey(),
                slotCount,
                members,
                members.stream().map(member -> new ZLinkRoutingIdSlotAllocationMember(
                    member.registration().meshName(), member.prefix())).toList()));
        }
        return List.copyOf(result);
    }

    private static void validateLeaseSafety(List<Group> groups, ZLinkLocationOptions options) {
        if (groups.isEmpty()) {
            return;
        }
        Duration renewalDeadline = options.heartbeatInterval()
            .plus(options.ownerLeaseRenewTimeout());
        Duration fencingDeadline = options.ownerLeaseTtl()
            .minus(options.routingIdFencingMargin());
        if (!renewalDeadline.minus(fencingDeadline).isNegative()) {
            throw new ZLinkConfigurationException(
                "routing ID lease options must satisfy heartbeatInterval + "
                    + "ownerLeaseRenewTimeout < ownerLeaseTtl - routingIdFencingMargin");
        }
    }

    private static void sleep(Duration duration) {
        try {
            Thread.sleep(duration.toMillis());
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new ZLinkConfigurationException(
                "routing ID allocation was interrupted before a slot became available", error);
        }
    }

    private record Member(
        MeshNodeRegistration registration,
        String prefix,
        int slotCount) {
        RoutingId routingId(int slot) {
            return RoutingId.from(prefix + slot);
        }
    }

    private record Group(
        String name,
        int slotCount,
        List<Member> members,
        List<ZLinkRoutingIdSlotAllocationMember> storeMembers) {
    }

    private record AcquiredGroup(Group group, ZLinkRoutingIdSlotAllocation allocation) {
    }
}
