package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.channels.ZLinkRouteSendContext;
import systems.zlink.framework.channels.ZLinkRouteSendHandler;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.handlers.ZLinkPublish;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.runtime.diagnostics.ZLinkDispatchErrorReporter;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkDispatchMode;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorEvent;
import systems.zlink.framework.configuration.ZLinkMessageDispatchErrorObserver;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamError;

final class DefaultZLinkFrameworkOptionsTest {
    @Test
    void addClientServerChannelRejectsDuplicateChannelName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile");  };

        assertThrows(
            ZLinkConfigurationException.class,
            () -> { var channel = options.addFanoutChannel("profile");  });
    }

    @Test
    void setDefaultRequestTimeoutRejectsZero() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.setDefaultRequestTimeout(Duration.ZERO));
    }

    @Test
    void globalConfigurationMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.codecs().addSerializer("application/x-test", new ZLinkJsonMessageSerializer());
        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var metadata = options.configureMetadata(); metadata.addForwardedMetadataKey("trace-id"); };
        options.useFilter(TestFilter.class);
        { var dispatch = options.configureDispatch(); dispatch.setSpotDispatchMode(ZLinkDispatchMode.DYNAMIC);
            dispatch.traceSampleRate(0.25d); };
        options.addSpotRemoteAddressResolver(TestSpotRemoteAddressResolver.class);
        { var registry = options.useRegistrySpotRemoteAddresses("game"); registry.setRouterChannelId("spot-router"); };

        assertTrue(options.registration().codecs().serializers().containsKey("application/x-test"));
        assertTrue(options.registration().handlerPackageMarkers()
            .contains(DefaultZLinkFrameworkOptionsTest.class));
        assertTrue(options.registration().metadataPolicy().forwardedApplicationKeys()
            .contains("trace-id"));
        assertTrue(options.registration().filters().contains(TestFilter.class));
        assertEquals(ZLinkDispatchMode.DYNAMIC,
            options.registration().dispatchOptions().spotDispatchMode());
        assertEquals(0.25d,
            options.registration().dispatchOptions().diagnostics().sampleRate());
        assertEquals(TestSpotRemoteAddressResolver.class,
            options.registration().spotRemoteAddressResolverType());
        assertEquals("game",
            options.registration().registrySpotRemoteAddresses().namespaceName());
        assertEquals("spot-router",
            options.registration().registrySpotRemoteAddresses().routerChannelId());
    }

    @Test
    void singleRegisteredSerializerProvidesDefaultStreamCodec() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.codecs().addSerializer("application/x-test", new ZLinkJsonMessageSerializer(), ignored -> true);
        options.codecs().addStreamCodec("application/x-test", ZLinkStreamCodec.PROTOBUF);

        assertEquals(
            ZLinkStreamCodec.PROTOBUF,
            options.registration().codecs().streamCodecForCustomSerializer().orElseThrow());
    }

    @Test
    void configureDispatchRejectsReplyErrorForSendAndPublish() {
        DefaultZLinkFrameworkOptions send = new DefaultZLinkFrameworkOptions();
        { var dispatch = send.configureDispatch(); dispatch.unhandled().setSend(ZLinkUnhandledDispatchAction.REPLY_ERROR); };

        assertThrows(ZLinkConfigurationException.class, send::validate);

        DefaultZLinkFrameworkOptions publish = new DefaultZLinkFrameworkOptions();
        { var dispatch = publish.configureDispatch(); dispatch.unhandled().setPublish(ZLinkUnhandledDispatchAction.REPLY_ERROR); };

        assertThrows(ZLinkConfigurationException.class, publish::validate);
    }

    @Test
    void configureDispatchRejectsInvalidDiagnosticsSampleRate() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch(); dispatch.traceSampleRate(1.1d); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void configureDispatchRegistersMessageDispatchErrorObserver() {
        DefaultZLinkFrameworkOptions byType = new DefaultZLinkFrameworkOptions();
        { var dispatch = byType.configureDispatch();
            dispatch.setMessageDispatchErrorObserver(TestDispatchErrorObserver.class); };

        assertEquals(
            TestDispatchErrorObserver.class,
            byType.registration().dispatchOptions().messageDispatchErrorObserverType());

        DefaultZLinkFrameworkOptions byInstance = new DefaultZLinkFrameworkOptions();
        TestDispatchErrorObserver observer = new TestDispatchErrorObserver();
        { var dispatch = byInstance.configureDispatch();
            dispatch.setMessageDispatchErrorObserver(observer); };

        assertEquals(
            observer,
            byInstance.registration().dispatchOptions().messageDispatchErrorObserver());
    }

    @Test
    void dispatchErrorReporterIsolatesObserverFailures() {
        ZLinkMessageDispatchErrorEvent error = new ZLinkMessageDispatchErrorEvent(
            ZLinkDispatchErrorSurface.CHANNEL,
            ZLinkDispatchMessageKind.REQUEST,
            ZLinkDispatchErrorReason.HANDLER_MISSING,
            ZLinkDispatchErrorAction.REPLY_ERROR,
            "missing",
            "profile",
            null,
            null,
            null,
            null,
            "corr-1",
            null);

        DefaultZLinkFrameworkOptions callbackFailure = new DefaultZLinkFrameworkOptions();
        { var dispatch = callbackFailure.configureDispatch();
            dispatch.setMessageDispatchErrorObserver(new ThrowingDispatchErrorObserver()); };
        ZLinkDispatchErrorReporter callbackReporter = new ZLinkDispatchErrorReporter(
            callbackFailure.registration().dispatchOptions(),
            ZLinkHandlerFactory.reflection(),
            Runnable::run);

        assertDoesNotThrow(() -> callbackReporter.report(error));
        assertEquals(1, callbackReporter.reportedCount());
        assertEquals(1, callbackReporter.observerFailureCount());

        DefaultZLinkFrameworkOptions factoryFailure = new DefaultZLinkFrameworkOptions();
        { var dispatch = factoryFailure.configureDispatch();
            dispatch.setMessageDispatchErrorObserver(TestDispatchErrorObserver.class); };
        ZLinkDispatchErrorReporter factoryReporter = new ZLinkDispatchErrorReporter(
            factoryFailure.registration().dispatchOptions(),
            ignored -> { throw new IllegalStateException("factory failed"); },
            Runnable::run);

        assertDoesNotThrow(() -> factoryReporter.report(error));
        assertEquals(1, factoryReporter.reportedCount());
        assertEquals(1, factoryReporter.observerFailureCount());

        DefaultZLinkFrameworkOptions noObserver = new DefaultZLinkFrameworkOptions();
        ZLinkDispatchErrorReporter noObserverReporter = new ZLinkDispatchErrorReporter(
            noObserver.registration().dispatchOptions(),
            ZLinkHandlerFactory.reflection(),
            Runnable::run);
        assertDoesNotThrow(() -> noObserverReporter.report(error));
        assertEquals(1, noObserverReporter.reportedCount());
        assertEquals(0, noObserverReporter.observerFailureCount());
    }

    @Test
    void dispatchErrorReporterDefersObserverConstructionToExecutor() {
        ZLinkMessageDispatchErrorEvent error = new ZLinkMessageDispatchErrorEvent(
            ZLinkDispatchErrorSurface.CHANNEL,
            ZLinkDispatchMessageKind.REQUEST,
            ZLinkDispatchErrorReason.HANDLER_MISSING,
            ZLinkDispatchErrorAction.REPLY_ERROR,
            "missing",
            "profile",
            null,
            null,
            null,
            null,
            "corr-1",
            null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageDispatchErrorObserver(TestDispatchErrorObserver.class); };
        List<Runnable> queued = new ArrayList<>();
        AtomicBoolean factoryCalled = new AtomicBoolean(false);
        ZLinkDispatchErrorReporter reporter = new ZLinkDispatchErrorReporter(
            options.registration().dispatchOptions(),
            ignored -> {
                factoryCalled.set(true);
                throw new IllegalStateException("factory failed");
            },
            queued::add);

        assertDoesNotThrow(() -> reporter.report(error));
        assertEquals(1, reporter.reportedCount());
        assertEquals(0, reporter.observerFailureCount());
        assertEquals(1, queued.size());
        assertEquals(false, factoryCalled.get());

        queued.get(0).run();

        assertTrue(factoryCalled.get());
        assertEquals(1, reporter.observerFailureCount());
    }

    @Test
    void clientServerChannelClientWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile"); channel.enableClient(); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };

        options.validate();
    }

    @Test
    void routeMeshClientWithManualConnectionDoesNotRequireBindEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("play"); channel.enableClient("inproc://play-a"); };

        options.validate();
    }

    @Test
    void registrySpotRemoteAddressesRejectsCustomResolverDuplicate() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("inproc://registry"); };
        { var channel = options.addRouteMesh("play");  };
        options.addSpotRemoteAddressResolver(TestSpotRemoteAddressResolver.class);
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void spotMeshUseRegistrySpotResolverUsesMeshNamespace() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("rooms")
            .useRegistrySpotResolver()
            ;

        assertEquals("rooms", options.registration().registrySpotRemoteAddresses().namespaceName());
    }

    @Test
    void registrySpotRemoteAddressesRequiresDiscoveryEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("play");  };
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void registrySpotRemoteAddressesRequiresRouteMeshChannel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("inproc://registry"); };
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void registrySpotRemoteAddressesRequiresRouterChannelWhenAmbiguous() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("inproc://registry"); };
        { var channel = options.addRouteMesh("play-a");  };
        { var channel = options.addRouteMesh("play-b");  };
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void spotPublisherClientUsesSpotMeshPubSubNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enablePubSub("inproc://publisher");}; };

        options.validate();
        assertEquals("game", options.registration().spotNodes().get(0).meshName());
    }

    @Test
    void spotRouterAndPubSubManualConnectionsMutateRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        RoutingId nodeRid =
            RoutingId.from("spot-node-1");
        RoutingId pubSubRid =
            RoutingId.from("spot-pub-1");

        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.setRouterRoutingId(nodeRid).connectRouter("inproc://spot-router-peer");
                node.setPubSubRoutingId(pubSubRid).connectPeerPub("inproc://spot-pub-peer"); }; };

        options.validate();
        assertEquals(nodeRid, options.registration().spotNodes().get(0).routerRoutingId());
        assertEquals(pubSubRid, options.registration().spotNodes().get(0).pubSubRoutingId());
        assertEquals(nodeRid, options.registration().spotNodes().get(0).nodeRoutingId());
        assertEquals(
            "inproc://spot-router-peer",
            options.registration().spotNodes().get(0).routerManualConnections().get(0).endpoint());
        assertEquals(
            List.of("inproc://spot-pub-peer"),
            options.registration().spotNodes().get(0).pubSubManualConnections());
    }

    @Test
    void spotRouterAndPubSubManualConnectionsRejectBlankEndpoint() {
        DefaultZLinkFrameworkOptions router = new DefaultZLinkFrameworkOptions();
        assertThrows(ZLinkConfigurationException.class, () ->
            { var mesh = router.addSpotMesh("game"); { var node = mesh; node.connectRouter(" "); }; });

        DefaultZLinkFrameworkOptions pubSub = new DefaultZLinkFrameworkOptions();
        assertThrows(ZLinkConfigurationException.class, () ->
            { var mesh = pubSub.addSpotMesh("game"); { var node = mesh; node.connectPeerPub(" "); }; });
    }

    @Test
    void spotNodeRejectsConflictingRouterAndPubSubRoutingIds() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.setRouterRoutingId(
                        RoutingId.from("node-a"));
                node.setPubSubRoutingId(
                        RoutingId.from("node-b")); }; };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void entrySpotRoutingIdMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        RoutingId entryRid =
            RoutingId.from("entry-spot");

        { var mesh = options.addSpotMesh("game"); { var node = mesh; var entry = node.configureEntrySpot(); entry.setRoutingId(entryRid); }; };

        options.validate();
        assertEquals(entryRid, options.registration().spotNodes().get(0).entrySpotRoutingId());
    }

    @Test
    void entrySpotRoutingIdRejectsNull() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            { var mesh = options.addSpotMesh("game"); { var node = mesh; var entry = node.configureEntrySpot(); entry.setRoutingId(null); }; });
    }

    @Test
    void clientServerChannelClientManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var channel = options.addClientServerChannel("profile"); channel.enableClient("inproc://profile-server"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void clientServerChannelServerRejectsBlankEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class,
            () -> options.addClientServerChannel("profile").enableServer(" "));
    }

    @Test
    void clientServerChannelServerWithoutRequestHandlerIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsMappedGroupAndExplicitRequestDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addHandlerGroup("scanned-request");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class); };

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate client/server request handler packet name"));
    }

    @Test
    void clientServerChannelRejectsDuplicateSendHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addSendHandler(SendHandler.class, String.class);
            channel.addSendHandler(SendHandler.class, String.class); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithOnlySendHandlerIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addSendHandler(SendHandler.class, String.class, "Notify"); };

        options.validate();
    }

    @Test
    void clientServerChannelRejectsBlankRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () -> { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, " "); });
    }

    @Test
    void clientServerChannelServerWithBindIsAccepted() {
        DefaultZLinkFrameworkOptions accepted = new DefaultZLinkFrameworkOptions();
        { var channel = accepted.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addRequestHandler(AnnotatedEchoHandler.class, AnnotatedPacket.class, String.class); };

        accepted.validate();
    }

    @Test
    void fanoutChannelPublisherRejectsBlankEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class,
            () -> options.addFanoutChannel("events").enablePublisher(" "));
    }

    @Test
    void fanoutChannelSubscriberWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber();
            channel.addPublishHandler(EventHandler.class, String.class, "Event"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelSubscriberManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber("inproc://events");
            channel.addPublishHandler(EventHandler.class, String.class, "Event"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void fanoutChannelRejectsDuplicatePublishHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber("inproc://events");
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
            channel.addPublishHandler(EventHandler.class, String.class, "Event"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelRejectsMappedGroupAndExplicitPublishDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber("inproc://events");
            channel.addHandlerGroup("scanned-publish");
            channel.addPublishHandler(EventHandler.class, String.class); };

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate fanout publish handler packet name"));
    }

    @Test
    void fanoutChannelAcceptsMappedAttributedPublishHandlerLikeDotnet() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber("inproc://events");
            channel.addHandlerGroup("scanned-attributed-publish"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshChannelWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("route"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"); };
        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsMappedGroupAndExplicitRequestDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route");
            channel.addHandlerGroup("scanned-route");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class); };

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate route mesh request handler packet name"));
    }

    @Test
    void routeMeshChannelRejectsDuplicateSendHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer");
            channel.addSendHandler(RouteSendHandler.class, String.class);
            channel.addSendHandler(RouteSendHandler.class, String.class); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsSendAndRequestWithSamePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMesh("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer");
            channel.addSendHandler(RouteSendHandler.class, String.class, "Notify");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Notify"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void channelDeduplicatesHandlerGroupsLikeDotnet() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addHandlerGroup("scanned-request");
            channel.addHandlerGroup("scanned-request"); };

        options.validate();
        assertEquals(
            List.of("scanned-request"),
            options.registration().channels().get(0).handlerGroups());
    }

    @Test
    void channelRejectsUnknownMappedHandlerGroup() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addHandlerGroup("missing-group"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelRejectsRequestHandlerGroup() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber("inproc://events");
            channel.addHandlerGroup("scanned-request"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithScannedHandlerGroupIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addHandlerGroup("scanned-request"); };

        options.validate();
    }

    @Test
    void clientServerChannelServerWithRepeatedScannedHandlerGroupIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addClientServerChannel("profile").enableServer("inproc://profile-server");
            channel.addHandlerGroup("scanned-secondary"); };

        options.validate();
    }

    @Test
    void streamNodeRejectsMultipleSessionTypes() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
                stream.registerSession(GameSession.class);
                stream.registerSession(GameSession.class); });
    }

    @Test
    void streamNodeActorGatewayRequiresConfiguredSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeActorGatewayRequiresRouterSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var mesh = options.addSpotMesh("game"); { var node = mesh;  }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeWithActorGatewayAndRouterSpotNodeIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router");
                node.addSpotFactory(TestSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class); };

        options.validate();
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return request;
        }
    }

    @ZLinkPacket("AnnotatedEcho")
    public record AnnotatedPacket(String value) {
    }

    public static final class AnnotatedEchoHandler
        implements ZLinkRequestHandler<AnnotatedPacket, String> {
        @Override
        public String handle(
            AnnotatedPacket request,
            ZLinkRequestContext context) {
            return request.value();
        }
    }

    @ZLinkHandlerGroup("scanned-publish")
    public static final class EventHandler implements ZLinkPublishHandler<String> {
        @Override
        public void handle(String message, ZLinkPublishContext context) {
        }
    }

    @ZLinkHandlerGroup("scanned-attributed-publish")
    public static final class AttributedEventHandler {
        @ZLinkPublish(packetName = "AttributedEvent")
        public void handle(
            String message,
            ZLinkPublishContext context) {
        }
    }

    public static final class SendHandler implements ZLinkSendHandler<String> {
        @Override
        public void handle(String message, ZLinkSendContext context) {
        }
    }

    @ZLinkHandlerGroup("scanned-request")
    public static final class ScannedRequestHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public String handle(String request, ZLinkRequestContext context) {
            return request;
        }
    }

    @ZLinkHandlerGroup("scanned-primary")
    @ZLinkHandlerGroup("scanned-secondary")
    public static final class MultiGroupScannedRequestHandler
        implements ZLinkRequestHandler<Integer, Integer> {
        @Override
        public Integer handle(Integer request, ZLinkRequestContext context) {
            return request;
        }
    }

    public static final class RouteSendHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public void handle(String message, ZLinkRouteSendContext context) {
        }
    }

    @ZLinkHandlerGroup("scanned-route")
    public static final class RouteEchoHandler
        implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public String handle(
            String request,
            ZLinkRouteRequestContext context) {
            return request;
        }
    }

    public static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class TestFilter implements ZLinkHandlerFilter {
        @Override
        public <T> CompletionStage<T> invokeAsync(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            return next.invokeAsync();
        }
    }

    public static final class TestSpotRemoteAddressResolver
        implements ZLinkSpotRemoteAddressResolver {
        @Override
        public CompletionStage<ZLinkSpotRemoteAddress> resolveSpotRemoteAddressAsync(RoutingId spotRid) {
            return CompletableFuture.completedFuture(
                new ZLinkSpotRemoteAddress(
                    "play",
                    RoutingId.from("node"),
                    spotRid,
                    ZLinkSpotKind.USER));
        }
    }

    public static final class TestDispatchErrorObserver
        implements ZLinkMessageDispatchErrorObserver {
        @Override
        public CompletionStage<Void> onDispatchError(ZLinkMessageDispatchErrorEvent error) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ThrowingDispatchErrorObserver
        implements ZLinkMessageDispatchErrorObserver {
        @Override
        public CompletionStage<Void> onDispatchError(ZLinkMessageDispatchErrorEvent error) {
            throw new IllegalStateException("observer failed");
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public void onConnected() {
        }

        @Override
        public void onDisconnected() {
        }

        @Override
        public void onError(ZLinkStreamError error) {
        }
    }
}
