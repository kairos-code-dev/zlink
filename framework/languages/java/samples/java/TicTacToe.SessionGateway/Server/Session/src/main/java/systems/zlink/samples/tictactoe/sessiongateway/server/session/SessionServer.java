package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSession;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers.AuthenticateSessionPacketHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers.CreateMatchSessionPacketHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers.JoinMatchSessionPacketHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers.PlaceMarkSessionPacketHandler;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;

public final class SessionServer {
    private SessionServer() {
    }

    public static void configureRelayNode(ZLinkFrameworkOptions options) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel, route -> {
            route.bind(SampleTopology.SessionRouteEndpoint);
            route.configureRouting(routing ->
                routing.setRoutingId(RoutingId.from(SampleNames.SessionRid)));
            route.useManualConnections(endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint));
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.SessionRelayNode, node -> {
                node.enableRouter(router -> {
                    router.setRouterBind(SampleTopology.SessionRouterEndpoint);
                    router.setRoutingId(RoutingId.from(SampleNames.SessionRid));
                });
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel);
                node.addSpotFactory(SessionRelaySpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }

    public static void configure(ZLinkFrameworkOptions options) {
        options.addClientServerChannel(SampleNames.ApiChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.ApiEndpoint))));
        options.addClientServerChannel(SampleNames.PlayChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.PlayChannelEndpoint))));
        options.addStreamNode(SampleNames.GatewayStream, stream -> {
            stream.bind(SampleTopology.SessionEndpoint);
            stream.attachActorGateway(SampleNames.SessionRelayNode);
            stream.registerSession(PlayerSession.class);
            stream.addSessionPacketHandler(AuthenticateSessionPacketHandler.class);
            stream.addSessionPacketHandler(CreateMatchSessionPacketHandler.class);
            stream.addSessionPacketHandler(JoinMatchSessionPacketHandler.class);
            stream.addSessionPacketHandler(PlaceMarkSessionPacketHandler.class);
        });
    }
}
