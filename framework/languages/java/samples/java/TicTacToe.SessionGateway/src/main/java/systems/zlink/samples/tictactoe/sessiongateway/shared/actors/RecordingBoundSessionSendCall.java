package systems.zlink.samples.tictactoe.sessiongateway.shared.actors;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;

public record RecordingBoundSessionSendCall(
    List<String> pushes,
    String message,
    String packetName) implements ZLinkBoundSessionSendCall {
    public RecordingBoundSessionSendCall(List<String> pushes, String message) {
        this(pushes, message, message);
    }

    @Override
    public ZLinkBoundSessionSendCall metadata(String key, String value) {
        return this;
    }

    @Override
    public ZLinkBoundSessionSendCall packetName(String packetName) {
        return new RecordingBoundSessionSendCall(pushes, message, packetName);
    }

    @Override
    public CompletionStage<Void> submitAsync() {
        pushes.add(packetName + ":" + message);
        return CompletableFuture.completedFuture(null);
    }
}
