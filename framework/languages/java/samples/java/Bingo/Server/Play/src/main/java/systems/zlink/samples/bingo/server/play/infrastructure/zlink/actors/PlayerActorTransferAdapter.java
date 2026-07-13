package systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors;

import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;

public final class PlayerActorTransferAdapter
    implements ZLinkActorTransferAdapter<PlayerActor> {
    @Override
    public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(
        PlayerActor actor) {
        return java.util.concurrent.CompletableFuture.completedFuture(ZLinkMessage.of(new TransferState(
            actor.displayName(),
            actor.roomId(),
            actor.destroyAfterEntrySpotJoin(),
            actor.disconnected())));
    }

    @Override
    public java.util.concurrent.CompletionStage<PlayerActor> transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state) {
        TransferState transferred = state.decode(TransferState.class);
        PlayerActor actor = new PlayerActor(actorId, context);
        actor.setDisplayName(transferred.displayName());
        if (transferred.roomId() != null && !transferred.roomId().isBlank()) {
            actor.joinRoom(transferred.roomId());
        }
        if (transferred.destroyAfterEntrySpotJoin()) {
            actor.markForDestroyAfterRoomLeave();
        }
        if (transferred.disconnected()) {
            actor.markDisconnected();
        }
        return java.util.concurrent.CompletableFuture.completedFuture(actor);
    }

    public record TransferState(
        String displayName,
        String roomId,
        boolean destroyAfterEntrySpotJoin,
        boolean disconnected) {
    }
}
