package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkBoundSession;

public final class GameNotificationPublisher {
    public CompletionStage<Void> publishAsync(ZLinkBoundSession session, String state) {
        return session.send(state)
            .packetName("GameStateChanged")
            .submitAsync();
    }
}
