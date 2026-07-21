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
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.configuration.ZLinkDispatchErrorAction;
import systems.zlink.framework.configuration.ZLinkDispatchErrorReason;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkDispatchFailure;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowObserver;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamError;

final class DefaultZLinkFrameworkOptionsTest {
    @Test
    void routeMeshBuilderRegistersOneMeshNodeWithChannelsAndPeers() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        var mesh = options.addRouteMesh("game")
            .listen("inproc://game");
        mesh.channelName("orders").setWeight(2);
        mesh.peerConnections().connect(
            RoutingId.from("game-2"),
            "inproc://game-2");

        options.validate();

        var registration = options.registration().meshNodes().getFirst();
        assertEquals("game", registration.meshName());
        assertEquals("inproc://game", registration.bindEndpoint());
        assertEquals(List.of("orders"), registration.channelNames());
        assertEquals(
            RoutingId.from("game-2"),
            registration.peers().getFirst().expectedRoutingId());
    }

    @Test
    void routeMeshChannelWeightAcceptsContractBoundaries() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        var mesh = options.addRouteMesh("game");

        assertDoesNotThrow(() -> mesh.channelName("disabled").setWeight(0));
        assertDoesNotThrow(() -> mesh.channelName("maximum").setWeight(100));
    }

    @Test
    void routeMeshChannelWeightRejectsValuesOutsideContractRange() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        var mesh = options.addRouteMesh("game");

        assertThrows(
            ZLinkConfigurationException.class,
            () -> mesh.channelName("negative").setWeight(-1));
        assertThrows(
            ZLinkConfigurationException.class,
            () -> mesh.channelName("too-large").setWeight(101));
    }

    @Test
    void routeMeshBuilderRejectsDuplicateMeshName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addRouteMesh("game").listen("inproc://game");

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addRouteMesh("game"));
    }

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

        options.codecs().use(codecs ->
            codecs.addSerializer("application/x-test", new ZLinkJsonMessageSerializer()));
        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var metadata = options.configureMetadata();
            metadata.allowSessionToActor("session-trace")
                .allowActorToSession("actor-trace"); };
        options.useFilter(TestFilter.class);
        options.configureDispatch().traceSampleRate(0.25d);

        assertTrue(options.registration().codecs().serializers().containsKey("application/x-test"));
        assertTrue(options.registration().handlerPackageMarkers()
            .contains(DefaultZLinkFrameworkOptionsTest.class));
        assertTrue(options.registration().metadataPolicy().sessionToActorKeys()
            .contains("session-trace"));
        assertTrue(options.registration().metadataPolicy().actorToSessionKeys()
            .contains("actor-trace"));
        assertTrue(options.registration().filters().contains(TestFilter.class));
        assertEquals(0.25d,
            options.registration().dispatchOptions().diagnostics().sampleRate());
    }

    @Test
    void locationStoreConfigurationMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions instance = new DefaultZLinkFrameworkOptions();
        ZLinkLocationStore store = new ZLinkInMemoryLocationStore();
        instance.addLocationStore(store);
        instance.configureLocations().setListPageSize(64);
        assertEquals(store, instance.registration().locations().storeInstance());
        assertEquals(64, instance.registration().locations().options().listPageSize());
    }

    @Test
    void singleRegisteredSerializerProvidesDefaultStreamCodec() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.codecs().use(codecs -> {
            codecs.addSerializer("application/x-test", new ZLinkJsonMessageSerializer(), ignored -> true);
            codecs.addStreamCodec("application/x-test", ZLinkStreamCodec.PROTOBUF);
        });

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
    void configureDispatchRegistersMessageFlowObserver() {
        DefaultZLinkFrameworkOptions byType = new DefaultZLinkFrameworkOptions();
        { var dispatch = byType.configureDispatch();
            dispatch.setMessageFlowObserver(TestMessageFlowObserver.class); };

        assertEquals(
            TestMessageFlowObserver.class,
            byType.registration().dispatchOptions().messageFlowObserverType());

        DefaultZLinkFrameworkOptions byInstance = new DefaultZLinkFrameworkOptions();
        TestMessageFlowObserver observer = new TestMessageFlowObserver();
        { var dispatch = byInstance.configureDispatch();
            dispatch.setMessageFlowObserver(observer); };

        assertEquals(
            observer,
            byInstance.registration().dispatchOptions().messageFlowObserver());
    }

    @Test
    void dispatchErrorReporterIsolatesObserverFailures() {
        ZLinkDispatchFailure error = new ZLinkDispatchFailure(
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
            null,
            null);

        DefaultZLinkFrameworkOptions callbackFailure = new DefaultZLinkFrameworkOptions();
        { var dispatch = callbackFailure.configureDispatch();
            dispatch.setMessageFlowObserver(new ThrowingMessageFlowObserver()); };
        ZLinkDispatchErrorReporter callbackReporter = new ZLinkDispatchErrorReporter(
            callbackFailure.registration().dispatchOptions(),
            ZLinkHandlerActivator.reflection(),
            Runnable::run);

        assertDoesNotThrow(() -> callbackReporter.report(error));
        assertEquals(1, callbackReporter.reportedCount());
        assertEquals(1, callbackReporter.observerFailureCount());

        DefaultZLinkFrameworkOptions factoryFailure = new DefaultZLinkFrameworkOptions();
        { var dispatch = factoryFailure.configureDispatch();
            dispatch.setMessageFlowObserver(TestMessageFlowObserver.class); };
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
            ZLinkHandlerActivator.reflection(),
            Runnable::run);
        assertDoesNotThrow(() -> noObserverReporter.report(error));
        assertEquals(1, noObserverReporter.reportedCount());
        assertEquals(0, noObserverReporter.observerFailureCount());
    }

    @Test
    void dispatchErrorReporterDefersObserverConstructionToExecutor() {
        ZLinkDispatchFailure error = new ZLinkDispatchFailure(
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
            null,
            null);
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var dispatch = options.configureDispatch();
            dispatch.setMessageFlowObserver(TestMessageFlowObserver.class); };
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
    void clientServerChannelClientWithoutManualConnectionUsesLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addLocationStore(new ZLinkInMemoryLocationStore());
        { var channel = options.addClientServerChannel("profile"); channel.enableClient(); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshClientWithManualConnectionDoesNotRequireBindEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMeshChannel("play"); channel.enableClient("inproc://play-a"); };

        options.validate();
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

        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.setRoutingId(nodeRid).enableRouter("inproc://spot-router-bind")
                    .connectRouter("inproc://spot-router-peer");
                node.enablePubSub("inproc://spot-pub-bind");
                node.connectPeerPub("inproc://spot-pub-peer"); }; };

        options.validate();
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
    void spotNodeRejectsReplacingRoutingId() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.setRoutingId(
                        RoutingId.from("node-a"));
                assertThrows(ZLinkConfigurationException.class, () -> node.setRoutingId(
                        RoutingId.from("node-b"))); }; };
    }

    @Test
    void entrySpotRoutingIdMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        RoutingId entryRid =
            RoutingId.from("entry-spot");

        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://entry-router");
                var entry = node.configureEntrySpot(); entry.setRoutingId(entryRid); }; };

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
    void clientServerChannelClientManualConnectionsAreAcceptedWithoutLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

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
    void fanoutChannelSubscriberWithoutManualConnectionUsesLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addLocationStore(new ZLinkInMemoryLocationStore());
        { var channel = options.addFanoutChannel("events"); channel.enableSubscriber();
            channel.addPublishHandler(EventHandler.class, String.class, "Event"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void fanoutChannelSubscriberManualConnectionsAreAcceptedWithoutLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

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

        { var channel = options.addRouteMeshChannel("route"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMeshChannel("route"); channel.enableClient(); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelClientWithoutManualConnectionUsesLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addLocationStore(new ZLinkInMemoryLocationStore());
        { var channel = options.addRouteMeshChannel("route"); channel.enableClient(); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshChannelManualConnectionsAreAcceptedWithoutLocationAutoConnect() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMeshChannel("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer"); };

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMeshChannel("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo"); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsMappedGroupAndExplicitRequestDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        { var channel = options.addRouteMeshChannel("route"); channel.enableServer("inproc://route");
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

        { var channel = options.addRouteMeshChannel("route"); channel.enableServer("inproc://route");
            channel.enableClient("inproc://route-peer");
            channel.addSendHandler(RouteSendHandler.class, String.class);
            channel.addSendHandler(RouteSendHandler.class, String.class); };

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsSendAndRequestWithSamePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        { var channel = options.addRouteMeshChannel("route"); channel.enableServer("inproc://route");
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
    void streamNodeRejectsBlankTlsServerPaths() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            { var stream = options.addStreamNode("gateway"); stream.bind("tls://127.0.0.1:1");
                stream.setTlsServer("", "server.key"); });
        assertThrows(ZLinkConfigurationException.class, () ->
            { var stream = options.addStreamNode("gateway2"); stream.bind("tls://127.0.0.1:2");
                stream.setTlsServer("server.crt", ""); });
    }

    @Test
    void streamActorDispatchRequiresConfiguredRouteMesh() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMesh("game").listen("inproc://play-mesh");
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.enableActorDispatch("game");
            stream.registerSession(GameSession.class); }

        options.validate();

        DefaultZLinkFrameworkOptions missing = new DefaultZLinkFrameworkOptions();
        { var stream = missing.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.enableActorDispatch("missing");
            stream.registerSession(GameSession.class); }
        assertThrows(ZLinkConfigurationException.class, missing::validate);
    }

    @Test
    void spotNodeRequiresAtLeastOneBoundCapability() {
        DefaultZLinkFrameworkOptions empty = new DefaultZLinkFrameworkOptions();
        empty.addSpotMesh("empty");
        assertThrows(ZLinkConfigurationException.class, empty::validate);

        DefaultZLinkFrameworkOptions routerWithoutBind = new DefaultZLinkFrameworkOptions();
        routerWithoutBind.addSpotMesh("router").connectRouter("inproc://peer-router");
        assertThrows(ZLinkConfigurationException.class, routerWithoutBind::validate);

        DefaultZLinkFrameworkOptions pubSubWithoutBind = new DefaultZLinkFrameworkOptions();
        pubSubWithoutBind.addSpotMesh("pubsub").connectPeerPub("inproc://peer-pub");
        assertThrows(ZLinkConfigurationException.class, pubSubWithoutBind::validate);
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handle(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    @ZLinkPacket("AnnotatedEcho")
    public record AnnotatedPacket(String value) {
    }

    public static final class AnnotatedEchoHandler
        implements ZLinkRequestHandler<AnnotatedPacket, String> {
        @Override
        public CompletionStage<String> handle(
            AnnotatedPacket request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request.value());
        }
    }

    @ZLinkHandlerGroup("scanned-publish")
    public static final class EventHandler implements ZLinkPublishHandler<String> {
        @Override
        public CompletionStage<Void> handle(String message, ZLinkPublishContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("scanned-attributed-publish")
    public static final class AttributedEventHandler {
        @ZLinkPublish(packetName = "AttributedEvent")
        public CompletionStage<Void> handle(
            String message,
            ZLinkPublishContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class SendHandler implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handle(String message, ZLinkSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("scanned-request")
    public static final class ScannedRequestHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handle(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    @ZLinkHandlerGroup("scanned-primary")
    @ZLinkHandlerGroup("scanned-secondary")
    public static final class MultiGroupScannedRequestHandler
        implements ZLinkRequestHandler<Integer, Integer> {
        @Override
        public CompletionStage<Integer> handle(Integer request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class RouteSendHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public CompletionStage<Void> handle(String message, ZLinkRouteSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("scanned-route")
    public static final class RouteEchoHandler
        implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handle(
            String request,
            ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }

        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class TestFilter implements ZLinkHandlerFilter {
        @Override
        public <T> CompletionStage<T> invoke(
            ZLinkInvocationContext context,
            ZLinkNext<T> next) {
            return next.invoke();
        }
    }

    abstract static class TestLocationStore implements
        ZLinkLocationStore {
    }


    public static final class TestMessageFlowObserver
        implements ZLinkMessageFlowObserver {
        @Override
        public CompletionStage<Void> onMessageFlow(ZLinkMessageFlowEvent error) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ThrowingMessageFlowObserver
        implements ZLinkMessageFlowObserver {
        @Override
        public CompletionStage<Void> onMessageFlow(ZLinkMessageFlowEvent error) {
            throw new IllegalStateException("observer failed");
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
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
    }
}
