package systems.zlink.framework.spring;

import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletionException;
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
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkChannelRuntimeOptions;
import systems.zlink.framework.channels.ZLinkFanoutClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.channels.ZLinkRouteMeshRuntimeOptions;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventDispatcher;
import systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler;
import systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime;
import systems.zlink.framework.monitoring.ZLinkSocketEvent;
import systems.zlink.framework.monitoring.ZLinkSocketEventKind;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendAdapterProvider;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.monitoring.DefaultZLinkMonitoringOptions;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.framework.spots.ZLinkSpotPublisherClient;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodec;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.spring.sessionfixtures.SubpackageDiscoveredPacketSession;
import systems.zlink.framework.spring.sessionfixtures.SubpackageSessionPacketAnchor;

final class ZLinkFrameworkAutoConfigurationTest {
    private static final AtomicInteger NEXT_PORT =
        new AtomicInteger(31_000 + (int) (ProcessHandle.current().pid() % 1_000));

    @Test
    void autoConfigurationStartsFrameworkLifecycleAndExposesClientBean() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkFrameworkLifecycle lifecycle =
                context.getBean(ZLinkFrameworkLifecycle.class);
            ZLinkClient client = context.getBean(ZLinkClient.class);
            ZLinkChannelRuntimeOptions runtimeOptions =
                context.getBean(ZLinkChannelRuntimeOptions.class);
            ZLinkFanoutClient fanout = context.getBean(ZLinkFanoutClient.class);
            ZLinkRouteClient route = context.getBean(ZLinkRouteClient.class);
            ZLinkRouteMeshRuntime routeMeshRuntime =
                context.getBean(ZLinkRouteMeshRuntime.class);
            ZLinkRouteMeshRuntimeOptions routeMeshRuntimeOptions =
                context.getBean(ZLinkRouteMeshRuntimeOptions.class);

