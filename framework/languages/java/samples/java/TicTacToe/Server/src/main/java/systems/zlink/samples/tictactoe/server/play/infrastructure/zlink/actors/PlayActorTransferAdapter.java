package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors;

import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.samples.tictactoe.shared.contracts.PlayerInfo;

public final class PlayActorTransferAdapter
    implements ZLinkActorTransferAdapter<PlayActor> {
    @Override
    public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(PlayActor actor) {
        return java.util.concurrent.CompletableFuture.completedFuture(ZLinkMessage.of(new TransferState(
            actor.joinedRoomId(),
            actor.playerOrNull(),
            actor.destroyAfterEntrySpotJoin(),
            actor.disconnected())));
    }

    @Override
    public java.util.concurrent.CompletionStage<PlayActor> transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state) {
        TransferState transferred = state.decode(TransferState.class);
        PlayActor actor = new PlayActor(actorId, context);
        if (transferred.player() != null) {
            actor.applyPlayer(transferred.player());
        }
        if (transferred.roomId() != null && !transferred.roomId().isBlank()) {
            actor.joinGame(transferred.roomId());
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
        String roomId,
        PlayerInfo player,
        boolean destroyAfterEntrySpotJoin,
        boolean disconnected) {
    }
}
