package systems.zlink.framework.runtime.host;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;
import systems.zlink.framework.runtime.locations.ZLinkLocationAutoConnectHost;

final class ZLinkFrameworkRuntimeDrainRouteTest {
    @Test
    void shutdownPublishesTheHostWideTerminalContract() throws Exception {
        DefaultZLinkFrameworkOptions options =
            new DefaultZLinkFrameworkOptions();
        ZLinkFrameworkRuntime runtime = ZLinkFrameworkRuntime.start(
            options,
            new ZLinkJavaBackendAdapterFactory());
        long deadline = System.nanoTime()
            + java.time.Duration.ofSeconds(1).toNanos();
        while (!runtime.isReady() && System.nanoTime() < deadline) {
            Thread.sleep(1);
        }
        CompletableFuture<ZLinkTerminationResult> observed =
            new CompletableFuture<>();
        runtime.observe(1).subscribe(new java.util.concurrent.Flow.Subscriber<>() {
            @Override
            public void onSubscribe(
                java.util.concurrent.Flow.Subscription subscription) {
                subscription.request(Long.MAX_VALUE);
            }

            @Override
            public void onNext(
                systems.zlink.framework.monitoring
                    .ZLinkFrameworkRuntimeEvent event) {
                event.runtime().terminalResult().ifPresent(observed::complete);
            }

            @Override
            public void onError(Throwable throwable) {
                observed.completeExceptionally(throwable);
            }

            @Override
            public void onComplete() {
            }
        });

        ZLinkTerminationResult result = runtime.shutdown(
                java.time.Duration.ofSeconds(1))
            .toCompletableFuture()
            .get(2, TimeUnit.SECONDS);

        assertEquals(ZLinkTerminationIntent.SHUTDOWN, result.effectiveIntent());
        assertEquals(ZLinkTerminationOutcome.STOPPED, result.outcome());
        assertEquals(ZLinkTerminationReason.NONE, result.reason());
        assertEquals(ZLinkFrameworkRuntimeState.STOPPED, runtime.state());
        assertEquals(
            result,
            runtime.snapshot().terminalResult().orElseThrow());
        assertEquals(result, observed.get(1, TimeUnit.SECONDS));
    }

    @Test
    void drainWaiterTimeoutDoesNotCompleteSharedDrainState() {
        CompletableFuture<String> shared = new CompletableFuture<>();

        ZLinkFrameworkRuntime.independentWaiter(shared)
            .toCompletableFuture()
            .orTimeout(1, TimeUnit.MILLISECONDS)
            .exceptionally(ignored -> null)
            .join();

        assertFalse(shared.isDone());
        shared.complete("drained");
        assertEquals("drained", shared.join());
    }

    @Test
    void drainTransferUsesSpotMeshRouteChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMeshChannel("game-spots");
        options.addSpotMesh("game-spots");

        assertEquals(
            "game-spots",
            ZLinkFrameworkRuntime.transferRouteChannelName(
                options.registration(), "game-spots"));
    }

    @Test
    void drainTransferRequiresActorHostCapabilityAndRejectsLocalNode() {
        RoutingId remote = RoutingId.from("play-b");
        ZLinkPeerLocation capable = peer(remote, List.of("actor:player"));
        ZLinkPeerLocation wrongType = peer(RoutingId.from("enemy-a"), List.of("actor:enemy"));
        ZLinkPeerLocation prefixOnly = peer(RoutingId.from("play-a"), List.of("actor:play"));

        assertTrue(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(capable, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(wrongType, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(prefixOnly, "player", Set.of()));
        assertFalse(ZLinkFrameworkRuntime.isEligibleActorHandoffTarget(
            capable, "player", Set.of(remote)));
    }

    private static ZLinkPeerLocation peer(RoutingId nodeRid, List<String> capabilities) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.SPOT_MESH, "game-spots", nodeRid,
            ZLinkLocationRole.SPOT, "", 100, false, 0, Map.of(), capabilities,
            "owner", 1, Instant.now());
    }
}
