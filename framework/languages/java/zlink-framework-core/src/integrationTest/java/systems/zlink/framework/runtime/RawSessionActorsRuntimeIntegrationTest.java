package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class RawSessionActorsRuntimeIntegrationTest {
    @Test
    void sessionGateway_relaysRemoteActorRequestThroughDiscoveredSpotMesh() throws Exception {
        SessionActorsRuntimeIntegrationTest.actorRelayRequests.clear();
        Zlink.version();
        String actorId =
            SessionActorsRuntimeIntegrationTest.uniqueActorId("raw-player");
        RoutingId sessionRid = RoutingId.from(
            SessionActorsRuntimeIntegrationTest.uniqueActorId("raw-session"));
        RoutingId playNodeRid = RoutingId.from(
            SessionActorsRuntimeIntegrationTest.uniqueActorId("raw-play-node"));
        RoutingId sessionNodeRid = RoutingId.from(
            SessionActorsRuntimeIntegrationTest.uniqueActorId("raw-session-node"));
        String registryPub = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String registryRouter = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String playRouter = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String playPub = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String sessionRouter = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String sessionPub = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        String streamEndpoint = SessionActorsRuntimeIntegrationTest.tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime play =
                 SessionActorsRuntimeIntegrationTest.startDiscoveredPlayRuntime(
                     registryRouter,
                     playRouter,
                     playPub,
                     false,
                     playNodeRid);
             ZLinkFrameworkRuntime session =
                 SessionActorsRuntimeIntegrationTest.startDiscoveredSessionRuntime(
                     registryRouter,
                     sessionRouter,
                     sessionPub,
                     streamEndpoint,
                     sessionNodeRid)) {
            var actor = play.actorManager()
                .createAsync(actorId, "player")
                .toCompletableFuture()
                .join();
            var joined = actor.context()
                .joinEntrySpot(playNodeRid)
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            ZLinkSessionActor bound = session.sessionActors(
                    "gateway",
                    sessionRid)
                .bindAsync(joined)
                .toCompletableFuture()
                .join();

            assertEquals(
                actorId + ":raw-hello",
                SessionActorsRuntimeIntegrationTest.relayUntilActorReceived(
                    bound,
                    new ZLinkStreamHeader(
                        "ActorEcho",
                        java.util.Map.of(),
                        Optional.of(1L)),
                    "raw-hello".getBytes(java.nio.charset.StandardCharsets.UTF_8),
                    actorId + ":raw-hello",
                    10,
                    TimeUnit.SECONDS));
        }
    }
}
