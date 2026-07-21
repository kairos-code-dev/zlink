package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;

class ZLinkAllocatedRoutingIdRuntimeTest {
    @Test
    void locationLeaseDefaultsMatchRoutingIdAllocationContract() {
        ZLinkLocationOptions options = new ZLinkLocationOptions();

        assertEquals(Duration.ofSeconds(10), options.heartbeatInterval());
        assertEquals(Duration.ofSeconds(30), options.ownerLeaseTtl());
        assertEquals(Duration.ofSeconds(5), options.routingIdFencingMargin());
        assertEquals(Duration.ofSeconds(3), options.ownerLeaseRenewTimeout());
    }

    @Test
    void rejectsAllocationWhenLeaseRenewalCannotFinishBeforeFencingDeadline() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMesh("room")
            .useAllocatedRoutingId(2, "play");
        ZLinkLocationOptions locationOptions = new ZLinkLocationOptions();
        locationOptions.setHeartbeatInterval(Duration.ofSeconds(22));
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationRuntime locations = new ZLinkLocationRuntime(
            ZLinkRegisteredLocationStores.fromUnified(store),
            locationOptions.ownerLeaseTtl(),
            locationOptions.heartbeatInterval());

        try {
            assertThrows(
                ZLinkConfigurationException.class,
                () -> new ZLinkAllocatedRoutingIdRuntime(
                    options.registration(), store, locations, locationOptions));
        } finally {
            locations.close();
        }
    }

    @Test
    void appliesEveryGroupBeforeReadyProviderCompletes() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        var play = options.addRouteMesh("room")
            .listen("inproc://play")
            .useAllocatedRoutingId(2, "play")
            .setRoutingIdAllocationGroup("bingo.play");
        play.channelName("room");
        var registration = options.registration().meshNodes().getFirst();
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkLocationOptions locationOptions = new ZLinkLocationOptions();
        ZLinkLocationRuntime locations = new ZLinkLocationRuntime(
            ZLinkRegisteredLocationStores.fromUnified(store),
            locationOptions.ownerLeaseTtl(),
            locationOptions.heartbeatInterval());
        locations.start(RoutingId.from("owner-node")).toCompletableFuture().join();

        try (ZLinkAllocatedRoutingIdRuntime runtime = new ZLinkAllocatedRoutingIdRuntime(
            options.registration(), store, locations, locationOptions)) {
            runtime.start();
            assertEquals(RoutingId.from("play1"), registration.routingId());
            runtime.markReady();
            var ready = runtime.waitForReadyAllocation("bingo.play")
                .toCompletableFuture().join();
            assertEquals(1, ready.slot());
            assertEquals(RoutingId.from("play1"), ready.meshNodeRoutingIds().get("room"));
        } finally {
            locations.close();
        }
    }
}
