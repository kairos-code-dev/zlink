package systems.zlink.samples.tictactoe.sessiongateway.shared.actors;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;

public record RecordingBoundSession(List<String> pushes) implements ZLinkBoundSession {
    @Override
    public <TMessage> ZLinkBoundSessionSendCall send(TMessage message) {
        return new RecordingBoundSessionSendCall(pushes, String.valueOf(message));
    }

    @Override
    public CompletionStage<Void> disconnectAsync() {
        return CompletableFuture.completedFuture(null);
    }
}
