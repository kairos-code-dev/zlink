package systems.zlink.samples.bingo;

import java.util.List;
import systems.zlink.samples.bingo.client.BingoClientApp;
import systems.zlink.samples.bingo.client.BingoClientOptions;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;

public final class BingoSample {
    private BingoSample() {
    }

    public static void main(String[] args) throws Exception {
        BingoClientApp app = new BingoClientApp(new BingoClientOptions(4));
        BingoRoomSpot room = new BingoRoomSpot("room-1", List.of(7, 11, 42, 42));
        app.run(room);
        System.out.println("Bingo sample self-check passed");
    }
}
