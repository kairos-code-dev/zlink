package systems.zlink.samples.bingo;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.samples.bingo.client.BingoClientApp;
import systems.zlink.samples.bingo.client.BingoClientOptions;
import systems.zlink.samples.bingo.client.BingoNotificationLoopbackServer;
import systems.zlink.samples.bingo.client.SampleAsync;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomState;

public final class BingoSample {
    private BingoSample() {
    }

    public static void main(String[] args) throws Exception {
        try (BingoNotificationLoopbackServer notifications =
                 new BingoNotificationLoopbackServer(29100);
             ZLinkFramework framework = ZLinkFramework.start(options ->
            options.addSpotMesh("bingo", mesh ->
                mesh.addNode("play", node -> node.addSpotFactory(BingoRoomSpot.class))))) {
            SampleAsync.await(framework.spotManager()
                .createAsync(BingoRoomSpot.class, RoutingId.from("room-1")));
            BingoClientApp app = new BingoClientApp(new BingoClientOptions(4));
            BingoRoomState room = new BingoRoomState("room-1", List.of(7, 11, 42, 42));
            app.run(room);
        }
        System.out.println("Bingo sample self-check passed");
    }
}
