package systems.zlink.samples.bingo;

import java.util.List;
import systems.zlink.framework.ZLinkRegistry;
import systems.zlink.samples.bingo.client.BingoClientApp;
import systems.zlink.samples.bingo.client.BingoClientOptions;
import systems.zlink.samples.bingo.server.api.ApiServerHostFactory;
import systems.zlink.samples.bingo.server.play.PlayServerHostFactory;
import systems.zlink.samples.bingo.server.registry.RegistryHostFactory;
import systems.zlink.samples.bingo.server.session.SessionServerHostFactory;

public final class BingoSample {
    private BingoSample() {
    }

    public static void main(String[] args) throws Exception {
        List<Integer> drawSequence = List.of(7, 11, 42, 42);
        try (ZLinkRegistry registry = RegistryHostFactory.start();
             var api = ApiServerHostFactory.start();
             var play = PlayServerHostFactory.start();
             var session = SessionServerHostFactory.start()) {
            BingoClientApp app = new BingoClientApp(new BingoClientOptions(4));
            app.run(drawSequence);
        }
        System.out.println("Bingo sample self-check passed");
    }
}
