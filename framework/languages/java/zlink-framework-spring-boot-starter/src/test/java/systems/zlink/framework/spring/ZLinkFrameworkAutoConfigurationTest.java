package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.monitoring.ZLinkRegistryEvent;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class ZLinkFrameworkAutoConfigurationTest {
    @Test
    void autoConfigurationStartsFrameworkLifecycleAndExposesClientBean() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkFrameworkLifecycle lifecycle =
                context.getBean(ZLinkFrameworkLifecycle.class);
            ZLinkClient client = context.getBean(ZLinkClient.class);
            ZLinkFanoutClient fanout = context.getBean(ZLinkFanoutClient.class);
            ZLinkRouteClient route = context.getBean(ZLinkRouteClient.class);

            assertTrue(lifecycle.isRunning());
            assertInstanceOf(ZLinkFrameworkLifecycle.class, client);
            assertInstanceOf(ZLinkFrameworkLifecycle.class, fanout);
            assertInstanceOf(ZLinkFrameworkLifecycle.class, route);
        }
    }

    @Test
    void multiTargetClientsThrowConfigurationExceptionWhenChannelIsMissing() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkFanoutClient fanout = context.getBean(ZLinkFanoutClient.class);
            ZLinkRouteClient route = context.getBean(ZLinkRouteClient.class);

            assertThrows(ZLinkConfigurationException.class, () ->
                fanout.publish("missing", "topic", "payload").submitAsync());
            assertThrows(ZLinkConfigurationException.class, () ->
                route.requestTo("missing", RoutingId.from("target"), "payload"));
        }
    }

    @Test
    void spotAndActorManagersAreNotBeansWithoutSpotNode() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkSpotManager.class));
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkSpotOutbound.class));
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkSpotPublisherClient.class));
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkActorManager.class));
        }
    }

    @Test
    void spotManagerIsBeanWhenSpotNodeExists() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                SpotNodeConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                ZLinkSpotManager.class,
                context.getBean(ZLinkSpotManager.class));
            assertInstanceOf(
                ZLinkSpotOutbound.class,
                context.getBean(ZLinkSpotOutbound.class));
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkSpotPublisherClient.class));
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkActorManager.class));
            ZLinkSpotOutbound outbound = context.getBean(ZLinkSpotOutbound.class);
            assertThrows(ZLinkConfigurationException.class, () ->
                outbound.sendToChannel("events", "hello"));
        }
    }

    @Test
    void actorManagerIsBeanWhenSpotNodeAndActorFactoryExist() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                SpotNodeWithActorConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                ZLinkSpotManager.class,
                context.getBean(ZLinkSpotManager.class));
            assertInstanceOf(
                ZLinkActorManager.class,
                context.getBean(ZLinkActorManager.class));
        }
    }

    @Test
    void springLifecycleCreatesSpotAndActorFactoryWithSpringDependencyInjection() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                InjectedSpotAndActorConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            context.getBean(ZLinkSpotManager.class)
                .createAsync(InjectedGameSpot.class)
                .toCompletableFuture()
                .join();
            ZLinkActor actor = context.getBean(ZLinkActorManager.class)
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            assertEquals("spring:spot", InjectedGameSpot.dependencyValue());
            assertInstanceOf(InjectedPlayerActor.class, actor);
            assertEquals("spring:player-1", ((InjectedPlayerActor) actor).dependencyValue());
        }
    }

    @Test
    void spotPublisherClientIsBeanOnlyWhenPublisherCapabilityExists() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                SpotPublisherConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkSpotPublisherClient publisher =
                context.getBean(ZLinkSpotPublisherClient.class);
            publisher.publishSpot("game.stage", "stage.events", "opened")
                .packetName("StageOpened")
                .submitAsync()
                .toCompletableFuture()
                .join();
        }
    }

    @Test
    void handlerFactoryCreatesHandlersWithSpringConstructorInjection() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                HandlerInjectionConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkHandlerFactory handlerFactory = context.getBean(ZLinkHandlerFactory.class);
            InjectedRequestHandler handler =
                (InjectedRequestHandler) handlerFactory.create(InjectedRequestHandler.class);

            String reply = handler.handleAsync("42", requestContext())
                .toCompletableFuture()
                .join();

            assertEquals("profile:42", reply);
        }
    }

    @Test
    void springLifecycleAutoDiscoversSessionPacketHandlersForSessionDispatcher()
        throws Exception {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(
                AutoDiscoveredSessionPacketConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            backendFactory.dispatchStreamPacket("auto.session.packet", "payload");

            context.getBean("sessionPacketHandled", CompletableFuture.class)
                .get(2, TimeUnit.SECONDS);
            assertEquals(1, context.getBean(AtomicInteger.class).get());
        }
    }

    @Test
    void annotatedHandlerGroupHandlesRequestsInsideSpringLifecycle() {
        String endpoint = "inproc://zlink-spring-annotated-" + UUID.randomUUID();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean("springAnnotatedEndpoint", String.class, () -> endpoint);
            context.register(
                ScannedHandlerConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ProfileReply reply = context.getBean(ZLinkClient.class)
                .requestToChannel("profile", new ProfileRequest("42"))
                .packetName("GetProfile")
                .submitAsync(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("profile:42"), reply);
            assertEquals(
                1,
                context.getBean(AnnotatedInjectedRequestHandler.class).requestCount());
            assertTrue(context.getBean(ZLinkFrameworkLifecycle.class).isRunning());
        }
    }

    @Test
    void scannedHandlersAndCollectionDependenciesAreSpringBeans() {
        String endpoint = "inproc://zlink-spring-auto-registered-" + UUID.randomUUID();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean("autoRegisteredEndpoint", String.class, () -> endpoint);
            context.register(
                AutoRegisteredHandlerConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ProfileReply reply = context.getBean(ZLinkClient.class)
                .requestToChannel("profile", new ProfileRequest("42"))
                .packetName("DecorateProfile")
                .submitAsync(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("profile:42:decorated"), reply);
            assertTrue(context.getBean(ZLinkFrameworkLifecycle.class).isRunning());
        }
    }

    @Test
    void handlerFiltersAreCreatedThroughSpringDependencyInjection() {
        String endpoint = "inproc://zlink-spring-filtered-" + UUID.randomUUID();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean("filteredEndpoint", String.class, () -> endpoint);
            context.register(
                FilteredHandlerConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ProfileReply reply = context.getBean(ZLinkClient.class)
                .requestToChannel("profile", new ProfileRequest("42"))
                .packetName("FilteredProfile")
                .submitAsync(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("filter:profile:42"), reply);
        }
    }

    @Test
    void runtimeEventDispatcherIsAlwaysRegistered() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                ZLinkRuntimeEventDispatcher.class,
                context.getBean(ZLinkRuntimeEventDispatcher.class));
        }
    }

    @Test
    void autoConfigurationKeepsUserRuntimeEventDispatcher() {
        ZLinkRuntimeEventDispatcher dispatcher = new ZLinkRuntimeEventDispatcher();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.registerBean(ZLinkRuntimeEventDispatcher.class, () -> dispatcher);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertEquals(dispatcher, context.getBean(ZLinkRuntimeEventDispatcher.class));
        }
    }

    @Test
    void monitoringCustomizerStartsLifecycleAndRegistersRuntimeEventHandlers() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(MonitoringConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertTrue(context.getBean(ZLinkMonitoringLifecycle.class).isRunning());
            assertTrue(backendFactory.calls().contains("factory.monitoring"));
            assertTrue(backendFactory.calls().contains("monitoring.open.router"));
            assertTrue(backendFactory.calls().contains("socketMonitor.onEvent"));

            context.getBean(ZLinkRuntimeEventDispatcher.class).publish(new ZLinkSocketEvent(
                "profile",
                Instant.EPOCH,
                ZLinkSocketEventKind.CONNECTED,
                Optional.empty(),
                "local",
                "remote",
                Optional.empty()));

            assertEquals(1, context.getBean(AtomicInteger.class).get());
        }

        List<String> calls = backendFactory.calls();
        assertTrue(calls.indexOf("close.socketMonitor") < calls.indexOf("close.router"));
        assertTrue(calls.indexOf("close.socketMonitor") < calls.indexOf("close.context"));
    }

    @Test
    void registryMonitoringUsesConfiguredSourceNameAsEventLabel()
        throws Exception {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterFactory.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(RegistryMonitoringConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            CompletableFuture<?> registryEventSource =
                context.getBean("registryEventSource", CompletableFuture.class);
            String sourceName = (String) registryEventSource.get(2, TimeUnit.SECONDS);

            assertEquals("ops-registry", sourceName);
            assertTrue(context.getBean(ZLinkMonitoringLifecycle.class).isRunning());
        }
    }

    @Test
    void autoConfigurationAppliesCustomizersBeforeRuntimeStarts() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterFactory.class, () -> backendFactory);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertEquals(
                List.of(
                    "factory.channel",
                    "create.context",
                    "create.dealer",
                    "dealer.setChannelName.profile",
                    "dealer.connect.inproc://profile-server"),
                backendFactory.calls());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
                "dealer.setChannelName.profile",
                "dealer.connect.inproc://profile-server",
                "close.dealer",
                "close.context"),
            backendFactory.calls());
    }

    @Configuration
    static class TestConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer profileChannelCustomizer() {
            return options -> options.addClientServerChannel("profile", channel ->
                channel.enableClient(client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server"))));
        }
    }

    @Configuration
    static class MonitoringConfig {
        @Bean
        AtomicInteger socketEventCount() {
            return new AtomicInteger();
        }

        @Bean
        SocketEventCounter socketEventCounter(AtomicInteger socketEventCount) {
            return new SocketEventCounter(socketEventCount);
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer monitoredServerChannelCustomizer() {
            return options -> options.addRouteMeshChannel("profile", channel -> {
                channel.bind("inproc://profile-monitor");
                channel.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-monitor"));
            });
        }

        @Bean
        ZLinkMonitoringOptionsCustomizer socketMonitoringCustomizer() {
            return options -> options.addSocketEvents(
                "profile",
                ZLinkSocketEventKind.CONNECTED);
        }
    }

    @Configuration
    static class RegistryMonitoringConfig {
        @Bean
        ZLinkEmbeddedRegistryOptions zlinkEmbeddedRegistryOptions() {
            ZLinkEmbeddedRegistryOptions options = new ZLinkEmbeddedRegistryOptions();
            options.setPubEndpoint("inproc://registry-monitor-pub");
            options.setRouterEndpoint("inproc://registry-monitor-router");
            return options;
        }

        @Bean
        CompletableFuture<String> registryEventSource() {
            return new CompletableFuture<>();
        }

        @Bean
        RegistryEventSourceRecorder registryEventSourceRecorder(
            CompletableFuture<String> registryEventSource) {
            return new RegistryEventSourceRecorder(registryEventSource);
        }

        @Bean
        ZLinkMonitoringOptionsCustomizer registryMonitoringCustomizer() {
            return options -> options.addRegistryEvents(
                "ops-registry",
                Duration.ofMillis(100));
        }
    }

    @Configuration
    static class AutoDiscoveredSessionPacketConfig {
        @Bean
        AtomicInteger sessionPacketCount() {
            return new AtomicInteger();
        }

        @Bean
        CompletableFuture<Void> sessionPacketHandled() {
            return new CompletableFuture<>();
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer autoDiscoveredSessionPacketCustomizer() {
            return options -> options.addStreamNode("client.stream", stream -> {
                stream.bind("inproc://auto-discovered-session");
                stream.registerSession(AutoDiscoveredPacketSession.class);
            });
        }
    }

    @Configuration
    static class SpotNodeConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer spotNodeCustomizer() {
            return options -> options.addSpotMesh("game", mesh ->
                mesh.addNode("play", node -> {
                    node.enableRouter();
                    node.addSpotFactory(GameSpot.class);
                }));
        }
    }

    @Configuration
    static class SpotNodeWithActorConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer spotNodeWithActorCustomizer() {
            return options -> {
                options.addSpotMesh("game", mesh ->
                    mesh.addNode("play", node -> {
                        node.enableRouter();
                        node.addSpotFactory(GameSpot.class);
                    }));
                options.addActorFactory("player", PlayerActorFactory.class);
            };
        }
    }

    @Configuration
    static class InjectedSpotAndActorConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("spring");
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer injectedSpotAndActorCustomizer() {
            return options -> {
                options.addSpotMesh("game", mesh ->
                    mesh.addNode("play", node -> {
                        node.enableRouter();
                        node.addSpotFactory(InjectedGameSpot.class);
                    }));
                options.addActorFactory("player", InjectedPlayerActorFactory.class);
            };
        }
    }

    @Configuration
    static class SpotPublisherConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer spotPublisherCustomizer() {
            return options -> options.addSpotMesh("game", mesh ->
                mesh.addNode("publisher", node -> {
                    node.enablePubSub();
                    node.attachSpotPublisherClient("game.stage");
                }));
        }
    }

    @Configuration
    static class HandlerInjectionConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("profile");
        }
    }

    @Configuration
    static class ScannedHandlerConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("profile");
        }

        @Bean
        AnnotatedInjectedRequestHandler annotatedInjectedRequestHandler(
            HandlerDependency dependency) {
            return new AnnotatedInjectedRequestHandler(dependency);
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer scannedHandlerCustomizer(String springAnnotatedEndpoint) {
            return options -> {
                options.codecs().addJson();
                options.addHandlersFromPackageOf(ScannedHandlerConfig.class);
                options.addClientServerChannel("profile", channel -> {
                    channel.enableServer(server -> server.bind(springAnnotatedEndpoint));
                    channel.enableClient(client -> client.useManualConnections(
                        endpoints -> endpoints.connect(springAnnotatedEndpoint)));
                    channel.addHandlerGroup("spring-scanned");
                });
            };
        }
    }

    @Configuration
    static class AutoRegisteredHandlerConfig {
        @Bean
        HandlerDependency autoRegisteredDependency() {
            return new HandlerDependency("profile");
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer autoRegisteredHandlerCustomizer(
            String autoRegisteredEndpoint) {
            return options -> {
                options.codecs().addJson();
                options.addHandlersFromPackageOf(AutoRegisteredHandlerConfig.class);
                options.addClientServerChannel("profile", channel -> {
                    channel.enableServer(server -> server.bind(autoRegisteredEndpoint));
                    channel.enableClient(client -> client.useManualConnections(
                        endpoints -> endpoints.connect(autoRegisteredEndpoint)));
                    channel.addHandlerGroup("spring-auto-registered");
                });
            };
        }
    }

    @Configuration
    static class FilteredHandlerConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("profile");
        }

        @Bean
        FilterDependency filterDependency() {
            return new FilterDependency("filter");
        }

        @Bean
        SpringInjectedReplyFilter springInjectedReplyFilter(FilterDependency dependency) {
            return new SpringInjectedReplyFilter(dependency);
        }

        @Bean
        ZLinkFrameworkOptionsCustomizer filteredHandlerCustomizer(String filteredEndpoint) {
            return options -> {
                options.codecs().addJson();
                options.useFilter(SpringInjectedReplyFilter.class);
                options.addClientServerChannel("profile", channel -> {
                    channel.enableServer(server -> server.bind(filteredEndpoint));
                    channel.enableClient(client -> client.useManualConnections(
                        endpoints -> endpoints.connect(filteredEndpoint)));
                    channel.addRequestHandler(
                        InjectedProfileRequestHandler.class,
                        ProfileRequest.class,
                        ProfileReply.class,
                        "FilteredProfile");
                });
            };
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        private final ZLinkSpotContext context;

        public GameSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }
    }

    public static final class InjectedGameSpot implements ZLinkSpot {
        private static final AtomicReference<String> DEPENDENCY_VALUE =
            new AtomicReference<>();
        private final ZLinkSpotContext context;

        public InjectedGameSpot(ZLinkSpotContext context, HandlerDependency dependency) {
            this.context = context;
            DEPENDENCY_VALUE.set(dependency.format("spot"));
        }

        static String dependencyValue() {
            return DEPENDENCY_VALUE.get();
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }
    }

    public static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;

        PlayerActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }
    }

    public static final class InjectedPlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;
        private final String dependencyValue;

        InjectedPlayerActor(
            String actorId,
            ZLinkActorContext context,
            String dependencyValue) {
            this.actorId = actorId;
            this.context = context;
            this.dependencyValue = dependencyValue;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }

        String dependencyValue() {
            return dependencyValue;
        }
    }

    public static final class PlayerActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> createAsync(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class InjectedPlayerActorFactory implements ZLinkActorFactory {
        private final HandlerDependency dependency;

        public InjectedPlayerActorFactory(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<ZLinkActor> createAsync(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new InjectedPlayerActor(
                actorId,
                context,
                dependency.format(actorId)));
        }
    }

    public static final class SocketEventCounter
        implements ZLinkRuntimeEventHandler<ZLinkSocketEvent> {
        private final AtomicInteger count;

        SocketEventCounter(AtomicInteger count) {
            this.count = count;
        }

        @Override
        public CompletionStage<Void> handleAsync(ZLinkSocketEvent event) {
            count.incrementAndGet();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class RegistryEventSourceRecorder
        implements ZLinkRuntimeEventHandler<ZLinkRegistryEvent> {
        private final CompletableFuture<String> sourceName;

        RegistryEventSourceRecorder(CompletableFuture<String> sourceName) {
            this.sourceName = sourceName;
        }

        @Override
        public CompletionStage<Void> handleAsync(ZLinkRegistryEvent event) {
            sourceName.complete(event.sourceName());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class AutoDiscoveredPacketSession implements ZLinkSession {
        private final ZLinkSessionContext context;
        private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher;

        public AutoDiscoveredPacketSession(
            ZLinkSessionContext context,
            ZLinkSessionPacketDispatcher<ZLinkSessionContext> dispatcher) {
            this.context = context;
            this.dispatcher = dispatcher;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatchAsync(
            ZLinkStreamHeader header,
            Message payload) {
            return dispatcher.tryHandleAsync(context, header, payload)
                .thenApply(ignored -> null);
        }
    }

    public static final class AutoDiscoveredSessionPacketHandler
        implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
        private final AtomicInteger count;
        private final CompletableFuture<Void> handled;

        public AutoDiscoveredSessionPacketHandler(
            AtomicInteger count,
            CompletableFuture<Void> handled) {
            this.count = count;
            this.handled = handled;
        }

        @Override
        public String packetName() {
            return "auto.session.packet";
        }

        @Override
        public CompletionStage<Void> handleAsync(
            ZLinkSessionContext context,
            ZLinkStreamHeader header,
            Message payload) {
            count.incrementAndGet();
            handled.complete(null);
            return CompletableFuture.completedFuture(null);
        }
    }

    static final class HandlerDependency {
        private final String prefix;

        HandlerDependency(String prefix) {
            this.prefix = prefix;
        }

        String format(String value) {
            return prefix + ":" + value;
        }
    }

    static final class FilterDependency {
        private final String prefix;

        FilterDependency(String prefix) {
            this.prefix = prefix;
        }

        String decorate(ProfileReply reply) {
            return prefix + ":" + reply.value();
        }
    }

    public static final class InjectedRequestHandler
        implements ZLinkRequestHandler<String, String> {
        private final HandlerDependency dependency;

        public InjectedRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(dependency.format(request));
        }
    }

    public static final class InjectedProfileRequestHandler
        implements ZLinkRequestHandler<ProfileRequest, ProfileReply> {
        private final HandlerDependency dependency;

        public InjectedProfileRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<ProfileReply> handleAsync(
            ProfileRequest request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(
                new ProfileReply(dependency.format(request.profileId())));
        }
    }

    public static final class SpringInjectedReplyFilter implements ZLinkHandlerFilter {
        private final FilterDependency dependency;

        public SpringInjectedReplyFilter(FilterDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public <T> CompletionStage<T> invokeAsync(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            return next.invokeAsync().thenApply(reply -> {
                @SuppressWarnings("unchecked")
                T decorated = (T) new ProfileReply(dependency.decorate((ProfileReply) reply));
                return decorated;
            });
        }
    }

    @ZLinkHandlerGroup("spring-scanned")
    public static final class AnnotatedInjectedRequestHandler {
        private final HandlerDependency dependency;
        private int requestCount;

        public AnnotatedInjectedRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @ZLinkRequest(packetName = "GetProfile")
        public CompletionStage<ProfileReply> handleAsync(ProfileRequest request) {
            requestCount++;
            return CompletableFuture.completedFuture(
                new ProfileReply(dependency.format(request.profileId())));
        }

        int requestCount() {
            return requestCount;
        }
    }

    public record ProfileRequest(String profileId) {
    }

    public record ProfileReply(String value) {
    }

    interface ProfileDecorator {
        String decorate(String value);
    }

    public static final class ProfileSuffixDecorator implements ProfileDecorator {
        @Override
        public String decorate(String value) {
            return value + ":decorated";
        }
    }

    @ZLinkHandlerGroup("spring-auto-registered")
    public static final class AutoRegisteredRequestHandler {
        private final HandlerDependency dependency;
        private final List<ProfileDecorator> decorators;

        public AutoRegisteredRequestHandler(
            HandlerDependency dependency,
            List<ProfileDecorator> decorators) {
            this.dependency = dependency;
            this.decorators = decorators;
        }

        @ZLinkRequest(packetName = "DecorateProfile")
        public CompletionStage<ProfileReply> handleAsync(ProfileRequest request) {
            String value = dependency.format(request.profileId());
            for (ProfileDecorator decorator : decorators) {
                value = decorator.decorate(value);
            }
            return CompletableFuture.completedFuture(new ProfileReply(value));
        }
    }

    private static ZLinkRequestContext requestContext() {
        return new ZLinkRequestContext() {
            @Override
            public java.util.Optional<String> channelName() {
                return java.util.Optional.of("profile");
            }

            @Override
            public java.util.Optional<String> packetName() {
                return java.util.Optional.of("GetProfile");
            }

            @Override
            public java.util.Optional<String> contentType() {
                return java.util.Optional.empty();
            }

            @Override
            public CancellationToken cancellationToken() {
                return () -> false;
            }
        };
    }
}
