package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSessionDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.shared.contracts.PlayResponseEnvelope;

public final class PlayNotificationRelay {
    private final PlayerSessionDirectory sessions;

    public PlayNotificationRelay(PlayerSessionDirectory sessions) {
        this.sessions = sessions;
    }

    public CompletionStage<Void> deliverAsync(String encodedResponse) {
        PlayResponseEnvelope.PlayResponse response = PlayResponseEnvelope.decode(encodedResponse);
        CompletionStage<Void> tail = CompletableFuture.completedFuture(null);
        for (PlayResponseEnvelope.Notification notification : response.notifications()) {
            tail = tail.thenCompose(ignored -> sessions
                .find(notification.recipientActorId())
                .map(context -> context.client()
                    .send(notification.payload())
                    .packetName(notification.packetName())
                    .submitAsync())
                .orElseGet(() -> CompletableFuture.completedFuture(null)));
        }
        return tail;
    }

    public String reply(String encodedResponse) {
        return PlayResponseEnvelope.decode(encodedResponse).reply();
    }
}