            assertTrue(lifecycle.isRunning());
            assertInstanceOf(ZLinkFrameworkLifecycle.class, client);
            assertInstanceOf(ZLinkFrameworkLifecycle.class, runtimeOptions);
            assertInstanceOf(ZLinkFrameworkLifecycle.class, fanout);
            assertInstanceOf(ZLinkFrameworkLifecycle.class, route);
            assertInstanceOf(
                systems.zlink.framework.runtime.host.ZLinkRouteMeshRuntimeService.class,
                routeMeshRuntime);
            assertThrows(
                ZLinkConfigurationException.class,
                () -> routeMeshRuntime.snapshot("missing"));
            assertInstanceOf(
                systems.zlink.framework.runtime.host.ZLinkRouteMeshRuntimeOptionsService.class,
                routeMeshRuntimeOptions);
        }
    }

    @Test
    void enableZLinkFrameworkImportsFrameworkAutoConfiguration() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(EnabledTestConfig.class);
            context.refresh();

            assertTrue(context.getBean(ZLinkFrameworkLifecycle.class).isRunning());
            assertInstanceOf(
                ZLinkFrameworkLifecycle.class,
                context.getBean(ZLinkClient.class));
        }
    }

    @Test
    void frameworkConfigurerPassesStreamCompressionToRuntimeRegistration() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                StreamCompressionConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            DefaultZLinkFrameworkOptions options =
                context.getBean(DefaultZLinkFrameworkOptions.class);
            assertSame(
                StreamCompressionConfig.COMPRESSION,
                options.registration().streamCompressionCodec());
        }
    }

    @Test
    void multiTargetClientsThrowConfigurationExceptionWhenChannelIsMissing() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkFanoutClient fanout = context.getBean(ZLinkFanoutClient.class);
            ZLinkRouteClient route = context.getBean(ZLinkRouteClient.class);

            assertThrows(ZLinkConfigurationException.class, () ->
                fanout.publish("missing", "topic", "payload").submit());
            assertThrows(ZLinkConfigurationException.class, () ->
                route.requestToNode("missing", RoutingId.from("target"), "payload"));
        }
    }

    @Test
    void spotAndActorManagersAreNotBeansWithoutSpotNode() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
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
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkActorDirectory.class));
        }
    }

    @Test
    void spotManagerIsBeanWhenSpotNodeExists() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
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
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkActorDirectory.class));
            ZLinkSpotOutbound outbound = context.getBean(ZLinkSpotOutbound.class);
            assertThrows(ZLinkConfigurationException.class, () ->
                outbound.sendToChannel("events", "hello"));
        }
    }

    @Test
    void actorClientAndDirectoryAreBeansWhenSpotNodeAndLocationStoreExist() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                SpotNodeWithLocationStoreConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                ZLinkFrameworkActorClientBean.class,
                context.getBean(ZLinkActorClient.class));
            assertInstanceOf(
                ZLinkFrameworkActorDirectoryBean.class,
                context.getBean(ZLinkActorDirectory.class));
            assertTrue(context.getBean(ZLinkActorDirectory.class)
                .find("missing-actor")
                .toCompletableFuture()
                .join()
                .isEmpty());
            assertThrows(NoSuchBeanDefinitionException.class, () ->
                context.getBean(ZLinkActorManager.class));
        }
    }

    @Test
    void actorManagerIsBeanWhenSpotNodeAndActorFactoryExist() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
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
            assertInstanceOf(
                ZLinkActorDirectory.class,
                context.getBean(ZLinkActorDirectory.class));
        }
    }

    @Test
    void springLifecycleCreatesSpotAndActorFactoryWithSpringDependencyInjection() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                InjectedSpotAndActorConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            context.getBean(ZLinkSpotManager.class)
                .create(InjectedGameSpot.class)
                .toCompletableFuture()
                .join();
            ActorRef actor = context.getBean(ZLinkActorManager.class)
                .create("player-1", "player")
                .toCompletableFuture()
                .join();

            assertEquals("spring:spot", InjectedGameSpot.dependencyValue());
            assertEquals("player-1", actor.actorId());
        }
    }

    @Test
    void springLifecycleFailsWhenSpotCannotBeConstructed() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                PrivateConstructorSpotConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            CompletionException error = assertThrows(CompletionException.class, () ->
                context.getBean(ZLinkSpotManager.class)
                    .create(PrivateConstructorSpot.class)
                    .toCompletableFuture()
                    .join());

            assertInstanceOf(ZLinkConfigurationException.class, error.getCause());
            assertTrue(error.getCause().getMessage().contains("failed to create spot"));
        }
    }

    @Test
    void spotPublisherClientIsBeanOnlyWhenPublisherCapabilityExists() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                SpotPublisherConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertInstanceOf(
                systems.zlink.httpclient.ZLinkFrameworkHttpExecutionTurn.class,
                context.getBean(systems.zlink.httpclient.ZLinkHttpExecutionTurn.class));

            ZLinkSpotPublisherClient publisher =
                context.getBean(ZLinkSpotPublisherClient.class);
            publisher.publish("game", "stage.events", new StageOpened("opened"))
                .submit();
        }
    }

    @Test
    void handlerFactoryCreatesHandlersWithSpringConstructorInjection() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(
                HandlerInjectionConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkHandlerActivator handlerFactory = context.getBean(ZLinkHandlerActivator.class);
            InjectedRequestHandler handler =
                (InjectedRequestHandler) handlerFactory.create(InjectedRequestHandler.class);

            String reply = handler.handle("42", requestContext())
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
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
            context.register(
                AutoDiscoveredSessionPacketConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            backendFactory.dispatchStreamPacket(
                "auto.session.packet",
                Message.from("{\"value\":\"payload\"}".getBytes(StandardCharsets.UTF_8)),
                ZLinkStreamCodec.JSON);

            context.getBean("sessionPacketHandled", CompletableFuture.class)
                .get(2, TimeUnit.SECONDS);
            assertEquals(1, context.getBean(AtomicInteger.class).get());
        }
    }

    @Test
    void springLifecycleAutoDiscoversSessionPacketHandlersFromApplicationSubpackages()
        throws Exception {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
            context.register(
                AutoDiscoveredSessionPacketSubpackageConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            backendFactory.dispatchStreamPacket(
                "subpackage.session.packet",
                Message.from("{\"value\":\"payload\"}".getBytes(StandardCharsets.UTF_8)),
                ZLinkStreamCodec.JSON);

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
                .requestToChannel("profile", new GetProfileRequest("42"))
                .submit(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("profile:42"), reply);
            assertEquals(
                0,
                context.getBean(AnnotatedInjectedRequestHandler.class).requestCount(),
                "ZLink-managed handlers are created per dispatch, not reused from the root singleton bean");
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
                .requestToChannel("profile", new DecorateProfileRequest("42"))
                .submit(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("profile:42:decorated"), reply);
            assertTrue(context.getBean(ZLinkFrameworkLifecycle.class).isRunning());
        }
    }

    @Test
    void scannedHandlersAndSetDependenciesAreSpringBeans() {
        String endpoint = "inproc://zlink-spring-auto-registered-set-" + UUID.randomUUID();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean("autoRegisteredSetEndpoint", String.class, () -> endpoint);
            context.register(
                AutoRegisteredSetHandlerConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ProfileReply reply = context.getBean(ZLinkClient.class)
                .requestToChannel("profile", new DecorateProfileSetRequest("42"))
                .submit(ProfileReply.class)
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
                .requestToChannel("profile", new FilteredProfileRequest("42"))
                .submit(ProfileReply.class)
                .toCompletableFuture()
                .join();

            assertEquals(new ProfileReply("filter:profile:42"), reply);
        }
    }

    @Test
    void routeMeshExplicitHandlersAreCreatedThroughSpringDependencyInjection() {
        String sourceEndpoint = tcpEndpoint();
        String targetEndpoint = tcpEndpoint();
        RoutingId sourceRid = RoutingId.from("spring-route-source");
        RoutingId targetRid = RoutingId.from("spring-route-target");

        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(RouteMeshEndpoints.class, () ->
                new RouteMeshEndpoints(sourceEndpoint, targetEndpoint, targetRid));
            context.register(
                RouteMeshHandlerConfig.class,
                ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            try (AnnotationConfigApplicationContext sourceContext =
                     new AnnotationConfigApplicationContext()) {
                sourceContext.registerBean(
                    ZLinkBackendAdapterProvider.class,
                    ZLinkJavaBackendAdapterFactory::new);
                sourceContext.registerBean(
                    ZLinkFrameworkConfigurer.class,
                    () -> options -> { var channel = options.addRouteMeshChannel("route"); channel.enableServer(sourceEndpoint);
                        channel.setRoutingId(sourceRid);
                        channel.enableClient(targetEndpoint); });
                sourceContext.register(
                    SourceRouteMeshConfig.class,
                    ZLinkFrameworkAutoConfiguration.class);
                sourceContext.refresh();

                String reply = sourceContext.getBean(ZLinkRouteClient.class)
                    .requestToNode("route", targetRid, new SpringRouteRequest("hello"))
                    .timeout(Duration.ofSeconds(3))
                    .submit(String.class)
                    .toCompletableFuture()
                    .join();

                assertEquals("route:hello", reply);
            }
        }
    }

    @Test
    void runtimeEventDispatcherIsAlwaysRegistered() {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
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
                ZLinkBackendAdapterProvider.class,
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
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
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
    void monitoringOptionsBeanExistsWithoutCustomizerAndDoesNotOpenBackend() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
            context.register(TestConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            assertTrue(context.getBean(ZLinkMonitoringLifecycle.class).isRunning());
            assertTrue(context.getBean(DefaultZLinkMonitoringOptions.class)
                .socketSourceNames()
                .isEmpty());
            assertFalse(backendFactory.calls().contains("factory.monitoring"));
        }
    }

    @Test
    void locationStoreBeanConfiguresFrameworkLocationRuntime() throws Exception {
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(
                ZLinkBackendAdapterProvider.class,
                FakeZLinkBackendAdapterFactory::new);
            context.register(LocationStoreBeanConfig.class, ZLinkFrameworkAutoConfiguration.class);
            context.refresh();

            ZLinkLocationRuntimeQuery query = context
                .getBean(ZLinkFrameworkLifecycle.class)
                .monitoringLocationRuntimeQuery();
            ZLinkLocationRuntimeStatus status = query.getStatus()
                .toCompletableFuture()
                .get(2, TimeUnit.SECONDS);

            assertTrue(status.ownerLeaseHealthy());
        }
    }

    @Test
    void autoConfigurationAppliesCustomizersBeforeRuntimeStarts() {
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        try (AnnotationConfigApplicationContext context =
                 new AnnotationConfigApplicationContext()) {
            context.registerBean(ZLinkBackendAdapterProvider.class, () -> backendFactory);
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
    @EnableZLinkFramework
    static class TestConfig {
        @Bean
        ZLinkFrameworkConfigurer profileChannelConfigurer() {
            return options -> { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class LocationStoreBeanConfig {
        @Bean
        ZLinkInMemoryLocationStore locationStore() {
            return new ZLinkInMemoryLocationStore();
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class StreamCompressionConfig {
        static final ZLinkStreamCompressionCodec COMPRESSION =
            new ZLinkStreamCompressionCodec() {
                @Override
                public byte[] compress(byte[] payload) {
                    return payload;
                }

                @Override
                public byte[] decompress(byte[] payload, int maxDecompressedSize) {
                    return payload;
                }
            };

        @Bean
        ZLinkFrameworkConfigurer streamCompressionConfigurer() {
            return options -> options.configureStreamCompression().use(COMPRESSION);
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class EnabledTestConfig {
        @Bean
        ZLinkFrameworkConfigurer profileChannelConfigurer() {
            return options -> { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };
        }
    }

    @Configuration
    @EnableZLinkFramework
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
        ZLinkFrameworkConfigurer monitoredServerChannelConfigurer() {
            return options -> { var channel = options.addRouteMeshChannel("profile"); channel.enableServer("inproc://profile-monitor");
                channel.enableClient("inproc://profile-monitor"); };
        }

        @Bean
        ZLinkMonitoringOptionsCustomizer socketMonitoringCustomizer() {
            return options -> options.addSocketEvents(
                "profile",
                ZLinkSocketEventKind.CONNECTED);
        }
    }

    @Configuration
    @EnableZLinkFramework
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
        AutoDiscoveredSessionPacketHandler autoDiscoveredSessionPacketHandler(
            AtomicInteger sessionPacketCount,
            CompletableFuture<Void> sessionPacketHandled) {
            return new AutoDiscoveredSessionPacketHandler(
                sessionPacketCount,
                sessionPacketHandled);
        }

        @Bean
        ZLinkFrameworkConfigurer autoDiscoveredSessionPacketConfigurer() {
            return options -> { var stream = options.addStreamNode("client.stream"); stream.bind("inproc://auto-discovered-session");
                stream.registerSession(AutoDiscoveredPacketSession.class); };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class AutoDiscoveredSessionPacketSubpackageConfig {
        @Bean
        AtomicInteger sessionPacketCount() {
            return new AtomicInteger();
        }

        @Bean
        CompletableFuture<Void> sessionPacketHandled() {
            return new CompletableFuture<>();
        }

        @Bean
        ZLinkFrameworkConfigurer autoDiscoveredSessionPacketConfigurer() {
            return options -> {
                options.addHandlersFromPackageOf(SubpackageSessionPacketAnchor.class);
                var stream = options.addStreamNode("client.stream");
                stream.bind("inproc://auto-discovered-session-subpackage");
                stream.registerSession(SubpackageDiscoveredPacketSession.class);
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class SpotNodeConfig {
        @Bean
        ZLinkFrameworkConfigurer spotNodeConfigurer() {
            return options -> { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                    node.addSpotFactory(GameSpot.class); }; };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class SpotNodeWithActorConfig {
        @Bean
        ZLinkFrameworkConfigurer spotNodeWithActorConfigurer() {
            return options -> {
                { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                        node.addSpotFactory(GameSpot.class);
                        node.addActorFactory("player", PlayerActorFactory.class); }; };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class SpotNodeWithLocationStoreConfig {
        @Bean
        ZLinkInMemoryLocationStore locationStore() {
            return new ZLinkInMemoryLocationStore();
        }

        @Bean
        ZLinkFrameworkConfigurer spotNodeWithLocationStoreConfigurer() {
            return options -> {
                var mesh = options.addSpotMesh("game");
                mesh.enableRouter("inproc://play-router");
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class PrivateConstructorSpotConfig {
        @Bean
        ZLinkFrameworkConfigurer privateConstructorSpotConfigurer() {
            return options -> { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                    node.addSpotFactory(PrivateConstructorSpot.class); }; };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class InjectedSpotAndActorConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("spring");
        }

        @Bean
        ZLinkFrameworkConfigurer injectedSpotAndActorConfigurer() {
            return options -> {
                { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                        node.addSpotFactory(InjectedGameSpot.class);
                        node.addActorFactory("player", InjectedPlayerActorFactory.class); }; };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class SpotPublisherConfig {
        @Bean
        ZLinkFrameworkConfigurer spotPublisherConfigurer() {
            return options -> {
                var mesh = options.addSpotMesh("game");
                mesh.enableRouter("inproc://spot-router");
                mesh.enablePubSub("inproc://spot-pub");
                mesh.addSpotFactory(GameSpot.class);
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class HandlerInjectionConfig {
        @Bean
        HandlerDependency handlerDependency() {
            return new HandlerDependency("profile");
        }
    }

    @Configuration
    @EnableZLinkFramework
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
        ZLinkFrameworkConfigurer scannedHandlerConfigurer(String springAnnotatedEndpoint) {
            return options -> {
                options.addHandlersFromPackageOf(ScannedHandlerConfig.class);
                { var channel = options.addClientServerChannel("profile").enableServer(springAnnotatedEndpoint);
            channel.enableClient(springAnnotatedEndpoint);
                    channel.addHandlerGroup("spring-scanned"); };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class AutoRegisteredHandlerConfig {
        @Bean
        HandlerDependency autoRegisteredDependency() {
            return new HandlerDependency("profile");
        }

        @Bean
        ZLinkFrameworkConfigurer autoRegisteredHandlerConfigurer(
            String autoRegisteredEndpoint) {
            return options -> {
                options.addHandlersFromPackageOf(AutoRegisteredHandlerConfig.class);
                { var channel = options.addClientServerChannel("profile").enableServer(autoRegisteredEndpoint);
            channel.enableClient(autoRegisteredEndpoint);
                    channel.addHandlerGroup("spring-auto-registered"); };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class AutoRegisteredSetHandlerConfig {
        @Bean
        HandlerDependency autoRegisteredSetDependency() {
            return new HandlerDependency("profile");
        }

        @Bean
        ZLinkFrameworkConfigurer autoRegisteredSetHandlerConfigurer(
            String autoRegisteredSetEndpoint) {
            return options -> {
                options.addHandlersFromPackageOf(AutoRegisteredSetHandlerConfig.class);
                { var channel = options.addClientServerChannel("profile").enableServer(autoRegisteredSetEndpoint);
            channel.enableClient(autoRegisteredSetEndpoint);
                    channel.addHandlerGroup("spring-auto-registered-set"); };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
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
        ZLinkFrameworkConfigurer filteredHandlerConfigurer(String filteredEndpoint) {
            return options -> {
                options.useFilter(SpringInjectedReplyFilter.class);
                { var channel = options.addClientServerChannel("profile").enableServer(filteredEndpoint);
            channel.enableClient(filteredEndpoint);
                    channel.addRequestHandler(
                        InjectedProfileRequestHandler.class,
                        FilteredProfileRequest.class,
                        ProfileReply.class,
                        "FilteredProfile"); };
            };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class RouteMeshHandlerConfig {
        @Bean
        HandlerDependency routeHandlerDependency() {
            return new HandlerDependency("route");
        }

        @Bean
        ZLinkFrameworkConfigurer routeMeshHandlerConfigurer(
            RouteMeshEndpoints endpoints) {
            return options -> { var channel = options.addRouteMeshChannel("route"); channel.enableServer(endpoints.targetEndpoint());
                channel.setRoutingId(endpoints.targetRid());
                channel.enableClient(endpoints.sourceEndpoint());
                channel.addRequestHandler(
                    InjectedRouteRequestHandler.class,
                    SpringRouteRequest.class,
                    String.class,
                    "SpringRoute"); };
        }
    }

    @Configuration
    @EnableZLinkFramework
    static class SourceRouteMeshConfig {
    }

    public static final class GameSpot implements ZLinkSpot<ZLinkActor> {
        private final ZLinkSpotContext context;

        public GameSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class InjectedGameSpot implements ZLinkSpot<ZLinkActor> {
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

        @Override
        public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class PrivateConstructorSpot implements ZLinkSpot<ZLinkActor> {
        private final ZLinkSpotContext context;

        private PrivateConstructorSpot(ZLinkSpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSpotContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
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
        public CompletionStage<ZLinkActor> create(
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
        public CompletionStage<ZLinkActor> create(
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
        public void handle(ZLinkSocketEvent event) {
            count.incrementAndGet();
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
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            return dispatcher.tryHandle(context, dispatch, payload).thenApply(ignored -> null);
        }
    }

    public static final class AutoDiscoveredSessionPacketHandler
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, AutoSessionPacket> {
        private final AtomicInteger count;
        private final CompletableFuture<Void> handled;

        public AutoDiscoveredSessionPacketHandler(
            AtomicInteger count,
            CompletableFuture<Void> handled) {
            this.count = count;
            this.handled = handled;
        }

        @Override
        public Class<AutoSessionPacket> messageType() {
            return AutoSessionPacket.class;
        }

        @Override
        public CompletionStage<Void> handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            AutoSessionPacket payload) {
            count.incrementAndGet();
            handled.complete(null);
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkPacket("auto.session.packet")
    public record AutoSessionPacket(String value) {
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
        public CompletionStage<String> handle(
            String request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(dependency.format(request));
        }
    }

    public static final class InjectedProfileRequestHandler
        implements ZLinkRequestHandler<FilteredProfileRequest, ProfileReply> {
        private final HandlerDependency dependency;

        public InjectedProfileRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<ProfileReply> handle(
            FilteredProfileRequest request,
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
        public <T> CompletionStage<T> invoke(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            return next.invoke().thenApply(reply -> {
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
        public CompletionStage<ProfileReply> handle(GetProfileRequest request) {
            requestCount++;
            return CompletableFuture.completedFuture(
                new ProfileReply(dependency.format(request.profileId())));
        }

        int requestCount() {
            return requestCount;
        }
    }

    @ZLinkPacket("StageOpened")
    public record StageOpened(String value) { }

    @ZLinkPacket("GetProfile")
    public record GetProfileRequest(String profileId) { }

    @ZLinkPacket("DecorateProfile")
    public record DecorateProfileRequest(String profileId) { }

    @ZLinkPacket("DecorateProfileSet")
    public record DecorateProfileSetRequest(String profileId) { }

    @ZLinkPacket("FilteredProfile")
    public record FilteredProfileRequest(String profileId) { }

    @ZLinkPacket("SpringRoute")
    public record SpringRouteRequest(String value) { }

    public record ProfileReply(String value) {
    }

    record RouteMeshEndpoints(
        String sourceEndpoint,
        String targetEndpoint,
        RoutingId targetRid) {
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
        public CompletionStage<ProfileReply> handle(DecorateProfileRequest request) {
            String value = dependency.format(request.profileId());
            for (ProfileDecorator decorator : decorators) {
                value = decorator.decorate(value);
            }
            return CompletableFuture.completedFuture(new ProfileReply(value));
        }
    }

    @ZLinkHandlerGroup("spring-auto-registered-set")
    public static final class AutoRegisteredSetRequestHandler {
        private final HandlerDependency dependency;
        private final Set<ProfileDecorator> decorators;

        public AutoRegisteredSetRequestHandler(
            HandlerDependency dependency,
            Set<ProfileDecorator> decorators) {
            this.dependency = dependency;
            this.decorators = decorators;
        }

        @ZLinkRequest(packetName = "DecorateProfileSet")
        public CompletionStage<ProfileReply> handle(DecorateProfileSetRequest request) {
            String value = dependency.format(request.profileId());
            for (ProfileDecorator decorator : decorators) {
                value = decorator.decorate(value);
            }
            return CompletableFuture.completedFuture(new ProfileReply(value));
        }
    }

    public static final class InjectedRouteRequestHandler
        implements ZLinkRouteRequestHandler<SpringRouteRequest, String> {
        private final HandlerDependency dependency;

        public InjectedRouteRequestHandler(HandlerDependency dependency) {
            this.dependency = dependency;
        }

        @Override
        public CompletionStage<String> handle(
            SpringRouteRequest request,
            ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture(dependency.format(request.value()));
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
            public java.util.Map<String, String> metadata() {
                return java.util.Map.of();
            }
        };
    }
}
