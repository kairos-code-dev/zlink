package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory;

public final class PlayNotificationRelay {
    private PlayNotificationRelay() {
    }

    public static CompletionStage<Void> deliverAsync(String encodedResponse) {
        GameNotificationPublisher.PlayResponse response = GameNotificationPublisher.decode(encodedResponse);
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (GameNotificationPublisher.Notification notification : response.notifications()) {
            tail = tail.thenCompose(ignored -> PlayerSessionDirectory
                .find(notification.recipientActorId())
                .map(context -> context.client()
                    .send(notification.payload())
                    .packetName(notification.packetName())
                    .submitAsync())
                .orElseGet(() -> CompletableFuture.completedFuture(null)));
        }
        return tail;
    }

    public static String reply(String encodedResponse) {
        return GameNotificationPublisher.decode(encodedResponse).reply();
    }
}
