package systems.zlink.samples.bingo.server.play;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.bingo.server.play.actors.PlayerActorFactory;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.handlers.AllocateBingoRoomHandler;
import systems.zlink.samples.bingo.server.play.handlers.EnsurePlayerActorHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class PlayServerHostFactory {
    private PlayServerHostFactory() {
    }

    public static ZLinkFramework start() {
        return ZLinkFramework.start(options -> {
            options.useDiscovery(discovery -> discovery.add(SampleTopology.RegistryRouterEndpoint));
            options.addClientServerChannel(SampleNames.PlayChannel, channel -> {
                channel.enableServer(server -> server.bind(SampleTopology.PlayChannelEndpoint));
                channel.addRequestHandler(
                    EnsurePlayerActorHandler.class,
                    Messages.EnsurePlayerActorReq.class,
                    Messages.EnsurePlayerActorRes.class,
                    "EnsurePlayerActor");
                channel.addRequestHandler(
                    AllocateBingoRoomHandler.class,
                    Messages.AllocateBingoRoomReq.class,
                    Messages.AllocateBingoRoomRes.class,
                    "AllocateBingoRoom");
            });
            options.addClientServerChannel(SampleNames.ApiChannel, channel -> channel.enableClient());
            options.addActorFactory(SampleNames.PlayerActorType, PlayerActorFactory.class);
            options.addSpotMesh(SampleNames.RoomSpotDiscovery, mesh ->
                mesh.addNode(SampleNames.RoomSpotNode, node -> {
                    node.enableRouter(router -> {
                        router.setRouterBind(SampleTopology.PlaySpotRouterEndpoint);
                        router.setRoutingId(RoutingId.from("2202"));
                    });
                    node.enablePubSub(pubSub -> pubSub.setPubBind(SampleTopology.PlaySpotEndpoint));
                    node.attachChannelClient(SampleNames.ApiChannel);
                    node.addSpotFactory(BingoRoomSpot.class);
                }));
        });
    }
}
