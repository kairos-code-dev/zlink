package systems.zlink.framework.spring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.context.annotation.AnnotationConfigApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterFactory;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

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
                    "dealer.connect.inproc://profile-server"),
                backendFactory.calls());
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "create.dealer",
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
    static class SpotNodeConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer spotNodeCustomizer() {
            return options -> options.addSpotMesh("game", mesh ->
                mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
        }
    }

    @Configuration
    static class SpotNodeWithActorConfig {
        @Bean
        ZLinkFrameworkOptionsCustomizer spotNodeWithActorCustomizer() {
            return options -> {
                options.addSpotMesh("game", mesh ->
                    mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
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
                    mesh.addNode("play", node -> node.addSpotFactory(InjectedGameSpot.class)));
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

    static final class HandlerDependency {
        private final String prefix;

        HandlerDependency(String prefix) {
            this.prefix = prefix;
        }

        String format(String value) {
            return prefix + ":" + value;
        }
    }

    public static final class InjectedRequestHandler
        implements systems.zlink.framework.channels.ZLinkRequestHandler<String, String> {
        private final HandlerDependency dependency;

        public InjectedRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<String> handleAsync(
            String request,
            systems.zlink.framework.channels.ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(dependency.format(request));
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

    private static systems.zlink.framework.channels.ZLinkRequestContext requestContext() {
        return new systems.zlink.framework.channels.ZLinkRequestContext() {
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
            public systems.zlink.framework.CancellationToken cancellationToken() {
                return () -> false;
            }
        };
    }
}
