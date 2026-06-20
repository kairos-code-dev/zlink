package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.ConcurrentHashMap;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;
import systems.zlink.framework.registry.ZLinkRegistryTopologyFilter;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.runtime.registry.ZLinkRemoteRegistryQueryClient;

final class DiscoveryScaleoutTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(36_000 + (int) (ProcessHandle.current().pid() % 8_000));

    @Test
    void DSC_008_requestToChannelTrafficSurvivesScaleOutAndScaleIn() throws Exception {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String endpointA = tcpEndpoint();
        String endpointB = tcpEndpoint();
        String endpointC = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = registryOptions(registryPub, registryRouter);
        ZLinkJavaBackendAdapterFactory backendFactory = new ZLinkJavaBackendAdapterFactory();
        clearObservedRequests();
        ZLinkFrameworkRuntime providerA = null;

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 backendFactory,
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkRemoteRegistryQueryClient query =
                 ZLinkRemoteRegistryQueryClient.connect(registryRouter, backendFactory);
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions(registryRouter), backendFactory);
             ZLinkFrameworkRuntime secondClient =
                 RuntimeTestSupport.startFramework(clientOptions(registryRouter), backendFactory)) {
            providerA = RuntimeTestSupport.startFramework(providerOptions(
                    registryRouter,
                    endpointA,
                    "provider-a",
                    ProviderAProbeHandler.class),
                backendFactory);
            awaitReadyEndpoint(query, endpointA);
            assertReplyFromOneOf(client, "DSC-008:a", List.of("api-a:DSC-008:a"));

            TrafficRun traffic = new TrafficRun();
            ExecutorService executor = Executors.newFixedThreadPool(2);
            Future<?> trafficA = executor.submit(() -> runTransitionTraffic(client, "DSC-008:client-a", traffic));
            Future<?> trafficB = executor.submit(() -> runTransitionTraffic(secondClient, "DSC-008:client-b", traffic));
            ZLinkFrameworkRuntime providerB = null;
            ZLinkFrameworkRuntime providerC = null;
            try {
                Thread.sleep(150);
                assertTrue(traffic.observedProviders().stream().allMatch("api-a"::equals),
                    "only provider A should be observed before scale-out");
                providerB = RuntimeTestSupport.startFramework(providerOptions(
                        registryRouter,
                        endpointB,
                        "provider-b",
                        ProviderBProbeHandler.class),
                    backendFactory);
                awaitReadyEndpoint(query, endpointB);
                providerC = RuntimeTestSupport.startFramework(providerOptions(
                        registryRouter,
                        endpointC,
                        "provider-c",
                        ProviderCProbeHandler.class),
                    backendFactory);
                awaitReadyEndpoint(query, endpointC);
                awaitTrafficProviders(traffic, Set.of("api-b", "api-c"));
                awaitNoDuplicateRequestEvidence(traffic.completedRequestIds());
                providerA.close();
                providerA = null;
                awaitEndpointNotReady(query, endpointA);
                Thread.sleep(150);
                traffic.stop();
                trafficA.get();
                trafficB.get();
                awaitNoDuplicateRequestEvidence(traffic.completedRequestIds());
                int apiARequestsBeforeStableScaleIn = observedRequests("api-a").size();
                assertProvidersObserved(
                    List.of(client, secondClient),
                    "DSC-008:in",
                    Set.of("api-b", "api-c"),
                    List.of("api-b", "api-c"));
                assertEquals(apiARequestsBeforeStableScaleIn, observedRequests("api-a").size(),
                    "api-a received requests after scale-in");
            } finally {
                traffic.stop();
                executor.shutdownNow();
                if (providerC != null) {
                    providerC.close();
                }
                if (providerB != null) {
                    providerB.close();
                }
            }

            awaitEndpointNotReady(query, endpointB);
            awaitEndpointNotReady(query, endpointC);
        } finally {
            if (providerA != null) {
                providerA.close();
            }
        }
    }

    @Test
    void DSC_009_sameRoutingIdDifferentEndpointReplacesDiscoveredProvider() {
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String firstEndpoint = tcpEndpoint();
        String replacementEndpoint = tcpEndpoint();
        RoutingId routingId = RoutingId.from("api-a");
        ZLinkEmbeddedRegistryOptions registryOptions = registryOptions(registryPub, registryRouter);
        ZLinkJavaBackendAdapterFactory backendFactory = new ZLinkJavaBackendAdapterFactory();

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
                 registryOptions,
                 backendFactory,
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkRemoteRegistryQueryClient query =
                 ZLinkRemoteRegistryQueryClient.connect(registryRouter, backendFactory);
             ZLinkFrameworkRuntime client =
                 RuntimeTestSupport.startFramework(clientOptions(registryRouter), backendFactory)) {
            try (ZLinkFrameworkRuntime first =
                     RuntimeTestSupport.startFramework(
                         providerOptions(registryRouter, firstEndpoint, routingId, OldProbeHandler.class),
                         backendFactory)) {
                awaitSingleReadyRoutingEndpoint(query, routingId, firstEndpoint);
                assertReplyFromOneOf(client, "DSC-009:first", List.of("old-api:DSC-009:first"));
            }

            awaitEndpointNotReady(query, firstEndpoint);

            try (ZLinkFrameworkRuntime replacement =
                     RuntimeTestSupport.startFramework(
                         providerOptions(registryRouter, replacementEndpoint, routingId, NewProbeHandler.class),
                         backendFactory)) {
                awaitSingleReadyRoutingEndpoint(query, routingId, replacementEndpoint);
                awaitEndpointNotReady(query, firstEndpoint);
                for (int attempt = 0; attempt < 20; attempt++) {
                    assertReplyFromOneOf(
                        client,
                        "DSC-009:replace:" + attempt,
                        List.of("new-api:DSC-009:replace:" + attempt));
                }
            }
        }
    }

    private static DefaultZLinkFrameworkOptions providerOptions(
        String registryRouter,
        String endpoint,
        String routingId,
        Class<? extends ZLinkRequestHandler<String, String>> handlerType) {
        return providerOptions(registryRouter, endpoint, RoutingId.from(routingId), handlerType);
    }

    private static DefaultZLinkFrameworkOptions providerOptions(
        String registryRouter,
        String endpoint,
        RoutingId routingId,
        Class<? extends ZLinkRequestHandler<String, String>> handlerType) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery().addRegistryEndpoint(registryRouter);
        options.addClientServerChannel("profile")
            .enableServer(endpoint)
            .serverRoutingId(routingId)
            .addRequestHandler(handlerType, String.class, String.class, "Probe");
        return options;
    }

    private static DefaultZLinkFrameworkOptions clientOptions(String registryRouter) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.setDefaultTimeout(Duration.ofMillis(150));
        options.useDiscovery().addRegistryEndpoint(registryRouter);
        options.addClientServerChannel("profile").enableClient();
        return options;
    }

    private static ZLinkEmbeddedRegistryOptions registryOptions(String pubEndpoint, String routerEndpoint) {
        ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
        options.setPubEndpoint(pubEndpoint);
        options.setRouterEndpoint(routerEndpoint);
        return options;
    }

    private static void assertReplyFromOneOf(
        ZLinkFrameworkRuntime client,
        String request,
        List<String> expectedReplies) {
        long deadline = System.nanoTime() + Duration.ofSeconds(4).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                String reply = client.client()
                    .requestToChannel("profile", request)
                    .packetName("Probe")
                    .timeout(Duration.ofMillis(150))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();
                assertTrue(expectedReplies.contains(reply),
                    "unexpected DSC reply: " + reply + ", expected " + expectedReplies);
                return;
            } catch (RuntimeException error) {
                lastFailure = error;
                Thread.onSpinWait();
            }
        }
        throw new AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: "
                + "requestToChannel did not reach an expected provider",
            lastFailure);
    }

    private static void assertProvidersObserved(
        List<ZLinkFrameworkRuntime> clients,
        String requestPrefix,
        Set<String> requiredProviders,
        List<String> allowedProviders) {
        Set<String> observedProviders = ConcurrentHashMap.newKeySet();
        List<String> requestIds = new ArrayList<>();
        long deadline = System.nanoTime() + Duration.ofSeconds(6).toNanos();
        int requestIndex = 0;
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline
            && (!observedProviders.containsAll(requiredProviders) || requestIndex < 12)) {
            ZLinkFrameworkRuntime client = clients.get(requestIndex % clients.size());
            String request = requestPrefix + ":" + requestIndex;
            requestIds.add(request);
            try {
                String reply = requestReply(client, request);
                String provider = providerName(reply);
                if (!allowedProviders.isEmpty()) {
                    assertTrue(allowedProviders.contains(provider),
                        "unexpected provider for " + request + ": " + reply);
                }
                observedProviders.add(provider);
            } catch (RuntimeException error) {
                lastFailure = error;
            }
            requestIndex++;
        }
        if (!observedProviders.containsAll(requiredProviders)) {
            throw new AssertionError("HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: "
                + "required providers were not observed: " + requiredProviders
                + ", observed " + observedProviders, lastFailure);
        }
        awaitNoDuplicateRequestEvidence(requestIds);
    }

    private static void runTransitionTraffic(
        ZLinkFrameworkRuntime client,
        String clientId,
        TrafficRun traffic) {
        int requestIndex = 0;
        while (!traffic.stopped()) {
            String request = clientId + ":" + requestIndex++;
            try {
                String reply = requestReply(client, request);
                traffic.record(request, providerName(reply));
            } catch (RuntimeException ignored) {
                // Transient timeout is allowed while discovery is converging; completed requests are
                // checked for exactly-once evidence below.
            }
        }
    }

    private static void awaitTrafficProviders(TrafficRun traffic, Set<String> requiredProviders) {
        long deadline = System.nanoTime() + Duration.ofSeconds(6).toNanos();
        while (System.nanoTime() < deadline) {
            if (traffic.observedProviders().containsAll(requiredProviders)) {
                return;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError("HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: "
            + "transition traffic did not observe providers "
            + requiredProviders + ", observed " + traffic.observedProviders());
    }

    private static String requestReply(ZLinkFrameworkRuntime client, String request) {
        return client.client()
            .requestToChannel("profile", request)
            .packetName("Probe")
            .timeout(Duration.ofMillis(150))
            .submit(String.class)
            .toCompletableFuture()
            .join();
    }

    private static String providerName(String reply) {
        int separator = reply.indexOf(':');
        return separator < 0 ? reply : reply.substring(0, separator);
    }

    private static void assertNoDuplicateRequestEvidence(List<String> requestIds) {
        Map<String, Integer> counts = new HashMap<>();
        for (String provider : List.of("api-a", "api-b", "api-c", "old-api", "new-api")) {
            for (String requestId : observedRequests(provider)) {
                counts.merge(requestId, 1, Integer::sum);
            }
        }
        for (String requestId : requestIds) {
            assertEquals(1, counts.getOrDefault(requestId, 0),
                "request evidence must be recorded by exactly one provider: " + requestId);
        }
    }

    private static void awaitNoDuplicateRequestEvidence(List<String> requestIds) {
        AssertionError lastFailure = null;
        long deadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
        while (System.nanoTime() < deadline) {
            try {
                assertNoDuplicateRequestEvidence(requestIds);
                return;
            } catch (AssertionError error) {
                lastFailure = error;
                Thread.onSpinWait();
            }
        }
        if (lastFailure != null) {
            throw lastFailure;
        }
    }

    private static void awaitReadyEndpoint(
        ZLinkRemoteRegistryQueryClient query,
        String endpoint) {
        awaitTopology(query, endpoint, true);
    }

    private static void awaitEndpointNotReady(
        ZLinkRemoteRegistryQueryClient query,
        String endpoint) {
        awaitTopology(query, endpoint, false);
    }

    private static void awaitSingleReadyRoutingEndpoint(
        ZLinkRemoteRegistryQueryClient query,
        RoutingId routingId,
        String endpoint) {
        long deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                List<ZLinkRegistryTopologyEntry> readyEntries = query.topology(new ZLinkRegistryTopologyFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of("profile"),
                        Optional.of(routingId),
                        Optional.of(TopologyState.READY),
                        Optional.empty()))
                    .toCompletableFuture()
                    .join();
                if (readyEntries.size() == 1 && endpoint.equals(readyEntries.get(0).endpoint())) {
                    return;
                }
            } catch (RuntimeException error) {
                lastFailure = error;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: "
                + "routing id did not resolve to a single ready endpoint: "
                + routingId + " -> " + endpoint,
            lastFailure);
    }

    private static void awaitTopology(
        ZLinkRemoteRegistryQueryClient query,
        String endpoint,
        boolean ready) {
        long deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                boolean isReady = query.topology(new ZLinkRegistryTopologyFilter(
                        Optional.empty(),
                        Optional.empty(),
                        Optional.of(ServiceRole.ROUTER),
                        Optional.of("profile"),
                        Optional.empty(),
                        Optional.empty(),
                        Optional.empty()))
                    .toCompletableFuture()
                    .join()
                    .stream()
                    .filter(entry -> endpoint.equals(entry.endpoint()))
                    .anyMatch(DiscoveryScaleoutTest::isReady);
                if (isReady == ready) {
                    return;
                }
            } catch (RuntimeException error) {
                lastFailure = error;
            }
            Thread.onSpinWait();
        }
        throw new AssertionError(
            "HAR-007 classification-required labels=core-capi,bindings,framework,sample,harness: "
                + "registry topology did not reach expected readiness for " + endpoint,
            lastFailure);
    }

    private static boolean isReady(ZLinkRegistryTopologyEntry entry) {
        return entry.state() == TopologyState.READY;
    }

    private static void recordRequest(String provider, String request) {
        OBSERVED_REQUESTS.computeIfAbsent(provider, ignored -> ConcurrentHashMap.newKeySet()).add(request);
    }

    private static Set<String> observedRequests(String provider) {
        return OBSERVED_REQUESTS.getOrDefault(provider, Set.of());
    }

    private static void clearObservedRequests() {
        OBSERVED_REQUESTS.clear();
    }

    private static final class TrafficRun {
        private final AtomicBoolean stopped = new AtomicBoolean();
        private final Set<String> observedProviders = ConcurrentHashMap.newKeySet();
        private final ConcurrentLinkedQueue<String> completedRequestIds = new ConcurrentLinkedQueue<>();

        boolean stopped() {
            return stopped.get();
        }

        void stop() {
            stopped.set(true);
        }

        void record(String requestId, String provider) {
            completedRequestIds.add(requestId);
            observedProviders.add(provider);
        }

        Set<String> observedProviders() {
            return observedProviders;
        }

        List<String> completedRequestIds() {
            return List.copyOf(completedRequestIds);
        }

    }

    private static String tcpEndpoint() {
        return "tcp://127.0.0.1:" + nextPort();
    }

    private static int nextPort() {
        for (int attempt = 0; attempt < 200; attempt++) {
            int port = NEXT_PORT.getAndIncrement();
            if (isBindable(port)) {
                return port;
            }
        }
        throw new IllegalStateException("failed to allocate tcp port");
    }

    private static boolean isBindable(int port) {
        try (ServerSocket server = new ServerSocket(port)) {
            server.setReuseAddress(false);
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    public static final class ProviderAProbeHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            recordRequest("api-a", request);
            return "api-a:" + request;
        }
    }

    public static final class ProviderBProbeHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            recordRequest("api-b", request);
            return "api-b:" + request;
        }
    }

    public static final class ProviderCProbeHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            recordRequest("api-c", request);
            return "api-c:" + request;
        }
    }

    public static final class OldProbeHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            recordRequest("old-api", request);
            return "old-api:" + request;
        }
    }

    public static final class NewProbeHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            recordRequest("new-api", request);
            return "new-api:" + request;
        }
    }

    private static final ConcurrentHashMap<String, Set<String>> OBSERVED_REQUESTS =
        new ConcurrentHashMap<>();
}
