package systems.zlink.samples.supportchat.server.support.actors;

import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;

public final class SupportUserActorTransferAdapter
    implements ZLinkActorTransferAdapter<SupportUserActor> {
    @Override
    public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(SupportUserActor actor) {
        return java.util.concurrent.CompletableFuture.completedFuture(
            ZLinkMessage.of(new TransferState(
                actor.displayName(), actor.role(), actor.participantId(), actor.conversationId())));
    }

    @Override
    public java.util.concurrent.CompletionStage<SupportUserActor> transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state) {
        TransferState transferred = state.decode(TransferState.class);
        SupportUserActor actor = new SupportUserActor(actorId, context);
        actor.setIdentity(transferred.displayName(), transferred.role(), transferred.participantId());
        actor.joinConversation(transferred.conversationId());
        return java.util.concurrent.CompletableFuture.completedFuture(actor);
    }

    public record TransferState(
        String displayName,
        String role,
        String participantId,
        String conversationId) {
    }
}
