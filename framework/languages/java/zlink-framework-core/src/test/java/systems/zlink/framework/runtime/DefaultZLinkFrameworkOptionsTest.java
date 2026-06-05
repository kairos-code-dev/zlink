package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
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
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.ZLinkInvocationContext;
import systems.zlink.framework.ZLinkNext;
import systems.zlink.framework.configuration.ZLinkDispatchMode;
import systems.zlink.framework.configuration.ZLinkUnhandledDispatchAction;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddress;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class DefaultZLinkFrameworkOptionsTest {
    @Test
    void addClientServerChannelRejectsDuplicateChannelName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> { });

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.addFanoutChannel("profile", channel -> { }));
    }

    @Test
    void setDefaultTimeoutRejectsZero() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(
            ZLinkConfigurationException.class,
            () -> options.setDefaultTimeout(Duration.ZERO));
    }

    @Test
    void globalConfigurationMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.codecs().addProtobuf();
        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.configureMetadata(metadata -> metadata.addForwardedMetadataKey("trace-id"));
        options.useFilter(TestFilter.class);
        options.configureDispatch(dispatch -> {
            dispatch.setSpotDispatchMode(ZLinkDispatchMode.DYNAMIC);
            dispatch.diagnostics().setSampleRate(0.25d);
        });
        options.addSpotRemoteAddressResolver(TestSpotRemoteAddressResolver.class);
        options.useRegistrySpotRemoteAddresses("game", registry ->
            registry.setRouterChannelId("spot-router"));

        assertTrue(options.registration().codecs().registeredCodecs().contains("protobuf"));
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
    void configureDispatchRejectsReplyErrorForSendAndPublish() {
        DefaultZLinkFrameworkOptions send = new DefaultZLinkFrameworkOptions();
        send.configureDispatch(dispatch ->
            dispatch.unhandled().setSend(ZLinkUnhandledDispatchAction.REPLY_ERROR));

        assertThrows(ZLinkConfigurationException.class, send::validate);

        DefaultZLinkFrameworkOptions publish = new DefaultZLinkFrameworkOptions();
        publish.configureDispatch(dispatch ->
            dispatch.unhandled().setPublish(ZLinkUnhandledDispatchAction.REPLY_ERROR));

        assertThrows(ZLinkConfigurationException.class, publish::validate);
    }

    @Test
    void configureDispatchRejectsInvalidDiagnosticsSampleRate() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.configureDispatch(dispatch -> dispatch.diagnostics().setSampleRate(1.1d));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelClientWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> channel.enableClient());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-server"))));

        options.validate();
    }

    @Test
    void clientServerSpotRouteEgressRequiresClientCapability() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("gateway", channel ->
            channel.enableSpotRouteEgress("play.route"));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void registrySpotRemoteAddressesRejectsCustomResolverDuplicate() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("inproc://registry"));
        options.addRouteMeshChannel("play", channel -> { });
        options.addSpotRemoteAddressResolver(TestSpotRemoteAddressResolver.class);
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void registrySpotRemoteAddressesRequiresDiscoveryEndpoint() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("play", channel -> { });
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void registrySpotRemoteAddressesRequiresRouterChannelWhenAmbiguous() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("inproc://registry"));
        options.addRouteMeshChannel("play-a", channel -> { });
        options.addRouteMeshChannel("play-b", channel -> { });
        options.useRegistrySpotRemoteAddresses("game");

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void spotPublisherClientRequiresPubSubCapability() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("publisher", node ->
                node.attachSpotPublisherClient("game.stage")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void duplicateSpotPublisherChannelAcrossNodesIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh -> {
            mesh.addNode("publisher-a", node -> {
                node.enablePubSub();
                node.attachSpotPublisherClient("game.stage");
            });
            mesh.addNode("publisher-b", node -> {
                node.enablePubSub();
                node.attachSpotPublisherClient("game.stage");
            });
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void spotPublisherClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("publisher", node -> {
                node.enablePubSub();
                node.attachSpotPublisherClient("game.stage", publisher ->
                    publisher.useManualConnections(endpoints ->
                        endpoints.connect("inproc://game-stage-pub")));
            }));

        options.validate();
        assertEquals(
            List.of("inproc://game-stage-pub"),
            options.registration()
                .spotNodes()
                .get(0)
                .attachedSpotPublisherClients()
                .get("game.stage")
                .manualConnections());
    }

    @Test
    void spotRouterAndPubSubManualConnectionsMutateRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        RoutingId nodeRid =
            RoutingId.from("spot-node-1");
        RoutingId pubSubRid =
            RoutingId.from("spot-pub-1");

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRoutingId(nodeRid);
                    router.useManualConnections(endpoints ->
                        endpoints.connect("inproc://spot-router-peer"));
                });
                node.enablePubSub(pubsub -> {
                    pubsub.setRoutingId(pubSubRid);
                    pubsub.useManualConnections(endpoints ->
                        endpoints.connect("inproc://spot-pub-peer"));
                });
            }));

        options.validate();
        assertEquals(nodeRid, options.registration().spotNodes().get(0).routerRoutingId());
        assertEquals(pubSubRid, options.registration().spotNodes().get(0).pubSubRoutingId());
        assertEquals(nodeRid, options.registration().spotNodes().get(0).nodeRoutingId());
        assertEquals(
            List.of("inproc://spot-router-peer"),
            options.registration().spotNodes().get(0).routerManualConnections());
        assertEquals(
            List.of("inproc://spot-pub-peer"),
            options.registration().spotNodes().get(0).pubSubManualConnections());
    }

    @Test
    void spotRouterAndPubSubManualConnectionsRejectBlankEndpoint() {
        DefaultZLinkFrameworkOptions router = new DefaultZLinkFrameworkOptions();
        assertThrows(ZLinkConfigurationException.class, () ->
            router.addSpotMesh("game", mesh ->
                mesh.addNode("play", node ->
                    node.enableRouter(capability ->
                        capability.useManualConnections(endpoints ->
                            endpoints.connect(" "))))));

        DefaultZLinkFrameworkOptions pubSub = new DefaultZLinkFrameworkOptions();
        assertThrows(ZLinkConfigurationException.class, () ->
            pubSub.addSpotMesh("game", mesh ->
                mesh.addNode("play", node ->
                    node.enablePubSub(capability ->
                        capability.useManualConnections(endpoints ->
                            endpoints.connect(" "))))));
    }

    @Test
    void spotNodeRejectsConflictingRouterAndPubSubRoutingIds() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.setRoutingId(
                        RoutingId.from("node-a")));
                node.enablePubSub(pubsub ->
                    pubsub.setRoutingId(
                        RoutingId.from("node-b")));
            }));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void entrySpotRoutingIdMutatesRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        RoutingId entryRid =
            RoutingId.from("entry-spot");

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node ->
                node.configureEntrySpot(entry -> entry.setRoutingId(entryRid))));

        options.validate();
        assertEquals(entryRid, options.registration().spotNodes().get(0).entrySpotRoutingId());
    }

    @Test
    void acceptedSpotRouteChannelManualConnectionsMutateRegistrationModel() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("api", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://api-server"))));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("api", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://api-router")));
            }));

        options.validate();
        assertEquals(
            List.of("inproc://api-router"),
            options.registration()
                .spotNodes()
                .get(0)
                .acceptedSpotRouteChannels()
                .get("api")
                .manualConnections());
    }

    @Test
    void acceptedSpotRouteChannelRequiresRouterCapability() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("api", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://api-server"))));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node ->
                node.acceptSpotRoutesFromChannel("api", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://api-router")))));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void acceptedSpotRouteChannelRejectsMissingOrWrongChannelKind() {
        DefaultZLinkFrameworkOptions missing = new DefaultZLinkFrameworkOptions();
        missing.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("missing", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://api-router")));
            }));
        assertThrows(ZLinkConfigurationException.class, missing::validate);

        DefaultZLinkFrameworkOptions fanout = new DefaultZLinkFrameworkOptions();
        fanout.addFanoutChannel("events", channel ->
            channel.enablePublisher(publisher ->
                publisher.bind("inproc://events")));
        fanout.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("events", acceptance ->
                    acceptance.useManualConnections(endpoints ->
                        endpoints.connect("inproc://events-router")));
            }));
        assertThrows(ZLinkConfigurationException.class, fanout::validate);
    }

    @Test
    void acceptedSpotRouteChannelRequiresDiscoveryOrManualConnection() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("api", channel -> { });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.acceptSpotRoutesFromChannel("api");
            }));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void acceptedSpotRouteChannelRejectsDuplicateRegistration() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addSpotMesh("game", mesh ->
                mesh.addNode("play", node -> {
                    node.acceptSpotRoutesFromChannel("api");
                    node.acceptSpotRoutesFromChannel("api");
                })));
    }

    @Test
    void entrySpotRoutingIdRejectsNull() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addSpotMesh("game", mesh ->
                mesh.addNode("play", node ->
                    node.configureEntrySpot(entry -> entry.setRoutingId(null)))));
    }

    @Test
    void attachedSpotChannelClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node ->
                node.attachChannelClient("profile", client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server")))));

        options.validate();
        assertEquals(
            List.of("inproc://profile-server"),
            options.registration()
                .spotNodes()
                .get(0)
                .attachedChannelClients()
                .get("profile")
                .manualConnections());
    }

    @Test
    void attachedSpotChannelClientRequiresDiscoveryOrManualConnections() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.attachChannelClient("profile")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void attachedSpotChannelClientManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node ->
                node.attachChannelClient("profile", client ->
                    client.useManualConnections(endpoints ->
                        endpoints.connect("inproc://profile-server")))));

        assertDoesNotThrow(options::validate);
    }

    @Test
    void clientServerChannelClientManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addClientServerChannel("profile", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints ->
                    endpoints.connect("inproc://profile-server"))));

        assertDoesNotThrow(options::validate);
    }

    @Test
    void clientServerChannelServerWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> channel.enableServer());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithoutRequestHandlerIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel ->
            channel.enableServer(server -> server.bind("inproc://profile-server")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, "Echo");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelRejectsMappedGroupAndExplicitRequestDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addHandlerGroup("scanned-request");
            channel.addRequestHandler(EchoHandler.class, String.class, String.class);
        });

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate client/server request handler packet name"));
    }

    @Test
    void clientServerChannelRejectsDuplicateSendHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addSendHandler(SendHandler.class, String.class);
            channel.addSendHandler(SendHandler.class, String.class);
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithOnlySendHandlerIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addSendHandler(SendHandler.class, String.class, "Notify");
        });

        options.validate();
    }

    @Test
    void clientServerChannelRejectsBlankRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () -> options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(EchoHandler.class, String.class, String.class, " ");
        }));
    }

    @Test
    void clientServerChannelServerWithBindIsAccepted() {
        DefaultZLinkFrameworkOptions accepted = new DefaultZLinkFrameworkOptions();
        accepted.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addRequestHandler(AnnotatedEchoHandler.class, AnnotatedPacket.class, String.class);
        });

        accepted.validate();
    }

    @Test
    void fanoutChannelPublisherWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> channel.enablePublisher());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelSubscriberWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber();
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelSubscriberManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertDoesNotThrow(options::validate);
    }

    @Test
    void fanoutChannelRejectsDuplicatePublishHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
            channel.addPublishHandler(EventHandler.class, String.class, "Event");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelRejectsMappedGroupAndExplicitPublishDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addHandlerGroup("scanned-publish");
            channel.addPublishHandler(EventHandler.class, String.class);
        });

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate fanout publish handler packet name"));
    }

    @Test
    void routeMeshChannelWithoutBindIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel ->
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route")));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelWithoutPeerAcquisitionPathIsRejected() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> channel.bind("inproc://route"));

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelManualConnectionsOverrideGlobalDiscovery() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://127.0.0.1:17001"));
        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
        });

        assertDoesNotThrow(options::validate);
    }

    @Test
    void routeMeshChannelRejectsDuplicateRequestHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Echo");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsMappedGroupAndExplicitRequestDuplicatePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route"));
            channel.addHandlerGroup("scanned-route");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class);
        });

        ZLinkConfigurationException exception =
            assertThrows(ZLinkConfigurationException.class, options::validate);

        assertTrue(exception.getMessage().contains(
            "duplicate route mesh request handler packet name"));
    }

    @Test
    void routeMeshChannelRejectsDuplicateSendHandlerPacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
            channel.addSendHandler(RouteSendHandler.class, String.class);
            channel.addSendHandler(RouteSendHandler.class, String.class);
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void routeMeshChannelRejectsSendAndRequestWithSamePacketName() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addRouteMeshChannel("route", channel -> {
            channel.bind("inproc://route");
            channel.useManualConnections(endpoints -> endpoints.connect("inproc://route-peer"));
            channel.addSendHandler(RouteSendHandler.class, String.class, "Notify");
            channel.addRequestHandler(RouteEchoHandler.class, String.class, String.class, "Notify");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void channelDeduplicatesHandlerGroupsLikeDotnet() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addHandlerGroup("scanned-request");
            channel.addHandlerGroup("scanned-request");
        });

        options.validate();
        assertEquals(
            List.of("scanned-request"),
            options.registration().channels().get(0).handlerGroups());
    }

    @Test
    void channelRejectsUnknownMappedHandlerGroup() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addHandlerGroup("missing-group");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void fanoutChannelRejectsRequestHandlerGroup() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addFanoutChannel("events", channel -> {
            channel.enableSubscriber(subscriber ->
                subscriber.useManualConnections(endpoints -> endpoints.connect("inproc://events")));
            channel.addHandlerGroup("scanned-request");
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void clientServerChannelServerWithScannedHandlerGroupIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addHandlerGroup("scanned-request");
        });

        options.validate();
    }

    @Test
    void clientServerChannelServerWithRepeatedScannedHandlerGroupIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addHandlersFromPackageOf(DefaultZLinkFrameworkOptionsTest.class);
        options.addClientServerChannel("profile", channel -> {
            channel.enableServer(server -> server.bind("inproc://profile-server"));
            channel.addHandlerGroup("scanned-secondary");
        });

        options.validate();
    }

    @Test
    void dealerMeshClientRequiresPeerAcquisitionPath() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addDealerMeshChannel("mesh", channel -> channel.enableClient());

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void dealerMeshClientWithManualConnectionIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addDealerMeshChannel("mesh", channel ->
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect("inproc://mesh"))));

        options.validate();
    }

    @Test
    void streamNodeRejectsMultipleSessionTypes() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        assertThrows(ZLinkConfigurationException.class, () ->
            options.addStreamNode("gateway", stream -> {
                stream.bind("inproc://gateway");
                stream.registerSession(GameSession.class);
                stream.registerSession(GameSession.class);
            }));
    }

    @Test
    void streamNodeActorGatewayRequiresConfiguredSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeActorGatewayRequiresRouterSpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> { }));
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        assertThrows(ZLinkConfigurationException.class, options::validate);
    }

    @Test
    void streamNodeWithActorGatewayAndRouterSpotNodeIsAccepted() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();

        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addSpotFactory(TestSpot.class);
            }));
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        options.validate();
    }

    public static final class EchoHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    @ZLinkPacket("AnnotatedEcho")
    public record AnnotatedPacket(String value) {
    }

    public static final class AnnotatedEchoHandler
        implements ZLinkRequestHandler<AnnotatedPacket, String> {
        @Override
        public CompletionStage<String> handleAsync(
            AnnotatedPacket request,
            ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request.value());
        }
    }

    @ZLinkHandlerGroup("scanned-publish")
    public static final class EventHandler implements ZLinkPublishHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkPublishContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class SendHandler implements ZLinkSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("scanned-request")
    public static final class ScannedRequestHandler implements ZLinkRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    @ZLinkHandlerGroup("scanned-primary")
    @ZLinkHandlerGroup("scanned-secondary")
    public static final class MultiGroupScannedRequestHandler
        implements ZLinkRequestHandler<Integer, Integer> {
        @Override
        public CompletionStage<Integer> handleAsync(Integer request, ZLinkRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class RouteSendHandler implements ZLinkRouteSendHandler<String> {
        @Override
        public CompletionStage<Void> handleAsync(String message, ZLinkRouteSendContext context) {
            return CompletableFuture.completedFuture(null);
        }
    }

    @ZLinkHandlerGroup("scanned-route")
    public static final class RouteEchoHandler
        implements ZLinkRouteRequestHandler<String, String> {
        @Override
        public CompletionStage<String> handleAsync(
            String request,
            ZLinkRouteRequestContext context) {
            return CompletableFuture.completedFuture(request);
        }
    }

    public static final class TestSpot implements ZLinkSpot {
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

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
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
    }
}
