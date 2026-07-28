package systems.zlink.samples.supportchat.server.support.actors;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.framework.actors.ZLinkActorRelocationAdapter;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;

public final class SupportUserActorRelocationAdapter
    implements ZLinkActorRelocationAdapter<SupportUserActor> {
    private static final ObjectMapper JSON = new ObjectMapper();

    @Override
    public java.util.concurrent.CompletionStage<byte[]> capture(
        SupportUserActor actor,
        ZLinkRelocationCancellation cancellation) {
        try {
            return java.util.concurrent.CompletableFuture.completedFuture(
                JSON.writeValueAsBytes(new TransferState(
                    actor.displayName(),
                    actor.role(),
                    actor.participantId(),
                    actor.conversationId())));
        } catch (java.io.IOException error) {
            return java.util.concurrent.CompletableFuture.failedFuture(error);
        }
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> restore(
        SupportUserActor actor,
        byte[] state,
        ZLinkRelocationCancellation cancellation) {
        try {
            TransferState transferred = JSON.readValue(state, TransferState.class);
            actor.setIdentity(
                transferred.displayName(),
                transferred.role(),
                transferred.participantId());
            actor.joinConversation(transferred.conversationId());
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        } catch (java.io.IOException error) {
            return java.util.concurrent.CompletableFuture.failedFuture(error);
        }
    }

    public record TransferState(
        String displayName,
        String role,
        String participantId,
        String conversationId) {
    }
}
