package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSession;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActorFactory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleTopology.SampleSessionNode;

public final class SessionServer {
    private SessionServer() {
    }

    public static void configureRelayNode(
        ZLinkFrameworkOptions options,
        SampleSessionNode sessionNode) {
        options.addRouteMeshChannel(SampleNames.PlayRouteChannel, route -> {
            route.bind(sessionNode.routeEndpoint());
            route.configureRouting(routing ->
                routing.setRoutingId(RoutingId.from(sessionNode.routingId())));
            route.useManualConnections(endpoints ->
                endpoints.connect(SampleTopology.PlayRouteEndpoint));
        });
        options.addSpotMesh(SampleNames.SpotMesh, mesh -> {
            mesh.addNode(SampleNames.SessionRelayNode, node -> {
                node.enableRouter(router -> {
                    router.bindRouter(sessionNode.routerEndpoint());
                    router.setRoutingId(RoutingId.from(sessionNode.routingId()));
                });
                node.acceptSpotRoutesFromChannel(SampleNames.PlayRouteChannel);
                node.addSpotFactory(SessionRelaySpot.class);
            });
        });
        options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
    }

    public static void configure(
        ZLinkFrameworkOptions options,
        SampleSessionNode sessionNode) {
        options.addClientServerChannel(SampleNames.ApiChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.ApiEndpoint))));
        options.addClientServerChannel(SampleNames.PlayChannel, channel ->
            channel.enableClient(client -> client.useManualConnections(
                endpoints -> endpoints.connect(SampleTopology.PlayChannelEndpoint))));
        options.addStreamNode(SampleNames.GatewayStream, stream -> {
            stream.bind(sessionNode.streamEndpoint());
            stream.attachActorGateway(SampleNames.SessionRelayNode);
            stream.registerSession(PlayerSession.class);
        });
    }
}
